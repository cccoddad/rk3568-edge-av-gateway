// 文件作用：实现用内容摘要代替真实压缩码流的视频/音频测试编码器。
// 主要知识点：编码器状态机、FNV-1a、时间基、关键帧周期、PCM PTS 连续性和移动语义。
#include "rkav/media/checksum_encoder.h"

#include <array>
#include <cstring>
#include <utility>

#include "rkav/common/checksum.h"
#include "rkav/common/clock.h"

namespace rkav {
namespace {

struct ChecksumPayload {
    std::uint64_t sequence{0};    // 来源帧或音频块序号。
    std::uint64_t byte_count{0};  // 原始输入字节数。
    std::uint64_t checksum{0};    // 原始内容的 FNV-1a 摘要。
};

/// 功能：把输入 Buffer 的基本信息和摘要序列化成固定大小的 Mock payload。
std::shared_ptr<Buffer> MakePayload(std::uint64_t sequence, const Buffer& input) {
    // Mock packet 只保存序号、原始字节数和摘要，不伪装成可播放的 H.264/AAC。
    const ChecksumPayload payload{sequence, static_cast<std::uint64_t>(input.size()),
                                  Fnv1a64(input.span())};
    auto output = Buffer::Allocate(sizeof(payload));  // 固定长度输出 Buffer。
    std::memcpy(output->data(), &payload, sizeof(payload));
    return output;
}

/// 功能：构造指定编码器模块的统一错误。
Error EncoderError(std::string_view module, ErrorCategory category, std::string message) {
    return Error{category, 0, std::string(module), "encode", std::move(message), false};
}

}  // namespace

/// 功能：保存视频关键帧周期，重置 flushed 状态并打开编码器。
Result<void> ChecksumVideoEncoder::Open(const VideoEncoderConfig& config) {
    std::scoped_lock lock(mutex_);
    if (open_) {
        return Result<void>::Failure(EncoderError(
            "checksum_video_encoder", ErrorCategory::kInvalidState, "encoder is already open"));
    }
    config_ = config;
    open_ = true;
    flushed_ = false;
    return Result<void>::Success();
}

/// 功能：校验原始帧、换算 PTS，并输出一个 Mock 视频摘要包。
Result<std::vector<EncodedPacket>> ChecksumVideoEncoder::Encode(const VideoFrame& frame) {
    std::scoped_lock lock(mutex_);
    if (!open_ || flushed_) {
        return Result<std::vector<EncodedPacket>>::Failure(
            EncoderError("checksum_video_encoder", ErrorCategory::kInvalidState,
                         "encoder is not accepting frames"));
    }
    auto validation = ValidateVideoFrame(frame);  // 跨后端统一帧布局校验结果。
    if (!validation) {
        return Result<std::vector<EncodedPacket>>::Failure(validation.error());
    }
    auto pts = RescaleTimestamp(frame.pts_us, Rational{1, 1'000'000});  // 包时间基仍为微秒。
    if (!pts) {
        return Result<std::vector<EncodedPacket>>::Failure(pts.error());
    }
    EncodedPacket packet{
        StreamKind::kVideo,
        frame.sequence,
        pts.value(),
        pts.value(),
        Rational{1, 1'000'000},
        frame.sequence % static_cast<std::uint64_t>(config_.mock_keyframe_interval) == 0U,
        Codec::kMockVideoChecksum,
        MakePayload(frame.sequence, *frame.buffer)};
    return Result<std::vector<EncodedPacket>>::Success({std::move(packet)});
}

/// 功能：结束视频输入；Mock 没有延迟缓存，因此不会产生尾包。
Result<std::vector<EncodedPacket>> ChecksumVideoEncoder::Flush() {
    std::scoped_lock lock(mutex_);
    if (!open_) {
        return Result<std::vector<EncodedPacket>>::Failure(EncoderError(
            "checksum_video_encoder", ErrorCategory::kInvalidState, "encoder is not open"));
    }
    flushed_ = true;
    return Result<std::vector<EncodedPacket>>::Success({});
}

/// 功能：关闭视频编码器并拒绝后续 Encode。
void ChecksumVideoEncoder::Close() noexcept {
    std::scoped_lock lock(mutex_);
    open_ = false;
}

/// 功能：重置期望 PTS 并打开音频编码器；当前配置没有额外字段。
Result<void> ChecksumAudioEncoder::Open(const AudioEncoderConfig&) {
    std::scoped_lock lock(mutex_);
    if (open_) {
        return Result<void>::Failure(EncoderError(
            "checksum_audio_encoder", ErrorCategory::kInvalidState, "encoder is already open"));
    }
    open_ = true;
    flushed_ = false;
    expected_next_pts_us_.reset();
    return Result<void>::Success();
}

/// 功能：校验 PCM 布局与连续 PTS，然后输出一个 Mock 音频摘要包。
Result<std::vector<EncodedPacket>> ChecksumAudioEncoder::Encode(const AudioFrame& frame) {
    std::scoped_lock lock(mutex_);
    if (!open_ || flushed_) {
        return Result<std::vector<EncodedPacket>>::Failure(
            EncoderError("checksum_audio_encoder", ErrorCategory::kInvalidState,
                         "encoder is not accepting frames"));
    }
    auto validation = ValidateAudioFrame(frame);
    if (!validation) {
        return Result<std::vector<EncodedPacket>>::Failure(validation.error());
    }
    // 音频不允许跳块或倒退，任何 PTS 不连续都说明上游已经破坏 PCM 连续性。
    if (expected_next_pts_us_.has_value() && frame.pts_us != *expected_next_pts_us_) {
        return Result<std::vector<EncodedPacket>>::Failure(EncoderError(
            "checksum_audio_encoder", ErrorCategory::kCodec, "audio PTS is not continuous"));
    }
    const TimestampUs duration_us =
        static_cast<TimestampUs>(frame.samples_per_channel) * 1'000'000 / frame.sample_rate;
    // 下一块 PTS = 当前块 PTS + 当前块真实持续时间。
    expected_next_pts_us_ = frame.pts_us + duration_us;

    EncodedPacket packet{StreamKind::kAudio,
                         frame.sequence,
                         frame.pts_us,
                         frame.pts_us,
                         Rational{1, 1'000'000},
                         true,
                         Codec::kMockAudioChecksum,
                         MakePayload(frame.sequence, *frame.buffer)};
    return Result<std::vector<EncodedPacket>>::Success({std::move(packet)});
}

/// 功能：结束音频输入；Mock 没有延迟缓存，因此不会产生尾包。
Result<std::vector<EncodedPacket>> ChecksumAudioEncoder::Flush() {
    std::scoped_lock lock(mutex_);
    if (!open_) {
        return Result<std::vector<EncodedPacket>>::Failure(EncoderError(
            "checksum_audio_encoder", ErrorCategory::kInvalidState, "encoder is not open"));
    }
    flushed_ = true;
    return Result<std::vector<EncodedPacket>>::Success({});
}

/// 功能：关闭音频编码器并拒绝后续 Encode。
void ChecksumAudioEncoder::Close() noexcept {
    std::scoped_lock lock(mutex_);
    open_ = false;
}

}  // namespace rkav
