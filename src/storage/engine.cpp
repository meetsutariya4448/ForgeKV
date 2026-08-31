#include "forgekv/storage/engine.hpp"

#include "forgekv/storage/crc32c.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>

namespace forgekv::storage {
namespace {

[[nodiscard]] std::string corruption_message(std::uint64_t offset, const std::string& detail) {
    return "corrupt segment record at offset " + std::to_string(offset) + ": " + detail;
}

[[noreturn]] void throw_system_error(const std::string& action) {
    throw StorageError(action + ": " + std::strerror(errno));
}

void close_fd_noexcept(int& fd) noexcept {
    if (fd >= 0) {
        static_cast<void>(::close(fd));
        fd = -1;
    }
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

void sync_fd(int fd, const std::string& description) {
    while (::fsync(fd) != 0) {
        if (errno == EINTR) continue;
        throw_system_error("failed to fsync " + description);
    }
}

[[nodiscard]] std::optional<std::uint64_t> parse_segment_id(std::string_view filename) {
    constexpr std::string_view prefix = "segment-";
    constexpr std::string_view suffix = ".fkv";
    if (!filename.starts_with(prefix) || !filename.ends_with(suffix)) return std::nullopt;
    const std::string_view digits =
        filename.substr(prefix.size(), filename.size() - prefix.size() - suffix.size());
    if (digits.size() != 20) return std::nullopt;
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (error != std::errc{} || end != digits.data() + digits.size() || value == 0) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::filesystem::path compaction_temp_path(
    const std::filesystem::path& directory, std::uint64_t first_segment_id) {
    return StorageEngine::segment_path_for_id(directory, first_segment_id).string() + ".compact";
}

[[nodiscard]] std::filesystem::path compaction_old_path(
    const std::filesystem::path& directory, std::uint64_t segment_id) {
    return StorageEngine::segment_path_for_id(directory, segment_id).string() + ".old";
}

void write_all(int fd, std::span<const std::byte> bytes, const std::string& description) {
    while (!bytes.empty()) {
        const std::size_t chunk = std::min(
            bytes.size(), static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
        const ssize_t written = ::write(fd, bytes.data(), chunk);
        if (written > 0) {
            bytes = bytes.subspan(static_cast<std::size_t>(written));
            continue;
        }
        if (written < 0 && errno == EINTR) continue;
        if (written == 0) throw StorageError(description + " made no progress");
        throw_system_error(description);
    }
}

void sync_directory(const std::filesystem::path& directory) {
    int directory_fd = ::open(directory.c_str(), O_RDONLY);
    if (directory_fd < 0) throw_system_error("failed to open directory for fsync");
    try {
        sync_fd(directory_fd, "database directory");
    } catch (...) {
        close_fd_noexcept(directory_fd);
        throw;
    }
    if (::close(directory_fd) != 0) throw_system_error("failed to close database directory");
}

}  // namespace

StorageEngine StorageEngine::open(std::filesystem::path database_directory,
                                  StorageOptions options) {
    return StorageEngine(std::move(database_directory), options);
}

StorageEngine StorageEngine::open(std::filesystem::path database_directory,
                                  std::size_t shard_count) {
    StorageOptions options;
    options.shard_count = shard_count;
    return StorageEngine(std::move(database_directory), options);
}

StorageEngine::StorageEngine(std::filesystem::path database_directory, StorageOptions options)
    : options_(options),
      database_directory_(std::move(database_directory)),
      active_segment_path_(segment_path_for_id(database_directory_, kInitialSegmentId)),
      index_(options_.shard_count) {
    if (options_.durability != DurabilityMode::kAlways &&
        options_.durability != DurabilityMode::kPeriodic &&
        options_.durability != DurabilityMode::kNone) {
        throw std::invalid_argument("durability mode is unsupported");
    }
    if (options_.durability == DurabilityMode::kPeriodic &&
        options_.sync_interval <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("periodic sync interval must be positive");
    }
    if (options_.segment_max_bytes == 0) {
        throw std::invalid_argument("segment size must be positive");
    }
    if (options_.compaction_min_segments < 2) {
        throw std::invalid_argument("compaction threshold must be at least two segments");
    }
    try {
        initialize();
        start_maintenance_threads();
    } catch (...) {
        open_.store(false);
        close_fd_noexcept(writer_fd_);
        throw;
    }
}

StorageEngine::~StorageEngine() { close_noexcept(); }

std::uint64_t StorageEngine::put(std::span<const std::byte> key,
                                 std::span<const std::byte> value) {
    return put_internal(key, value, std::nullopt);
}

std::uint64_t StorageEngine::put_ex(std::span<const std::byte> key,
                                    std::span<const std::byte> value,
                                    std::chrono::milliseconds ttl) {
    if (ttl <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("TTL must be positive");
    }
    return put_internal(key, value, ttl);
}

std::uint64_t StorageEngine::put_internal(std::span<const std::byte> key,
                                          std::span<const std::byte> value,
                                          std::optional<std::chrono::milliseconds> ttl) {
    ensure_open();
    std::lock_guard write_lock(write_mutex_);
    ensure_open();
    ensure_sequence_available();
    const std::uint64_t expires_at_unix_ms = ttl ? expiration_from_ttl(*ttl) : 0;
    const std::uint64_t sequence = last_sequence_.load() + 1;
    const Bytes encoded =
        encode_record(Operation::kPut, sequence, key, value, expires_at_unix_ms);
    rotate_segment_locked(encoded.size());
    const std::uint64_t record_offset = segment_size_;
    const RecordHeader header = decode_record_header(encoded);
    std::string owned_key = key_string(key);
    std::string expiration_key = owned_key;

    append_encoded(encoded);
    last_sequence_.store(sequence);
    if (options_.durability == DurabilityMode::kAlways) synchronize_locked();

    const std::uint64_t value_offset =
        record_offset + header.header_size + header.key_length;
    try {
        index_.insert_or_assign(std::move(owned_key),
                                RecordLocation{active_segment_id_,
                                               record_offset,
                                               value_offset,
                                               header.key_length,
                                               header.value_length,
                                               sequence,
                                               header.payload_checksum,
                                               expires_at_unix_ms,
                                               header.header_size});
        if (expires_at_unix_ms != 0) {
            schedule_expiration(std::move(expiration_key), sequence, expires_at_unix_ms);
        }
    } catch (...) {
        write_failed_.store(true);
        throw;
    }
    return sequence;
}

std::optional<Bytes> StorageEngine::get(std::span<const std::byte> key) const {
    ensure_open();
    validate_lookup_key(key);
    const std::string owned_key = key_string(key);
    std::shared_lock segment_lock(segment_set_mutex_);
    const auto location_result = index_.find(owned_key);
    if (!location_result) return std::nullopt;
    const RecordLocation& location = *location_result;
    if (location.expires_at_unix_ms != 0 && location.expires_at_unix_ms <= now_unix_ms()) {
        static_cast<void>(index_.erase_if_sequence(owned_key, location.sequence));
        return std::nullopt;
    }
    const bool valid_header_size = location.header_size == kRecordHeaderSizeV1 ||
                                   location.header_size == kRecordHeaderSizeV2;
    if (!valid_header_size || location.segment_id == 0 || location.value_offset < location.key_length ||
        location.record_offset >
            std::numeric_limits<std::uint64_t>::max() - location.header_size ||
        location.record_offset + location.header_size !=
            location.value_offset - location.key_length) {
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

    const auto segment_path = segment_path_for_id(database_directory_, location.segment_id);
    std::ifstream reader(segment_path, std::ios::binary);
    if (!reader) {
        throw StorageError("failed to open segment for reading: " + segment_path.string());
    }
    reader.seekg(static_cast<std::streamoff>(payload_offset));
    if (!reader) throw StorageError("failed to seek to record payload");

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
    if (location.expires_at_unix_ms != 0 && location.expires_at_unix_ms <= now_unix_ms()) {
        static_cast<void>(index_.erase_if_sequence(owned_key, location.sequence));
        return std::nullopt;
    }
    return Bytes(payload.begin() + location.key_length, payload.end());
}

TtlResult StorageEngine::ttl(std::span<const std::byte> key) const {
    ensure_open();
    validate_lookup_key(key);
    const std::string owned_key = key_string(key);
    const auto location = index_.find(owned_key);
    if (!location) return TtlResult{TtlState::kNotFound};
    if (location->expires_at_unix_ms == 0) return TtlResult{TtlState::kPersistent};
    const std::uint64_t now = now_unix_ms();
    if (location->expires_at_unix_ms <= now) {
        static_cast<void>(index_.erase_if_sequence(owned_key, location->sequence));
        return TtlResult{TtlState::kNotFound};
    }
    return TtlResult{TtlState::kExpiring, location->expires_at_unix_ms - now};
}

EraseResult StorageEngine::erase(std::span<const std::byte> key) {
    ensure_open();
    validate_lookup_key(key);
    std::lock_guard write_lock(write_mutex_);
    ensure_open();
    ensure_sequence_available();
    const std::uint64_t sequence = last_sequence_.load() + 1;
    const Bytes encoded = encode_record(Operation::kDelete, sequence, key, {});
    rotate_segment_locked(encoded.size());
    const std::string owned_key = key_string(key);
    const auto current = index_.find(owned_key);
    const bool existed = current &&
                         (current->expires_at_unix_ms == 0 ||
                          current->expires_at_unix_ms > now_unix_ms());

    append_encoded(encoded);
    last_sequence_.store(sequence);
    if (options_.durability == DurabilityMode::kAlways) synchronize_locked();
    static_cast<void>(index_.erase(owned_key));
    return EraseResult{sequence, existed};
}

CompactionStats StorageEngine::compact() {
    ensure_open();
    return compact_internal(false);
}

void StorageEngine::close() {
    if (!open_.exchange(false)) return;

    sync_thread_.request_stop();
    expiration_thread_.request_stop();
    compaction_thread_.request_stop();
    sync_wait_cv_.notify_all();
    expiration_cv_.notify_all();
    compaction_cv_.notify_all();
    if (sync_thread_.joinable()) sync_thread_.join();
    if (expiration_thread_.joinable()) expiration_thread_.join();
    if (compaction_thread_.joinable()) compaction_thread_.join();

    std::exception_ptr close_error;
    {
        std::lock_guard write_lock(write_mutex_);
        try {
            if (options_.durability != DurabilityMode::kNone &&
                (dirty_ || directory_sync_pending_ || parent_directory_sync_pending_)) {
                synchronize_locked();
            }
        } catch (...) {
            close_error = std::current_exception();
        }
        if (writer_fd_ >= 0 && ::close(writer_fd_) != 0 && !close_error) {
            close_error = std::make_exception_ptr(
                StorageError("failed to close active segment: " + std::string(std::strerror(errno))));
        }
        writer_fd_ = -1;
    }
    if (close_error) std::rethrow_exception(close_error);
}

std::size_t StorageEngine::size() const noexcept { return index_.size(); }

std::uint64_t StorageEngine::last_sequence() const noexcept { return last_sequence_.load(); }

std::uint64_t StorageEngine::last_synced_sequence() const noexcept {
    return last_synced_sequence_.load();
}

DurabilityMode StorageEngine::durability_mode() const noexcept { return options_.durability; }

std::size_t StorageEngine::segment_count() const {
    std::shared_lock segment_lock(segment_set_mutex_);
    return discover_segment_ids().size();
}

CompactionStats StorageEngine::last_compaction() const {
    std::lock_guard stats_lock(compaction_stats_mutex_);
    return last_compaction_;
}

std::uint64_t StorageEngine::bytes_appended() const noexcept { return bytes_appended_.load(); }

std::uint64_t StorageEngine::compaction_count() const noexcept {
    return compaction_count_.load();
}

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
    if (error) throw StorageError("failed to inspect database directory: " + error.message());
    if (exists && !std::filesystem::is_directory(database_directory_, error)) {
        throw StorageError("database path is not a directory: " + database_directory_.string());
    }
    if (error) throw StorageError("failed to inspect database path type: " + error.message());
    if (!exists) {
        const bool created = std::filesystem::create_directories(database_directory_, error);
        if (!created && error) {
            throw StorageError("failed to create database directory: " + error.message());
        }
        parent_directory_sync_pending_ = created;
    }

    recover_compaction_artifacts();
    auto segment_ids = discover_segment_ids();
    if (segment_ids.empty()) {
        active_segment_id_ = kInitialSegmentId;
        active_segment_path_ = segment_path_for_id(database_directory_, active_segment_id_);
        const int creator =
            ::open(active_segment_path_.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (creator < 0) throw_system_error("failed to create active segment");
        int owned_creator = creator;
        if (::close(owned_creator) != 0) throw_system_error("failed to close new active segment");
        directory_sync_pending_ = true;
    } else {
        active_segment_id_ = segment_ids.back();
        active_segment_path_ = segment_path_for_id(database_directory_, active_segment_id_);
    }

    replay_segments();
    last_synced_sequence_.store(last_sequence_.load());
    open_writer();
    open_.store(true);
}

void StorageEngine::start_maintenance_threads() {
    expiration_thread_ =
        std::jthread([this](std::stop_token stop_token) { run_expiration(stop_token); });
    if (options_.durability == DurabilityMode::kPeriodic) {
        sync_thread_ =
            std::jthread([this](std::stop_token stop_token) { run_periodic_sync(stop_token); });
    }
    if (options_.background_compaction) {
        compaction_thread_ =
            std::jthread([this](std::stop_token stop_token) { run_compaction(stop_token); });
    }
}

void StorageEngine::recover_compaction_artifacts() {
    std::vector<std::uint64_t> old_ids;
    std::vector<std::filesystem::path> temporary_files;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(database_directory_, error)) {
        if (error) throw StorageError("failed to inspect database directory: " + error.message());
        const std::string filename = entry.path().filename().string();
        if (filename.ends_with(".old")) {
            const auto id = parse_segment_id(
                std::string_view(filename).substr(0, filename.size() - std::string_view(".old").size()));
            if (id) old_ids.push_back(*id);
        } else if (filename.ends_with(".compact")) {
            const auto base = std::string_view(filename).substr(
                0, filename.size() - std::string_view(".compact").size());
            if (parse_segment_id(base)) temporary_files.push_back(entry.path());
        }
    }
    if (!old_ids.empty()) {
        std::sort(old_ids.begin(), old_ids.end());
        const bool replacement_exists =
            std::filesystem::exists(segment_path_for_id(database_directory_, old_ids.front()), error);
        if (error) throw StorageError("failed to inspect compaction replacement: " + error.message());
        for (const std::uint64_t id : old_ids) {
            const auto old_path = compaction_old_path(database_directory_, id);
            if (replacement_exists) {
                std::filesystem::remove(old_path, error);
            } else {
                std::filesystem::rename(old_path, segment_path_for_id(database_directory_, id), error);
            }
            if (error) throw StorageError("failed to recover interrupted compaction: " + error.message());
        }
    }
    for (const auto& path : temporary_files) {
        std::filesystem::remove(path, error);
        if (error) throw StorageError("failed to remove abandoned compaction file: " + error.message());
    }
}

std::vector<std::uint64_t> StorageEngine::discover_segment_ids() const {
    std::vector<std::uint64_t> ids;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(database_directory_, error)) {
        if (error) throw StorageError("failed to list storage segments: " + error.message());
        if (!entry.is_regular_file(error)) {
            if (error) throw StorageError("failed to inspect storage segment: " + error.message());
            continue;
        }
        const auto id = parse_segment_id(entry.path().filename().string());
        if (id) ids.push_back(*id);
    }
    std::sort(ids.begin(), ids.end());
    if (std::adjacent_find(ids.begin(), ids.end()) != ids.end()) {
        throw CorruptionError("duplicate storage segment id");
    }
    return ids;
}

void StorageEngine::replay_segments() {
    const auto ids = discover_segment_ids();
    if (ids.empty()) throw StorageError("database contains no active segment");
    for (const std::uint64_t id : ids) replay_segment(id, id == active_segment_id_);
}

void StorageEngine::replay_segment(std::uint64_t segment_id, bool active) {
    const auto segment_path = segment_path_for_id(database_directory_, segment_id);
    std::error_code error;
    const std::uintmax_t file_size_raw = std::filesystem::file_size(segment_path, error);
    if (error) throw StorageError("failed to read segment size: " + error.message());
    if (file_size_raw > std::numeric_limits<std::uint64_t>::max()) {
        throw StorageError("active segment is too large to address");
    }
    const auto file_size = static_cast<std::uint64_t>(file_size_raw);

    std::ifstream reader(segment_path, std::ios::binary);
    if (!reader) throw StorageError("failed to open segment for recovery");

    std::uint64_t offset = 0;
    bool truncated_tail = false;
    while (offset < file_size) {
        const std::uint64_t remaining = file_size - offset;
        if (remaining < kRecordHeaderSizeV1) {
            truncated_tail = true;
            break;
        }

        std::array<std::byte, 8> prefix{};
        read_exact(reader, prefix, offset);
        std::uint16_t header_size = 0;
        try {
            header_size = encoded_record_header_size(prefix);
        } catch (const DecodeError& decode_error) {
            throw CorruptionError(corruption_message(offset, decode_error.what()));
        }
        if (remaining < header_size) {
            truncated_tail = true;
            break;
        }

        Bytes header_bytes(header_size);
        std::copy(prefix.begin(), prefix.end(), header_bytes.begin());
        read_exact(reader, std::span<std::byte>(header_bytes).subspan(prefix.size()),
                   offset + prefix.size());

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
        auto payload = std::span<std::byte>(encoded).subspan(header.header_size);
        read_exact(reader, payload, offset + header.header_size);

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

        apply_recovered_record(header, decoded.record, offset, segment_id);
        last_sequence_.store(decoded.record.sequence);
        offset += decoded.encoded_size;
    }
    reader.close();

    if (truncated_tail && !active) {
        throw CorruptionError("inactive segment has a truncated tail: " + segment_path.string());
    }
    if (truncated_tail) {
        std::filesystem::resize_file(segment_path, offset, error);
        if (error) {
            throw StorageError("failed to truncate incomplete segment tail: " + error.message());
        }
        dirty_ = true;
    }
    if (active) segment_size_ = offset;
}

void StorageEngine::open_writer() {
    writer_fd_ = ::open(active_segment_path_.c_str(), O_WRONLY | O_APPEND);
    if (writer_fd_ < 0) throw_system_error("failed to open active segment for append");
}

void StorageEngine::rotate_segment_locked(std::size_t incoming_size) {
    if (segment_size_ == 0 ||
        incoming_size <= options_.segment_max_bytes - std::min(segment_size_, options_.segment_max_bytes)) {
        return;
    }
    if (active_segment_id_ == std::numeric_limits<std::uint64_t>::max()) {
        throw StorageError("segment id space is exhausted");
    }
    std::unique_lock segment_lock(segment_set_mutex_);
    if (options_.durability != DurabilityMode::kNone && dirty_) synchronize_locked();
    if (::close(writer_fd_) != 0) {
        writer_fd_ = -1;
        throw_system_error("failed to close rotated segment");
    }
    writer_fd_ = -1;

    const std::uint64_t next_id = active_segment_id_ + 1;
    const auto next_path = segment_path_for_id(database_directory_, next_id);
    const int next_fd = ::open(next_path.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_EXCL, 0644);
    if (next_fd < 0) {
        write_failed_.store(true);
        throw_system_error("failed to create rotated segment");
    }
    writer_fd_ = next_fd;
    active_segment_id_ = next_id;
    active_segment_path_ = next_path;
    segment_size_ = 0;
    directory_sync_pending_ = true;
    segment_lock.unlock();
    compaction_cv_.notify_one();
}

void StorageEngine::close_noexcept() noexcept {
    try {
        close();
    } catch (...) {
        write_failed_.store(true);
        close_fd_noexcept(writer_fd_);
    }
}

void StorageEngine::ensure_open() const {
    if (!open_.load()) throw StorageError("storage engine is closed");
    if (write_failed_.load()) {
        throw StorageError("storage engine is unavailable after a persistence failure");
    }
}

void StorageEngine::ensure_sequence_available() const {
    if (last_sequence_.load() == std::numeric_limits<std::uint64_t>::max()) {
        throw StorageError("record sequence space is exhausted");
    }
}

void StorageEngine::append_encoded(std::span<const std::byte> encoded) {
    const std::size_t encoded_size = encoded.size();
    if (segment_size_ >
        std::numeric_limits<std::uint64_t>::max() - static_cast<std::uint64_t>(encoded.size())) {
        throw StorageError("active segment offset space is exhausted");
    }
    try {
        write_all(writer_fd_, encoded, "storage append");
    } catch (...) {
        write_failed_.store(true);
        throw;
    }
    segment_size_ += encoded_size;
    bytes_appended_.fetch_add(encoded_size);
    dirty_ = true;
}

void StorageEngine::synchronize_locked() {
    if (writer_fd_ < 0) throw StorageError("active segment is not open");
    try {
        sync_fd(writer_fd_, "active segment");
        synchronize_directory_locked();
        dirty_ = false;
        last_synced_sequence_.store(last_sequence_.load());
    } catch (...) {
        write_failed_.store(true);
        throw;
    }
}

void StorageEngine::synchronize_directory_locked() {
    if (directory_sync_pending_) {
        int directory_fd = ::open(database_directory_.c_str(), O_RDONLY);
        if (directory_fd < 0) throw_system_error("failed to open database directory for fsync");
        try {
            sync_fd(directory_fd, "database directory");
        } catch (...) {
            close_fd_noexcept(directory_fd);
            throw;
        }
        if (::close(directory_fd) != 0) {
            throw_system_error("failed to close database directory");
        }
        directory_sync_pending_ = false;
    }
    if (parent_directory_sync_pending_) {
        std::filesystem::path parent = database_directory_.parent_path();
        if (parent.empty()) parent = ".";
        int parent_fd = ::open(parent.c_str(), O_RDONLY);
        if (parent_fd < 0) throw_system_error("failed to open parent directory for fsync");
        try {
            sync_fd(parent_fd, "database parent directory");
        } catch (...) {
            close_fd_noexcept(parent_fd);
            throw;
        }
        if (::close(parent_fd) != 0) {
            throw_system_error("failed to close database parent directory");
        }
        parent_directory_sync_pending_ = false;
    }
}

void StorageEngine::run_periodic_sync(std::stop_token stop_token) {
    std::stop_callback wake_waiter(stop_token, [this] { sync_wait_cv_.notify_all(); });
    while (!stop_token.stop_requested() && open_.load()) {
        std::unique_lock wait_lock(sync_wait_mutex_);
        sync_wait_cv_.wait_for(wait_lock, options_.sync_interval,
                               [&] { return stop_token.stop_requested() || !open_.load(); });
        wait_lock.unlock();
        if (stop_token.stop_requested() || !open_.load()) break;
        std::lock_guard write_lock(write_mutex_);
        if (!open_.load() ||
            (!dirty_ && !directory_sync_pending_ && !parent_directory_sync_pending_)) {
            continue;
        }
        try {
            synchronize_locked();
        } catch (...) {
            write_failed_.store(true);
            return;
        }
    }
}

void StorageEngine::run_expiration(std::stop_token stop_token) {
    std::stop_callback wake_waiter(stop_token, [this] { expiration_cv_.notify_all(); });
    while (!stop_token.stop_requested() && open_.load()) {
        std::unique_lock expiration_lock(expiration_mutex_);
        if (expirations_.empty()) {
            expiration_cv_.wait(expiration_lock, [&] {
                return stop_token.stop_requested() || !open_.load() || !expirations_.empty();
            });
            continue;
        }

        const std::uint64_t now = now_unix_ms();
        const std::uint64_t deadline = expirations_.top().expires_at_unix_ms;
        if (deadline > now) {
            const std::uint64_t delay = deadline - now;
            const auto bounded_delay = std::chrono::milliseconds(
                static_cast<std::chrono::milliseconds::rep>(std::min<std::uint64_t>(
                    delay, static_cast<std::uint64_t>(
                               std::numeric_limits<std::chrono::milliseconds::rep>::max()))));
            expiration_cv_.wait_for(expiration_lock, bounded_delay, [&] {
                return stop_token.stop_requested() || !open_.load() || expirations_.empty() ||
                       expirations_.top().expires_at_unix_ms < deadline;
            });
            continue;
        }

        ExpirationEntry entry = expirations_.top();
        expirations_.pop();
        expiration_lock.unlock();
        static_cast<void>(index_.erase_if_sequence(entry.key, entry.sequence));
    }
}

void StorageEngine::run_compaction(std::stop_token stop_token) {
    std::stop_callback wake_waiter(stop_token, [this] { compaction_cv_.notify_all(); });
    while (!stop_token.stop_requested() && open_.load()) {
        std::unique_lock wait_lock(compaction_wait_mutex_);
        compaction_cv_.wait_for(wait_lock, std::chrono::milliseconds{250}, [&] {
            return stop_token.stop_requested() || !open_.load();
        });
        wait_lock.unlock();
        if (stop_token.stop_requested() || !open_.load()) break;
        try {
            static_cast<void>(compact_internal(true));
        } catch (...) {
            // Compaction is opportunistic. The append path remains authoritative after a
            // pre-publication failure, and startup cleanup resolves publication artifacts.
        }
    }
}

CompactionStats StorageEngine::compact_internal(bool require_threshold) {
    std::lock_guard compaction_lock(compaction_mutex_);
    ensure_open();
    const auto started = std::chrono::steady_clock::now();
    CompactionStats stats;

    std::vector<std::uint64_t> candidates;
    {
        std::shared_lock segment_lock(segment_set_mutex_);
        const auto ids = discover_segment_ids();
        for (const std::uint64_t id : ids) {
            if (id != active_segment_id_) candidates.push_back(id);
        }
    }
    const std::size_t threshold = require_threshold ? options_.compaction_min_segments : 2U;
    if (candidates.size() < threshold) return stats;

    struct CopiedRecord {
        std::string key;
        std::uint64_t sequence;
        Bytes encoded;
        RecordLocation location;
    };
    std::vector<CopiedRecord> copied;
    std::vector<std::pair<std::string, std::uint64_t>> expired;
    auto entries = index_.snapshot();
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return left.second.sequence < right.second.sequence;
    });
    const std::set<std::uint64_t> candidate_set(candidates.begin(), candidates.end());
    const std::uint64_t compaction_time = now_unix_ms();
    const std::uint64_t output_id = candidates.front();
    std::uint64_t output_offset = 0;

