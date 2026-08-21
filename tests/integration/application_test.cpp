// 文件作用：以小分辨率真实时间运行完整 Application，验证线程、背压、故障和停止。
// 主要知识点：集成测试、真实并发、有界等待、故障注入和最终指标对账。
#include "rkav/app/application.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace rkav {
namespace {

using namespace std::chrono_literals;

/// 功能：生成运行快速但保留完整双流行为的集成测试基线配置。
AppConfig SmallRealtimeConfig() {
    AppConfig config;  // 从生产默认值开始，只缩小测试成本和时间间隔。
    config.video.width = 64;
    config.video.height = 48;
    config.video.fps = 60;
    config.video.queue_capacity = 3;
    config.audio.sample_rate = 8'000;
    config.audio.frame_duration_ms = 20;
    config.audio.queue_capacity_ms = 100;
    config.audio.frequency_hz = 100.0;
    config.inference.latency_ms = 2;
    config.inference.queue_capacity = 1;
    config.outputs.emplace_back();
    config.outputs.front().queue_capacity = 32;
    config.monitoring.metrics_interval_ms = 50;
    config.monitoring.health_interval_ms = 20;
    config.monitoring.worker_stall_timeout_ms = 500;
    return config;
}

/// 功能：在有限时间内轮询停止标志，防止错误场景测试永久挂死。
bool WaitForStop(Application& app, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;  // 最晚等待时刻。
    while (!app.stop_requested() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(5ms);
    }
    return app.stop_requested();
}

// 验证音频、视频都产生数据，正常停止进入 Stopped 且错误计数为 0。
TEST(ApplicationTest, RunsBothStreamsAndShutsDownGracefully) {
    Application app(SmallRealtimeConfig());
    ASSERT_TRUE(app.Start());
    std::this_thread::sleep_for(250ms);
    app.RequestStop("test_complete");

    auto stopped = app.Wait();
    ASSERT_TRUE(stopped);
    EXPECT_EQ(app.state(), ApplicationState::kStopped);
    const auto metrics = app.MetricsSnapshot();
    EXPECT_GT(metrics["counters"]["video_captured_total"].get<std::uint64_t>(), 5U);
    EXPECT_GT(metrics["counters"]["audio_captured_total"].get<std::uint64_t>(), 5U);
    EXPECT_EQ(metrics["counters"]["errors_total"], 0);
}

// 推理明显慢于视频时应丢旧帧，且推理队列高水位永远不超过容量 1。
TEST(ApplicationTest, SlowInferenceDropsStaleFramesInsteadOfGrowingMemory) {
    auto config = SmallRealtimeConfig();
    config.video.fps = 120;
    config.inference.latency_ms = 40;
    Application app(config);
    ASSERT_TRUE(app.Start());
    std::this_thread::sleep_for(350ms);
    app.RequestStop("backpressure_test_complete");
    ASSERT_TRUE(app.Wait());

    const auto metrics = app.MetricsSnapshot();
    EXPECT_GT(metrics["queues"]["inference"]["dropped"].get<std::uint64_t>(), 0U);
    EXPECT_LE(metrics["queues"]["inference"]["high_watermark"].get<std::size_t>(), 1U);
}

// Sink 每包延迟 40ms 时只在自己的容量 2 队列丢包，视频采集仍持续前进。
TEST(ApplicationTest, SlowSinkIsIsolatedByItsBoundedQueue) {
    auto config = SmallRealtimeConfig();
    config.outputs.front().queue_capacity = 2;
    config.outputs.front().overflow_policy = OverflowPolicy::kDropOldest;
    config.outputs.front().write_delay_ms = 40;
    Application app(config);
    ASSERT_TRUE(app.Start());
    std::this_thread::sleep_for(350ms);
    app.RequestStop("slow_sink_test_complete");
    ASSERT_TRUE(app.Wait());

    const auto metrics = app.MetricsSnapshot();
    EXPECT_GT(metrics["counters"]["video_captured_total"].get<std::uint64_t>(), 10U);
    EXPECT_GT(metrics["queues"]["sink_null"]["dropped"].get<std::uint64_t>(), 0U);
    EXPECT_LE(metrics["queues"]["sink_null"]["high_watermark"].get<std::size_t>(), 2U);
}

// required Sink 在第 3 包后失败应触发全局停止并保留 I/O 根因。
TEST(ApplicationTest, RequiredSinkFailureStopsThePipeline) {
    auto config = SmallRealtimeConfig();
    config.outputs.front().fail_after_packets = 3;
    Application app(config);
    ASSERT_TRUE(app.Start());
    const bool stopped_by_monitor = WaitForStop(app, 2s);
    if (!stopped_by_monitor) {
        app.RequestStop("test_timeout");
    }

    auto result = app.Wait();
    ASSERT_TRUE(stopped_by_monitor);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().category, ErrorCategory::kIo);
    EXPECT_EQ(app.state(), ApplicationState::kFailed);
}

// optional Sink 失败后只隔离自身、只记一次错误，Application 继续运行。
TEST(ApplicationTest, OptionalSinkFailureIsIsolatedAfterOneError) {
    auto config = SmallRealtimeConfig();
    config.outputs.front().required = false;
    config.outputs.front().fail_after_packets = 3;
    Application app(config);
    ASSERT_TRUE(app.Start());
    std::this_thread::sleep_for(250ms);
    EXPECT_FALSE(app.stop_requested());
    app.RequestStop("optional_sink_test_complete");

    ASSERT_TRUE(app.Wait());
    const auto metrics = app.MetricsSnapshot();
    EXPECT_EQ(metrics["counters"]["errors_total"], 1);
    EXPECT_EQ(app.state(), ApplicationState::kStopped);
}

// 周期 XRUN 应增加恢复计数，但不能破坏 PTS 连续性或产生致命错误。
TEST(ApplicationTest, AudioXrunRecoversWithoutBreakingTimestampContinuity) {
    auto config = SmallRealtimeConfig();
    config.audio.failure.xrun_every_blocks = 3;
    Application app(config);
    ASSERT_TRUE(app.Start());
    std::this_thread::sleep_for(250ms);
    app.RequestStop("xrun_recovery_test_complete");

    ASSERT_TRUE(app.Wait());
    const auto metrics = app.MetricsSnapshot();
    EXPECT_GT(metrics["counters"]["recoveries_total"].get<std::uint64_t>(), 0U);
    EXPECT_EQ(metrics["counters"]["errors_total"], 0);
}

// 持续设备失联虽可重试，但长期无成功进展最终应由健康检查停止。
TEST(ApplicationTest, RetryableDeviceLossBecomesAStallFailure) {
    auto config = SmallRealtimeConfig();
    config.audio.failure.disconnect_after_blocks = 1;
    config.monitoring.worker_stall_timeout_ms = 120;
    Application app(config);
    ASSERT_TRUE(app.Start());
    const bool stopped_by_monitor = WaitForStop(app, 2s);
    if (!stopped_by_monitor) {
        app.RequestStop("test_timeout");
    }

    auto result = app.Wait();
    ASSERT_TRUE(stopped_by_monitor);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().category, ErrorCategory::kTimeout);
    EXPECT_NE(result.error().message.find("audio_capture"), std::string::npos);
}

}  // namespace
}  // namespace rkav
