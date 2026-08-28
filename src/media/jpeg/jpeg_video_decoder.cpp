// 文件作用：使用固定版本 libjpeg-turbo 将内存中的 MJPEG/JPEG 帧解码为紧密 RGB888。
// 主要知识点：TurboJPEG 内存 API、尺寸溢出保护、RAII 和可恢复坏帧分类。
#include "rkav/media/jpeg_video_decoder.h"

#include <turbojpeg.h>

#include <cstddef>
#include <limits>
#include <new>
#include <string>
#include <utility>

namespace rkav {
namespace {

constexpr int kMaximumWidth = 7680;
constexpr int kMaximumHeight = 4320;
constexpr int kRgbChannels = 3;

Error DecoderError(ErrorCategory category, std::string operation, std::string message,
                   bool retryable = false) {
    return Error{category, 0, "jpeg_decoder", std::move(operation), std::move(message), retryable};
}

std::string TurboJpegMessage(tjhandle handle, std::string prefix) {
    const char* const detail = tj3GetErrorStr(handle);
    if (detail != nullptr && detail[0] != '\0') {
        prefix += ": ";
        prefix += detail;
    }
    return prefix;
}

}  // namespace

struct JpegVideoDecoder::Impl {
    tjhandle handle{nullptr};

    ~Impl() {
        if (handle != nullptr) {
            tj3Destroy(handle);
        }
    }
};

JpegVideoDecoder::JpegVideoDecoder() = default;

JpegVideoDecoder::~JpegVideoDecoder() { Close(); }

Result<void> JpegVideoDecoder::Open() {
    std::scoped_lock lock(mutex_);
    if (impl_) {
        return Result<void>::Failure(
            DecoderError(ErrorCategory::kInvalidState, "open", "decoder is already open"));
    }

    auto candidate = std::make_unique<Impl>();
    candidate->handle = tj3Init(TJINIT_DECOMPRESS);
    if (candidate->handle == nullptr) {
        return Result<void>::Failure(DecoderError(ErrorCategory::kResourceExhausted, "open",
                                                  "cannot create TurboJPEG decompressor"));
    }
    const int maximum_pixels = kMaximumWidth * kMaximumHeight;
    if (tj3Set(candidate->handle, TJPARAM_MAXPIXELS, maximum_pixels) < 0) {
        return Result<void>::Failure(
            DecoderError(ErrorCategory::kInvalidState, "configure",
                         TurboJpegMessage(candidate->handle, "cannot set JPEG pixel limit")));
    }
    impl_ = std::move(candidate);
    return Result<void>::Success();
}

Result<VideoFrame> JpegVideoDecoder::Decode(const VideoFrame& frame) {
    std::scoped_lock lock(mutex_);
    if (!impl_) {
        return Result<VideoFrame>::Failure(
            DecoderError(ErrorCategory::kInvalidState, "decode", "decoder is not open"));
    }
    auto validation = ValidateVideoFrame(frame);
    if (!validation) {
        return Result<VideoFrame>::Failure(validation.error());
    }
    if (frame.memory.kind != MemoryKind::kCpu || frame.format != PixelFormat::kMjpeg) {
        return Result<VideoFrame>::Failure(
            DecoderError(ErrorCategory::kNotSupported, "decode",
                         "JPEG decoder requires a CPU MJPEG frame"));
    }

    const auto* const compressed =
        reinterpret_cast<const unsigned char*>(frame.buffer->data());
    const std::size_t compressed_size = frame.buffer->size();
    if (tj3DecompressHeader(impl_->handle, compressed, compressed_size) < 0) {
        return Result<VideoFrame>::Failure(
            DecoderError(ErrorCategory::kCodec, "read_header",
                         TurboJpegMessage(impl_->handle, "invalid or incomplete JPEG header"),
                         true));
    }

    const int width = tj3Get(impl_->handle, TJPARAM_JPEGWIDTH);
    const int height = tj3Get(impl_->handle, TJPARAM_JPEGHEIGHT);
    if (width <= 0 || height <= 0 || width > kMaximumWidth || height > kMaximumHeight ||
        width > std::numeric_limits<int>::max() / kRgbChannels) {
        return Result<VideoFrame>::Failure(
            DecoderError(ErrorCategory::kCodec, "validate_dimensions",
                         "JPEG dimensions are outside the supported range", true));
    }
    const int stride = width * kRgbChannels;
    const auto row_bytes = static_cast<std::size_t>(stride);
    const auto rows = static_cast<std::size_t>(height);
    if (rows > std::numeric_limits<std::size_t>::max() / row_bytes) {
        return Result<VideoFrame>::Failure(
            DecoderError(ErrorCategory::kResourceExhausted, "allocate",
                         "decoded JPEG size overflows address space"));
    }

    std::shared_ptr<Buffer> pixels;
    try {
        pixels = Buffer::Allocate(row_bytes * rows);
    } catch (const std::bad_alloc&) {
        return Result<VideoFrame>::Failure(
            DecoderError(ErrorCategory::kResourceExhausted, "allocate",
                         "cannot allocate decoded RGB frame"));
    }

    auto* const destination = reinterpret_cast<unsigned char*>(pixels->data());
    if (tj3Decompress8(impl_->handle, compressed, compressed_size, destination, stride, TJPF_RGB) <
        0) {
        return Result<VideoFrame>::Failure(
            DecoderError(ErrorCategory::kCodec, "decompress",
                         TurboJpegMessage(impl_->handle, "JPEG decompression failed"), true));
    }

    VideoFrame decoded{frame.sequence, frame.pts_us, width, height, stride, PixelFormat::kRgb888,
                       std::move(pixels), FrameMemory{MemoryKind::kCpu, -1}};
    auto decoded_validation = ValidateVideoFrame(decoded);
    if (!decoded_validation) {
        return Result<VideoFrame>::Failure(decoded_validation.error());
    }
    return Result<VideoFrame>::Success(std::move(decoded));
}

void JpegVideoDecoder::Close() noexcept {
    std::scoped_lock lock(mutex_);
    impl_.reset();
}

}  // namespace rkav