    {
        std::shared_lock segment_lock(segment_set_mutex_);
        std::error_code error;
        for (const std::uint64_t id : candidates) {
            const auto size = std::filesystem::file_size(
                segment_path_for_id(database_directory_, id), error);
            if (error) throw StorageError("failed to measure compaction input: " + error.message());
            stats.bytes_before += static_cast<std::uint64_t>(size);
        }

        for (const auto& [key, location] : entries) {
            if (!candidate_set.contains(location.segment_id)) continue;
            const std::uint64_t encoded_size_u64 =
                static_cast<std::uint64_t>(location.header_size) + location.key_length +
                location.value_length;
            if ((location.header_size != kRecordHeaderSizeV1 &&
                 location.header_size != kRecordHeaderSizeV2) ||
                encoded_size_u64 > kMaxRecordSize) {
                throw CorruptionError("invalid record location encountered during compaction");
            }
            const auto path = segment_path_for_id(database_directory_, location.segment_id);
            std::ifstream reader(path, std::ios::binary);
            if (!reader) throw StorageError("failed to open compaction input: " + path.string());
            reader.seekg(static_cast<std::streamoff>(location.record_offset));
            if (!reader) throw StorageError("failed to seek compaction input");
            Bytes encoded(static_cast<std::size_t>(encoded_size_u64));
            read_exact(reader, encoded, location.record_offset);
            DecodedRecord decoded;
            try {
                decoded = decode_record(encoded);
            } catch (const DecodeError& error_value) {
                throw CorruptionError(corruption_message(location.record_offset,
                                                          error_value.what()));
            }
            const std::string decoded_key = key_string(decoded.record.key);
            if (decoded.record.sequence != location.sequence || decoded_key != key) {
                throw CorruptionError("index and segment disagree during compaction");
            }
            if (decoded.record.expires_at_unix_ms != 0 &&
                decoded.record.expires_at_unix_ms <= compaction_time) {
                expired.emplace_back(key, location.sequence);
                continue;
            }
            Bytes rewritten = encode_record(Operation::kPut, decoded.record.sequence,
                                            decoded.record.key, decoded.record.value,
                                            decoded.record.expires_at_unix_ms);
            const RecordHeader header = decode_record_header(rewritten);
            const std::uint64_t value_offset =
                output_offset + header.header_size + header.key_length;
            copied.push_back(CopiedRecord{
                key,
                location.sequence,
                std::move(rewritten),
                RecordLocation{output_id, output_offset, value_offset, header.key_length,
                               header.value_length, location.sequence, header.payload_checksum,
                               header.expires_at_unix_ms, header.header_size}});
            output_offset += copied.back().encoded.size();
        }
    }

