#include "forgekv/concurrency/worker_pool.hpp"
#include <stdexcept>
namespace forgekv::concurrency {
WorkerPool::WorkerPool(std::size_t worker_count, std::size_t queue_capacity)
    : queue_(queue_capacity), worker_count_(worker_count) {
    if (worker_count == 0) throw std::invalid_argument("worker count must be positive");
    workers_.reserve(worker_count);
    for (std::size_t index = 0; index < worker_count; ++index) {
        workers_.emplace_back([this] { run_worker(); });
    }
}
WorkerPool::~WorkerPool() { shutdown(); }
bool WorkerPool::try_submit(Task task) {
    if (!task || stopping_.load()) return false;
    return queue_.try_push(std::move(task));
}
void WorkerPool::shutdown() {
    bool expected = false;
    if (!stopping_.compare_exchange_strong(expected, true)) return;
    queue_.close();
    workers_.clear();
    worker_count_.store(0);
}
std::size_t WorkerPool::queued_tasks() const { return queue_.size(); }
std::size_t WorkerPool::queue_capacity() const noexcept { return queue_.capacity(); }
std::size_t WorkerPool::worker_count() const noexcept { return worker_count_.load(); }
bool WorkerPool::stopping() const noexcept { return stopping_.load(); }
void WorkerPool::run_worker() {
    while (auto task = queue_.pop()) {
        try { (*task)(); }
        catch (...) { /* Task owners communicate through their result channel. */ }
    }
}
}  // namespace forgekv::concurrency
