#pragma once

#include "forgekv/concurrency/worker_pool.hpp"
#include "forgekv/protocol/frame.hpp"
#include "forgekv/storage/engine.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace forgekv::network {

class NetworkError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct ServerConfig {
    std::string bind_address = "127.0.0.1";
    std::uint16_t port = 7391;
    std::filesystem::path database_directory = "forgekv-data";
    std::chrono::milliseconds io_timeout{250};
    std::size_t worker_count = 4;
    std::size_t queue_capacity = 256;
    std::size_t max_connections = 128;
    std::size_t index_shards = 16;
    storage::DurabilityMode durability = storage::DurabilityMode::kPeriodic;
    std::chrono::milliseconds sync_interval{1000};
    std::uint64_t segment_max_bytes = 64U * 1024U * 1024U;
    std::size_t compaction_min_segments = 4;
    bool background_compaction = true;
};

class TcpServer {
public:
    explicit TcpServer(ServerConfig config);
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    ~TcpServer();

    void serve(std::stop_token stop_token);
    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] std::size_t active_connections() const noexcept;
    [[nodiscard]] std::size_t queued_requests() const;

private:
    void open_listener();
    void handle_connection(int client_fd, std::stop_token stop_token);
    [[nodiscard]] protocol::Frame dispatch(const protocol::Frame& request);
    [[nodiscard]] protocol::Frame dispatch_via_pool(const protocol::Frame& request);
    void reap_connections();

    struct ConnectionWorker {
        std::shared_ptr<std::atomic_bool> done;
        std::jthread thread;
    };

    ServerConfig config_;
    storage::StorageEngine storage_;
    concurrency::WorkerPool worker_pool_;
    std::vector<ConnectionWorker> connections_;
    std::atomic_size_t active_connections_ = 0;
    std::atomic_uint64_t get_operations_ = 0;
    std::atomic_uint64_t put_operations_ = 0;
    std::atomic_uint64_t delete_operations_ = 0;
    std::atomic_uint64_t exists_operations_ = 0;
    std::atomic_uint64_t put_ex_operations_ = 0;
    std::atomic_uint64_t ttl_operations_ = 0;
    std::atomic_uint64_t request_errors_ = 0;
    int listener_fd_ = -1;
    std::uint16_t bound_port_ = 0;
};

class TcpClient {
public:
    [[nodiscard]] static TcpClient connect(const std::string& host, std::uint16_t port,
                                           std::chrono::milliseconds timeout =
                                               std::chrono::milliseconds{2000});
    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;
    TcpClient(TcpClient&& other) noexcept;
    TcpClient& operator=(TcpClient&& other) noexcept;
    ~TcpClient();

    [[nodiscard]] protocol::Frame request(const protocol::Frame& request);
    [[nodiscard]] std::vector<protocol::Frame> pipeline(
        std::span<const protocol::Frame> requests);

private:
    explicit TcpClient(int socket_fd);
    int socket_fd_ = -1;
};

}  // namespace forgekv::network
