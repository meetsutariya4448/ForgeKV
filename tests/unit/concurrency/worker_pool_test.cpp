#include "forgekv/concurrency/bounded_queue.hpp"
#include "forgekv/concurrency/worker_pool.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <future>
namespace forgekv::concurrency {
namespace {
TEST(BoundedQueueTest, RejectsBeyondCapacityAndAfterClose) {
    BoundedQueue<int> queue(2);
    EXPECT_TRUE(queue.try_push(1));
    EXPECT_TRUE(queue.try_push(2));
    EXPECT_FALSE(queue.try_push(3));
    EXPECT_EQ(queue.pop(), 1);
    queue.close();
    EXPECT_FALSE(queue.try_push(4));
    EXPECT_EQ(queue.pop(), 2);
    EXPECT_EQ(queue.pop(), std::nullopt);
}
TEST(WorkerPoolTest, ExecutesSubmittedTasks) {
    WorkerPool pool(2, 8);
    std::atomic_int count = 0;
    for (int index = 0; index < 8; ++index) ASSERT_TRUE(pool.try_submit([&] { ++count; }));
    pool.shutdown();
    EXPECT_EQ(count.load(), 8);
}
TEST(WorkerPoolTest, DeterministicallyRejectsWhenQueueIsFull) {
    WorkerPool pool(1, 1);
    std::promise<void> release;
    auto gate = release.get_future().share();
    std::promise<void> started;
    ASSERT_TRUE(pool.try_submit([&] { started.set_value(); gate.wait(); }));
    started.get_future().wait();
    ASSERT_TRUE(pool.try_submit([] {}));
    EXPECT_FALSE(pool.try_submit([] {}));
    release.set_value();
    pool.shutdown();
}
TEST(WorkerPoolTest, ShutdownDrainsAndRejectsNewWork) {
    WorkerPool pool(1, 4);
    std::atomic_int count = 0;
    ASSERT_TRUE(pool.try_submit([&] { ++count; }));
    ASSERT_TRUE(pool.try_submit([&] { ++count; }));
    pool.shutdown();
    EXPECT_EQ(count.load(), 2);
    EXPECT_TRUE(pool.stopping());
    EXPECT_FALSE(pool.try_submit([] {}));
}
TEST(WorkerPoolTest, ReportsWorkersSafelyAcrossShutdown) {
    WorkerPool pool(2, 4);
    EXPECT_EQ(pool.worker_count(), 2U);
    pool.shutdown();
    EXPECT_EQ(pool.worker_count(), 0U);
}
}  // namespace
}  // namespace forgekv::concurrency
