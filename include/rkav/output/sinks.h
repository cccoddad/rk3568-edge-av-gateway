// 文件作用：声明末端校验用 Null Sink 和包元数据落盘用 JSONL Sink。
// 主要知识点：接口实现、文件流、DTS 单调性、故障注入和线程安全状态机。
#pragma once

#include <fstream>
#include <mutex>
#include <optional>

#include "rkav/output/packet_sink.h"

namespace rkav {

class ValidatingNullSink final : public IPacketSink {
   public:
    /// 重置包计数和每路 DTS，并进入可写状态。
    Result<void> Open(const OutputConfig& config) override;
    /// 校验包结构和可选 DTS 单调性，但不保存 payload。
    Result<void> Write(const EncodedPacket& packet) override;
    /// 标记刷新完成，之后拒绝 Write。
    Result<void> Flush() override;
    /// 关闭 Sink。
    void Close() noexcept override;
    /// 返回日志和指标使用的稳定名称。
    [[nodiscard]] std::string name() const override { return "null"; }

   private:
    /// 分别校验视频和音频 DTS 不倒退。
    Result<void> ValidateTimestamp(const EncodedPacket& packet);

    std::mutex mutex_;               // 保护以下 Sink 状态。
    OutputConfig config_;            // Open 后生效的配置快照。
    bool open_{false};               // 是否已经打开。
    bool flushed_{false};            // 是否已经完成 Flush。
    std::uint64_t packet_count_{0};  // 已成功消费包数，用于故障注入位置。
    std::optional<std::int64_t> last_video_dts_;  // 上一个视频包 DTS。
    std::optional<std::int64_t> last_audio_dts_;  // 上一个音频包 DTS。
};

class JsonLinePacketSink final : public IPacketSink {
   public:
    /// 创建父目录并以截断模式打开 JSONL 文件。
    Result<void> Open(const OutputConfig& config) override;
    /// 把一个编码包的元数据写成单行 JSON，不写二进制 payload。
    Result<void> Write(const EncodedPacket& packet) override;
    /// 刷新文件流并结束写入。
    Result<void> Flush() override;
    /// 关闭文件流。
    void Close() noexcept override;
    /// 返回日志和指标使用的稳定名称。
    [[nodiscard]] std::string name() const override { return "jsonl"; }

   private:
    std::mutex mutex_;               // 保护文件流和状态。
    OutputConfig config_;            // 输出路径、延迟和故障注入配置。
    std::ofstream output_;           // JSONL 文件输出流。
    std::uint64_t packet_count_{0};  // 已成功写入包数。
    bool open_{false};               // 文件是否已打开。
    bool flushed_{false};            // 是否已完成最终刷新。
};

/// 根据 config.type 创建具体 Sink；未知类型返回 kNotSupported。
Result<std::unique_ptr<IPacketSink>> CreatePacketSink(const OutputConfig& config);

}  // namespace rkav
