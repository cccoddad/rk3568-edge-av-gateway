// 文件作用：实现 Linux V4L2 MMAP 视频采集、格式/帧率协商和可取消超时读取。
// 主要知识点：open/ioctl/poll/mmap、VIDIOC_DQBUF/QBUF、RAII 清理和统一单调 PTS。
#include "rkav/capture/v4l2_video_capture.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <system_error>
#include <utility>

namespace rkav {
namespace {

Error V4L2Error(ErrorCategory category, std::string_view operation, std::string message,
                int native_code = 0, bool retryable = false) {
    if (native_code != 0) {
        message += ": " + std::error_code(native_code, std::generic_category()).message();
    }
    return Error{category,           native_code, "v4l2_video", std::string(operation),
                 std::move(message), retryable};
}

int IoctlRetry(int file_descriptor, unsigned long request, void* argument) {
    int result = 0;
    do {
        result = ::ioctl(file_descriptor, request, argument);
    } while (result == -1 && errno == EINTR);
    return result;
}

std::uint32_t ToFourcc(PixelFormat format) {
    switch (format) {
        case PixelFormat::kMjpeg:
            return V4L2_PIX_FMT_MJPEG;
        case PixelFormat::kYuyv422:
            return V4L2_PIX_FMT_YUYV;
        case PixelFormat::kNv12:
            return V4L2_PIX_FMT_NV12;
        case PixelFormat::kRgb888:
            return V4L2_PIX_FMT_RGB24;
        case PixelFormat::kBgr888:
            return V4L2_PIX_FMT_BGR24;
        case PixelFormat::kUnknown:
            break;
    }
    return 0U;
}

PixelFormat FromFourcc(std::uint32_t fourcc) {
    switch (fourcc) {
        case V4L2_PIX_FMT_MJPEG:
            return PixelFormat::kMjpeg;
        case V4L2_PIX_FMT_YUYV:
            return PixelFormat::kYuyv422;
        case V4L2_PIX_FMT_NV12:
            return PixelFormat::kNv12;
        case V4L2_PIX_FMT_RGB24:
            return PixelFormat::kRgb888;
        case V4L2_PIX_FMT_BGR24:
            return PixelFormat::kBgr888;
        default:
            return PixelFormat::kUnknown;
    }
}

}  // namespace

V4L2VideoCapture::V4L2VideoCapture(std::shared_ptr<IClock> clock) : clock_(std::move(clock)) {}

V4L2VideoCapture::~V4L2VideoCapture() { Close(); }

Result<VideoCapabilities> V4L2VideoCapture::Open(const VideoConfig& config) {
    std::scoped_lock lock(mutex_);
    if (open_) {
        return Result<VideoCapabilities>::Failure(
            V4L2Error(ErrorCategory::kInvalidState, "open", "capture device is already open"));
    }

    file_descriptor_ = ::open(config.device.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (file_descriptor_ < 0) {
        const int native_code = errno;
        const ErrorCategory category =
            native_code == ENOENT ? ErrorCategory::kDeviceNotFound : ErrorCategory::kIo;
        return Result<VideoCapabilities>::Failure(
            V4L2Error(category, "open", "cannot open " + config.device, native_code, true));
    }

    const auto fail_and_close = [this](Error error) {
        CloseUnlocked();
        return Result<VideoCapabilities>::Failure(std::move(error));
    };

    v4l2_capability capability{};
    if (IoctlRetry(file_descriptor_, VIDIOC_QUERYCAP, &capability) < 0) {
        return fail_and_close(
            V4L2Error(ErrorCategory::kIo, "query_capabilities", "VIDIOC_QUERYCAP failed", errno));
    }
    const std::uint32_t device_caps = (capability.capabilities & V4L2_CAP_DEVICE_CAPS) != 0U
                                          ? capability.device_caps
                                          : capability.capabilities;
    if ((device_caps & V4L2_CAP_VIDEO_CAPTURE) == 0U || (device_caps & V4L2_CAP_STREAMING) == 0U) {
        return fail_and_close(V4L2Error(ErrorCategory::kNotSupported, "query_capabilities",
                                        "device lacks video-capture or streaming capability"));
    }

    const std::uint32_t requested_fourcc = ToFourcc(config.format);
    if (requested_fourcc == 0U) {
        return fail_and_close(V4L2Error(ErrorCategory::kNotSupported, "set_format",
                                        "requested pixel format has no V4L2 mapping"));
    }
    v4l2_format format{};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = static_cast<std::uint32_t>(config.width);
    format.fmt.pix.height = static_cast<std::uint32_t>(config.height);
    format.fmt.pix.pixelformat = requested_fourcc;
    format.fmt.pix.field = V4L2_FIELD_ANY;
    if (IoctlRetry(file_descriptor_, VIDIOC_S_FMT, &format) < 0) {
        return fail_and_close(
            V4L2Error(ErrorCategory::kNotSupported, "set_format", "VIDIOC_S_FMT failed", errno));
    }
    const PixelFormat negotiated_format = FromFourcc(format.fmt.pix.pixelformat);
    if (negotiated_format == PixelFormat::kUnknown) {
        return fail_and_close(V4L2Error(ErrorCategory::kNotSupported, "set_format",
                                        "driver negotiated an unsupported pixel format"));
    }
    if (format.fmt.pix.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        format.fmt.pix.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        format.fmt.pix.bytesperline > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        return fail_and_close(V4L2Error(ErrorCategory::kResourceExhausted, "set_format",
                                        "negotiated layout exceeds application limits"));
    }

    v4l2_streamparm parameters{};
    parameters.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parameters.parm.capture.timeperframe.numerator = 1U;
    parameters.parm.capture.timeperframe.denominator = static_cast<std::uint32_t>(config.fps);
    if (IoctlRetry(file_descriptor_, VIDIOC_S_PARM, &parameters) < 0) {
        return fail_and_close(V4L2Error(ErrorCategory::kNotSupported, "set_frame_rate",
                                        "VIDIOC_S_PARM failed", errno));
    }
    const auto& time_per_frame = parameters.parm.capture.timeperframe;
    int negotiated_fps = config.fps;
    if (time_per_frame.numerator > 0U && time_per_frame.denominator > 0U) {
        const std::uint32_t rounded =
            (time_per_frame.denominator + time_per_frame.numerator / 2U) / time_per_frame.numerator;
        if (rounded > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
            return fail_and_close(V4L2Error(ErrorCategory::kResourceExhausted, "set_frame_rate",
                                            "negotiated frame rate exceeds application limits"));
        }
        negotiated_fps = static_cast<int>(rounded);
    }

    v4l2_requestbuffers request{};
    request.count = static_cast<std::uint32_t>(config.mmap_buffer_count);
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;
    if (IoctlRetry(file_descriptor_, VIDIOC_REQBUFS, &request) < 0) {
        return fail_and_close(V4L2Error(ErrorCategory::kNotSupported, "request_buffers",
                                        "VIDIOC_REQBUFS failed", errno));
    }
    if (request.count < 2U) {
        return fail_and_close(V4L2Error(ErrorCategory::kResourceExhausted, "request_buffers",
                                        "driver returned fewer than two MMAP buffers"));
    }

    buffers_.reserve(request.count);
    for (std::uint32_t index = 0; index < request.count; ++index) {
        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = index;
        if (IoctlRetry(file_descriptor_, VIDIOC_QUERYBUF, &buffer) < 0) {
            return fail_and_close(
                V4L2Error(ErrorCategory::kIo, "query_buffer", "VIDIOC_QUERYBUF failed", errno));
        }
        void* address = ::mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                               file_descriptor_, static_cast<off_t>(buffer.m.offset));
        if (address == MAP_FAILED) {
            return fail_and_close(
                V4L2Error(ErrorCategory::kIo, "map_buffer", "mmap failed", errno));
        }
        buffers_.push_back(MappedBuffer{address, static_cast<std::size_t>(buffer.length)});
    }

    for (std::uint32_t index = 0; index < request.count; ++index) {
        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = index;
        if (IoctlRetry(file_descriptor_, VIDIOC_QBUF, &buffer) < 0) {
            return fail_and_close(
                V4L2Error(ErrorCategory::kIo, "queue_buffer", "VIDIOC_QBUF failed", errno));
        }
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (IoctlRetry(file_descriptor_, VIDIOC_STREAMON, &type) < 0) {
        return fail_and_close(
            V4L2Error(ErrorCategory::kIo, "stream_on", "VIDIOC_STREAMON failed", errno));
    }

    config_ = config;
    capabilities_ = VideoCapabilities{static_cast<int>(format.fmt.pix.width),
                                      static_cast<int>(format.fmt.pix.height), negotiated_fps,
                                      negotiated_format};
    stride_ = 0;
    if (negotiated_format != PixelFormat::kMjpeg) {
        stride_ = static_cast<int>(format.fmt.pix.bytesperline);
    }
    if (negotiated_format == PixelFormat::kYuyv422 && stride_ == 0) {
        stride_ = capabilities_.width * 2;
    }
    sequence_ = 0;
    streaming_ = true;
    open_ = true;
    return Result<VideoCapabilities>::Success(capabilities_);
}

Result<VideoFrame> V4L2VideoCapture::Read(std::stop_token stop) {
    if (stop.stop_requested()) {
        return Result<VideoFrame>::Failure(
            V4L2Error(ErrorCategory::kCancelled, "read", "read was cancelled"));
    }
    std::scoped_lock lock(mutex_);
    if (!open_ || file_descriptor_ < 0) {
        if (stop.stop_requested()) {
            return Result<VideoFrame>::Failure(
                V4L2Error(ErrorCategory::kCancelled, "read", "read was cancelled"));
        }
        return Result<VideoFrame>::Failure(
            V4L2Error(ErrorCategory::kInvalidState, "read", "capture device is not open"));
    }
    if (stop.stop_requested()) {
        return Result<VideoFrame>::Failure(
            V4L2Error(ErrorCategory::kCancelled, "read", "read was cancelled"));
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(config_.capture_timeout_ms);
    while (!stop.stop_requested()) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            return Result<VideoFrame>::Failure(V4L2Error(
                ErrorCategory::kTimeout, "poll", "timed out waiting for a video frame", 0, true));
        }
        const int poll_timeout = static_cast<int>(std::min<std::int64_t>(remaining.count(), 50));
        pollfd descriptor{file_descriptor_, static_cast<short>(POLLIN | POLLPRI), 0};
        const int poll_result = ::poll(&descriptor, 1, poll_timeout);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return Result<VideoFrame>::Failure(
                V4L2Error(ErrorCategory::kIo, "poll", "poll failed", errno, true));
        }
        if (poll_result == 0) {
            continue;
        }
        if ((descriptor.revents & static_cast<short>(POLLERR | POLLHUP | POLLNVAL)) != 0) {
            return Result<VideoFrame>::Failure(
                V4L2Error(ErrorCategory::kDeviceDisconnected, "poll",
                          "video device reported a poll error or disconnect", 0, true));
        }
        if ((descriptor.revents & static_cast<short>(POLLIN | POLLPRI)) != 0) {
            break;
        }
    }
    if (stop.stop_requested()) {
        return Result<VideoFrame>::Failure(
            V4L2Error(ErrorCategory::kCancelled, "read", "read was cancelled"));
    }

