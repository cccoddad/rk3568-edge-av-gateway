// 文件作用：验证 PacketRouter 对可重试与不可重试写入错误的隔离和存活语义。
// 主要知识点：每 Sink 独立线程队列、错误分类、retryable 语义、致命错误汇聚。
#include "rkav/output/packet_router.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "rkav/common/buffer.h"
#include "rkav/common/clock.h"

namespace rkav {
namespace {

class ScriptedSink final : public IPacketSink {
   public:
    // failures_before_success 表示前多少次写入返回注入错误；负数表示永久失败。
    ScriptedSink(int failures_before_success, bool retryable)
        : failures_before_success_(failures_before_success), retryable_(retryable) {}

    Result<void> Open(const OutputConfig&, std::span<const EncodedStreamInfo>) override {
        return Result<void>::Success();
    }
    Result<void> Write(const EncodedPacket&) override {
        const int seen = writes_.fetch_add(1, std::memory_order_acq_rel);
        if (failures_before_success_ < 0 || seen < failures_before_success_) {
            return Result<void>::Failure(Error{ErrorCategory::kIo, 0, "scripted_sink", "write",
                                               "injected write failure", retryable_});
        }
        successes_.fetch_add(1, std::memory_order_acq_rel);
        return Result<void>::Success();
    }
    Result<void> Flush() override { return Result<void>::Success(); }
    void Close() noexcept override {}
    [[nodiscard]] std::string name() const override { return "scripted"; }

    [[nodiscard]] int writes() const noexcept { return writes_.load(std::memory_order_acquire); }
    [[nodiscard]] int successes() const noexcept {
        return successes_.load(std::memory_order_acquire);
    }

   private:
    int failures_before_success_;
    bool retryable_;
    std::atomic<int> writes_{0};
    std::atomic<int> successes_{0};
};

std::shared_ptr<const EncodedPacket> MakePacket() {
    auto packet = std::make_shared<EncodedPacket>();
    packet->kind = StreamKind::kVideo;
    packet->codec = Codec::kH264;
    packet->time_base = Rational{1, 1'000'000};
    packet->buffer = Buffer::Allocate(8);
    return packet;
}

OutputConfig MakeSinkConfig(bool required) {
    OutputConfig config;
    config.type = "null";
    config.required = required;
    config.queue_capacity = 16;
    return config;
}

bool WaitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

// 可重试错误不能隔离 worker：后续包继续写入，恢复后计数器正常增长。
TEST(PacketRouterTest, RetryableFailuresKeepWorkerAliveAndRecover) {
    auto clock = std::make_shared<ManualClock>(1'000'000);
    MetricsRegistry metrics;
    PacketRouter router(clock, metrics);
    auto sink = std::make_unique<ScriptedSink>(3, true);  // 前 3 次写入可重试失败。
    ScriptedSink* raw = sink.get();
    ASSERT_TRUE(router.AddSink(MakeSinkConfig(true), std::move(sink), {}));
    ASSERT_TRUE(router.Start());

    for (int i = 0; i < 8; ++i) {
        router.Submit(MakePacket());
    }
    EXPECT_TRUE(
        WaitUntil([raw] { return raw->writes() >= 8; }, std::chrono::milliseconds(2000)));
    EXPECT_EQ(raw->writes(), 8);
    EXPECT_EQ(raw->successes(), 5);
    router.Stop(CloseMode::kDrain);

    EXPECT_FALSE(router.has_fatal_error());
    EXPECT_GE(metrics.Counter(MetricCounter::kErrors), 1U);
    EXPECT_EQ(metrics.Counter(MetricCounter::kPacketsConsumed), 5U);
}

// 不可重试错误在可选输出上仍按旧语义立即隔离，只隔离自身不产生致命错误。
TEST(PacketRouterTest, NonRetryableFailureStillIsolatesOptionalSink) {
    auto clock = std::make_shared<ManualClock>(1'000'000);
    MetricsRegistry metrics;
    PacketRouter router(clock, metrics);
    auto sink = std::make_unique<ScriptedSink>(-1, false);  // 永久不可重试失败。
    ScriptedSink* raw = sink.get();
    ASSERT_TRUE(router.AddSink(MakeSinkConfig(false), std::move(sink), {}));
    ASSERT_TRUE(router.Start());

    for (int i = 0; i < 6; ++i) {
        router.Submit(MakePacket());
    }
    EXPECT_TRUE(
        WaitUntil([raw] { return raw->writes() >= 1; }, std::chrono::milliseconds(2000)));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(raw->writes(), 1);
    router.Stop(CloseMode::kDrain);

    EXPECT_FALSE(router.has_fatal_error());
    EXPECT_EQ(metrics.Counter(MetricCounter::kErrors), 1U);
    EXPECT_EQ(metrics.Counter(MetricCounter::kPacketsConsumed), 0U);
}

// 不可重试错误在必需输出上仍然是致命错误，保留快速失败语义。
TEST(PacketRouterTest, NonRetryableFailureOnRequiredSinkIsFatal) {
    auto clock = std::make_shared<ManualClock>(1'000'000);
    MetricsRegistry metrics;
    PacketRouter router(clock, metrics);
    auto sink = std::make_unique<ScriptedSink>(-1, false);
    ASSERT_TRUE(router.AddSink(MakeSinkConfig(true), std::move(sink), {}));
    ASSERT_TRUE(router.Start());

    router.Submit(MakePacket());
    EXPECT_TRUE(WaitUntil([&router] { return router.has_fatal_error(); },
                          std::chrono::milliseconds(2000)));
    router.Stop(CloseMode::kDrain);

    ASSERT_TRUE(router.fatal_error().has_value());
    EXPECT_EQ(router.fatal_error()->category, ErrorCategory::kIo);
    EXPECT_FALSE(router.fatal_error()->retryable);
}

}  // namespace
}  // namespace rkav
