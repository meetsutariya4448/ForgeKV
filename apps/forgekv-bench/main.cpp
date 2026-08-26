#include "forgekv/index/sharded_index.hpp"
#include "forgekv/storage/location.hpp"

#include <atomic>
#include <barrier>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct Options {
    std::size_t threads = 4;
    std::size_t operations_per_thread = 100'000;
    std::size_t key_count = 4'096;
    std::size_t repetitions = 3;
    std::vector<std::size_t> shard_counts{1, 4, 16, 64, 256};
};

std::size_t parse_positive_size(std::string_view text, std::string_view name) {
    std::size_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0) {
        throw std::invalid_argument("invalid " + std::string(name) + ": " + std::string(text));
    }
    return value;
}

std::vector<std::size_t> parse_shards(std::string_view text) {
    std::vector<std::size_t> shards;
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        const std::string_view item = text.substr(0, comma);
        shards.push_back(parse_positive_size(item, "shard count"));
        if (comma == std::string_view::npos) break;
        text.remove_prefix(comma + 1);
    }
    if (shards.empty()) throw std::invalid_argument("shard list must not be empty");
    return shards;
}

void print_usage() {
    std::cerr << "usage: forgekv-bench contention [--threads COUNT] "
                 "[--operations-per-thread COUNT] [--keys COUNT] "
                 "[--repetitions COUNT] [--shards 1,4,16,64,256]\n";
}

Options parse_options(int argc, char** argv) {
    if (argc < 2 || std::string_view(argv[1]) != "contention") {
        print_usage();
        throw std::invalid_argument("expected contention benchmark");
    }
    Options options;
    for (int index = 2; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--threads" && index + 1 < argc) {
            options.threads = parse_positive_size(argv[++index], "thread count");
        } else if (argument == "--operations-per-thread" && index + 1 < argc) {
            options.operations_per_thread =
                parse_positive_size(argv[++index], "operation count");
        } else if (argument == "--keys" && index + 1 < argc) {
            options.key_count = parse_positive_size(argv[++index], "key count");
        } else if (argument == "--repetitions" && index + 1 < argc) {
            options.repetitions = parse_positive_size(argv[++index], "repetition count");
        } else if (argument == "--shards" && index + 1 < argc) {
            options.shard_counts = parse_shards(argv[++index]);
        } else {
            print_usage();
            throw std::invalid_argument("unknown or incomplete benchmark option");
        }
    }
    return options;
}

forgekv::storage::RecordLocation location(std::uint64_t sequence) {
    return forgekv::storage::RecordLocation{1, sequence * 64, sequence * 64 + 32, 8, 16,
                                            sequence, 0};
}

std::vector<std::string> make_keys(std::size_t count) {
    std::vector<std::string> keys;
    keys.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        keys.push_back("contention-key-" + std::to_string(index));
    }
    return keys;
}

double run_once(std::size_t shard_count, const Options& options,
                const std::vector<std::string>& keys, bool same_key,
                std::uint64_t& checksum) {
    forgekv::index::ShardedIndex index(shard_count);
    for (std::size_t key_index = 0; key_index < keys.size(); ++key_index) {
        index.insert_or_assign(keys[key_index], location(key_index + 1));
    }

    std::barrier start_line(static_cast<std::ptrdiff_t>(options.threads + 1));
    std::vector<std::jthread> workers;
    workers.reserve(options.threads);
    std::atomic_uint64_t combined_checksum = 0;
    for (std::size_t thread_index = 0; thread_index < options.threads; ++thread_index) {
        workers.emplace_back([&, thread_index] {
            std::uint64_t local_checksum = 0;
            start_line.arrive_and_wait();
            for (std::size_t operation = 0; operation < options.operations_per_thread;
                 ++operation) {
                const std::size_t key_index = same_key
                                                  ? 0
                                                  : (thread_index * 1'009 + operation * 17) %
                                                        keys.size();
                if (operation % 10 == 0) {
                    index.insert_or_assign(
                        keys[key_index],
                        location(static_cast<std::uint64_t>(operation + thread_index + 1)));
                } else {
                    const auto found = index.find(keys[key_index]);
                    if (found) local_checksum += found->sequence;
                }
            }
            combined_checksum.fetch_add(local_checksum);
        });
    }

    const auto started = std::chrono::steady_clock::now();
    start_line.arrive_and_wait();
    workers.clear();
    const auto finished = std::chrono::steady_clock::now();
    checksum = combined_checksum.load();
    return std::chrono::duration<double>(finished - started).count();
}

void run_contention(const Options& options) {
    const auto keys = make_keys(options.key_count);
    std::cout << "workload,shards,threads,keys,operations,repetition,seconds,ops_per_second,checksum\n";
    for (const std::size_t shard_count : options.shard_counts) {
        for (const bool same_key : {true, false}) {
            std::uint64_t ignored_checksum = 0;
            Options warmup = options;
            warmup.operations_per_thread = 1'000;
            static_cast<void>(run_once(shard_count, warmup, keys, same_key,
                                       ignored_checksum));
            for (std::size_t repetition = 1; repetition <= options.repetitions; ++repetition) {
                std::uint64_t checksum = 0;
                const double seconds =
                    run_once(shard_count, options, keys, same_key, checksum);
                const std::size_t total_operations =
                    options.threads * options.operations_per_thread;
                const double throughput = static_cast<double>(total_operations) / seconds;
                std::cout << (same_key ? "same-key" : "distributed-key") << ','
                          << shard_count << ',' << options.threads << ',' << options.key_count
                          << ',' << total_operations << ',' << repetition << ',' << std::fixed
                          << std::setprecision(6) << seconds << ',' << std::setprecision(2)
                          << throughput << ',' << checksum << '\n';
            }
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        run_contention(parse_options(argc, argv));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "forgekv-bench: " << error.what() << '\n';
        return 2;
    }
}
