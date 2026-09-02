#include "forgekv/network/tcp.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <atomic>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace forgekv::network {
namespace {

protocol::Bytes bytes(std::string_view value) {
    const auto* begin = reinterpret_cast<const std::byte*>(value.data());
    return protocol::Bytes(begin, begin + value.size());
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        std::random_device random;
        path_ = std::filesystem::temp_directory_path() /
                ("forgekv-network-test-" + std::to_string(random()));
        std::filesystem::remove_all(path_);
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

class RunningServer {
public:
    explicit RunningServer(const std::filesystem::path& path)
        : RunningServer(ServerConfig{"127.0.0.1", 0, path,
                                     std::chrono::milliseconds{50}}) {}

    explicit RunningServer(ServerConfig config)
        : server_(std::move(config)),
          thread_([this](std::stop_token token) {
              try { server_.serve(token); }
              catch (...) { error_ = std::current_exception(); }
          }) {}

    ~RunningServer() {
        thread_.request_stop();
        thread_.join();
        EXPECT_EQ(error_, nullptr);
    }
    std::uint16_t port() const { return server_.port(); }
    std::size_t active_connections() const { return server_.active_connections(); }

private:
    TcpServer server_;
    std::exception_ptr error_;
    std::jthread thread_;
};

bool wait_for_connection_count(const RunningServer& server, std::size_t expected) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (server.active_connections() == expected) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    return false;
}

protocol::Frame request(protocol::Opcode opcode, std::uint64_t id, protocol::Bytes key,
                        protocol::Bytes value = {}) {
    return protocol::Frame{protocol::FrameKind::kRequest, opcode, protocol::Status::kOk, id,
                           std::move(key), std::move(value)};
}

int connect_raw(std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_GE(fd, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    EXPECT_EQ(::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr), 1);
    EXPECT_EQ(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
    return fd;
}

std::pair<int, std::uint16_t> listen_for_failure_test() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_GE(fd, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = 0;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    EXPECT_EQ(::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
    EXPECT_EQ(::listen(fd, 1), 0);
    socklen_t length = sizeof(address);
    EXPECT_EQ(::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length), 0);
    return {fd, ntohs(address.sin_port)};
}

void send_raw_all(int fd, std::span<const std::byte> data) {
    while (!data.empty()) {
        const ssize_t count = ::send(fd, data.data(), data.size(), 0);
        ASSERT_GT(count, 0);
        if (count <= 0) return;
        data = data.subspan(static_cast<std::size_t>(count));
    }
}

protocol::Frame receive_frame(int fd) {
    protocol::FrameParser parser;
    std::array<std::byte, 256> buffer{};
    for (;;) {
        const ssize_t count = ::recv(fd, buffer.data(), buffer.size(), 0);
        EXPECT_GT(count, 0);
        if (count <= 0) throw NetworkError("test connection closed");
        auto frames = parser.feed(
            std::span<const std::byte>(buffer).first(static_cast<std::size_t>(count)));
        if (!frames.empty()) return std::move(frames.front());
    }
}

TEST(NetworkConfigurationTest, RejectsNonpositiveTimeoutsBeforeOpeningResources) {
    TemporaryDirectory temporary;
    ServerConfig config;
    config.database_directory = temporary.path();
    config.io_timeout = std::chrono::milliseconds::zero();

    EXPECT_THROW(static_cast<void>(TcpServer(config)), std::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(temporary.path()));
    EXPECT_THROW(static_cast<void>(TcpClient::connect(
                     "127.0.0.1", 7391, std::chrono::milliseconds::zero())),
                 std::invalid_argument);
}

TEST(NetworkConfigurationTest, RejectsInvalidCapacityBeforeOpeningStorage) {
    TemporaryDirectory temporary;
    for (void (*configure_invalid)(ServerConfig&) : {
             +[](ServerConfig& config) { config.worker_count = 0; },
             +[](ServerConfig& config) { config.queue_capacity = 0; },
             +[](ServerConfig& config) { config.max_connections = 0; },
         }) {
        ServerConfig config;
        config.database_directory = temporary.path();
        configure_invalid(config);

        EXPECT_THROW(static_cast<void>(TcpServer(config)), std::invalid_argument);
        EXPECT_FALSE(std::filesystem::exists(temporary.path()));
    }
}

TEST(NetworkConfigurationTest, RejectsInvalidClientEndpointsBeforeResolution) {
    EXPECT_THROW(static_cast<void>(TcpClient::connect("", 7391)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(TcpClient::connect(std::string("localhost\0ignored", 17),
                                                      7391)),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(TcpClient::connect("127.0.0.1", 0)),
                 std::invalid_argument);
}

TEST(NetworkIntegrationTest, BinaryCrudExistsAndPersistentConnectionWork) {
    TemporaryDirectory temporary;
    RunningServer server(temporary.path());
    auto client = TcpClient::connect("127.0.0.1", server.port());
    const protocol::Bytes key{std::byte{0}, std::byte{0xff}, std::byte{'k'}};
    const protocol::Bytes value{std::byte{1}, std::byte{0}, std::byte{0xfe}};

    EXPECT_EQ(client.request(request(protocol::Opcode::kPut, 1, key, value)).status,
              protocol::Status::kOk);
    const auto get = client.request(request(protocol::Opcode::kGet, 2, key));
    EXPECT_EQ(get.status, protocol::Status::kOk);
    EXPECT_EQ(get.value, value);
    EXPECT_EQ(client.request(request(protocol::Opcode::kExists, 3, key)).value,
              protocol::Bytes({std::byte{1}}));
    EXPECT_EQ(client.request(request(protocol::Opcode::kDelete, 4, key)).status,
              protocol::Status::kOk);
    EXPECT_EQ(client.request(request(protocol::Opcode::kGet, 5, key)).status,
              protocol::Status::kNotFound);
}

TEST(NetworkIntegrationTest, ClientPipelineMatchesOrderedResponses) {
    TemporaryDirectory temporary;
    RunningServer server(temporary.path());
    auto client = TcpClient::connect("127.0.0.1", server.port());
    const std::vector<protocol::Frame> requests{
        request(protocol::Opcode::kPut, 1, bytes("one"), bytes("first")),
        request(protocol::Opcode::kPut, 2, bytes("two"), bytes("second")),
        request(protocol::Opcode::kGet, 3, bytes("one")),
        request(protocol::Opcode::kGet, 4, bytes("two"))};

    const auto responses = client.pipeline(requests);
    ASSERT_EQ(responses.size(), requests.size());
    EXPECT_EQ(responses[0].status, protocol::Status::kOk);
    EXPECT_EQ(responses[1].status, protocol::Status::kOk);
    EXPECT_EQ(responses[2].value, bytes("first"));
    EXPECT_EQ(responses[3].value, bytes("second"));
}

TEST(NetworkIntegrationTest, PingAndStatsExposeBoundedObservability) {
    TemporaryDirectory temporary;
    RunningServer server(temporary.path());
    auto client = TcpClient::connect("127.0.0.1", server.port());
    const auto ping = client.request(request(protocol::Opcode::kPing, 1, {}));
    EXPECT_EQ(ping.status, protocol::Status::kOk);
    EXPECT_EQ(ping.value, bytes("PONG"));
    static_cast<void>(
        client.request(request(protocol::Opcode::kPut, 2, bytes("key"), bytes("value"))));
    const auto stats = client.request(request(protocol::Opcode::kStats, 3, {}));
    EXPECT_EQ(stats.status, protocol::Status::kOk);
    const std::string json(reinterpret_cast<const char*>(stats.value.data()), stats.value.size());
    EXPECT_NE(json.find("\"put\":1"), std::string::npos);
    EXPECT_NE(json.find("\"segments\":"), std::string::npos);
    EXPECT_NE(json.find("\"index_entries\":1"), std::string::npos);
}

TEST(NetworkIntegrationTest, RejectsSemanticErrorWithoutDroppingConnection) {
    TemporaryDirectory temporary;
    RunningServer server(temporary.path());
    auto client = TcpClient::connect("127.0.0.1", server.port());
    EXPECT_EQ(client.request(request(protocol::Opcode::kGet, 1, bytes("key"), bytes("bad"))).status,
              protocol::Status::kInvalidRequest);
    EXPECT_EQ(client.request(request(protocol::Opcode::kPut, 2, bytes("key"), bytes("ok"))).status,
              protocol::Status::kOk);
}

TEST(NetworkIntegrationTest, PutExTtlAndExpirationWork) {
    TemporaryDirectory temporary;
    RunningServer server(temporary.path());
    auto client = TcpClient::connect("127.0.0.1", server.port());
    const protocol::Bytes key = bytes("ephemeral");

    EXPECT_EQ(client.request(request(protocol::Opcode::kPutEx, 1, key,
                                     protocol::encode_put_ex_payload(100, bytes("value"))))
                  .status,
              protocol::Status::kOk);
    const auto ttl_response =
        client.request(request(protocol::Opcode::kTtl, 2, key));
    EXPECT_EQ(ttl_response.status, protocol::Status::kOk);
    EXPECT_GT(protocol::decode_ttl_payload(ttl_response.value), 0U);
    EXPECT_LE(protocol::decode_ttl_payload(ttl_response.value), 100U);
    EXPECT_EQ(client.request(request(protocol::Opcode::kGet, 3, key)).value,
              bytes("value"));

    std::this_thread::sleep_for(std::chrono::milliseconds{150});
    EXPECT_EQ(client.request(request(protocol::Opcode::kGet, 4, key)).status,
              protocol::Status::kNotFound);
    EXPECT_EQ(client.request(request(protocol::Opcode::kTtl, 5, key)).status,
              protocol::Status::kNotFound);
}

TEST(NetworkIntegrationTest, TtlReportsPersistentKey) {
    TemporaryDirectory temporary;
    RunningServer server(temporary.path());
    auto client = TcpClient::connect("127.0.0.1", server.port());
    EXPECT_EQ(client.request(request(protocol::Opcode::kPut, 1, bytes("key"), bytes("value")))
                  .status,
              protocol::Status::kOk);
    EXPECT_EQ(client.request(request(protocol::Opcode::kTtl, 2, bytes("key"))).status,
              protocol::Status::kNoExpiry);
}

TEST(NetworkIntegrationTest, HandlesByteAtATimeTcpFragmentation) {
    TemporaryDirectory temporary;
    RunningServer server(temporary.path());
    const int fd = connect_raw(server.port());
    const auto encoded = protocol::encode_frame(
        request(protocol::Opcode::kPut, 77, bytes("fragmented"), bytes("value")));
    for (const std::byte byte : encoded) {
        ASSERT_EQ(::send(fd, &byte, 1, 0), 1);
    }
    const auto response = receive_frame(fd);
    EXPECT_EQ(response.request_id, 77U);
    EXPECT_EQ(response.status, protocol::Status::kOk);
    ::close(fd);
}

TEST(NetworkIntegrationTest, RestartRecoversNetworkWrittenValue) {
    TemporaryDirectory temporary;
    {
        RunningServer server(temporary.path());
        auto client = TcpClient::connect("127.0.0.1", server.port());
        EXPECT_EQ(client.request(request(protocol::Opcode::kPut, 1, bytes("key"), bytes("value"))).status,
                  protocol::Status::kOk);
    }
    RunningServer restarted(temporary.path());
    auto client = TcpClient::connect("127.0.0.1", restarted.port());
    const auto response = client.request(request(protocol::Opcode::kGet, 2, bytes("key")));
    EXPECT_EQ(response.status, protocol::Status::kOk);
    EXPECT_EQ(response.value, bytes("value"));
}

TEST(NetworkIntegrationTest, MalformedConnectionClosesAndServerContinues) {
    TemporaryDirectory temporary;
    RunningServer server(temporary.path());
    int fd = connect_raw(server.port());
    auto malformed = protocol::encode_frame(request(protocol::Opcode::kGet, 1, bytes("key")));
    malformed[0] = std::byte{'X'};
    send_raw_all(fd, malformed);
    std::array<std::byte, 1> byte{};
    EXPECT_EQ(::recv(fd, byte.data(), byte.size(), 0), 0);
    ::close(fd);

    auto client = TcpClient::connect("127.0.0.1", server.port());
    EXPECT_EQ(client.request(request(protocol::Opcode::kPut, 2, bytes("key"), bytes("value"))).status,
              protocol::Status::kOk);
}

TEST(NetworkIntegrationTest, ServesConcurrentClientsWithoutLosingWrites) {
    TemporaryDirectory temporary;
    ServerConfig config{"127.0.0.1", 0, temporary.path(), std::chrono::milliseconds{100}};
    config.worker_count = 4;
    config.queue_capacity = 32;
    config.max_connections = 16;
    RunningServer server(std::move(config));
    std::atomic_int failures = 0;
    std::vector<std::jthread> clients;

    for (int client_index = 0; client_index < 8; ++client_index) {
        clients.emplace_back([&, client_index] {
            try {
                auto client = TcpClient::connect("127.0.0.1", server.port());
                const std::string key = "key-" + std::to_string(client_index);
                const std::string value = "value-" + std::to_string(client_index);
                if (client.request(request(protocol::Opcode::kPut,
                                           static_cast<std::uint64_t>(client_index + 1),
                                           bytes(key), bytes(value))).status !=
                    protocol::Status::kOk) {
                    failures.fetch_add(1);
                    return;
                }
                const auto response = client.request(
                    request(protocol::Opcode::kGet,
                            static_cast<std::uint64_t>(client_index + 100), bytes(key)));
                if (response.status != protocol::Status::kOk || response.value != bytes(value)) {
                    failures.fetch_add(1);
                }
            } catch (...) {
                failures.fetch_add(1);
            }
        });
    }
    clients.clear();
    EXPECT_EQ(failures.load(), 0);
}

TEST(NetworkIntegrationTest, EnforcesConfiguredConnectionLimit) {
    TemporaryDirectory temporary;
    ServerConfig config{"127.0.0.1", 0, temporary.path(), std::chrono::milliseconds{50}};
    config.max_connections = 1;
    RunningServer server(std::move(config));
    const int first = connect_raw(server.port());
    ASSERT_TRUE(wait_for_connection_count(server, 1));

    auto rejected = TcpClient::connect("127.0.0.1", server.port(),
                                       std::chrono::milliseconds{250});
    EXPECT_THROW(static_cast<void>(rejected.request(
                     request(protocol::Opcode::kGet, 1, bytes("key")))), NetworkError);
    ::close(first);
}

TEST(NetworkIntegrationTest, ReturnsOverloadedWhenRequestQueueSaturates) {
    TemporaryDirectory temporary;
    ServerConfig config{"127.0.0.1", 0, temporary.path(), std::chrono::seconds{10}};
    config.worker_count = 1;
    config.queue_capacity = 1;
    config.max_connections = 8;
    RunningServer server(std::move(config));

    std::vector<TcpClient> clients;
    clients.reserve(8);
    for (int index = 0; index < 8; ++index) {
        clients.push_back(TcpClient::connect("127.0.0.1", server.port(),
                                             std::chrono::seconds{10}));
    }
    ASSERT_TRUE(wait_for_connection_count(server, 8));

    const protocol::Bytes large_value(1024 * 1024, std::byte{0x5a});
    std::barrier start_line(9);
    std::atomic_int overloaded = 0;
    std::atomic_int failures = 0;
    std::vector<std::jthread> requests;
    requests.reserve(clients.size());
    for (std::size_t index = 0; index < clients.size(); ++index) {
        requests.emplace_back([&, index] {
            start_line.arrive_and_wait();
            try {
                const auto response = clients[index].request(request(
                    protocol::Opcode::kPut, static_cast<std::uint64_t>(index + 1),
                    bytes("key-" + std::to_string(index)), large_value));
                if (response.status == protocol::Status::kOverloaded) {
                    overloaded.fetch_add(1);
                } else if (response.status != protocol::Status::kOk) {
                    failures.fetch_add(1);
                }
            } catch (...) {
                failures.fetch_add(1);
            }
        });
    }
    start_line.arrive_and_wait();
    requests.clear();

    EXPECT_GT(overloaded.load(), 0);
    EXPECT_EQ(failures.load(), 0);
}

TEST(NetworkIntegrationTest, ShutdownInterruptsIdleConnections) {
    TemporaryDirectory temporary;
    int idle = -1;
    {
        ServerConfig config{"127.0.0.1", 0, temporary.path(), std::chrono::milliseconds{25}};
        RunningServer server(std::move(config));
        idle = connect_raw(server.port());
        ASSERT_TRUE(wait_for_connection_count(server, 1));
    }
    ::close(idle);
}

TEST(NetworkFailureTest, ConnectionRefusalIsReportedPromptly) {
    const auto [listener, port] = listen_for_failure_test();
    ::close(listener);
    const auto started = std::chrono::steady_clock::now();
    EXPECT_THROW(static_cast<void>(TcpClient::connect(
                     "127.0.0.1", port, std::chrono::milliseconds{100})),
                 NetworkError);
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds{2});
}

TEST(NetworkFailureTest, ResponseTimeoutIsReportedWithinConfiguredBound) {
    const auto [listener, port] = listen_for_failure_test();
    std::jthread peer([listener] {
        const int accepted = ::accept(listener, nullptr, nullptr);
        if (accepted >= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds{150});
            ::close(accepted);
        }
        ::close(listener);
    });
    auto client = TcpClient::connect("127.0.0.1", port, std::chrono::milliseconds{25});
    const auto started = std::chrono::steady_clock::now();
    EXPECT_THROW(static_cast<void>(
                     client.request(request(protocol::Opcode::kGet, 1, bytes("key")))),
                 NetworkError);
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds{1});
}

