#include "forgekv/index/sharded_index.hpp"
#include <mutex>
#include <stdexcept>
namespace forgekv::index {
ShardedIndex::ShardedIndex(std::size_t shard_count) {
    if (shard_count == 0) throw std::invalid_argument("shard count must be positive");
    shards_.reserve(shard_count);
    for (std::size_t index = 0; index < shard_count; ++index) shards_.push_back(std::make_unique<Shard>());
}
std::size_t ShardedIndex::shard_for(std::string_view key) const noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char byte : key) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ULL;
    }
    return static_cast<std::size_t>(hash % shards_.size());
}
std::optional<storage::RecordLocation> ShardedIndex::find(std::string_view key) const {
    const Shard& shard = *shards_[shard_for(key)];
    std::shared_lock lock(shard.mutex);
    const auto iterator = shard.entries.find(std::string(key));
    if (iterator == shard.entries.end()) return std::nullopt;
    return iterator->second;
}
void ShardedIndex::insert_or_assign(std::string key, storage::RecordLocation location) {
    Shard& shard = *shards_[shard_for(key)];
    std::unique_lock lock(shard.mutex);
    auto [iterator, inserted] = shard.entries.insert_or_assign(std::move(key), location);
    static_cast<void>(iterator);
    if (inserted) size_.fetch_add(1);
}
bool ShardedIndex::erase(std::string_view key) {
    Shard& shard = *shards_[shard_for(key)];
    std::unique_lock lock(shard.mutex);
    const bool erased = shard.entries.erase(std::string(key)) != 0;
    if (erased) size_.fetch_sub(1);
    return erased;
}
bool ShardedIndex::erase_if_sequence(std::string_view key, std::uint64_t sequence) {
    Shard& shard = *shards_[shard_for(key)];
    std::unique_lock lock(shard.mutex);
    const auto iterator = shard.entries.find(std::string(key));
    if (iterator == shard.entries.end() || iterator->second.sequence != sequence) return false;
    shard.entries.erase(iterator);
    size_.fetch_sub(1);
    return true;
}
bool ShardedIndex::replace_if_sequence(std::string_view key, std::uint64_t sequence,
                                       storage::RecordLocation location) {
    Shard& shard = *shards_[shard_for(key)];
    std::unique_lock lock(shard.mutex);
    const auto iterator = shard.entries.find(std::string(key));
    if (iterator == shard.entries.end() || iterator->second.sequence != sequence) return false;
    iterator->second = location;
    return true;
}
bool ShardedIndex::contains(std::string_view key) const { return find(key).has_value(); }
std::vector<std::pair<std::string, storage::RecordLocation>> ShardedIndex::snapshot() const {
    std::vector<std::pair<std::string, storage::RecordLocation>> result;
    result.reserve(size());
    for (const auto& shard_pointer : shards_) {
        const Shard& shard = *shard_pointer;
        std::shared_lock lock(shard.mutex);
        result.insert(result.end(), shard.entries.begin(), shard.entries.end());
    }
    return result;
}
std::size_t ShardedIndex::size() const noexcept { return size_.load(); }
std::size_t ShardedIndex::shard_count() const noexcept { return shards_.size(); }
}  // namespace forgekv::index