    v4l2_buffer dequeued{};
    dequeued.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    dequeued.memory = V4L2_MEMORY_MMAP;
    if (IoctlRetry(file_descriptor_, VIDIOC_DQBUF, &dequeued) < 0) {
        const int native_code = errno;
        const ErrorCategory category =
            native_code == ENODEV ? ErrorCategory::kDeviceDisconnected : ErrorCategory::kIo;
        return Result<VideoFrame>::Failure(
            V4L2Error(category, "dequeue_buffer", "VIDIOC_DQBUF failed", native_code, true));
    }
    if (dequeued.index >= buffers_.size() || dequeued.bytesused == 0U ||
        static_cast<std::size_t>(dequeued.bytesused) > buffers_[dequeued.index].length) {
        if (dequeued.index < buffers_.size()) {
            static_cast<void>(IoctlRetry(file_descriptor_, VIDIOC_QBUF, &dequeued));
        }
        return Result<VideoFrame>::Failure(
            V4L2Error(ErrorCategory::kIo, "dequeue_buffer", "driver returned an invalid buffer"));
    }

    std::shared_ptr<Buffer> payload;
    try {
        payload = Buffer::Allocate(static_cast<std::size_t>(dequeued.bytesused));
        std::memcpy(payload->data(), buffers_[dequeued.index].address,
                    static_cast<std::size_t>(dequeued.bytesused));
    } catch (const std::bad_alloc&) {
        static_cast<void>(IoctlRetry(file_descriptor_, VIDIOC_QBUF, &dequeued));
        return Result<VideoFrame>::Failure(V4L2Error(
            ErrorCategory::kResourceExhausted, "copy_buffer", "cannot allocate frame buffer"));
    }

