// 文件作用：实现包校验 Null Sink、JSONL 元数据 Sink 和 Sink 工厂。
// 主要知识点：文件系统、ofstream、DTS 单调性、确定性故障注入和状态机。
#include "rkav/output/sinks.h"

#include <chrono>
#include <filesystem>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

namespace rkav {
namespace {

/// 功能：构造指定 Sink 模块的统一写入错误。
Error SinkError(std::string_view module, ErrorCategory category, std::string message,
                bool retryable = false) {
    return Error{category, 0, std::string(module), "write", std::move(message), retryable};
}

/// 功能：在确定包序号注入失败或人工延迟，便于稳定复现慢输出和 I/O 故障。
Result<void> ApplyFaultAndDelay(const OutputConfig& config, std::uint64_t packet_count,
                                std::string_view module) {
    // 故障和延迟均由配置确定，集成测试可以在相同包序号稳定复现。
    if (config.fail_after_packets.has_value() &&
        packet_count >= *config.fail_after_packets) {
        return Result<void>::Failure(
            SinkError(module, ErrorCategory::kIo, "injected packet sink failure", false));
    }
    if (config.write_delay_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(config.write_delay_ms));
    }
    return Result<void>::Success();
}

}  // namespace

/// 功能：重置计数和 DTS，并打开 Null Sink。
Result<void> ValidatingNullSink::Open(const OutputConfig& config) {
    std::scoped_lock lock(mutex_);
    if (open_) {
        return Result<void>::Failure(
            SinkError("null_sink", ErrorCategory::kInvalidState, "sink is already open"));
    }
    config_ = config;
    packet_count_ = 0;
    last_video_dts_.reset();
    last_audio_dts_.reset();
    flushed_ = false;
    open_ = true;
    return Result<void>::Success();
}

/// 功能：按媒体类型分别检查 DTS 不小于上一包。
Result<void> ValidatingNullSink::ValidateTimestamp(const EncodedPacket& packet) {
    // 音频和视频维护各自 DTS，不能拿两条不同时间线互相比较。
    auto& last = packet.kind == StreamKind::kVideo ? last_video_dts_ : last_audio_dts_;
    if (last.has_value() && packet.dts < *last) {
        return Result<void>::Failure(
            SinkError("null_sink", ErrorCategory::kCodec, "packet DTS moved backwards"));
    }
    last = packet.dts;
    return Result<void>::Success();
}

/// 功能：执行故障注入、包结构校验和可选时间戳校验，不保存 payload。
Result<void> ValidatingNullSink::Write(const EncodedPacket& packet) {
    std::scoped_lock lock(mutex_);
    if (!open_ || flushed_) {
        return Result<void>::Failure(
            SinkError("null_sink", ErrorCategory::kInvalidState, "sink is not writable"));
    }
    auto fault = ApplyFaultAndDelay(config_, packet_count_, "null_sink");
    if (!fault) {
        return fault;
    }
    // Null Sink 虽然不落盘，仍承担包结构和时间戳的末端验收职责。
    auto validation = ValidatePacket(packet);
    if (!validation) {
        return validation;
    }
    if (config_.validate_timestamps) {
        validation = ValidateTimestamp(packet);
        if (!validation) {
            return validation;
        }
    }
    ++packet_count_;
    return Result<void>::Success();
}

/// 功能：结束 Null Sink 写入；之后 Write 返回状态错误。
Result<void> ValidatingNullSink::Flush() {
    std::scoped_lock lock(mutex_);
    if (!open_) {
        return Result<void>::Failure(
            SinkError("null_sink", ErrorCategory::kInvalidState, "sink is not open"));
    }
    flushed_ = true;
    return Result<void>::Success();
}

/// 功能：关闭 Null Sink；允许重复调用。
void ValidatingNullSink::Close() noexcept {
    std::scoped_lock lock(mutex_);
    open_ = false;
}

