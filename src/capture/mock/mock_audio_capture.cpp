// 文件作用：实现可实时运行、可故障注入的确定性 PCM 音频源。
// 主要知识点：PCM 交错布局、正弦波采样、相位连续、PTS 计算、锁粒度和 XRUN 恢复。
#include "rkav/capture/mock_audio_capture.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <numbers>
#include <utility>

namespace rkav {
namespace {

/// 功能：构造 mock_audio 模块统一错误；retryable 决定上层是否可退避重试。
Error AudioError(ErrorCategory category, std::string message, bool retryable) {
    return Error{category, 0, "mock_audio", "read", std::move(message), retryable};
}

}  // namespace

/// 功能：保存外部统一时钟，不在构造阶段打开采集器。
MockAudioCapture::MockAudioCapture(std::shared_ptr<IClock> clock) : clock_(std::move(clock)) {}

/// 功能：应用配置、重置音频时间轴和故障注入状态。
/// 返回：成功时返回实际采样率、声道、每块样本数和样本格式。
Result<AudioCapabilities> MockAudioCapture::Open(const AudioConfig& config) {
    std::scoped_lock lock(mutex_);
    if (open_) {
        return Result<AudioCapabilities>::Failure(
            AudioError(ErrorCategory::kInvalidState, "capture is already open", false));
    }
    config_ = config;
    start_us_ = clock_->NowUs();
    block_sequence_ = 0;
    sample_index_ = 0;
    last_injected_xrun_sequence_.reset();
    open_ = true;
    const int samples = config.sample_rate * config.frame_duration_ms / 1000;  // 每声道每块样本数。
    return Result<AudioCapabilities>::Success(
        AudioCapabilities{config.sample_rate, config.channels, samples, config.format});
}

/// 功能：生成一个连续 PCM 数据块，或在配置指定的位置返回可诊断故障。
/// 参数 stop：停止时可中断实时节拍等待；返回 kCancelled 不算设备错误。
Result<AudioFrame> MockAudioCapture::Read(std::stop_token stop) {
    AudioConfig config;              // 本次读取使用的配置快照。
    TimestampUs start_us = 0;        // 音频时间轴起点，单位微秒。
    std::uint64_t sequence = 0;      // 本次准备生成的逻辑块序号。
    std::uint64_t first_sample = 0;  // 本块首样本在连续波形中的绝对索引。
    if (stop.stop_requested()) {
        return Result<AudioFrame>::Failure(
            AudioError(ErrorCategory::kCancelled, "read was cancelled", false));
    }
    {
        // 只在复制共享状态时持锁，生成大量样本时不占用 mutex_。
        std::scoped_lock lock(mutex_);
        if (!open_) {
            if (stop.stop_requested()) {
                return Result<AudioFrame>::Failure(
                    AudioError(ErrorCategory::kCancelled, "read was cancelled", false));
            }
            return Result<AudioFrame>::Failure(
                AudioError(ErrorCategory::kInvalidState, "capture is not open", false));
        }
        config = config_;
        start_us = start_us_;
        sequence = block_sequence_;
        first_sample = sample_index_;
    }

    if (stop.stop_requested()) {
        return Result<AudioFrame>::Failure(
            AudioError(ErrorCategory::kCancelled, "read was cancelled", false));
    }
    if (config.failure.disconnect_after_blocks.has_value() &&
        sequence >= *config.failure.disconnect_after_blocks) {
        return Result<AudioFrame>::Failure(AudioError(ErrorCategory::kDeviceDisconnected,
                                                      "injected microphone disconnection", true));
    }
    if (config.failure.xrun_every_blocks.has_value() && sequence > 0U &&
        sequence % *config.failure.xrun_every_blocks == 0U) {
        std::scoped_lock lock(mutex_);
        if (last_injected_xrun_sequence_ != sequence) {
            // 保留当前逻辑块；Recover 后下一次 Read 重试同一块，不静默制造时间戳缺口。
            last_injected_xrun_sequence_ = sequence;
            return Result<AudioFrame>::Failure(
                AudioError(ErrorCategory::kXrun, "injected audio XRUN", true));
        }
    }

    const TimestampUs frame_duration_us = static_cast<TimestampUs>(config.frame_duration_ms) * 1000;
    const TimestampUs pts_us =
        start_us + static_cast<TimestampUs>(sequence) * frame_duration_us;  // 本块 PTS。
    if (config.realtime && !clock_->WaitUntil(pts_us, stop)) {
        return Result<AudioFrame>::Failure(
            AudioError(ErrorCategory::kCancelled, "read was cancelled", false));
    }

    const int samples_per_channel =
        config.sample_rate * config.frame_duration_ms / 1000;  // 单声道样本数。
    const std::size_t total_samples =
        static_cast<std::size_t>(samples_per_channel) *
        static_cast<std::size_t>(config.channels);  // 全声道样本总数。
    auto buffer = Buffer::Allocate(total_samples * sizeof(std::int16_t));  // S16_LE 输出内存。
    auto* output = reinterpret_cast<std::int16_t*>(buffer->data());  // 便于按 int16_t 写样本。
    // 相位基于全局 sample_index，而不是每块从零开始，保证块边界波形连续。
    for (int sample = 0; sample < samples_per_channel; ++sample) {
        std::int16_t value = 0;
        if (config.signal == "sine") {
            const double phase =
                2.0 * std::numbers::pi * config.frequency_hz *
                static_cast<double>(first_sample + static_cast<std::uint64_t>(sample)) /
                static_cast<double>(config.sample_rate);  // 当前样本弧度相位。
            const double normalized = std::clamp(config.amplitude * std::sin(phase), -1.0, 1.0);
            value = static_cast<std::int16_t>(std::lround(
                normalized * static_cast<double>(std::numeric_limits<std::int16_t>::max())));
        }
        for (int channel = 0; channel < config.channels; ++channel) {
            output[static_cast<std::size_t>(sample) * static_cast<std::size_t>(config.channels) +
                   static_cast<std::size_t>(channel)] = value;
        }
    }
    {
        std::scoped_lock lock(mutex_);
        ++block_sequence_;
        sample_index_ += static_cast<std::uint64_t>(samples_per_channel);
    }

    AudioFrame frame{sequence,
                     pts_us,
                     config.sample_rate,
                     config.channels,
                     samples_per_channel,
                     SampleFormat::kS16LE,
                     std::move(buffer)};
    return Result<AudioFrame>::Success(std::move(frame));
}

/// 功能：模拟驱动完成 XRUN 恢复；Mock 无硬件命令，只验证当前仍处于打开状态。
Result<void> MockAudioCapture::Recover() {
    std::scoped_lock lock(mutex_);
    if (!open_) {
        return Result<void>::Failure(
            AudioError(ErrorCategory::kInvalidState, "capture is not open", false));
    }
    return Result<void>::Success();
}

/// 功能：线程安全地关闭采集器；函数不抛异常且允许重复调用。
void MockAudioCapture::Close() noexcept {
    std::scoped_lock lock(mutex_);
    open_ = false;
}

}  // namespace rkav
