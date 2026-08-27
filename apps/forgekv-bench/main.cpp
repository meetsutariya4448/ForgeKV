#include "forgekv/index/sharded_index.hpp"
#include "forgekv/network/tcp.hpp"
#include "forgekv/storage/location.hpp"

#include <sys/utsname.h>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef FORGEKV_GIT_SHA
#define FORGEKV_GIT_SHA "unknown"
#endif
#ifndef FORGEKV_COMPILER_DESCRIPTION
#define FORGEKV_COMPILER_DESCRIPTION "unknown"
#endif
#ifndef FORGEKV_GIT_DIRTY
#define FORGEKV_GIT_DIRTY 1
#endif
#ifndef FORGEKV_BUILD_DESCRIPTION
#define FORGEKV_BUILD_DESCRIPTION "unknown"
#endif

namespace {

using Clock = std::chrono::steady_clock;

struct ContentionOptions {
    std::size_t threads = 4;
    std::size_t operations_per_thread = 100'000;
    std::size_t key_count = 4'096;
    std::size_t repetitions = 3;
    std::vector<std::size_t> shard_counts{1, 4, 16, 64, 256};
};

struct NetworkOptions {
    std::string host = "127.0.0.1";
    std::uint16_t port = 7391;
    std::size_t connections = 4;
    std::size_t threads = 4;
    std::uint64_t requests = 10'000;
    std::chrono::seconds duration{0};
    double read_ratio = 0.8;
    std::size_t key_count = 1'000;
    std::size_t value_size = 128;
    std::size_t pipeline_depth = 1;
    std::size_t warmup_requests = 1'000;
    std::uint64_t seed = 1;
    std::size_t repetition = 1;
    std::size_t server_workers = 0;
    std::size_t server_shards = 0;
    std::string durability = "unspecified";
    std::string ram_description = "unspecified";
    std::string storage_medium = "unspecified";
    std::string output_prefix;
};

struct NetworkResult {
    std::uint64_t operations = 0;
    std::uint64_t errors = 0;
    std::uint64_t connection_errors = 0;
    double seconds = 0;
    double operations_per_second = 0;
    double p50_us = 0;
    double p95_us = 0;
    double p99_us = 0;
    double max_us = 0;
    std::vector<double> raw_latency_us;
};

std::size_t parse_positive_size(std::string_view text, std::string_view name) {
    std::size_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0) {
        throw std::invalid_argument("invalid " + std::string(name) + ": " + std::string(text));
    }
    return value;
}

std::uint64_t parse_u64(std::string_view text, std::string_view name) {
    std::uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::invalid_argument("invalid " + std::string(name) + ": " + std::string(text));
    }
    return value;
}

double parse_ratio(std::string_view text) {
    std::size_t consumed = 0;
    const double value = std::stod(std::string(text), &consumed);
    if (consumed != text.size() || !std::isfinite(value) || value < 0 || value > 1) {
        throw std::invalid_argument("read ratio must be between 0 and 1");
    }
    return value;
}

std::uint16_t parse_port(std::string_view text) {
    const std::uint64_t value = parse_u64(text, "port");
    if (value == 0 || value > 65'535) throw std::invalid_argument("port is outside 1..65535");
    return static_cast<std::uint16_t>(value);
}

std::vector<std::size_t> parse_shards(std::string_view text) {
    std::vector<std::size_t> shards;
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        shards.push_back(parse_positive_size(text.substr(0, comma), "shard count"));
        if (comma == std::string_view::npos) break;
        text.remove_prefix(comma + 1);
    }
    return shards;
}

void print_usage() {
    std::cerr
        << "usage: forgekv-bench network [--host HOST] [--port PORT] [--connections N] "
           "[--threads N] [--requests N] [--duration SECONDS] [--read-ratio 0..1] "
           "[--key-count N] [--value-size BYTES] [--pipeline-depth N] "
           "[--warmup-requests N] [--seed N] [--output-prefix PATH]\n"
           "       forgekv-bench contention [--threads N] [--operations-per-thread N] "
           "[--keys N] [--repetitions N] [--shards 1,4,16,64,256]\n";
}

forgekv::protocol::Bytes bytes(std::string_view value) {
    const auto* begin = reinterpret_cast<const std::byte*>(value.data());
    return forgekv::protocol::Bytes(begin, begin + value.size());
}

