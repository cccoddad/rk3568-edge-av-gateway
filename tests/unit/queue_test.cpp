// 文件作用：逐项验证 BoundedQueue 的四种核心关闭、溢出、超时和取消行为。
// 主要知识点：生产者/消费者、背压、optional、stop_token 和确定性并发测试。
#include <chrono>
#include <stop_token>

#include <gtest/gtest.h>

#include "rkav/queue/bounded_queue.h"

namespace rkav {
namespace {

using namespace std::chrono_literals;

// 队列满时丢最旧元素，剩余元素仍必须保持 FIFO 顺序。
TEST(BoundedQueueTest, DropOldestKeepsNewestItemsInOrder) {
    BoundedQueue<int> queue(2, OverflowPolicy::kDropOldest);
    EXPECT_EQ(queue.Push(1), QueueStatus::kOk);
    EXPECT_EQ(queue.Push(2), QueueStatus::kOk);
    EXPECT_EQ(queue.Push(3), QueueStatus::kOk);

    const auto first = queue.Pop({}, 0ms);
    const auto second = queue.Pop({}, 0ms);
    ASSERT_TRUE(first.item.has_value());
    ASSERT_TRUE(second.item.has_value());
    EXPECT_EQ(*first.item, 2);
    EXPECT_EQ(*second.item, 3);
    EXPECT_EQ(queue.Snapshot().dropped, 1U);
}

// keep_latest 在满队列时清空所有历史元素，只留下最新输入。
TEST(BoundedQueueTest, KeepLatestDiscardsAllStaleItems) {
    BoundedQueue<int> queue(3, OverflowPolicy::kKeepLatest);
    EXPECT_EQ(queue.Push(1), QueueStatus::kOk);
    EXPECT_EQ(queue.Push(2), QueueStatus::kOk);
    EXPECT_EQ(queue.Push(3), QueueStatus::kOk);
    EXPECT_EQ(queue.Push(4), QueueStatus::kOk);

    const auto result = queue.Pop({}, 0ms);
    ASSERT_TRUE(result.item.has_value());
    EXPECT_EQ(*result.item, 4);
    EXPECT_EQ(queue.Snapshot().dropped, 3U);
}

// Drain 关闭拒绝新 Push，但允许 Pop 取完关闭前已经入队的数据。
TEST(BoundedQueueTest, DrainClosePreservesQueuedItems) {
    BoundedQueue<int> queue(2);
    EXPECT_EQ(queue.Push(7), QueueStatus::kOk);
    queue.Close(CloseMode::kDrain);

    const auto value = queue.Pop({}, 0ms);
    const auto end = queue.Pop({}, 0ms);
    ASSERT_TRUE(value.item.has_value());
    EXPECT_EQ(*value.item, 7);
    EXPECT_EQ(end.status, QueueStatus::kClosed);
}

// Abort 立即清空存量，并把被清空元素计入 dropped。
TEST(BoundedQueueTest, AbortCloseDropsQueuedItems) {
    BoundedQueue<int> queue(2);
    EXPECT_EQ(queue.Push(7), QueueStatus::kOk);
    EXPECT_EQ(queue.Push(8), QueueStatus::kOk);
    queue.Close(CloseMode::kAbort);

    EXPECT_EQ(queue.Pop({}, 0ms).status, QueueStatus::kClosed);
    EXPECT_EQ(queue.Snapshot().dropped, 2U);
}

// 阻塞策略既要能超时，也要能被已经请求的 stop_token 立即取消。
TEST(BoundedQueueTest, BlockPolicyReportsTimeoutAndCancellation) {
    BoundedQueue<int> queue(1, OverflowPolicy::kBlockProducer);
    EXPECT_EQ(queue.Push(1), QueueStatus::kOk);
    EXPECT_EQ(queue.Push(2, {}, 1ms), QueueStatus::kTimeout);

    std::stop_source stopped;
    stopped.request_stop();
    EXPECT_EQ(queue.Push(3, stopped.get_token()), QueueStatus::kCancelled);
    EXPECT_EQ(queue.Snapshot().push_timeouts, 1U);
}

}  // namespace
}  // namespace rkav
