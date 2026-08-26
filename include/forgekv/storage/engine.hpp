#pragma once

#include "forgekv/storage/record.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace forgekv::storage {

inline constexpr std::uint64_t kInitialSegmentId = 1;

struct RecordLocation {
    std::uint64_t segment_id;
    std::uint64_t record_offset;
    std::uint64_t value_offset;
    std::uint32_t key_length;
    std::uint32_t value_length;
    std::uint64_t sequence;
    std::uint32_t payload_checksum;
};

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
    [[nodiscard]] static StorageEngine open(std::filesystem::path database_directory);

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
    explicit StorageEngine(std::filesystem::path database_directory);

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
    std::unordered_map<std::string, RecordLocation> index_;
    std::uint64_t segment_size_ = 0;
    std::uint64_t last_sequence_ = 0;
    bool open_ = false;
    bool write_failed_ = false;
};

}  // namespace forgekv::storage
