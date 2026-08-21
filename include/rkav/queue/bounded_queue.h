// 文件作用：实现线程间传递媒体数据的固定容量阻塞队列及多种溢出策略。
// 主要知识点：模板、互斥锁、条件变量、stop_token、背压、Drain/Abort 和运行指标。
#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <utility>

namespace rkav {

// 不同媒体需要不同背压策略：视频通常保新鲜，音频通常阻塞以保持连续。
enum class OverflowPolicy { kBlockProducer, kDropNewest, kDropOldest, kKeepLatest };
enum class QueueStatus { kOk, kTimeout, kClosed, kDropped, kCancelled };
// Drain 允许消费者处理完存量数据；Abort 立即丢弃，供致命错误快速退出。
enum class CloseMode { kDrain, kAbort };

struct QueueSnapshot {
    std::size_t size{0};             // 快照时仍在队列中的元素数。
    std::size_t capacity{0};         // 固定容量上限。
    std::size_t high_watermark{0};   // 历史最大占用，用于判断容量是否合适。
    std::uint64_t pushed{0};         // 成功入队次数。
    std::uint64_t popped{0};         // 成功出队次数。
    std::uint64_t dropped{0};        // 因溢出或 Abort 被丢弃的元素数。
    std::uint64_t push_timeouts{0};  // 生产者等待空位超时次数。
    std::uint64_t pop_timeouts{0};   // 消费者等待数据超时次数。
    bool closed{false};              // 是否已禁止新的 Push。
};

template <typename T>
struct QueuePopResult {
    QueueStatus status{QueueStatus::kClosed};  // 本次 Pop 的明确结果。
    std::optional<T> item;                     // 仅 kOk 时包含元素。
};

template <typename T>
class BoundedQueue {
   public:
    /// 创建固定容量队列；capacity 为 0 属于编程错误并抛 invalid_argument。
    explicit BoundedQueue(std::size_t capacity,
                          OverflowPolicy policy = OverflowPolicy::kBlockProducer)
        : capacity_(capacity), policy_(policy) {
        if (capacity_ == 0) {
            throw std::invalid_argument("bounded queue capacity must be greater than zero");
        }
    }

    // 队列内部持有互斥锁和条件变量，复制既无明确语义也不受标准库支持，因此显式禁用。
    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    /// 尝试把 item 移入队列；满队列行为由 policy 决定。
    /// timeout 仅对 kBlockProducer 有意义，无限值表示一直等到空位、关闭或取消。
    QueueStatus Push(T item, std::stop_token stop = {},
                     std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
        // 唤醒条件变量，否则无限等待的生产者无法及时响应退出请求。
        std::stop_callback cancel_callback(stop, [this] { not_full_.notify_all(); });
        std::unique_lock lock(mutex_);

        if (closed_) {
            return QueueStatus::kClosed;
        }
        if (stop.stop_requested()) {
            return QueueStatus::kCancelled;
        }

        if (items_.size() >= capacity_) {
            // 只有队列确实已满时才执行溢出策略，任何分支都不得突破容量上限。
            switch (policy_) {
                case OverflowPolicy::kDropNewest:
                    ++dropped_;
                    return QueueStatus::kDropped;
                case OverflowPolicy::kDropOldest:
                    items_.pop_front();
                    ++dropped_;
                    break;
                case OverflowPolicy::kKeepLatest:
                    // 丢掉全部历史数据，随后只放入这次最新到达的数据。
                    dropped_ += static_cast<std::uint64_t>(items_.size());
                    items_.clear();
                    break;
                case OverflowPolicy::kBlockProducer: {
                    const auto ready = [this, &stop] {
                        return closed_ || stop.stop_requested() || items_.size() < capacity_;
                    };
                    bool awakened = true;
                    if (timeout == std::chrono::milliseconds::max()) {
                        not_full_.wait(lock, ready);
                    } else {
                        awakened = not_full_.wait_for(lock, timeout, ready);
                    }
                    if (!awakened) {
                        ++push_timeouts_;
                        return QueueStatus::kTimeout;
                    }
                    if (stop.stop_requested()) {
                        return QueueStatus::kCancelled;
                    }
                    if (closed_) {
                        return QueueStatus::kClosed;
                    }
                    break;
                }
            }
        }

        items_.push_back(std::move(item));
        ++pushed_;
        high_watermark_ = std::max(high_watermark_, items_.size());
        lock.unlock();
        not_empty_.notify_one();
        return QueueStatus::kOk;
    }