    const auto temporary = compaction_temp_path(database_directory_, output_id);
    const int temporary_fd =
        ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_TRUNC, 0644);
    if (temporary_fd < 0) throw_system_error("failed to create compaction output");
    int owned_temporary_fd = temporary_fd;
    try {
        for (const auto& record : copied) {
            write_all(owned_temporary_fd, record.encoded, "compaction output write");
        }
        if (options_.durability != DurabilityMode::kNone) {
            sync_fd(owned_temporary_fd, "compaction output");
        }
        if (::close(owned_temporary_fd) != 0) {
            owned_temporary_fd = -1;
            throw_system_error("failed to close compaction output");
        }
        owned_temporary_fd = -1;
    } catch (...) {
        close_fd_noexcept(owned_temporary_fd);
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }

    std::lock_guard write_lock(write_mutex_);
    ensure_open();
    std::unique_lock segment_lock(segment_set_mutex_);
    std::vector<std::uint64_t> moved;
    bool published = false;
    try {
        for (const std::uint64_t id : candidates) {
            std::filesystem::rename(segment_path_for_id(database_directory_, id),
                                    compaction_old_path(database_directory_, id));
            moved.push_back(id);
        }
        if (options_.durability != DurabilityMode::kNone) sync_directory(database_directory_);
        std::filesystem::rename(temporary,
                                segment_path_for_id(database_directory_, output_id));
        published = true;
        if (options_.durability != DurabilityMode::kNone) sync_directory(database_directory_);
    } catch (...) {
        if (!published) {
            for (auto iterator = moved.rbegin(); iterator != moved.rend(); ++iterator) {
                std::error_code ignored;
                std::filesystem::rename(compaction_old_path(database_directory_, *iterator),
                                        segment_path_for_id(database_directory_, *iterator), ignored);
            }
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        }
        throw;
    }

