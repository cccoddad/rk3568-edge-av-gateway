// 文件作用：把编码包并行分发到多个彼此隔离的输出 Sink。
// 主要知识点：每 Sink 一线程一队列、shared_ptr 扇出、原子状态和致命错误汇聚。
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "rkav/common/clock.h"
#include "rkav/config/config.h"
#include "rkav/monitor/metrics.h"
#include "rkav/output/packet_sink.h"
#include "rkav/queue/bounded_queue.h"

namespace rkav {

class PacketRouter {
   public:
    /// 保存共享时钟和指标引用；此时尚未启动输出线程。
    PacketRouter(std::shared_ptr<IClock> clock, MetricsRegistry& metrics);
    /// 析构时使用 Abort 兜底回收所有 Sink worker。
    ~PacketRouter();

    /// 在启动前打开并注册一个 Sink，为它创建独立有界队列。
    Result<void> AddSink(const OutputConfig& config, std::unique_ptr<IPacketSink> sink,
                         std::span<const EncodedStreamInfo> streams);
    /// 为所有已注册 Sink 启动消费线程；没有 Sink 时返回配置错误。
    Result<void> Start();
    // Submit 只向各 Sink 的独立有界队列投递，不在编码线程中执行实际 I/O。
    void Submit(std::shared_ptr<const EncodedPacket> packet);
    /// 关闭全部 Sink 队列并等待线程结束；Drain 与 Abort 语义同 BoundedQueue。
    void Stop(CloseMode mode = CloseMode::kDrain) noexcept;

    /// 是否有 required Sink 报告致命写入错误。
    [[nodiscard]] bool has_fatal_error() const noexcept;
    /// 返回最先发生的 required Sink 错误。
    [[nodiscard]] std::optional<Error> fatal_error() const;
    /// 把所有 Sink 队列快照写入 MetricsRegistry。
    void CollectQueueMetrics();

   private:
    struct SinkWorker;

    std::shared_ptr<IClock> clock_;  // worker 进展时间使用的统一时钟。
    MetricsRegistry& metrics_;       // 由 Application 拥有，生命周期长于 Router。
    std::vector<std::unique_ptr<SinkWorker>> workers_;  // 每个已注册输出的运行上下文。
    std::atomic_bool started_{false};                   // 路由是否已开始接收包。
    std::atomic_bool fatal_{false};                     // required Sink 是否发生致命错误。
    mutable std::mutex error_mutex_;                    // 保护下面的首个错误对象。
    std::optional<Error> fatal_error_;                  // 最先发生的 required Sink 错误。
};

}  // namespace rkav