    /// 等待并取出队首元素；队列 Drain 关闭后仍会先返回剩余元素。
    QueuePopResult<T> Pop(std::stop_token stop = {},
                          std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
        // 同 Push 一样，取消请求必须能唤醒正在等待数据的消费者。
        std::stop_callback cancel_callback(stop, [this] { not_empty_.notify_all(); });
        std::unique_lock lock(mutex_);
        const auto ready = [this, &stop] {
            return aborted_ || !items_.empty() || closed_ || stop.stop_requested();
        };

        bool awakened = true;
        if (timeout == std::chrono::milliseconds::max()) {
            not_empty_.wait(lock, ready);
        } else {
            awakened = not_empty_.wait_for(lock, timeout, ready);
        }

        if (!awakened) {
            ++pop_timeouts_;
            return {QueueStatus::kTimeout, std::nullopt};
        }
        if (aborted_) {
            return {QueueStatus::kClosed, std::nullopt};
        }
        if (stop.stop_requested()) {
            return {QueueStatus::kCancelled, std::nullopt};
        }
        if (items_.empty()) {
            return {QueueStatus::kClosed, std::nullopt};
        }

        T item = std::move(items_.front());
        items_.pop_front();
        ++popped_;
        lock.unlock();
        not_full_.notify_one();
        return {QueueStatus::kOk, std::move(item)};
    }

    /// 关闭队列并唤醒全部等待者；函数可重复调用，Abort 可升级已发出的 Drain。
    void Close(CloseMode mode = CloseMode::kDrain) noexcept {
        {
            std::scoped_lock lock(mutex_);
            if (closed_ && (!aborted_ || mode == CloseMode::kDrain)) {
                return;
            }
            closed_ = true;
            if (mode == CloseMode::kAbort) {
                // Abort 的存量数据同样计入 dropped，便于停止后核对数据去向。
                aborted_ = true;
                dropped_ += static_cast<std::uint64_t>(items_.size());
                items_.clear();
            }
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    /// 在同一把锁下复制全部统计，保证快照内部字段彼此一致。
    [[nodiscard]] QueueSnapshot Snapshot() const {
        std::scoped_lock lock(mutex_);
        return QueueSnapshot{items_.size(), capacity_,      high_watermark_, pushed_, popped_,
                             dropped_,      push_timeouts_, pop_timeouts_,   closed_};
    }

   private:
    const std::size_t capacity_;         // 构造后不变的元素数量上限。
    const OverflowPolicy policy_;        // 构造后不变的满队列处理方式。
    mutable std::mutex mutex_;           // 保护队列、关闭状态和全部统计字段。
    std::condition_variable not_empty_;  // 消费者等待“有数据或关闭”。
    std::condition_variable not_full_;   // 生产者等待“有空位或关闭”。
    std::deque<T> items_;                // 实际 FIFO 元素容器。
    bool closed_{false};                 // true 后拒绝任何新 Push。
    bool aborted_{false};                // true 表示不再 Drain 存量。
    std::size_t high_watermark_{0};      // 历史最大占用。
    std::uint64_t pushed_{0};            // 成功入队总数。
    std::uint64_t popped_{0};            // 成功出队总数。
    std::uint64_t dropped_{0};           // 丢弃总数。
    std::uint64_t push_timeouts_{0};     // Push 超时总数。
    std::uint64_t pop_timeouts_{0};      // Pop 超时总数。
};

/// 把溢出策略转换成配置和日志使用的文本。
inline const char* ToString(OverflowPolicy policy) noexcept {
    switch (policy) {
        case OverflowPolicy::kBlockProducer:
            return "block_producer";
        case OverflowPolicy::kDropNewest:
            return "drop_newest";
        case OverflowPolicy::kDropOldest:
            return "drop_oldest";
        case OverflowPolicy::kKeepLatest:
            return "keep_latest";
    }
    return "unknown";
}

}  // namespace rkav
