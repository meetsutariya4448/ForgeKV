#include "forgekv/network/tcp.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <future>
#include <limits>
#include <string_view>

namespace forgekv::network {
namespace {

void close_socket(int& fd) noexcept {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

[[noreturn]] void throw_errno(std::string_view action) {
    throw NetworkError(std::string(action) + ": " + std::strerror(errno));
}

void set_timeouts(int fd, std::chrono::milliseconds timeout) {
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    const auto microseconds =
        std::chrono::duration_cast<std::chrono::microseconds>(timeout - seconds);
    timeval value{};
    value.tv_sec = static_cast<decltype(value.tv_sec)>(seconds.count());
    value.tv_usec = static_cast<decltype(value.tv_usec)>(microseconds.count());
    if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value)) != 0 ||
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &value, sizeof(value)) != 0) {
        throw_errno("setsockopt timeout");
    }
#ifdef SO_NOSIGPIPE
    int enabled = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0) {
        throw_errno("setsockopt SO_NOSIGPIPE");
    }
#endif
}

void send_all(int fd, std::span<const std::byte> bytes) {
    while (!bytes.empty()) {
        const std::size_t chunk = std::min(
            bytes.size(), static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
#ifdef MSG_NOSIGNAL
        constexpr int kSendFlags = MSG_NOSIGNAL;
#else
        constexpr int kSendFlags = 0;
#endif
        const ssize_t sent = ::send(fd, bytes.data(), chunk, kSendFlags);
        if (sent > 0) {
            bytes = bytes.subspan(static_cast<std::size_t>(sent));
            continue;
        }
        if (sent < 0 && errno == EINTR) continue;
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            throw NetworkError("socket write timed out");
        }
        throw_errno("socket write");
    }
}

protocol::Bytes message_bytes(std::string_view message) {
    const auto* begin = reinterpret_cast<const std::byte*>(message.data());
    return protocol::Bytes(begin, begin + message.size());
}

protocol::Frame response_for(const protocol::Frame& request, protocol::Status status,
                             protocol::Bytes value = {}) {
    return protocol::Frame{protocol::FrameKind::kResponse, request.opcode, status,
                           request.request_id, {}, std::move(value)};
}

}  // namespace

TcpServer::TcpServer(ServerConfig config)
    : config_(std::move(config)),
      storage_(storage::StorageEngine::open(config_.database_directory, config_.index_shards)),
      worker_pool_(config_.worker_count, config_.queue_capacity) {
    if (config_.max_connections == 0) {
        throw std::invalid_argument("max connections must be positive");
    }
    connections_.reserve(config_.max_connections);
    open_listener();
}

TcpServer::~TcpServer() { close_socket(listener_fd_); }

void TcpServer::serve(std::stop_token stop_token) {
    while (!stop_token.stop_requested()) {
        reap_connections();
        pollfd descriptor{listener_fd_, POLLIN, 0};
        const int result = ::poll(&descriptor, 1, 100);
        if (result == 0) continue;
        if (result < 0 && errno == EINTR) continue;
        if (result < 0) throw_errno("poll listener");
        if ((descriptor.revents & POLLIN) == 0) continue;

        int client_fd = ::accept(listener_fd_, nullptr, nullptr);
        if (client_fd < 0 && errno == EINTR) continue;
        if (client_fd < 0) throw_errno("accept");
        if (active_connections_.load() >= config_.max_connections) {
            close_socket(client_fd);
            continue;
        }
        try {
            set_timeouts(client_fd, config_.io_timeout);
        } catch (...) {
            close_socket(client_fd);
            throw;
        }
        auto done = std::make_shared<std::atomic_bool>(false);
        active_connections_.fetch_add(1);
        try {
            connections_.push_back(ConnectionWorker{done, std::jthread(
                [this, client_fd, done](std::stop_token connection_stop) mutable {
                    int owned_fd = client_fd;
                    try { handle_connection(owned_fd, connection_stop); }
                    catch (...) { }
                    close_socket(owned_fd);
                    active_connections_.fetch_sub(1);
                    done->store(true);
                })});
        } catch (...) {
            active_connections_.fetch_sub(1);
            close_socket(client_fd);
            throw;
        }
    }
    for (auto& connection : connections_) connection.thread.request_stop();
    connections_.clear();
    worker_pool_.shutdown();
}

std::uint16_t TcpServer::port() const noexcept { return bound_port_; }
std::size_t TcpServer::active_connections() const noexcept { return active_connections_.load(); }
std::size_t TcpServer::queued_requests() const { return worker_pool_.queued_tasks(); }

void TcpServer::open_listener() {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    addrinfo* addresses = nullptr;
    const std::string service = std::to_string(config_.port);
    const int status = ::getaddrinfo(config_.bind_address.c_str(), service.c_str(), &hints, &addresses);
    if (status != 0) throw NetworkError(std::string("getaddrinfo: ") + gai_strerror(status));

    for (addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        const int fd = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) continue;
        int reuse = 1;
        static_cast<void>(::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)));
        if (::bind(fd, address->ai_addr, address->ai_addrlen) == 0 && ::listen(fd, 16) == 0) {
            listener_fd_ = fd;
            break;
        }
        int temporary = fd;
        close_socket(temporary);
    }
    ::freeaddrinfo(addresses);
    if (listener_fd_ < 0) throw NetworkError("failed to bind listening socket");

    sockaddr_storage local{};
    socklen_t length = sizeof(local);
    if (::getsockname(listener_fd_, reinterpret_cast<sockaddr*>(&local), &length) != 0) {
        close_socket(listener_fd_);
        throw_errno("getsockname");
    }
    if (local.ss_family == AF_INET) {
        bound_port_ = ntohs(reinterpret_cast<sockaddr_in*>(&local)->sin_port);
    } else {
        bound_port_ = ntohs(reinterpret_cast<sockaddr_in6*>(&local)->sin6_port);
    }
}

