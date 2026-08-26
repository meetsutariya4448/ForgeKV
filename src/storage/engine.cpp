#include "forgekv/storage/engine.hpp"

#include "forgekv/storage/crc32c.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>

namespace forgekv::storage {
namespace {

[[nodiscard]] std::string corruption_message(std::uint64_t offset, const std::string& detail) {
    return "corrupt segment record at offset " + std::to_string(offset) + ": " + detail;
}

void read_exact(std::ifstream& stream, std::span<std::byte> destination,
                std::uint64_t record_offset) {
    if (destination.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        throw StorageError("read size exceeds stream limits");
    }
    stream.read(reinterpret_cast<char*>(destination.data()),
                static_cast<std::streamsize>(destination.size()));
    if (stream.gcount() != static_cast<std::streamsize>(destination.size())) {
        throw StorageError("short segment read at offset " + std::to_string(record_offset));
    }
}

}  // namespace

StorageEngine StorageEngine::open(std::filesystem::path database_directory, std::size_t shard_count) {
    return StorageEngine(std::move(database_directory), shard_count);
}

StorageEngine::StorageEngine(std::filesystem::path database_directory, std::size_t shard_count)
    : database_directory_(std::move(database_directory)),
      active_segment_path_(segment_path_for_id(database_directory_, kInitialSegmentId)),
      index_(shard_count) {
    initialize();
}

StorageEngine::~StorageEngine() { close_noexcept(); }

std::uint64_t StorageEngine::put(std::span<const std::byte> key,
                                 std::span<const std::byte> value) {
    ensure_open();
    std::lock_guard write_lock(write_mutex_);
    ensure_open();
    ensure_sequence_available();
    const std::uint64_t sequence = last_sequence_.load() + 1;
    const Bytes encoded = encode_record(Operation::kPut, sequence, key, value);
    const std::uint64_t record_offset = segment_size_;
    const RecordHeader header = decode_record_header(encoded);
    std::string owned_key = key_string(key);

    append_encoded(encoded);
    last_sequence_.store(sequence);
    const std::uint64_t value_offset = record_offset + kRecordHeaderSize + header.key_length;
    try {
        index_.insert_or_assign(std::move(owned_key),
                                RecordLocation{kInitialSegmentId,
                                               record_offset,
                                               value_offset,
                                               header.key_length,
                                               header.value_length,
                                               sequence,
                                               header.payload_checksum});
    } catch (...) {
        // The log is authoritative. Prevent sequence reuse and require reopen/replay if publication
        // fails after the record has been appended.
        write_failed_ = true;
        throw;
    }
    return sequence;
}

std::optional<Bytes> StorageEngine::get(std::span<const std::byte> key) const {
    ensure_open();
    validate_lookup_key(key);
    const auto location_result = index_.find(key_string(key));
    if (!location_result) return std::nullopt;
    const RecordLocation& location = *location_result;
    if (location.segment_id != kInitialSegmentId || location.value_offset < location.key_length ||
        location.record_offset > std::numeric_limits<std::uint64_t>::max() - kRecordHeaderSize ||
        location.record_offset + kRecordHeaderSize != location.value_offset - location.key_length) {
        throw CorruptionError("in-memory record location has inconsistent offsets");
    }
    const std::uint64_t payload_offset = location.value_offset - location.key_length;
    const std::uint64_t payload_size_u64 =
        static_cast<std::uint64_t>(location.key_length) + location.value_length;
    if (payload_size_u64 > kMaxKeySize + kMaxValueSize ||
        payload_size_u64 > std::numeric_limits<std::size_t>::max()) {
        throw CorruptionError("in-memory record location has an invalid payload size");
    }
    if (payload_offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        throw StorageError("record offset exceeds stream limits");
    }

    std::ifstream reader(active_segment_path_, std::ios::binary);
    if (!reader) {
        throw StorageError("failed to open segment for reading: " + active_segment_path_.string());
    }
    reader.seekg(static_cast<std::streamoff>(payload_offset));
    if (!reader) {
        throw StorageError("failed to seek to record payload");
    }

    Bytes payload(static_cast<std::size_t>(payload_size_u64));
    read_exact(reader, payload, payload_offset);
    if (crc32c(payload) != location.payload_checksum) {
        throw CorruptionError(corruption_message(location.record_offset,
                                                  "payload checksum changed after recovery"));
    }

    const auto stored_key = std::span<const std::byte>(payload).first(location.key_length);
    if (!std::equal(stored_key.begin(), stored_key.end(), key.begin(), key.end())) {
        throw CorruptionError(corruption_message(location.record_offset,
                                                  "stored key does not match index entry"));
    }
    return Bytes(payload.begin() + location.key_length, payload.end());
}

EraseResult StorageEngine::erase(std::span<const std::byte> key) {
    ensure_open();
    validate_lookup_key(key);
    std::lock_guard write_lock(write_mutex_);
    ensure_open();
    ensure_sequence_available();
    const std::uint64_t sequence = last_sequence_.load() + 1;
    const Bytes encoded = encode_record(Operation::kDelete, sequence, key, {});
    const std::string owned_key = key_string(key);
    const bool existed = index_.contains(owned_key);

    append_encoded(encoded);
    last_sequence_.store(sequence);
    static_cast<void>(index_.erase(owned_key));
    return EraseResult{sequence, existed};
}

void StorageEngine::close() {
    std::lock_guard write_lock(write_mutex_);
    if (!open_.load()) {
        return;
    }
    writer_.flush();
    if (!writer_) {
        write_failed_.store(true);
        throw StorageError("failed to flush active segment during close");
    }
    writer_.close();
    if (writer_.fail()) {
        write_failed_.store(true);
        throw StorageError("failed to close active segment");
    }
    open_.store(false);
}

std::size_t StorageEngine::size() const noexcept { return index_.size(); }

std::uint64_t StorageEngine::last_sequence() const noexcept { return last_sequence_.load(); }

const std::filesystem::path& StorageEngine::database_directory() const noexcept {
    return database_directory_;
}

std::filesystem::path StorageEngine::segment_path_for_id(
    const std::filesystem::path& database_directory, std::uint64_t segment_id) {
    std::ostringstream filename;
    filename << "segment-" << std::setw(20) << std::setfill('0') << segment_id << ".fkv";
    return database_directory / filename.str();
}

void StorageEngine::initialize() {
    std::error_code error;
    const bool exists = std::filesystem::exists(database_directory_, error);
    if (error) {
        throw StorageError("failed to inspect database directory: " + error.message());
    }
    if (exists && !std::filesystem::is_directory(database_directory_, error)) {
        throw StorageError("database path is not a directory: " + database_directory_.string());
    }
    if (error) {
        throw StorageError("failed to inspect database path type: " + error.message());
    }
    if (!exists && !std::filesystem::create_directories(database_directory_, error) && error) {
        throw StorageError("failed to create database directory: " + error.message());
    }

    if (!std::filesystem::exists(active_segment_path_, error)) {
        std::ofstream creator(active_segment_path_, std::ios::binary);
        if (!creator) {
            throw StorageError("failed to create active segment: " + active_segment_path_.string());
        }
    } else if (!std::filesystem::is_regular_file(active_segment_path_, error)) {
        throw StorageError("active segment path is not a regular file");
    }
    if (error) {
        throw StorageError("failed to inspect active segment: " + error.message());
    }

    replay_segment();
    open_writer();
    open_.store(true);
}

void StorageEngine::replay_segment() {
    std::error_code error;
    const std::uintmax_t file_size_raw = std::filesystem::file_size(active_segment_path_, error);
    if (error) {
        throw StorageError("failed to read active segment size: " + error.message());
    }
    if (file_size_raw > std::numeric_limits<std::uint64_t>::max()) {
        throw StorageError("active segment is too large to address");
    }
    const auto file_size = static_cast<std::uint64_t>(file_size_raw);

    std::ifstream reader(active_segment_path_, std::ios::binary);
    if (!reader) {
        throw StorageError("failed to open active segment for recovery");
    }

    std::uint64_t offset = 0;
    bool truncated_tail = false;
    while (offset < file_size) {
        const std::uint64_t remaining = file_size - offset;
        if (remaining < kRecordHeaderSize) {
            truncated_tail = true;
            break;
        }

        std::array<std::byte, kRecordHeaderSize> header_bytes{};
        read_exact(reader, header_bytes, offset);

        RecordHeader header{};
        try {
            header = decode_record_header(header_bytes);
        } catch (const DecodeError& decode_error) {
            throw CorruptionError(corruption_message(offset, decode_error.what()));
        }

        if (header.encoded_size > remaining) {
            truncated_tail = true;
            break;
        }

        Bytes encoded(header.encoded_size);
        std::copy(header_bytes.begin(), header_bytes.end(), encoded.begin());
        auto payload = std::span<std::byte>(encoded).subspan(kRecordHeaderSize);
        read_exact(reader, payload, offset + kRecordHeaderSize);

        DecodedRecord decoded{};
        try {
            decoded = decode_record(encoded);
        } catch (const DecodeError& decode_error) {
            throw CorruptionError(corruption_message(offset, decode_error.what()));
        }
        if (decoded.record.sequence <= last_sequence_.load()) {
            throw CorruptionError(corruption_message(offset,
                                                      "sequence is duplicate or decreasing"));
        }

        apply_recovered_record(header, decoded.record, offset);
        last_sequence_.store(decoded.record.sequence);
        offset += decoded.encoded_size;
    }
    reader.close();

    if (truncated_tail) {
        std::filesystem::resize_file(active_segment_path_, offset, error);
        if (error) {
            throw StorageError("failed to truncate incomplete segment tail: " + error.message());
        }
    }
    segment_size_ = offset;
}

void StorageEngine::open_writer() {
    writer_.open(active_segment_path_, std::ios::binary | std::ios::app);
    if (!writer_) {
        throw StorageError("failed to open active segment for append");
    }
}

void StorageEngine::close_noexcept() noexcept {
    if (!open_.load()) {
        return;
    }
    writer_.flush();
    writer_.close();
    open_.store(false);
}

void StorageEngine::ensure_open() const {
    if (!open_.load()) {
        throw StorageError("storage engine is closed");
    }
    if (write_failed_.load()) {
        throw StorageError("storage engine is unavailable after a mutation failure");
    }
}

void StorageEngine::ensure_sequence_available() const {
    if (last_sequence_.load() == std::numeric_limits<std::uint64_t>::max()) {
        throw StorageError("record sequence space is exhausted");
    }
}

void StorageEngine::append_encoded(std::span<const std::byte> encoded) {
    if (encoded.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        throw StorageError("encoded record exceeds stream write limits");
    }
    if (segment_size_ >
        std::numeric_limits<std::uint64_t>::max() - static_cast<std::uint64_t>(encoded.size())) {
        throw StorageError("active segment offset space is exhausted");
    }
    writer_.write(reinterpret_cast<const char*>(encoded.data()),
                  static_cast<std::streamsize>(encoded.size()));
    writer_.flush();
    if (!writer_) {
        write_failed_ = true;
        throw StorageError("failed to append and flush storage record");
    }
    segment_size_ += encoded.size();
}

void StorageEngine::apply_recovered_record(const RecordHeader& header, const Record& record,
                                           std::uint64_t record_offset) {
    const std::string key = key_string(record.key);
    if (record.operation == Operation::kDelete) {
        static_cast<void>(index_.erase(key));
        return;
    }

    const std::uint64_t value_offset = record_offset + kRecordHeaderSize + header.key_length;
    index_.insert_or_assign(key, RecordLocation{kInitialSegmentId,
                                                record_offset,
                                                value_offset,
                                                header.key_length,
                                                header.value_length,
                                                record.sequence,
                                                header.payload_checksum});
}

std::string StorageEngine::key_string(std::span<const std::byte> key) {
    return std::string(reinterpret_cast<const char*>(key.data()), key.size());
}

void StorageEngine::validate_lookup_key(std::span<const std::byte> key) {
    if (key.empty() || key.size() > kMaxKeySize) {
        throw std::invalid_argument("key length is outside supported bounds");
    }
}

}  // namespace forgekv::storage
