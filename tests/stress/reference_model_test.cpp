#include "forgekv/storage/engine.hpp"

#include <unistd.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace forgekv::storage {
namespace {

class StressDirectory {
public:
    StressDirectory() {
        static std::atomic_uint64_t counter = 0;
        path_ = std::filesystem::temp_directory_path() /
                ("forgekv-model-stress-" +
                 std::to_string(static_cast<long long>(::getpid())) + "-" +
                 std::to_string(++counter));
        std::filesystem::remove_all(path_);
    }
    ~StressDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

Bytes stress_bytes(std::string_view text) {
    const auto* begin = reinterpret_cast<const std::byte*>(text.data());
    return Bytes(begin, begin + text.size());
}

std::string stress_key(std::uint64_t number) { return "key-" + std::to_string(number); }

void assert_matches(StorageEngine& engine,
                    const std::unordered_map<std::string, Bytes>& reference,
                    std::size_t key_count) {
    EXPECT_EQ(engine.size(), reference.size());
    for (std::size_t index = 0; index < key_count; ++index) {
        const std::string key = stress_key(index);
        const auto expected = reference.find(key);
        if (expected == reference.end()) {
            EXPECT_FALSE(engine.get(stress_bytes(key)).has_value()) << key;
        } else {
            EXPECT_EQ(engine.get(stress_bytes(key)), std::optional<Bytes>(expected->second)) << key;
        }
    }
}

TEST(ReferenceModelStressTest, SeededSingleThreadOperationsMatchModel) {
    constexpr std::uint64_t seed = 0x5eed'0001ULL;
    SCOPED_TRACE("seed=" + std::to_string(seed));
    StressDirectory temporary;
    StorageEngine engine = StorageEngine::open(temporary.path());
    std::unordered_map<std::string, Bytes> reference;
    std::mt19937_64 random(seed);
    for (int operation = 0; operation < 2'000; ++operation) {
        const std::string key = stress_key(random() % 64);
        const int kind = static_cast<int>(random() % 4);
        if (kind <= 1) {
            const Bytes value = stress_bytes("value-" + std::to_string(random()));
            static_cast<void>(engine.put(stress_bytes(key), value));
            reference[key] = value;
        } else if (kind == 2) {
            const bool expected = reference.erase(key) != 0;
            EXPECT_EQ(engine.erase(stress_bytes(key)).existed, expected);
        } else {
            const auto expected = reference.find(key);
            EXPECT_EQ(engine.get(stress_bytes(key)).has_value(), expected != reference.end());
        }
    }
    assert_matches(engine, reference, 64);
}

TEST(ReferenceModelStressTest, SeededBatchesMatchAcrossEveryRestart) {
    constexpr std::uint64_t seed = 0x5eed'0002ULL;
    SCOPED_TRACE("seed=" + std::to_string(seed));
    StressDirectory temporary;
    std::unordered_map<std::string, Bytes> reference;
    std::mt19937_64 random(seed);
    for (int batch = 0; batch < 10; ++batch) {
        {
            StorageEngine engine = StorageEngine::open(temporary.path());
            for (int operation = 0; operation < 100; ++operation) {
                const std::string key = stress_key(random() % 40);
                if (random() % 3 == 0) {
                    static_cast<void>(engine.erase(stress_bytes(key)));
                    reference.erase(key);
                } else {
                    const Bytes value = stress_bytes("batch-" + std::to_string(batch) + "-" +
                                                     std::to_string(random()));
                    static_cast<void>(engine.put(stress_bytes(key), value));
                    reference[key] = value;
                }
            }
        }
        StorageEngine recovered = StorageEngine::open(temporary.path());
        assert_matches(recovered, reference, 40);
    }
}

TEST(ReferenceModelStressTest, ConcurrentMutationsMatchSequenceOrderedModel) {
    constexpr std::uint64_t seed = 0x5eed'0003ULL;
    SCOPED_TRACE("seed=" + std::to_string(seed));
    struct Event {
        std::uint64_t sequence;
        std::string key;
        std::optional<Bytes> value;
    };
    StressDirectory temporary;
    StorageEngine engine = StorageEngine::open(temporary.path());
    std::mutex event_mutex;
    std::vector<Event> events;
    std::vector<std::jthread> workers;
    for (std::uint64_t thread_id = 0; thread_id < 8; ++thread_id) {
        workers.emplace_back([&, thread_id] {
            std::mt19937_64 random(seed + thread_id);
            for (int operation = 0; operation < 250; ++operation) {
                const std::string key = stress_key(random() % 80);
                Event event;
                event.key = key;
                if (random() % 4 == 0) {
                    event.sequence = engine.erase(stress_bytes(key)).sequence;
                } else {
                    event.value = stress_bytes("thread-" + std::to_string(thread_id) + "-" +
                                               std::to_string(random()));
                    event.sequence = engine.put(stress_bytes(key), *event.value);
                }
                std::lock_guard lock(event_mutex);
                events.push_back(std::move(event));
            }
        });
    }
    workers.clear();
    std::sort(events.begin(), events.end(), [](const Event& left, const Event& right) {
        return left.sequence < right.sequence;
    });
    std::unordered_map<std::string, Bytes> reference;
    for (const auto& event : events) {
        if (event.value) reference[event.key] = *event.value;
        else reference.erase(event.key);
    }
    assert_matches(engine, reference, 80);
    engine.close();
    StorageEngine recovered = StorageEngine::open(temporary.path());
    assert_matches(recovered, reference, 80);
}

}  // namespace
}  // namespace forgekv::storage