    if (IoctlRetry(file_descriptor_, VIDIOC_QBUF, &dequeued) < 0) {
        return Result<VideoFrame>::Failure(
            V4L2Error(ErrorCategory::kIo, "requeue_buffer", "VIDIOC_QBUF failed", errno, true));
    }

    VideoFrame frame{sequence_++, clock_->NowUs(),      capabilities_.width, capabilities_.height,
                     stride_,     capabilities_.format, std::move(payload),  FrameMemory{}};
    return Result<VideoFrame>::Success(std::move(frame));
}

void V4L2VideoCapture::Close() noexcept {
    std::scoped_lock lock(mutex_);
    CloseUnlocked();
}

void V4L2VideoCapture::CloseUnlocked() noexcept {
    if (streaming_ && file_descriptor_ >= 0) {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        static_cast<void>(IoctlRetry(file_descriptor_, VIDIOC_STREAMOFF, &type));
    }
    streaming_ = false;
    for (const auto& buffer : buffers_) {
        if (buffer.address != nullptr && buffer.address != MAP_FAILED && buffer.length > 0U) {
            static_cast<void>(::munmap(buffer.address, buffer.length));
        }
    }
    buffers_.clear();
    if (file_descriptor_ >= 0) {
        static_cast<void>(::close(file_descriptor_));
    }
    file_descriptor_ = -1;
    stride_ = 0;
    sequence_ = 0;
    open_ = false;
}

}  // namespace rkav
