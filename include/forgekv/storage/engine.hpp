#pragma once

#include "forgekv/index/sharded_index.hpp"
#include "forgekv/storage/location.hpp"
#include "forgekv/storage/record.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace forgekv::storage {

inline constexpr std::uint64_t kInitialSegmentId = 1;

enum class DurabilityMode { kAlways, kPeriodic, kNone };

struct StorageOptions {
    std::size_t shard_count = 16;
    DurabilityMode durability = DurabilityMode::kPeriodic;
    std::chrono::milliseconds sync_interval{1000};
    std::uint64_t segment_max_bytes = 64U * 1024U * 1024U;
    std::size_t compaction_min_segments = 4;
    bool background_compaction = true;
};

struct CompactionStats {
    bool ran = false;
    std::size_t input_segments = 0;
    std::uint64_t bytes_before = 0;
    std::uint64_t bytes_after = 0;
    std::uint64_t bytes_written = 0;
    std::chrono::nanoseconds duration{};
};

struct EraseResult {
    std::uint64_t sequence;
    bool existed;
};

enum class TtlState { kNotFound, kPersistent, kExpiring };

struct TtlResult {
    TtlState state;
    std::uint64_t remaining_ms = 0;
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
                                            StorageOptions options = {});
    [[nodiscard]] static StorageEngine open(std::filesystem::path database_directory,
                                            std::size_t shard_count);

    StorageEngine(const StorageEngine&) = delete;
    StorageEngine& operator=(const StorageEngine&) = delete;
    StorageEngine(StorageEngine&&) = delete;
    StorageEngine& operator=(StorageEngine&&) = delete;
    ~StorageEngine();

    [[nodiscard]] std::uint64_t put(std::span<const std::byte> key,
                                    std::span<const std::byte> value);
    [[nodiscard]] std::uint64_t put_ex(std::span<const std::byte> key,
                                       std::span<const std::byte> value,
                                       std::chrono::milliseconds ttl);
    [[nodiscard]] std::optional<Bytes> get(std::span<const std::byte> key) const;
    [[nodiscard]] TtlResult ttl(std::span<const std::byte> key) const;
    [[nodiscard]] EraseResult erase(std::span<const std::byte> key);
    [[nodiscard]] CompactionStats compact();

    void close();

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::uint64_t last_sequence() const noexcept;
    [[nodiscard]] std::uint64_t last_synced_sequence() const noexcept;
    [[nodiscard]] DurabilityMode durability_mode() const noexcept;
    [[nodiscard]] std::size_t segment_count() const;
    [[nodiscard]] CompactionStats last_compaction() const;
    [[nodiscard]] std::uint64_t bytes_appended() const noexcept;
    [[nodiscard]] std::uint64_t compaction_count() const noexcept;
    [[nodiscard]] const std::filesystem::path& database_directory() const noexcept;

    [[nodiscard]] static std::filesystem::path segment_path_for_id(
        const std::filesystem::path& database_directory, std::uint64_t segment_id);

private:
    struct ExpirationEntry {
        std::uint64_t expires_at_unix_ms;
        std::uint64_t sequence;
        std::string key;
    };

    struct ExpirationLater {
        bool operator()(const ExpirationEntry& left, const ExpirationEntry& right) const noexcept {
            return left.expires_at_unix_ms > right.expires_at_unix_ms;
        }
    };

    StorageEngine(std::filesystem::path database_directory, StorageOptions options);

    void initialize();
    void start_maintenance_threads();
    void recover_compaction_artifacts();
    [[nodiscard]] std::vector<std::uint64_t> discover_segment_ids() const;
    void replay_segments();
    void replay_segment(std::uint64_t segment_id, bool active);
    void open_writer();
    void rotate_segment_locked(std::size_t incoming_size);
    void close_noexcept() noexcept;
    void ensure_open() const;
    void ensure_sequence_available() const;
    void append_encoded(std::span<const std::byte> encoded);
    void synchronize_locked();
    void synchronize_directory_locked();
    void run_periodic_sync(std::stop_token stop_token);
    void run_expiration(std::stop_token stop_token);
    void run_compaction(std::stop_token stop_token);
    [[nodiscard]] CompactionStats compact_internal(bool require_threshold);
    void schedule_expiration(std::string key, std::uint64_t sequence,
                             std::uint64_t expires_at_unix_ms);
    [[nodiscard]] std::uint64_t put_internal(std::span<const std::byte> key,
                                             std::span<const std::byte> value,
                                             std::optional<std::chrono::milliseconds> ttl);
    void apply_recovered_record(const RecordHeader& header, const Record& record,
                                std::uint64_t record_offset, std::uint64_t segment_id);

    [[nodiscard]] static std::uint64_t now_unix_ms() noexcept;
    [[nodiscard]] static std::uint64_t expiration_from_ttl(std::chrono::milliseconds ttl);
    [[nodiscard]] static std::string key_string(std::span<const std::byte> key);
    static void validate_lookup_key(std::span<const std::byte> key);

    StorageOptions options_;
    std::filesystem::path database_directory_;
    std::filesystem::path active_segment_path_;
    mutable index::ShardedIndex index_;
    mutable std::mutex write_mutex_;
    mutable std::shared_mutex segment_set_mutex_;
    std::mutex compaction_mutex_;
    std::mutex compaction_wait_mutex_;
    std::condition_variable compaction_cv_;
    mutable std::mutex compaction_stats_mutex_;
    std::mutex sync_wait_mutex_;
    std::condition_variable sync_wait_cv_;
    mutable std::mutex expiration_mutex_;
    std::condition_variable expiration_cv_;
    std::priority_queue<ExpirationEntry, std::vector<ExpirationEntry>, ExpirationLater>
        expirations_;
    std::jthread sync_thread_;
    std::jthread expiration_thread_;
    std::jthread compaction_thread_;
    int writer_fd_ = -1;
    std::uint64_t active_segment_id_ = kInitialSegmentId;
    std::uint64_t segment_size_ = 0;
    std::atomic_uint64_t last_sequence_ = 0;
    std::atomic_uint64_t last_synced_sequence_ = 0;
    std::atomic_uint64_t bytes_appended_ = 0;
    std::atomic_uint64_t compaction_count_ = 0;
    std::atomic_bool open_ = false;
    std::atomic_bool write_failed_ = false;
    bool dirty_ = false;
    bool directory_sync_pending_ = false;
    bool parent_directory_sync_pending_ = false;
    CompactionStats last_compaction_;
};

}  // namespace forgekv::storage