    for (const auto& record : copied) {
        static_cast<void>(
            index_.replace_if_sequence(record.key, record.sequence, record.location));
    }
    for (const auto& [key, sequence] : expired) {
        static_cast<void>(index_.erase_if_sequence(key, sequence));
    }
    for (const std::uint64_t id : candidates) {
        std::error_code error;
        std::filesystem::remove(compaction_old_path(database_directory_, id), error);
        if (error) throw StorageError("failed to remove compacted segment: " + error.message());
    }
    if (options_.durability != DurabilityMode::kNone) sync_directory(database_directory_);

    stats.ran = true;
    stats.input_segments = candidates.size();
    stats.bytes_after = output_offset;
    stats.bytes_written = output_offset;
    stats.duration = std::chrono::steady_clock::now() - started;
    compaction_count_.fetch_add(1);
    {
        std::lock_guard stats_lock(compaction_stats_mutex_);
        last_compaction_ = stats;
    }
    return stats;
}

void StorageEngine::schedule_expiration(std::string key, std::uint64_t sequence,
                                        std::uint64_t expires_at_unix_ms) {
    {
        std::lock_guard expiration_lock(expiration_mutex_);
        expirations_.push(ExpirationEntry{expires_at_unix_ms, sequence, std::move(key)});
    }
    expiration_cv_.notify_one();
}

