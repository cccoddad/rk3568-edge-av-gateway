// 文件作用：定义管道计数器、延迟分位数、队列快照和 worker 健康状态。
// 主要知识点：原子计数、滑动样本窗口、互斥锁、JSON 快照和“存活不等于有进展”。
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "rkav/common/types.h"
#include "rkav/queue/bounded_queue.h"

namespace rkav {

enum class MetricCounter {
    kVideoCaptured,
    kAudioCaptured,
    kInferenceRequests,
    kInferenceResults,
    kVideoPackets,
    kAudioPackets,
    kPacketsRouted,
    kPacketsConsumed,
    kErrors,
    kRecoveries,
    kExpiredDetections,
    kOverlayApplied,
    kOverlaySkipped,
    kCount,
};

enum class WorkerState { kStarting, kRunning, kDegraded, kStopping, kStopped, kFailed };

class MetricsRegistry {
   public:
    /// 创建指标仓库；latency_window 是每个阶段最多保留的延迟样本数。
    explicit MetricsRegistry(std::size_t latency_window = 4096);

    /// 原子增加某个累计计数器，适合工作线程高频调用。
    void Increment(MetricCounter counter, std::uint64_t amount = 1) noexcept;
    /// 读取指定累计计数器当前值。
    [[nodiscard]] std::uint64_t Counter(MetricCounter counter) const noexcept;
    /// 记录一个阶段的耗时样本，单位微秒；负值会被忽略。
    void ObserveLatency(std::string_view stage, TimestampUs latency_us);
    /// 更新某个有界队列的最近快照。
    void UpdateQueue(std::string_view name, const QueueSnapshot& snapshot);
    /// 返回包含计数、p50/p95/p99 和队列状态的 JSON 快照。
    [[nodiscard]] nlohmann::json Snapshot() const;

   private:
    static constexpr std::size_t kCounterCount = static_cast<std::size_t>(MetricCounter::kCount);

    // 高频计数使用原子变量；需要成组更新的延迟窗口和队列快照使用同一把锁。
    std::array<std::atomic<std::uint64_t>, kCounterCount> counters_{};
    const std::size_t latency_window_;  // 每个阶段的最大延迟样本数。
    mutable std::mutex mutex_;          // 保护下面两个容器。
    std::map<std::string, std::vector<TimestampUs>> latencies_;  // 阶段名到微秒样本。
    std::map<std::string, QueueSnapshot> queues_;                // 队列名到最近快照。
};

class WorkerHealth {
   public:
    /// 标记 worker 已启动，并把当前时间视为初始进展时间。
    void MarkStarted(TimestampUs now_us) noexcept;
    /// 标记成功处理一个数据单元，并清零连续错误数。
    void MarkProgress(TimestampUs now_us) noexcept;
    /// 记录一次错误的发生时间并增加连续错误数。
    void MarkError(TimestampUs now_us) noexcept;
    /// 显式设置 worker 生命周期状态。
    void SetState(WorkerState state) noexcept;

    [[nodiscard]] TimestampUs last_progress_us() const noexcept;
    [[nodiscard]] TimestampUs last_error_us() const noexcept;
    [[nodiscard]] std::uint64_t consecutive_errors() const noexcept;
    [[nodiscard]] WorkerState state() const noexcept;

   private:
    std::atomic<TimestampUs> last_progress_us_{0};  // 最近成功处理数据的微秒时刻。
    std::atomic<TimestampUs> last_error_us_{0};     // 最近发生错误的微秒时刻。
    std::atomic<std::uint64_t> consecutive_errors_{0};       // 自上次成功以来的错误数。
    std::atomic<WorkerState> state_{WorkerState::kStopped};  // 当前生命周期状态。
};

/// 把 WorkerState 转换成稳定文本。
const char* ToString(WorkerState state) noexcept;

}  // namespace rkav