std::string key_for(std::uint64_t index) {
    std::ostringstream stream;
    stream << "bench-key-" << std::setw(12) << std::setfill('0') << index;
    return stream.str();
}

ContentionOptions parse_contention_options(int argc, char** argv) {
    ContentionOptions options;
    for (int index = 2; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--threads" && index + 1 < argc) {
            options.threads = parse_positive_size(argv[++index], "thread count");
        } else if (argument == "--operations-per-thread" && index + 1 < argc) {
            options.operations_per_thread = parse_positive_size(argv[++index], "operation count");
        } else if (argument == "--keys" && index + 1 < argc) {
            options.key_count = parse_positive_size(argv[++index], "key count");
        } else if (argument == "--repetitions" && index + 1 < argc) {
            options.repetitions = parse_positive_size(argv[++index], "repetition count");
        } else if (argument == "--shards" && index + 1 < argc) {
            options.shard_counts = parse_shards(argv[++index]);
        } else {
            throw std::invalid_argument("unknown or incomplete contention option");
        }
    }
    return options;
}

NetworkOptions parse_network_options(int argc, char** argv) {
    NetworkOptions options;
    for (int index = 2; index < argc; ++index) {
        const std::string_view argument = argv[index];
        auto next = [&]() -> std::string_view {
            if (index + 1 >= argc) throw std::invalid_argument("missing value for " + std::string(argument));
            return argv[++index];
        };
        if (argument == "--host") options.host = next();
        else if (argument == "--port") options.port = parse_port(next());
        else if (argument == "--connections") options.connections = parse_positive_size(next(), "connections");
        else if (argument == "--threads") options.threads = parse_positive_size(next(), "threads");
        else if (argument == "--requests") options.requests = parse_u64(next(), "requests");
        else if (argument == "--duration") options.duration = std::chrono::seconds(parse_u64(next(), "duration"));
        else if (argument == "--read-ratio") options.read_ratio = parse_ratio(next());
        else if (argument == "--key-count") options.key_count = parse_positive_size(next(), "key count");
        else if (argument == "--value-size") options.value_size = parse_positive_size(next(), "value size");
        else if (argument == "--pipeline-depth") options.pipeline_depth = parse_positive_size(next(), "pipeline depth");
        else if (argument == "--warmup-requests") options.warmup_requests = static_cast<std::size_t>(parse_u64(next(), "warmup requests"));
        else if (argument == "--seed") options.seed = parse_u64(next(), "seed");
        else if (argument == "--repetition") options.repetition = parse_positive_size(next(), "repetition");
        else if (argument == "--server-workers") options.server_workers = parse_positive_size(next(), "server workers");
        else if (argument == "--server-shards") options.server_shards = parse_positive_size(next(), "server shards");
        else if (argument == "--durability") options.durability = next();
        else if (argument == "--ram-description") options.ram_description = next();
        else if (argument == "--storage-medium") options.storage_medium = next();
        else if (argument == "--output-prefix") options.output_prefix = next();
        else throw std::invalid_argument("unknown or incomplete network option: " + std::string(argument));
    }
    if (options.requests == 0 && options.duration == std::chrono::seconds::zero()) {
        throw std::invalid_argument("requests and duration cannot both be zero");
    }
    if (options.value_size > forgekv::storage::kMaxValueSize) {
        throw std::invalid_argument("value size exceeds protocol limit");
    }
    options.threads = std::min(options.threads, options.connections);
    return options;
}

forgekv::storage::RecordLocation location(std::uint64_t sequence) {
    return forgekv::storage::RecordLocation{1, sequence * 64, sequence * 64 + 32, 8, 16,
                                            sequence, 0};
}

