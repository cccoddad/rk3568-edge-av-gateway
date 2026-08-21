// 文件作用：实现线程安全指标收集、延迟分位数统计和 worker 健康状态记录。
// 主要知识点：atomic、滑动窗口、百分位数、锁保护容器和 acquire/release 内存序。
#include "rkav/monitor/metrics.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace rkav {
namespace {

constexpr std::array<const char*, static_cast<std::size_t>(MetricCounter::kCount)>
    kCounterNames{"video_captured_total",   "audio_captured_total", // 枚举索引到 JSON 键。
                  "inference_requests_total", "inference_results_total",
                  "video_packets_total",    "audio_packets_total",
                  "packets_routed_total",    "packets_consumed_total",
                  "errors_total",           "recoveries_total",
                  "expired_detections_total"};

/// 功能：在传入样本副本上计算指定百分位数；空样本返回 0。
TimestampUs Percentile(std::vector<TimestampUs> values, double percentile) {
    if (values.empty()) {
        return 0;
    }
    // 在快照副本上排序，不改变正在收集的原始窗口。
    std::sort(values.begin(), values.end());
    const double position =
        percentile * static_cast<double>(values.size() - 1U);  // 百分位对应浮点位置。
    const auto index = static_cast<std::size_t>(std::llround(position)); // 最近样本索引。
    return values[index];
}

}  // namespace

/// 功能：创建指标仓库，并保证窗口至少能容纳一个样本。
MetricsRegistry::MetricsRegistry(std::size_t latency_window)
    : latency_window_(std::max<std::size_t>(1U, latency_window)) {}

/// 功能：无锁增加指定累计计数器；relaxed 足够，因为这里只关心最终数值。
void MetricsRegistry::Increment(MetricCounter counter, std::uint64_t amount) noexcept {
    counters_[static_cast<std::size_t>(counter)].fetch_add(amount, std::memory_order_relaxed);
}

/// 功能：读取一个累计计数器的当前值。
std::uint64_t MetricsRegistry::Counter(MetricCounter counter) const noexcept {
    return counters_[static_cast<std::size_t>(counter)].load(std::memory_order_relaxed);
}

/// 功能：向指定阶段的有界延迟窗口追加一个微秒样本。
void MetricsRegistry::ObserveLatency(std::string_view stage, TimestampUs latency_us) {
    if (latency_us < 0) {
        return;
    }
    std::scoped_lock lock(mutex_);
    auto& samples = latencies_[std::string(stage)];  // 该阶段可变样本数组。
    if (samples.size() >= latency_window_) {
        // 每次淘汰最旧的四分之一，降低每次满窗口都移动整个数组的开销。
        samples.erase(samples.begin(), samples.begin() +
                                            static_cast<std::ptrdiff_t>(samples.size() / 4U));
    }
    samples.push_back(latency_us);
}

/// 功能：保存队列的最新一致性快照。
void MetricsRegistry::UpdateQueue(std::string_view name, const QueueSnapshot& snapshot) {
    std::scoped_lock lock(mutex_);
    queues_[std::string(name)] = snapshot;
}

/// 功能：在锁保护下把全部计数、延迟和队列信息整理为 JSON。
nlohmann::json MetricsRegistry::Snapshot() const {
    nlohmann::json result;  // 本次返回的完整指标对象。
    for (std::size_t index = 0; index < kCounterCount; ++index) {
        result["counters"][kCounterNames[index]] =
            counters_[index].load(std::memory_order_relaxed);
    }

    std::scoped_lock lock(mutex_);
    for (const auto& [name, samples] : latencies_) {
        result["latency_us"][name] = {{"samples", samples.size()},
                                       {"p50", Percentile(samples, 0.50)},
                                       {"p95", Percentile(samples, 0.95)},
                                       {"p99", Percentile(samples, 0.99)}};
    }
    for (const auto& [name, queue] : queues_) {
        result["queues"][name] = {{"size", queue.size},
                                   {"capacity", queue.capacity},
                                   {"high_watermark", queue.high_watermark},
                                   {"pushed", queue.pushed},
                                   {"popped", queue.popped},
                                   {"dropped", queue.dropped},
                                   {"push_timeouts", queue.push_timeouts},
                                   {"pop_timeouts", queue.pop_timeouts},
                                   {"closed", queue.closed}};
    }
    return result;
}

/// 功能：标记 worker 进入 Running，并初始化进展时间。
void WorkerHealth::MarkStarted(TimestampUs now_us) noexcept {
    last_progress_us_.store(now_us, std::memory_order_relaxed);
    consecutive_errors_.store(0, std::memory_order_relaxed);
    state_.store(WorkerState::kRunning, std::memory_order_release);
}

/// 功能：记录成功进展；一次成功会清零此前连续错误数。
void WorkerHealth::MarkProgress(TimestampUs now_us) noexcept {
    last_progress_us_.store(now_us, std::memory_order_relaxed);
    consecutive_errors_.store(0, std::memory_order_relaxed);
}

/// 功能：记录错误时间，并增加连续错误次数。
void WorkerHealth::MarkError(TimestampUs now_us) noexcept {
    last_error_us_.store(now_us, std::memory_order_relaxed);
    consecutive_errors_.fetch_add(1, std::memory_order_relaxed);
}

/// 功能：发布新的生命周期状态。
void WorkerHealth::SetState(WorkerState state) noexcept {
    state_.store(state, std::memory_order_release);
}

/// 功能：返回最近一次成功进展时间，单位微秒。
TimestampUs WorkerHealth::last_progress_us() const noexcept {
    return last_progress_us_.load(std::memory_order_relaxed);
}

/// 功能：返回最近一次错误时间，单位微秒。
TimestampUs WorkerHealth::last_error_us() const noexcept {
    return last_error_us_.load(std::memory_order_relaxed);
}

/// 功能：返回从最近成功进展之后累计的错误次数。
std::uint64_t WorkerHealth::consecutive_errors() const noexcept {
    return consecutive_errors_.load(std::memory_order_relaxed);
}

/// 功能：返回当前 worker 状态；acquire 与 SetState 的 release 配对。
WorkerState WorkerHealth::state() const noexcept {
    return state_.load(std::memory_order_acquire);
}

/// 功能：把 worker 状态转换成稳定字符串。
const char* ToString(WorkerState state) noexcept {
    switch (state) {
        case WorkerState::kStarting:
            return "starting";
        case WorkerState::kRunning:
            return "running";
        case WorkerState::kDegraded:
            return "degraded";
        case WorkerState::kStopping:
            return "stopping";
        case WorkerState::kStopped:
            return "stopped";
        case WorkerState::kFailed:
            return "failed";
    }
    return "unknown";
}

}  // namespace rkav
