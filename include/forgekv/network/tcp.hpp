#pragma once

#include "forgekv/protocol/frame.hpp"
#include "forgekv/storage/engine.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <stop_token>
#include <string>

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
};

class TcpServer {
public:
    explicit TcpServer(ServerConfig config);
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    ~TcpServer();

    void serve(std::stop_token stop_token);
    [[nodiscard]] std::uint16_t port() const noexcept;

private:
    void open_listener();
    void handle_connection(int client_fd, std::stop_token stop_token);
    [[nodiscard]] protocol::Frame dispatch(const protocol::Frame& request);

    ServerConfig config_;
    storage::StorageEngine storage_;
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

private:
    explicit TcpClient(int socket_fd);
    int socket_fd_ = -1;
};

}  // namespace forgekv::network
