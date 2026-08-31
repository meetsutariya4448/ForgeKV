#pragma once
#include "forgekv/concurrency/bounded_queue.hpp"
#include <atomic>
#include <cstddef>
#include <functional>
#include <thread>
#include <vector>
namespace forgekv::concurrency {
class WorkerPool {
public:
    using Task = std::function<void()>;
    WorkerPool(std::size_t worker_count, std::size_t queue_capacity);
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;
    ~WorkerPool();
    [[nodiscard]] bool try_submit(Task task);
    void shutdown();
    [[nodiscard]] std::size_t queued_tasks() const;
    [[nodiscard]] std::size_t queue_capacity() const noexcept;
    [[nodiscard]] std::size_t worker_count() const noexcept;
    [[nodiscard]] bool stopping() const noexcept;
private:
    void run_worker();
    BoundedQueue<Task> queue_;
    std::vector<std::jthread> workers_;
    std::atomic_size_t worker_count_ = 0;
    std::atomic_bool stopping_ = false;
};
}  // namespace forgekv::concurrency
