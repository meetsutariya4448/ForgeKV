#pragma once
#include "forgekv/storage/location.hpp"
#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
namespace forgekv::index {
class ShardedIndex {
public:
    explicit ShardedIndex(std::size_t shard_count);
    [[nodiscard]] std::optional<storage::RecordLocation> find(std::string_view key) const;
    void insert_or_assign(std::string key, storage::RecordLocation location);
    [[nodiscard]] bool erase(std::string_view key);
    [[nodiscard]] bool erase_if_sequence(std::string_view key, std::uint64_t sequence);
    [[nodiscard]] bool replace_if_sequence(std::string_view key, std::uint64_t sequence,
                                           storage::RecordLocation location);
    [[nodiscard]] bool contains(std::string_view key) const;
    [[nodiscard]] std::vector<std::pair<std::string, storage::RecordLocation>> snapshot() const;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t shard_count() const noexcept;
    [[nodiscard]] std::size_t shard_for(std::string_view key) const noexcept;
private:
    struct Shard {
        mutable std::shared_mutex mutex;
        std::unordered_map<std::string, storage::RecordLocation> entries;
    };
    std::vector<std::unique_ptr<Shard>> shards_;
    std::atomic_size_t size_ = 0;
};
}  // namespace forgekv::index
