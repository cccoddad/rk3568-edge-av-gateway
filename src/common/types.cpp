// 文件作用：实现媒体类型转文本，以及视频帧、音频帧和编码包的数据合法性校验。
// 主要知识点：像素格式内存布局、PCM 大小计算、整数溢出保护、跨模块防御式校验。
#include "rkav/common/types.h"

#include <cstddef>
#include <limits>
#include <utility>

namespace rkav {
namespace {

/// 功能：构造 types 模块统一使用的不可重试校验错误。
Error ValidationError(std::string message) {
    return Error{ErrorCategory::kInvalidConfig, 0, "types", "validate", std::move(message),
                 false};
}

/// 功能：根据像素格式、stride 和高度计算一帧至少需要多少字节。
/// 返回：成功时为最小字节数；尺寸、格式或计算溢出时为 Error。
Result<std::size_t> MinimumVideoBytes(const VideoFrame& frame) {
    if (frame.width <= 0 || frame.height <= 0 || frame.stride <= 0) {
        return Result<std::size_t>::Failure(ValidationError("invalid video dimensions or stride"));
    }

    const auto height = static_cast<std::size_t>(frame.height);  // 图像行数。
    const auto stride = static_cast<std::size_t>(frame.stride);  // 每行实际字节数。
    if (height > std::numeric_limits<std::size_t>::max() / stride) {
        return Result<std::size_t>::Failure(ValidationError("video size calculation overflow"));
    }
    // stride 已包含每行可能存在的对齐填充，不能简单使用 width * height。
    const std::size_t luma_bytes = height * stride;

    switch (frame.format) {
        case PixelFormat::kRgb888:
        case PixelFormat::kBgr888:
            if (frame.width > std::numeric_limits<int>::max() / 3 ||
                frame.stride < frame.width * 3) {
                return Result<std::size_t>::Failure(
                    ValidationError("RGB stride is smaller than width * 3"));
            }
            return Result<std::size_t>::Success(luma_bytes);
        case PixelFormat::kYuyv422:
            if (frame.width > std::numeric_limits<int>::max() / 2 ||
                frame.stride < frame.width * 2) {
                return Result<std::size_t>::Failure(
                    ValidationError("YUYV stride is smaller than width * 2"));
            }
            return Result<std::size_t>::Success(luma_bytes);
        case PixelFormat::kNv12:
            // NV12 = 完整 Y 平面 + 半尺寸交错 UV 平面，总大小为 Y 的 1.5 倍。
            if (luma_bytes > std::numeric_limits<std::size_t>::max() - luma_bytes / 2U) {
                return Result<std::size_t>::Failure(ValidationError("NV12 size overflow"));
            }
            return Result<std::size_t>::Success(luma_bytes + luma_bytes / 2U);
        case PixelFormat::kMjpeg:
            return Result<std::size_t>::Success(1U);
        case PixelFormat::kUnknown:
            return Result<std::size_t>::Failure(ValidationError("unknown pixel format"));
    }
    return Result<std::size_t>::Failure(ValidationError("unknown pixel format"));
}

}  // namespace

/// 功能：将像素格式转换成配置文件和日志使用的文本。
const char* ToString(PixelFormat format) noexcept {
    switch (format) {
        case PixelFormat::kUnknown:
            return "UNKNOWN";
        case PixelFormat::kMjpeg:
            return "MJPEG";
        case PixelFormat::kYuyv422:
            return "YUYV422";
        case PixelFormat::kNv12:
            return "NV12";
        case PixelFormat::kRgb888:
            return "RGB888";
        case PixelFormat::kBgr888:
            return "BGR888";
    }
    return "UNKNOWN";
}

/// 功能：将音频样本格式转换成文本。
const char* ToString(SampleFormat format) noexcept {
    return format == SampleFormat::kS16LE ? "S16_LE" : "F32_LE";
}

/// 功能：将音频/视频流类型转换成文本。
const char* ToString(StreamKind kind) noexcept {
    return kind == StreamKind::kVideo ? "video" : "audio";
}

/// 功能：将编码格式转换成文本；Mock 编码与真实编码使用不同名称。
const char* ToString(Codec codec) noexcept {
    switch (codec) {
        case Codec::kUnknown:
            return "unknown";
        case Codec::kMockVideoChecksum:
            return "mock_video_checksum";
        case Codec::kMockAudioChecksum:
            return "mock_audio_checksum";
        case Codec::kH264:
            return "h264";
        case Codec::kAac:
            return "aac";
    }
    return "unknown";
}

/// 功能：校验视频帧的缓冲区、布局和时间戳。
Result<void> ValidateVideoFrame(const VideoFrame& frame) {
    if (!frame.buffer) {
        return Result<void>::Failure(ValidationError("video buffer is null"));
    }
    auto minimum = MinimumVideoBytes(frame);  // 当前布局要求的最小字节数。
    if (!minimum) {
        return Result<void>::Failure(minimum.error());
    }
    if (frame.buffer->size() < minimum.value()) {
        return Result<void>::Failure(ValidationError("video buffer is smaller than its layout"));
    }
    if (frame.pts_us < 0) {
        return Result<void>::Failure(ValidationError("video PTS is negative"));
    }
    return Result<void>::Success();
}

/// 功能：校验 PCM 参数并确认缓冲区足够容纳声明的全部样本。
Result<void> ValidateAudioFrame(const AudioFrame& frame) {
    if (!frame.buffer) {
        return Result<void>::Failure(ValidationError("audio buffer is null"));
    }
    if (frame.sample_rate <= 0 || frame.channels <= 0 || frame.samples_per_channel <= 0) {
        return Result<void>::Failure(ValidationError("invalid audio dimensions"));
    }
    const std::size_t bytes_per_sample =
        frame.format == SampleFormat::kS16LE ? 2U : 4U;  // 单个声道样本字节数。
    const auto samples =
        static_cast<std::size_t>(frame.samples_per_channel);  // 每声道样本数。
    const auto channels = static_cast<std::size_t>(frame.channels);  // 声道数量。
    if (samples > std::numeric_limits<std::size_t>::max() / channels ||
        samples * channels > std::numeric_limits<std::size_t>::max() / bytes_per_sample) {
        return Result<void>::Failure(ValidationError("audio size calculation overflow"));
    }
    // PCM 最小字节数 = 每声道样本数 * 声道数 * 单样本字节数。
    const std::size_t minimum = samples * channels * bytes_per_sample;
    if (frame.buffer->size() < minimum) {
        return Result<void>::Failure(ValidationError("audio buffer is smaller than its layout"));
    }
    if (frame.pts_us < 0) {
        return Result<void>::Failure(ValidationError("audio PTS is negative"));
    }
    return Result<void>::Success();
}

/// 功能：校验编码包 payload、时间基以及媒体类型和 codec 是否匹配。
Result<void> ValidatePacket(const EncodedPacket& packet) {
    if (!packet.buffer || packet.buffer->size() == 0U) {
        return Result<void>::Failure(ValidationError("encoded packet buffer is empty"));
    }
    if (packet.codec == Codec::kUnknown) {
        return Result<void>::Failure(ValidationError("encoded packet codec is unknown"));
    }
    if (packet.time_base.numerator <= 0 || packet.time_base.denominator <= 0) {
        return Result<void>::Failure(ValidationError("encoded packet time base is invalid"));
    }
    // 防止音频包误用视频 codec，或视频包误用音频 codec。
    const bool codec_matches =
        (packet.kind == StreamKind::kVideo &&
         (packet.codec == Codec::kMockVideoChecksum || packet.codec == Codec::kH264)) ||
        (packet.kind == StreamKind::kAudio &&
         (packet.codec == Codec::kMockAudioChecksum || packet.codec == Codec::kAac));
    if (!codec_matches) {
        return Result<void>::Failure(ValidationError("packet codec does not match stream kind"));
    }
    return Result<void>::Success();
}

}  // namespace rkav
