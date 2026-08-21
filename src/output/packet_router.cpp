// 文件作用：实现编码包到多个输出端的异步、隔离、有界分发。
// 主要知识点：jthread、每 Sink 一队列、共享不可变包、原子状态和错误汇聚。
#include "rkav/output/packet_router.h"

#include <chrono>
#include <thread>
#include <utility>

#include "rkav/common/logger.h"

namespace rkav {

struct PacketRouter::SinkWorker {
    // 每个输出独占线程和有界队列，单个慢输出不会阻塞编码线程或其他输出。
    OutputConfig config;                // 当前输出的独立配置。
    std::unique_ptr<IPacketSink> sink;  // 具体输出实现的唯一所有权。
    std::unique_ptr<BoundedQueue<std::shared_ptr<const EncodedPacket>>> queue;  // 包队列。
    std::jthread thread;  // 唯一调用该 Sink::Write 的消费线程。
    WorkerHealth health;  // 输出线程的进展和错误状态。
};

/// 功能：保存统一时钟和共享指标仓库；尚不创建线程。
PacketRouter::PacketRouter(std::shared_ptr<IClock> clock, MetricsRegistry& metrics)
    : clock_(std::move(clock)), metrics_(metrics) {}

/// 功能：使用 Abort 兜底停止，确保异常路径也不会遗留线程。
PacketRouter::~PacketRouter() { Stop(CloseMode::kAbort); }

/// 功能：启动前打开一个 Sink 并为其创建独立有界队列。
Result<void> PacketRouter::AddSink(const OutputConfig& config, std::unique_ptr<IPacketSink> sink) {
    if (started_.load(std::memory_order_acquire)) {
        return Result<void>::Failure(Error{ErrorCategory::kInvalidState, 0, "packet_router",
                                           "add_sink", "router is already running", false});
    }
    // 先 Open 再纳入 workers_，失败的 Sink 不会留下半初始化 worker。
    auto opened = sink->Open(config);  // Sink 资源打开结果。
    if (!opened) {
        return opened;
    }
    auto worker = std::make_unique<SinkWorker>();  // 新输出的完整运行上下文。
    worker->config = config;
    worker->sink = std::move(sink);
    worker->queue = std::make_unique<BoundedQueue<std::shared_ptr<const EncodedPacket>>>(
        config.queue_capacity, config.overflow_policy);
    workers_.push_back(std::move(worker));
    return Result<void>::Success();
}

/// 功能：为每个已注册 Sink 创建一个消费线程，并开始接受 Submit。
Result<void> PacketRouter::Start() {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return Result<void>::Failure(Error{ErrorCategory::kInvalidState, 0, "packet_router",
                                           "start", "router is already running", false});
    }
    if (workers_.empty()) {
        started_.store(false, std::memory_order_release);
        return Result<void>::Failure(Error{ErrorCategory::kInvalidConfig, 0, "packet_router",
                                           "start", "router has no sinks", false});
    }

    for (auto& worker : workers_) {
        SinkWorker* const target = worker.get();  // 线程使用的稳定上下文地址。
        // 捕获稳定的裸指针；worker 由 workers_ 持有，Stop/join 前不会被释放。
        target->thread = std::jthread([this, target](std::stop_token stop) {
            target->health.MarkStarted(clock_->NowUs());
            while (!stop.stop_requested()) {
                auto popped =
                    target->queue->Pop(stop, std::chrono::milliseconds(250));  // 本次取包结果。
                if (popped.status == QueueStatus::kTimeout) {
                    continue;
                }
                if (popped.status != QueueStatus::kOk || !popped.item.has_value()) {
                    break;
                }
                auto result = target->sink->Write(**popped.item);  // 同步写入结果。
                if (!result) {
                    metrics_.Increment(MetricCounter::kErrors);
                    target->health.MarkError(clock_->NowUs());
                    Logger::Instance().Log(LogLevel::kError, "packet_router", "sink_write_failed",
                                           DescribeError(result.error()),
                                           {{"sink", target->sink->name()}});
                    if (target->config.required) {
                        {
                            std::scoped_lock lock(error_mutex_);
                            if (!fatal_error_.has_value()) {
                                fatal_error_ = result.error();
                            }
                        }
                        fatal_.store(true, std::memory_order_release);
                        break;
                    }
                    // 非必需输出失败后立即隔离并释放存量，避免后续每个包重复打印同一错误。
                    target->health.SetState(WorkerState::kDegraded);
                    target->queue->Close(CloseMode::kAbort);
                    break;
                }
                metrics_.Increment(MetricCounter::kPacketsConsumed);
                target->health.MarkProgress(clock_->NowUs());
            }
            auto flushed = target->sink->Flush();
            if (!flushed) {
                Logger::Instance().Log(LogLevel::kWarn, "packet_router", "sink_flush_failed",
                                       DescribeError(flushed.error()),
                                       {{"sink", target->sink->name()}});
            }
            target->sink->Close();
            if (target->health.state() != WorkerState::kDegraded) {
                target->health.SetState(WorkerState::kStopped);
            }
        });
    }
    return Result<void>::Success();
}

/// 功能：把同一个不可变包非阻塞投递到全部 Sink 队列。
void PacketRouter::Submit(std::shared_ptr<const EncodedPacket> packet) {
    if (!started_.load(std::memory_order_acquire) || !packet) {
        return;
    }
    for (auto& worker : workers_) {
        // 零等待投递保证编码线程不被慢 Sink 反向阻塞，丢包由队列指标体现。
        const auto status = worker->queue->Push(packet, {}, std::chrono::milliseconds(0));
        if (status == QueueStatus::kOk) {
            metrics_.Increment(MetricCounter::kPacketsRouted);
        }
    }
}

/// 功能：关闭所有队列，再按模式请求停止并 join 全部 Sink 线程。
void PacketRouter::Stop(CloseMode mode) noexcept {
    if (!started_.exchange(false, std::memory_order_acq_rel)) {
        for (auto& worker : workers_) {
            worker->sink->Close();
        }
        return;
    }
    // 先关闭全部入口，再 join 输出线程，避免线程间停止顺序造成新的包进入。
    for (auto& worker : workers_) {
        worker->queue->Close(mode);
    }
    for (auto& worker : workers_) {
        if (mode == CloseMode::kAbort) {
            worker->thread.request_stop();
        }
        if (worker->thread.joinable()) {
            worker->thread.join();
        }
    }
}

/// 功能：无锁查询 required Sink 是否发生致命错误。
bool PacketRouter::has_fatal_error() const noexcept {
    return fatal_.load(std::memory_order_acquire);
}

/// 功能：在线程安全保护下复制第一个致命错误。
std::optional<Error> PacketRouter::fatal_error() const {
    std::scoped_lock lock(error_mutex_);
    return fatal_error_;
}

/// 功能：为每个输出队列生成名为 sink_<name> 的指标快照。
void PacketRouter::CollectQueueMetrics() {
    for (const auto& worker : workers_) {
        metrics_.UpdateQueue("sink_" + worker->sink->name(), worker->queue->Snapshot());
    }
}

}  // namespace rkav
