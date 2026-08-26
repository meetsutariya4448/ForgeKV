#pragma once

#include "forgekv/storage/record.hpp"
#include "forgekv/index/sharded_index.hpp"
#include "forgekv/storage/location.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <atomic>
#include <mutex>

namespace forgekv::storage {

inline constexpr std::uint64_t kInitialSegmentId = 1;

struct EraseResult {
    std::uint64_t sequence;
    bool existed;
};

class StorageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class CorruptionError : public StorageError {
public:
    using StorageError::StorageError;
};

class StorageEngine {
public:
    [[nodiscard]] static StorageEngine open(std::filesystem::path database_directory,
                                            std::size_t shard_count = 16);

    StorageEngine(const StorageEngine&) = delete;
    StorageEngine& operator=(const StorageEngine&) = delete;
    StorageEngine(StorageEngine&&) = delete;
    StorageEngine& operator=(StorageEngine&&) = delete;
    ~StorageEngine();

    [[nodiscard]] std::uint64_t put(std::span<const std::byte> key,
                                    std::span<const std::byte> value);
    [[nodiscard]] std::optional<Bytes> get(std::span<const std::byte> key) const;
    [[nodiscard]] EraseResult erase(std::span<const std::byte> key);

    void close();

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::uint64_t last_sequence() const noexcept;
    [[nodiscard]] const std::filesystem::path& database_directory() const noexcept;

    [[nodiscard]] static std::filesystem::path segment_path_for_id(
        const std::filesystem::path& database_directory, std::uint64_t segment_id);

private:
    StorageEngine(std::filesystem::path database_directory, std::size_t shard_count);

    void initialize();
    void replay_segment();
    void open_writer();
    void close_noexcept() noexcept;
    void ensure_open() const;
    void ensure_sequence_available() const;
    void append_encoded(std::span<const std::byte> encoded);
    void apply_recovered_record(const RecordHeader& header, const Record& record,
                                std::uint64_t record_offset);

    [[nodiscard]] static std::string key_string(std::span<const std::byte> key);
    static void validate_lookup_key(std::span<const std::byte> key);

    std::filesystem::path database_directory_;
    std::filesystem::path active_segment_path_;
    std::ofstream writer_;
    index::ShardedIndex index_;
    mutable std::mutex write_mutex_;
    std::uint64_t segment_size_ = 0;
    std::atomic_uint64_t last_sequence_ = 0;
    std::atomic_bool open_ = false;
    std::atomic_bool write_failed_ = false;
};

}  // namespace forgekv::storage