/// 功能：创建父目录并打开/截断 JSONL 文件。
Result<void> JsonLinePacketSink::Open(const OutputConfig& config) {
    std::scoped_lock lock(mutex_);
    if (open_) {
        return Result<void>::Failure(
            SinkError("jsonl_sink", ErrorCategory::kInvalidState, "sink is already open"));
    }
    std::error_code error;  // 使用 error_code 避免文件系统异常穿出接口。
    const std::filesystem::path path(config.path);  // 配置转换后的平台路径。
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return Result<void>::Failure(
                SinkError("jsonl_sink", ErrorCategory::kIo,
                          "cannot create output directory: " + error.message()));
        }
    }
    output_.open(path, std::ios::out | std::ios::trunc);
    if (!output_) {
        return Result<void>::Failure(
            SinkError("jsonl_sink", ErrorCategory::kIo, "cannot open output file"));
    }
    config_ = config;
    packet_count_ = 0;
    flushed_ = false;
    open_ = true;
    return Result<void>::Success();
}

/// 功能：校验包并把可读元数据写为一行 JSON。
Result<void> JsonLinePacketSink::Write(const EncodedPacket& packet) {
    std::scoped_lock lock(mutex_);
    if (!open_ || flushed_) {
        return Result<void>::Failure(
            SinkError("jsonl_sink", ErrorCategory::kInvalidState, "sink is not writable"));
    }
    auto fault = ApplyFaultAndDelay(config_, packet_count_, "jsonl_sink");
    if (!fault) {
        return fault;
    }
    auto validation = ValidatePacket(packet);
    if (!validation) {
        return validation;
    }
    // JSONL 只记录可诊断元数据，不展开二进制 payload，避免日志文件失控。
    nlohmann::json record{{"stream", ToString(packet.kind)}, // 当前包的 JSON 元数据。
                          {"codec", ToString(packet.codec)},
                          {"source_sequence", packet.source_sequence},
                          {"pts", packet.pts},
                          {"dts", packet.dts},
                          {"time_base",
                           {{"num", packet.time_base.numerator},
                            {"den", packet.time_base.denominator}}},
                          {"key_frame", packet.key_frame},
                          {"payload_bytes", packet.buffer->size()}};
    output_ << record.dump() << '\n';
    if (!output_) {
        return Result<void>::Failure(
            SinkError("jsonl_sink", ErrorCategory::kIo, "failed to write output file"));
    }
    ++packet_count_;
    return Result<void>::Success();
}

/// 功能：刷新文件流并结束写入。
Result<void> JsonLinePacketSink::Flush() {
    std::scoped_lock lock(mutex_);
    if (!open_) {
        return Result<void>::Failure(
            SinkError("jsonl_sink", ErrorCategory::kInvalidState, "sink is not open"));
    }
    output_.flush();
    if (!output_) {
        return Result<void>::Failure(
            SinkError("jsonl_sink", ErrorCategory::kIo, "failed to flush output file"));
    }
    flushed_ = true;
    return Result<void>::Success();
}

/// 功能：关闭文件流并退出打开状态；允许重复调用。
void JsonLinePacketSink::Close() noexcept {
    std::scoped_lock lock(mutex_);
    if (output_.is_open()) {
        output_.close();
    }
    open_ = false;
}

/// 功能：根据 type 创建具体 Sink；工厂把类型选择从 Application 中隔离出来。
Result<std::unique_ptr<IPacketSink>> CreatePacketSink(const OutputConfig& config) {
    if (config.type == "null") {
        return Result<std::unique_ptr<IPacketSink>>::Success(
            std::make_unique<ValidatingNullSink>());
    }
    if (config.type == "jsonl") {
        return Result<std::unique_ptr<IPacketSink>>::Success(
            std::make_unique<JsonLinePacketSink>());
    }
    return Result<std::unique_ptr<IPacketSink>>::Failure(
        SinkError("sink_factory", ErrorCategory::kNotSupported,
                  "unsupported sink type: " + config.type));
}

}  // namespace rkav