TEST(NetworkFailureTest, ConnectionResetIsReported) {
    const auto [listener, port] = listen_for_failure_test();
    std::jthread peer([listener] {
        const int accepted = ::accept(listener, nullptr, nullptr);
        if (accepted >= 0) {
            linger reset{1, 0};
            static_cast<void>(::setsockopt(accepted, SOL_SOCKET, SO_LINGER, &reset,
                                           sizeof(reset)));
            ::close(accepted);
        }
        ::close(listener);
    });
    auto client = TcpClient::connect("127.0.0.1", port, std::chrono::milliseconds{250});
    EXPECT_THROW(static_cast<void>(
                     client.request(request(protocol::Opcode::kGet, 1, bytes("key")))),
                 NetworkError);
}

TEST(NetworkFailureTest, InvalidResponseSemanticsAreRejected) {
    const auto [listener, port] = listen_for_failure_test();
    std::jthread peer([listener] {
        int accepted = ::accept(listener, nullptr, nullptr);
        if (accepted >= 0) {
            const auto malformed = protocol::encode_frame(protocol::Frame{
                protocol::FrameKind::kResponse, protocol::Opcode::kExists,
                protocol::Status::kOk, 1, {}, {std::byte{2}}});
            send_raw_all(accepted, malformed);
            ::close(accepted);
        }
        ::close(listener);
    });
    auto client = TcpClient::connect("127.0.0.1", port, std::chrono::milliseconds{250});

    EXPECT_THROW(static_cast<void>(client.request(
                     request(protocol::Opcode::kExists, 1, bytes("key")))),
                 NetworkError);
}

}  // namespace
}  // namespace forgekv::network
