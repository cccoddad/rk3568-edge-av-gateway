// 文件作用：实现按固定节拍生成 RGB 色条、移动框和可识别帧号的 Mock 视频源。
// 主要知识点：RGB888 内存布局、stride、绝对帧节拍、像素写入和可重复故障注入。
#include "rkav/capture/mock_video_capture.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <thread>

namespace rkav {
namespace {

/// 功能：构造 mock_video 模块统一错误。
Error VideoError(ErrorCategory category, std::string message, bool retryable) {
    return Error{category, 0, "mock_video", "read", std::move(message), retryable};
}

/// 功能：向 RGB888 帧的指定像素写入颜色；坐标越界时安全忽略。
void SetPixel(VideoFrame& frame, int x, int y, unsigned char red, unsigned char green,
              unsigned char blue) {
    if (x < 0 || y < 0 || x >= frame.width || y >= frame.height) {
        return;
    }
    const auto offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride) +
                        static_cast<std::size_t>(x) * 3U;  // 目标 R 字节在 Buffer 中的偏移。
    auto bytes = frame.buffer->writable_span();  // 不拥有内存的可写字节视图。
    bytes[offset] = static_cast<std::byte>(red);
    bytes[offset + 1U] = static_cast<std::byte>(green);
    bytes[offset + 2U] = static_cast<std::byte>(blue);
}

}  // namespace

/// 功能：根据帧序号生成水平方向移动的目标框，用于 Mock 推理和画面内容对账。
RectF SyntheticBoxForFrame(std::uint64_t sequence, int width, int height) {
    // 检测框按 sequence 水平移动，因此同一输入序号永远得到同一结果。
    const int box_width = std::max(8, width / 8);   // 目标框宽度，至少 8 像素。
    const int box_height = std::max(8, height / 6); // 目标框高度，至少 8 像素。
    const int travel = std::max(1, width - box_width); // 左上角可移动的水平范围。
    const int x = static_cast<int>(sequence % static_cast<std::uint64_t>(travel));
    const int y = std::max(0, (height - box_height) / 2); // 垂直方向保持居中。
    return RectF{static_cast<float>(x), static_cast<float>(y), static_cast<float>(box_width),
                 static_cast<float>(box_height)};
}

/// 功能：在已分配的 RGB888 Frame 中绘制可验证测试图。
/// 返回：Frame 布局不合法或不是 RGB888 时返回错误。
Result<void> GenerateRgbTestPattern(std::uint64_t sequence, VideoFrame& frame) {
    auto validation = ValidateVideoFrame(frame);
    if (!validation) {
        return validation;
    }
    if (frame.format != PixelFormat::kRgb888) {
        return Result<void>::Failure(
            VideoError(ErrorCategory::kNotSupported, "test pattern requires RGB888", false));
    }

    // 固定色条既便于肉眼检查，也能用 checksum 验证像素布局是否改变。
    constexpr unsigned char kColors[8][3] = {{255, 255, 255}, {255, 255, 0}, {0, 255, 255},
                                              {0, 255, 0},     {255, 0, 255}, {255, 0, 0},
                                              {0, 0, 255},     {32, 32, 32}};
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            const int color_index = std::min(7, x * 8 / frame.width);
            SetPixel(frame, x, y, kColors[color_index][0], kColors[color_index][1],
                     kColors[color_index][2]);
        }
    }

    const RectF box = SyntheticBoxForFrame(sequence, frame.width, frame.height); // 移动目标框。
    const int left = static_cast<int>(box.x);
    const int top = static_cast<int>(box.y);
    const int right = left + static_cast<int>(box.width);
    const int bottom = top + static_cast<int>(box.height);
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            SetPixel(frame, x, y, 16, 16, 16);
        }
    }

    // 用黑白小块编码帧序号低 32 位，无需字体库也能从图像内容识别帧号。
    const int block_size =
        std::max(1, std::min(frame.width, frame.height) / 128);  // 帧号位块边长。
    for (int bit = 0; bit < 32; ++bit) {
        const bool enabled = (sequence & (1ULL << bit)) != 0ULL;
        const int origin_x = 2 + (bit % 16) * (block_size + 1);
        const int origin_y = 2 + (bit / 16) * (block_size + 1);
        for (int y = 0; y < block_size; ++y) {
            for (int x = 0; x < block_size; ++x) {
                const unsigned char value = enabled ? 255 : 0;
                SetPixel(frame, origin_x + x, origin_y + y, value, value, value);
            }
        }
    }
    return Result<void>::Success();
}

