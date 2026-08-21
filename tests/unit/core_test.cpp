// 文件作用：验证时钟、时间基、letterbox、媒体布局和指标等公共核心逻辑。
// 主要知识点：GoogleTest 断言、边界值测试、浮点比较和 JSON 指标验证。
#include <gtest/gtest.h>

#include "rkav/common/clock.h"
#include "rkav/common/types.h"
#include "rkav/monitor/metrics.h"
#include "rkav/vision/geometry.h"

namespace rkav {
namespace {

// 验证 30 FPS 中不能整除的微秒余数不会随帧数累积成时间漂移。
TEST(FramePacerTest, KeepsFractionalFrameRateErrorBounded) {
    const FramePacer pacer(100, 30);

    ASSERT_TRUE(pacer.DeadlineFor(0));
    EXPECT_EQ(pacer.DeadlineFor(0).value(), 100);
    EXPECT_EQ(pacer.DeadlineFor(1).value(), 33'433);
    EXPECT_EQ(pacer.DeadlineFor(29).value(), 966'766);
    EXPECT_EQ(pacer.DeadlineFor(30).value(), 1'000'100);
}

// 验证 1.5 秒换算到 90 kHz 视频时间基后应为 135000 个刻度。
TEST(ClockTest, RescalesMicrosecondsToNinetyKhzTicks) {
    auto result = RescaleTimestamp(1'500'000, Rational{1, 90'000});

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), 135'000);
}

// 验证 16:9 原图放入正方形模型输入后的补边及反向映射。
TEST(GeometryTest, LetterboxRoundTripMapsToOriginalImage) {
    auto transform = ComputeLetterboxTransform(1920, 1080, 640, 640);

    ASSERT_TRUE(transform);
    EXPECT_EQ(transform.value().resized_width, 640);
    EXPECT_EQ(transform.value().resized_height, 360);
    EXPECT_EQ(transform.value().pad_top, 140);

    const RectF source = MapBoxToSource(RectF{0.0F, 140.0F, 640.0F, 360.0F}, transform.value());
    EXPECT_FLOAT_EQ(source.x, 0.0F);
    EXPECT_FLOAT_EQ(source.y, 0.0F);
    EXPECT_FLOAT_EQ(source.width, 1920.0F);
    EXPECT_FLOAT_EQ(source.height, 1080.0F);
}

// 验证模型框越过补边和边界时，最终原图坐标仍被裁剪在合法范围内。
TEST(GeometryTest, ClipsModelBoxesThatOverlapPadding) {
    const auto transform = ComputeLetterboxTransform(1920, 1080, 640, 640);
    ASSERT_TRUE(transform);

    const RectF source = MapBoxToSource(RectF{-10.0F, 100.0F, 700.0F, 500.0F}, transform.value());
    EXPECT_FLOAT_EQ(source.x, 0.0F);
    EXPECT_FLOAT_EQ(source.y, 0.0F);
    EXPECT_FLOAT_EQ(source.width, 1920.0F);
    EXPECT_FLOAT_EQ(source.height, 1080.0F);
}

// 验证 RGB888 stride 小于 width*3 时必须拒绝，防止后续像素访问越界。
TEST(TypeValidationTest, RejectsRgbFrameWithShortStride) {
    VideoFrame frame{0, 0, 64, 48, 64, PixelFormat::kRgb888, Buffer::Allocate(64U * 48U), {}};

    auto result = ValidateVideoFrame(frame);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().category, ErrorCategory::kInvalidConfig);
}

// 验证累计计数、延迟样本和队列统计都能出现在最终 JSON 中。
TEST(MetricsTest, ExposesCountersLatencyAndQueueState) {
    MetricsRegistry metrics;
    metrics.Increment(MetricCounter::kVideoCaptured, 3);
    metrics.ObserveLatency("stage", 10);
    metrics.ObserveLatency("stage", 20);
    metrics.UpdateQueue("frames", QueueSnapshot{1, 4, 2, 3, 2, 1, 0, 0, false});

    const auto snapshot = metrics.Snapshot();
    EXPECT_EQ(snapshot["counters"]["video_captured_total"], 3);
    EXPECT_EQ(snapshot["latency_us"]["stage"]["samples"], 2);
    EXPECT_EQ(snapshot["queues"]["frames"]["dropped"], 1);
}

}  // namespace
}  // namespace rkav
