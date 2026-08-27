#include "forgekv/network/tcp.hpp"

#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <exception>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <thread>

namespace {
volatile std::sig_atomic_t stop_requested = 0;
void handle_signal(int) { stop_requested = 1; }

std::uint16_t parse_port(const std::string& text) {
    unsigned value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value > 65535U) {
        throw std::invalid_argument("invalid port: " + text);
    }
    return static_cast<std::uint16_t>(value);
}

std::size_t parse_positive_size(const std::string& text, std::string_view name) {
    std::size_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0) {
        throw std::invalid_argument("invalid " + std::string(name) + ": " + text);
    }
    return value;
}

forgekv::storage::DurabilityMode parse_durability(std::string_view text) {
    if (text == "always") return forgekv::storage::DurabilityMode::kAlways;
    if (text == "periodic") return forgekv::storage::DurabilityMode::kPeriodic;
    if (text == "none") return forgekv::storage::DurabilityMode::kNone;
    throw std::invalid_argument("invalid durability mode: " + std::string(text));
}

std::string_view durability_name(forgekv::storage::DurabilityMode mode) {
    switch (mode) {
        case forgekv::storage::DurabilityMode::kAlways:
            return "always";
        case forgekv::storage::DurabilityMode::kPeriodic:
            return "periodic";
        case forgekv::storage::DurabilityMode::kNone:
            return "none";
    }
    return "unknown";
}

void print_usage() {
    std::cerr << "usage: forgekv-server [--host ADDRESS] [--port PORT] [--data PATH] "
                 "[--workers COUNT] [--queue-capacity COUNT] "
                 "[--max-connections COUNT] [--index-shards COUNT] "
                 "[--durability always|periodic|none] [--sync-interval-ms COUNT] "
                 "[--segment-max-bytes COUNT] [--compaction-min-segments COUNT] "
                 "[--no-background-compaction]\n";
}
}  // namespace

int main(int argc, char** argv) {
    forgekv::network::ServerConfig config;
    try {
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--host" && index + 1 < argc) config.bind_address = argv[++index];
            else if (argument == "--port" && index + 1 < argc) config.port = parse_port(argv[++index]);
            else if (argument == "--data" && index + 1 < argc) config.database_directory = argv[++index];
            else if (argument == "--workers" && index + 1 < argc) {
                config.worker_count = parse_positive_size(argv[++index], "worker count");
            } else if (argument == "--queue-capacity" && index + 1 < argc) {
                config.queue_capacity = parse_positive_size(argv[++index], "queue capacity");
            } else if (argument == "--max-connections" && index + 1 < argc) {
                config.max_connections = parse_positive_size(argv[++index], "connection limit");
            } else if (argument == "--index-shards" && index + 1 < argc) {
                config.index_shards = parse_positive_size(argv[++index], "index shard count");
            } else if (argument == "--durability" && index + 1 < argc) {
                config.durability = parse_durability(argv[++index]);
            } else if (argument == "--sync-interval-ms" && index + 1 < argc) {
                const std::size_t milliseconds =
                    parse_positive_size(argv[++index], "sync interval");
                if (milliseconds > static_cast<std::size_t>(
                                       std::numeric_limits<std::chrono::milliseconds::rep>::max())) {
                    throw std::invalid_argument("sync interval is too large");
                }
                config.sync_interval = std::chrono::milliseconds{
                    static_cast<std::chrono::milliseconds::rep>(milliseconds)};
            } else if (argument == "--segment-max-bytes" && index + 1 < argc) {
                config.segment_max_bytes = parse_positive_size(argv[++index], "segment size");
            } else if (argument == "--compaction-min-segments" && index + 1 < argc) {
                config.compaction_min_segments =
                    parse_positive_size(argv[++index], "compaction threshold");
            } else if (argument == "--no-background-compaction") {
                config.background_compaction = false;
            } else {
                print_usage();
                return 2;
            }
        }
        forgekv::network::TcpServer server(config);
        std::cout << "ForgeKV listening on " << config.bind_address << ':' << server.port()
                  << " using " << config.database_directory << " (workers=" << config.worker_count
                  << ", queue=" << config.queue_capacity
                  << ", connections=" << config.max_connections
                  << ", shards=" << config.index_shards
                  << ", durability=" << durability_name(config.durability)
                  << ", sync_ms=" << config.sync_interval.count()
                  << ", segment_bytes=" << config.segment_max_bytes
                  << ", compaction_threshold=" << config.compaction_min_segments
                  << ", background_compaction=" << std::boolalpha
                  << config.background_compaction << ")\n";
        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);
        std::atomic_bool finished = false;
        std::exception_ptr server_error;
        std::jthread thread([&](std::stop_token token) {
            try { server.serve(token); }
            catch (...) { server_error = std::current_exception(); }
            finished.store(true);
        });
        while (stop_requested == 0 && !finished.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        thread.request_stop();
        thread.join();
        if (server_error) std::rethrow_exception(server_error);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "forgekv-server: " << error.what() << '\n';
        return 1;
    }
}