double run_contention_once(std::size_t shard_count, const ContentionOptions& options,
                           const std::vector<std::string>& keys, bool same_key,
                           std::uint64_t& checksum) {
    forgekv::index::ShardedIndex index(shard_count);
    for (std::size_t key_index = 0; key_index < keys.size(); ++key_index) {
        index.insert_or_assign(keys[key_index], location(key_index + 1));
    }
    std::barrier start_line(static_cast<std::ptrdiff_t>(options.threads + 1));
    std::vector<std::jthread> workers;
    std::atomic_uint64_t combined = 0;
    for (std::size_t thread_index = 0; thread_index < options.threads; ++thread_index) {
        workers.emplace_back([&, thread_index] {
            std::uint64_t local = 0;
            start_line.arrive_and_wait();
            for (std::size_t operation = 0; operation < options.operations_per_thread; ++operation) {
                const std::size_t key_index = same_key ? 0 :
                    (thread_index * 1'009 + operation * 17) % keys.size();
                if (operation % 10 == 0) index.insert_or_assign(keys[key_index], location(operation + 1));
                else if (const auto found = index.find(keys[key_index])) local += found->sequence;
            }
            combined.fetch_add(local);
        });
    }
    const auto started = Clock::now();
    start_line.arrive_and_wait();
    workers.clear();
    checksum = combined.load();
    return std::chrono::duration<double>(Clock::now() - started).count();
}

void run_contention(const ContentionOptions& options) {
    std::vector<std::string> keys;
    for (std::size_t index = 0; index < options.key_count; ++index) {
        keys.push_back("contention-key-" + std::to_string(index));
    }
    std::cout << "workload,shards,threads,keys,operations,repetition,seconds,ops_per_second,checksum\n";
    for (const std::size_t shards : options.shard_counts) {
        for (const bool same_key : {true, false}) {
            for (std::size_t repetition = 1; repetition <= options.repetitions; ++repetition) {
                std::uint64_t checksum = 0;
                const double seconds = run_contention_once(shards, options, keys, same_key, checksum);
                const std::size_t operations = options.threads * options.operations_per_thread;
                std::cout << (same_key ? "same-key" : "distributed-key") << ',' << shards << ','
                          << options.threads << ',' << options.key_count << ',' << operations << ','
                          << repetition << ',' << seconds << ',' << operations / seconds << ','
                          << checksum << '\n';
            }
        }
    }
}

double percentile(const std::vector<double>& sorted, double fraction) {
    if (sorted.empty()) return 0;
    const double rank = std::ceil(fraction * static_cast<double>(sorted.size()));
    const std::size_t index = static_cast<std::size_t>(std::max(1.0, rank)) - 1;
    return sorted[std::min(index, sorted.size() - 1)];
}

NetworkResult run_network(const NetworkOptions& options) {
    const std::string value_text(options.value_size, 'v');
    const auto value = bytes(value_text);
    std::uint64_t request_id = 1;
    {
        auto client = forgekv::network::TcpClient::connect(options.host, options.port);
        for (std::size_t index = 0; index < options.key_count; ++index) {
            const auto key = bytes(key_for(index));
            const forgekv::protocol::Frame request{forgekv::protocol::FrameKind::kRequest,
                forgekv::protocol::Opcode::kPut, forgekv::protocol::Status::kOk,
                request_id++, key, value};
            const auto response = client.request(request);
            if (response.status != forgekv::protocol::Status::kOk) {
                throw std::runtime_error("dataset preload failed");
            }
        }
        for (std::size_t index = 0; index < options.warmup_requests; ++index) {
            const auto key = bytes(key_for(index % options.key_count));
            const forgekv::protocol::Frame request{forgekv::protocol::FrameKind::kRequest,
                forgekv::protocol::Opcode::kGet, forgekv::protocol::Status::kOk,
                request_id++, key, {}};
            static_cast<void>(client.request(request));
        }
    }

    std::vector<forgekv::network::TcpClient> clients;
    clients.reserve(options.connections);
    for (std::size_t index = 0; index < options.connections; ++index) {
        clients.push_back(forgekv::network::TcpClient::connect(options.host, options.port));
    }
    std::atomic_uint64_t next_operation = 0;
    std::atomic_uint64_t next_request_id = request_id;
    std::atomic_uint64_t errors = 0;
    std::atomic_uint64_t connection_errors = 0;
    std::vector<std::vector<double>> thread_latencies(options.threads);
    std::barrier start_line(static_cast<std::ptrdiff_t>(options.threads + 1));
    const auto deadline = options.duration == std::chrono::seconds::zero()
                              ? Clock::time_point::max()
                              : Clock::now() + options.duration;
    std::vector<std::jthread> workers;
    for (std::size_t thread_index = 0; thread_index < options.threads; ++thread_index) {
        workers.emplace_back([&, thread_index] {
            auto& latencies = thread_latencies[thread_index];
            std::size_t client_index = thread_index;
            start_line.arrive_and_wait();
            for (;;) {
                if (Clock::now() >= deadline) break;
                const std::uint64_t first = next_operation.fetch_add(options.pipeline_depth);
                if (options.requests != 0 && first >= options.requests) break;
                const std::uint64_t remaining = options.requests == 0
                    ? options.pipeline_depth : std::min<std::uint64_t>(options.pipeline_depth,
                                                                       options.requests - first);
                std::vector<forgekv::protocol::Frame> batch;
                batch.reserve(static_cast<std::size_t>(remaining));
                for (std::uint64_t offset = 0; offset < remaining; ++offset) {
                    const std::uint64_t operation = first + offset;
                    const std::uint64_t mixed = operation * 0x9e3779b97f4a7c15ULL + options.seed;
                    const bool read = mixed % 1'000'000 <
                        static_cast<std::uint64_t>(options.read_ratio * 1'000'000.0);
                    const auto key = bytes(key_for((mixed >> 20U) % options.key_count));
                    batch.push_back(forgekv::protocol::Frame{
                        forgekv::protocol::FrameKind::kRequest,
                        read ? forgekv::protocol::Opcode::kGet : forgekv::protocol::Opcode::kPut,
                        forgekv::protocol::Status::kOk, next_request_id.fetch_add(1), key,
                        read ? forgekv::protocol::Bytes{} : value});
                }
                try {
                    const auto batch_started = Clock::now();
                    const auto responses = clients[client_index].pipeline(batch);
                    const double elapsed_us = std::chrono::duration<double, std::micro>(
                                                  Clock::now() - batch_started).count();
                    for (const auto& response : responses) {
                        if (response.status != forgekv::protocol::Status::kOk) errors.fetch_add(1);
                        latencies.push_back(elapsed_us);
                    }
                } catch (...) {
                    connection_errors.fetch_add(1);
                    break;
                }
                client_index += options.threads;
                if (client_index >= clients.size()) client_index = thread_index;
            }
        });
    }
    const auto started = Clock::now();
    start_line.arrive_and_wait();
    workers.clear();
    const double seconds = std::chrono::duration<double>(Clock::now() - started).count();
    NetworkResult result;
    result.seconds = seconds;
    result.errors = errors.load();
    result.connection_errors = connection_errors.load();
    for (auto& values : thread_latencies) {
        result.raw_latency_us.insert(result.raw_latency_us.end(), values.begin(), values.end());
    }
    result.operations = result.raw_latency_us.size();
    result.operations_per_second = seconds == 0 ? 0 : static_cast<double>(result.operations) / seconds;
    std::sort(result.raw_latency_us.begin(), result.raw_latency_us.end());
    result.p50_us = percentile(result.raw_latency_us, 0.50);
    result.p95_us = percentile(result.raw_latency_us, 0.95);
    result.p99_us = percentile(result.raw_latency_us, 0.99);
    result.max_us = result.raw_latency_us.empty() ? 0 : result.raw_latency_us.back();
    return result;
}

std::string utc_timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm value{};
    gmtime_r(&now, &value);
    std::ostringstream stream;
    stream << std::put_time(&value, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

std::string system_description() {
    utsname info{};
    if (::uname(&info) != 0) return "unknown";
    return std::string(info.sysname) + " " + info.release + " " + info.machine;
}

std::string json_escape(std::string_view value) {
    std::string escaped;
    for (const char character : value) {
        switch (character) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped.push_back(character); break;
        }
    }
    return escaped;
}

std::string csv_escape(std::string_view value) {
    if (value.find_first_of(",\"\r\n") == std::string_view::npos) return std::string(value);
    std::string escaped{"\""};
    for (const char character : value) {
        if (character == '"') escaped.push_back('"');
        escaped.push_back(character);
    }
    escaped.push_back('"');
    return escaped;
}

void write_outputs(const NetworkOptions& options, const NetworkResult& result) {
    std::cout << "operations  ops/s       p50-us    p95-us    p99-us    max-us    errors  conn-errors\n"
              << std::setw(10) << result.operations << "  " << std::setw(10) << std::fixed
              << std::setprecision(1) << result.operations_per_second << "  " << std::setw(9)
              << result.p50_us << "  " << std::setw(9) << result.p95_us << "  " << std::setw(9)
              << result.p99_us << "  " << std::setw(9) << result.max_us << "  " << std::setw(6)
              << result.errors << "  " << result.connection_errors << '\n';
    if (options.output_prefix.empty()) return;

    const std::string timestamp = utc_timestamp();
    const std::string system = system_description();
    std::ofstream json(options.output_prefix + ".json");
    std::ofstream csv(options.output_prefix + ".csv");
    std::ofstream raw(options.output_prefix + "-latency-us.csv");
    if (!json || !csv || !raw) throw std::runtime_error("failed to create benchmark output files");
    json << "{\n"
         << "  \"timestamp_utc\": \"" << json_escape(timestamp) << "\",\n"
         << "  \"git_sha\": \"" << json_escape(FORGEKV_GIT_SHA) << "\",\n"
         << "  \"working_tree_dirty\": " << (FORGEKV_GIT_DIRTY != 0 ? "true" : "false") << ",\n"
         << "  \"compiler\": \"" << json_escape(FORGEKV_COMPILER_DESCRIPTION) << "\",\n"
         << "  \"build\": \"" << json_escape(FORGEKV_BUILD_DESCRIPTION) << "\",\n"
         << "  \"system\": \"" << json_escape(system) << "\",\n"
         << "  \"hardware_threads\": " << std::thread::hardware_concurrency() << ",\n"
         << "  \"ram\": \"" << json_escape(options.ram_description) << "\",\n"
         << "  \"storage_medium\": \"" << json_escape(options.storage_medium) << "\",\n"
         << "  \"host\": \"" << json_escape(options.host) << "\", \"port\": " << options.port << ",\n"
         << "  \"connections\": " << options.connections << ", \"threads\": " << options.threads << ",\n"
         << "  \"request_bound\": " << options.requests << ", \"duration_bound_s\": " << options.duration.count() << ",\n"
         << "  \"read_ratio\": " << options.read_ratio << ", \"key_count\": " << options.key_count << ",\n"
         << "  \"value_size\": " << options.value_size << ", \"pipeline_depth\": " << options.pipeline_depth << ",\n"
         << "  \"warmup_requests\": " << options.warmup_requests << ", \"seed\": " << options.seed << ",\n"
         << "  \"server_workers\": " << options.server_workers << ", \"server_shards\": " << options.server_shards << ",\n"
         << "  \"durability\": \"" << json_escape(options.durability) << "\", \"repetition\": " << options.repetition << ",\n"
         << "  \"operations\": " << result.operations << ", \"seconds\": " << result.seconds << ",\n"
         << "  \"operations_per_second\": " << result.operations_per_second << ",\n"
         << "  \"latency_us\": {\"p50\": " << result.p50_us << ", \"p95\": " << result.p95_us
         << ", \"p99\": " << result.p99_us << ", \"max\": " << result.max_us << "},\n"
         << "  \"errors\": " << result.errors << ", \"connection_errors\": " << result.connection_errors << "\n}\n";
    csv << "timestamp_utc,git_sha,working_tree_dirty,compiler,system,connections,threads,requests,duration_s,read_ratio,key_count,value_size,pipeline_depth,warmup,seed,workers,shards,durability,repetition,operations,seconds,ops_per_second,p50_us,p95_us,p99_us,max_us,errors,connection_errors\n"
        << csv_escape(timestamp) << ',' << csv_escape(FORGEKV_GIT_SHA) << ',' << FORGEKV_GIT_DIRTY << ','
        << csv_escape(FORGEKV_COMPILER_DESCRIPTION) << ',' << csv_escape(system) << ','
        << options.connections << ',' << options.threads << ',' << options.requests << ',' << options.duration.count() << ','
        << options.read_ratio << ',' << options.key_count << ',' << options.value_size << ',' << options.pipeline_depth << ','
        << options.warmup_requests << ',' << options.seed << ',' << options.server_workers << ',' << options.server_shards << ','
        << csv_escape(options.durability) << ',' << options.repetition << ',' << result.operations << ',' << result.seconds << ','
        << result.operations_per_second << ',' << result.p50_us << ',' << result.p95_us << ',' << result.p99_us << ','
        << result.max_us << ',' << result.errors << ',' << result.connection_errors << '\n';
    raw << "latency_us\n";
    for (const double value_us : result.raw_latency_us) raw << value_us << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            print_usage();
            return 2;
        }
        const std::string_view mode = argv[1];
        if (mode == "contention") run_contention(parse_contention_options(argc, argv));
        else if (mode == "network") {
            const NetworkOptions options = parse_network_options(argc, argv);
            write_outputs(options, run_network(options));
        } else {
            throw std::invalid_argument("expected network or contention benchmark");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "forgekv-bench: " << error.what() << '\n';
        return 2;
    }
}