void TcpServer::handle_connection(int client_fd, std::stop_token stop_token) {
    protocol::FrameParser parser;
    std::array<std::byte, 8192> buffer{};
    while (!stop_token.stop_requested()) {
        const ssize_t received = ::recv(client_fd, buffer.data(), buffer.size(), 0);
        if (received == 0) return;
        if (received < 0 && errno == EINTR) continue;
        if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (received < 0) throw_errno("socket read");
        try {
            const auto frames = parser.feed(
                std::span<const std::byte>(buffer).first(static_cast<std::size_t>(received)));
            for (const auto& frame : frames) {
                send_all(client_fd, protocol::encode_frame(dispatch_via_pool(frame)));
            }
        } catch (const protocol::ProtocolError&) {
            return;
        }
    }
}

protocol::Frame TcpServer::dispatch_via_pool(const protocol::Frame& request) {
    auto promise = std::make_shared<std::promise<protocol::Frame>>();
    auto result = promise->get_future();
    if (!worker_pool_.try_submit([this, request, promise] {
            try { promise->set_value(dispatch(request)); }
            catch (...) { promise->set_exception(std::current_exception()); }
        })) {
        return response_for(request, protocol::Status::kOverloaded,
                            message_bytes("request queue is full"));
    }
    return result.get();
}

void TcpServer::reap_connections() {
    std::erase_if(connections_, [](const ConnectionWorker& connection) {
        return connection.done->load();
    });
}

protocol::Frame TcpServer::dispatch(const protocol::Frame& request) {
    if (!protocol::request_semantics_valid(request)) {
        return response_for(request, protocol::Status::kInvalidRequest,
                            message_bytes("invalid request semantics"));
    }
    try {
        switch (request.opcode) {
            case protocol::Opcode::kPut:
                static_cast<void>(storage_.put(request.key, request.value));
                return response_for(request, protocol::Status::kOk);
            case protocol::Opcode::kGet: {
                auto value = storage_.get(request.key);
                return value ? response_for(request, protocol::Status::kOk, std::move(*value))
                             : response_for(request, protocol::Status::kNotFound);
            }
            case protocol::Opcode::kDelete: {
                const auto result = storage_.erase(request.key);
                return response_for(request, result.existed ? protocol::Status::kOk
                                                            : protocol::Status::kNotFound);
            }
            case protocol::Opcode::kExists: {
                const bool exists = storage_.get(request.key).has_value();
                return response_for(request, protocol::Status::kOk,
                                    {exists ? std::byte{1} : std::byte{0}});
            }
        }
    } catch (const storage::StorageError&) {
        return response_for(request, protocol::Status::kStorageError,
                            message_bytes("storage operation failed"));
    } catch (const std::exception&) {
        return response_for(request, protocol::Status::kInternalError,
                            message_bytes("internal server error"));
    }
    return response_for(request, protocol::Status::kInternalError);
}

TcpClient TcpClient::connect(const std::string& host, std::uint16_t port,
                             std::chrono::milliseconds timeout) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    const std::string service = std::to_string(port);
    const int status = ::getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses);
    if (status != 0) throw NetworkError(std::string("getaddrinfo: ") + gai_strerror(status));
    int connected = -1;
    for (addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        int fd = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) continue;
        try {
            set_timeouts(fd, timeout);
        } catch (...) {
            close_socket(fd);
            ::freeaddrinfo(addresses);
            throw;
        }
        if (::connect(fd, address->ai_addr, address->ai_addrlen) == 0) {
            connected = fd;
            break;
        }
        close_socket(fd);
    }
    ::freeaddrinfo(addresses);
    if (connected < 0) throw NetworkError("failed to connect to server");
    return TcpClient(connected);
}

TcpClient::TcpClient(int socket_fd) : socket_fd_(socket_fd) {}
TcpClient::TcpClient(TcpClient&& other) noexcept : socket_fd_(other.socket_fd_) { other.socket_fd_ = -1; }
TcpClient& TcpClient::operator=(TcpClient&& other) noexcept {
    if (this != &other) {
        close_socket(socket_fd_);
        socket_fd_ = other.socket_fd_;
        other.socket_fd_ = -1;
    }
    return *this;
}
TcpClient::~TcpClient() { close_socket(socket_fd_); }

protocol::Frame TcpClient::request(const protocol::Frame& request_frame) {
    if (request_frame.kind != protocol::FrameKind::kRequest) {
        throw std::invalid_argument("client can only send request frames");
    }
    send_all(socket_fd_, protocol::encode_frame(request_frame));
    protocol::FrameParser parser;
    std::array<std::byte, 8192> buffer{};
    for (;;) {
        const ssize_t received = ::recv(socket_fd_, buffer.data(), buffer.size(), 0);
        if (received == 0) throw NetworkError("server disconnected before response");
        if (received < 0 && errno == EINTR) continue;
        if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            throw NetworkError("response read timed out");
        }
        if (received < 0) throw_errno("response read");
        const auto frames = parser.feed(
            std::span<const std::byte>(buffer).first(static_cast<std::size_t>(received)));
        if (!frames.empty()) {
            const auto& response = frames.front();
            if (response.kind != protocol::FrameKind::kResponse ||
                response.request_id != request_frame.request_id ||
                response.opcode != request_frame.opcode) {
                throw NetworkError("response does not match request");
            }
            return response;
        }
    }
}

}  // namespace forgekv::network