/// 功能：保存共享时钟；真正的采集状态在 Open 时初始化。
MockVideoCapture::MockVideoCapture(std::shared_ptr<IClock> clock) : clock_(std::move(clock)) {}

/// 功能：保存视频配置、重置读取次数，并返回实际输出能力。
Result<VideoCapabilities> MockVideoCapture::Open(const VideoConfig& config) {
    std::scoped_lock lock(mutex_);
    if (open_) {
        return Result<VideoCapabilities>::Failure(
            VideoError(ErrorCategory::kInvalidState, "capture is already open", false));
    }
    config_ = config;
    start_us_ = clock_->NowUs();
    attempt_ = 0;
    open_ = true;
    return Result<VideoCapabilities>::Success(
        VideoCapabilities{config.width, config.height, config.fps, config.format});
}

/// 功能：等待本帧绝对 deadline，生成一帧 RGB888 数据，或按配置注入失败。
Result<VideoFrame> MockVideoCapture::Read(std::stop_token stop) {
    VideoConfig config;          // 本次读取使用的配置快照。
    TimestampUs start_us = 0;    // 视频时间轴起点，单位微秒。
    std::uint64_t attempt = 0;   // 当前 Read 尝试序号，同时作为帧序号。
    {
        // 仅复制共享状态时持锁；逐像素生成阶段不阻塞 Close 获取 mutex_。
        std::scoped_lock lock(mutex_);
        if (!open_) {
            return Result<VideoFrame>::Failure(
                VideoError(ErrorCategory::kInvalidState, "capture is not open", false));
        }
        config = config_;
        start_us = start_us_;
        attempt = attempt_++;
    }

    if (stop.stop_requested()) {
        return Result<VideoFrame>::Failure(
            VideoError(ErrorCategory::kCancelled, "read was cancelled", false));
    }
    if (config.failure.fail_after_frames.has_value() &&
        attempt >= *config.failure.fail_after_frames &&
        attempt < *config.failure.fail_after_frames + config.failure.fail_for_frames) {
        return Result<VideoFrame>::Failure(VideoError(
            ErrorCategory::kDeviceDisconnected, "injected camera disconnection", true));
    }

    // PTS 和等待 deadline 都由同一个绝对节拍器计算，避免两套时间逻辑不一致。
    FramePacer pacer(start_us, static_cast<std::uint32_t>(config.fps)); // 绝对帧节拍器。
    auto deadline = pacer.DeadlineFor(attempt);  // 本帧应到达的绝对 PTS/deadline。
    if (!deadline) {
        return Result<VideoFrame>::Failure(deadline.error());
    }
    if (config.realtime && !clock_->WaitUntil(deadline.value(), stop)) {
        return Result<VideoFrame>::Failure(
            VideoError(ErrorCategory::kCancelled, "read was cancelled", false));
    }
    if (config.failure.read_delay_ms > 0) {
        const TimestampUs delayed_until =
            clock_->NowUs() + static_cast<TimestampUs>(config.failure.read_delay_ms) * 1000;
        if (!clock_->WaitUntil(delayed_until, stop)) {
            return Result<VideoFrame>::Failure(
                VideoError(ErrorCategory::kCancelled, "read delay was cancelled", false));
        }
    }

    const int stride = config.width * 3;  // RGB888 每像素 3 字节，本实现无额外行填充。
    const std::size_t buffer_size = static_cast<std::size_t>(stride) *
                                    static_cast<std::size_t>(config.height);  // 整帧字节数。
    VideoFrame frame{attempt, deadline.value(), config.width, config.height, stride,
                     PixelFormat::kRgb888, Buffer::Allocate(buffer_size), FrameMemory{}};
    auto generated = GenerateRgbTestPattern(attempt, frame);
    if (!generated) {
        return Result<VideoFrame>::Failure(generated.error());
    }
    return Result<VideoFrame>::Success(std::move(frame));
}

/// 功能：线程安全地关闭 Mock 视频源；允许重复调用。
void MockVideoCapture::Close() noexcept {
    std::scoped_lock lock(mutex_);
    open_ = false;
}

}  // namespace rkav