void StorageEngine::apply_recovered_record(const RecordHeader& header, const Record& record,
                                           std::uint64_t record_offset,
                                           std::uint64_t segment_id) {
    const std::string key = key_string(record.key);
    if (record.operation == Operation::kDelete ||
        (record.expires_at_unix_ms != 0 && record.expires_at_unix_ms <= now_unix_ms())) {
        static_cast<void>(index_.erase(key));
        return;
    }

    const std::uint64_t value_offset = record_offset + header.header_size + header.key_length;
    index_.insert_or_assign(key, RecordLocation{segment_id,
                                                record_offset,
                                                value_offset,
                                                header.key_length,
                                                header.value_length,
                                                record.sequence,
                                                header.payload_checksum,
                                                record.expires_at_unix_ms,
                                                header.header_size});
    if (record.expires_at_unix_ms != 0) {
        schedule_expiration(key, record.sequence, record.expires_at_unix_ms);
    }
}

std::uint64_t StorageEngine::now_unix_ms() noexcept {
    const auto count = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    return count <= 0 ? 0 : static_cast<std::uint64_t>(count);
}

std::uint64_t StorageEngine::expiration_from_ttl(std::chrono::milliseconds ttl) {
    if (ttl <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("TTL must be positive");
    }
    const auto ttl_count = static_cast<std::uint64_t>(ttl.count());
    const std::uint64_t now = now_unix_ms();
    if (ttl_count > std::numeric_limits<std::uint64_t>::max() - now) {
        throw std::invalid_argument("TTL expiration is not representable");
    }
    return now + ttl_count;
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
