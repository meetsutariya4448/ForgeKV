#include "forgekv/cluster/replication.hpp"

#include "forgekv/storage/crc32c.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <stdexcept>

namespace forgekv::cluster {
namespace {

constexpr std::array<std::byte, 4> kMagic = {
    std::byte{'F'}, std::byte{'K'}, std::byte{'R'}, std::byte{'P'}};
constexpr std::uint16_t kVersion = 1;
constexpr std::uint16_t kHeaderSize = 48;
constexpr std::size_t kMaxPrimaryId = 255;

std::uint16_t read_u16(std::span<const std::byte> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset])) << 8U) |
        std::to_integer<std::uint8_t>(bytes[offset + 1]));
}

std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value = (value << 8U) | std::to_integer<std::uint8_t>(bytes[offset + index]);
    }
    return value;
}

std::uint64_t read_u64(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value = (value << 8U) | std::to_integer<std::uint8_t>(bytes[offset + index]);
    }
    return value;
}

void write_u16(std::span<std::byte> bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>((value >> 8U) & 0xffU);
    bytes[offset + 1] = static_cast<std::byte>(value & 0xffU);
}

void write_u32(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        const auto shift = static_cast<unsigned>((3U - index) * 8U);
        bytes[offset + index] = static_cast<std::byte>((value >> shift) & 0xffU);
    }
}

void write_u64(std::span<std::byte> bytes, std::size_t offset, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        const auto shift = static_cast<unsigned>((7U - index) * 8U);
        bytes[offset + index] = static_cast<std::byte>((value >> shift) & 0xffU);
    }
}

std::uint64_t now_unix_ms() {
    const auto count = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch()).count();
    return count <= 0 ? 0 : static_cast<std::uint64_t>(count);
}

}  // namespace

storage::Bytes encode_replication_message(const ReplicationMessage& message) {
    if (message.primary_id.empty() || message.primary_id.size() > kMaxPrimaryId) {
        throw std::invalid_argument("replication primary id is outside bounds");
    }
    if (message.sequence == 0 || message.key.empty() ||
        message.key.size() > storage::kMaxKeySize ||
        message.value.size() > storage::kMaxValueSize) {
        throw std::invalid_argument("replication message fields are outside bounds");
    }
    if (message.operation != storage::Operation::kPut &&
        message.operation != storage::Operation::kDelete) {
        throw std::invalid_argument("replication operation is unsupported");
    }
    if (message.operation == storage::Operation::kDelete &&
        (!message.value.empty() || message.expires_at_unix_ms != 0)) {
        throw std::invalid_argument("replicated DELETE has value or expiration");
    }
    const std::uint64_t payload_size = message.primary_id.size() + message.key.size() +
                                       message.value.size();
    if (payload_size > std::numeric_limits<std::size_t>::max() - kHeaderSize) {
        throw std::length_error("replication message size is not representable");
    }
    storage::Bytes encoded(kHeaderSize + static_cast<std::size_t>(payload_size));
    std::copy(kMagic.begin(), kMagic.end(), encoded.begin());
    write_u16(encoded, 4, kVersion);
    write_u16(encoded, 6, kHeaderSize);
    encoded[8] = static_cast<std::byte>(message.operation);
    encoded[9] = std::byte{0};
    write_u16(encoded, 10, 0);
    write_u64(encoded, 12, message.sequence);
    write_u64(encoded, 20, message.expires_at_unix_ms);
    write_u16(encoded, 28, static_cast<std::uint16_t>(message.primary_id.size()));
    write_u16(encoded, 30, 0);
    write_u32(encoded, 32, static_cast<std::uint32_t>(message.key.size()));
    write_u32(encoded, 36, static_cast<std::uint32_t>(message.value.size()));
    write_u32(encoded, 40, storage::crc32c(std::span<const std::byte>(encoded).first(40)));
    auto payload = std::span<std::byte>(encoded).subspan(kHeaderSize);
    const auto* primary_begin = reinterpret_cast<const std::byte*>(message.primary_id.data());
    std::copy(primary_begin, primary_begin + message.primary_id.size(), payload.begin());
    std::copy(message.key.begin(), message.key.end(),
              payload.subspan(message.primary_id.size()).begin());
    std::copy(message.value.begin(), message.value.end(),
              payload.subspan(message.primary_id.size() + message.key.size()).begin());
    write_u32(encoded, 44, storage::crc32c(payload));
    return encoded;
}

ReplicationMessage decode_replication_message(std::span<const std::byte> bytes) {
    if (bytes.size() < kHeaderSize) throw std::invalid_argument("replication header is truncated");
    if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin()) || read_u16(bytes, 4) != kVersion ||
        read_u16(bytes, 6) != kHeaderSize || bytes[9] != std::byte{0} || read_u16(bytes, 10) != 0 ||
        read_u16(bytes, 30) != 0) {
        throw std::invalid_argument("replication header is invalid or unsupported");
    }
    if (read_u32(bytes, 40) != storage::crc32c(bytes.first(40))) {
        throw std::invalid_argument("replication header checksum mismatch");
    }
    const auto operation_byte = std::to_integer<std::uint8_t>(bytes[8]);
    storage::Operation operation;
    if (operation_byte == static_cast<std::uint8_t>(storage::Operation::kPut)) {
        operation = storage::Operation::kPut;
    } else if (operation_byte == static_cast<std::uint8_t>(storage::Operation::kDelete)) {
        operation = storage::Operation::kDelete;
    } else {
        throw std::invalid_argument("replication operation is unknown");
    }
    const std::uint64_t sequence = read_u64(bytes, 12);
    const std::uint64_t expiration = read_u64(bytes, 20);
    const std::uint16_t primary_length = read_u16(bytes, 28);
    const std::uint32_t key_length = read_u32(bytes, 32);
    const std::uint32_t value_length = read_u32(bytes, 36);
    const std::uint64_t payload_size = static_cast<std::uint64_t>(primary_length) + key_length + value_length;
    if (sequence == 0 || primary_length == 0 || primary_length > kMaxPrimaryId || key_length == 0 ||
        key_length > storage::kMaxKeySize || value_length > storage::kMaxValueSize ||
        payload_size > bytes.size() - kHeaderSize || bytes.size() != kHeaderSize + payload_size) {
        throw std::invalid_argument("replication lengths or sequence are invalid");
    }
    const auto payload = bytes.subspan(kHeaderSize);
    if (read_u32(bytes, 44) != storage::crc32c(payload)) {
        throw std::invalid_argument("replication payload checksum mismatch");
    }
    const auto primary_end = payload.begin() + primary_length;
    const auto key_end = primary_end + key_length;
    ReplicationMessage message{
        std::string(reinterpret_cast<const char*>(payload.data()), primary_length), sequence,
        operation, expiration, storage::Bytes(primary_end, key_end),
        storage::Bytes(key_end, payload.end())};
    static_cast<void>(encode_replication_message(message));
    return message;
}

std::string ReplicaState::bytes_string(std::span<const std::byte> value) {
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

std::string ReplicaState::stream_key(std::string_view primary_id,
                                     std::span<const std::byte> key) {
    std::string result(primary_id);
    result.push_back('\0');
    result.append(reinterpret_cast<const char*>(key.data()), key.size());
    return result;
}

ReplicaApplyResult ReplicaState::apply(const ReplicationMessage& message) {
    static_cast<void>(encode_replication_message(message));
    std::lock_guard lock(mutex_);
    const std::string stream = stream_key(message.primary_id, message.key);
    const std::uint64_t last = stream_sequences_[stream];
    if (message.sequence <= last) return ReplicaApplyResult::kDuplicate;
    if (message.sequence != last + 1) return ReplicaApplyResult::kGap;
    const std::string key = bytes_string(message.key);
    if (message.operation == storage::Operation::kDelete) {
        values_.erase(key);
    } else {
        values_[key] = StoredValue{message.primary_id, message.sequence,
                                   message.expires_at_unix_ms, message.value};
    }
    stream_sequences_[stream] = message.sequence;
    return ReplicaApplyResult::kApplied;
}

std::optional<storage::Bytes> ReplicaState::get(std::span<const std::byte> key) const {
    std::lock_guard lock(mutex_);
    const auto iterator = values_.find(bytes_string(key));
    if (iterator == values_.end()) return std::nullopt;
    if (iterator->second.expires_at_unix_ms != 0 &&
        iterator->second.expires_at_unix_ms <= now_unix_ms()) return std::nullopt;
    return iterator->second.value;
}

std::uint64_t ReplicaState::last_sequence(std::string_view primary_id,
                                          std::span<const std::byte> key) const {
    std::lock_guard lock(mutex_);
    const auto iterator = stream_sequences_.find(stream_key(primary_id, key));
    return iterator == stream_sequences_.end() ? 0 : iterator->second;
}

std::vector<ReplicationMessage> ReplicaState::snapshot() const {
    std::lock_guard lock(mutex_);
    std::vector<ReplicationMessage> messages;
    messages.reserve(values_.size());
    for (const auto& [key, stored] : values_) {
        messages.push_back(ReplicationMessage{stored.primary_id, stored.sequence,
                                              storage::Operation::kPut,
                                              stored.expires_at_unix_ms,
                                              storage::Bytes(reinterpret_cast<const std::byte*>(key.data()),
                                                             reinterpret_cast<const std::byte*>(key.data()) + key.size()),
                                              stored.value});
    }
    return messages;
}

void ReplicaState::install_snapshot(std::span<const ReplicationMessage> messages) {
    std::lock_guard lock(mutex_);
    values_.clear();
    stream_sequences_.clear();
    for (const auto& message : messages) {
        static_cast<void>(encode_replication_message(message));
        const std::string key = bytes_string(message.key);
        values_[key] = StoredValue{message.primary_id, message.sequence,
                                   message.expires_at_unix_ms, message.value};
        stream_sequences_[stream_key(message.primary_id, message.key)] = message.sequence;
    }
}

ReplicatedCluster::ReplicatedCluster(std::vector<Node> nodes, std::size_t replication_factor,
                                     std::size_t virtual_nodes)
    : ring_(virtual_nodes), replication_factor_(replication_factor) {
    if (replication_factor_ == 0 || replication_factor_ > 3 || nodes.size() < replication_factor_) {
        throw std::invalid_argument("replication factor must be 1..3 and fit membership");
    }
    ring_.set_nodes(nodes);
    for (const auto& node : nodes) {
        endpoints_.emplace(node.id, Endpoint{std::make_unique<ReplicaState>()});
    }
}

ReplicationResult ReplicatedCluster::put(std::span<const std::byte> key,
                                         std::span<const std::byte> value,
                                         AcknowledgementMode mode,
                                         std::chrono::milliseconds timeout) {
    return mutate(storage::Operation::kPut, key, value, mode, timeout);
}

ReplicationResult ReplicatedCluster::erase(std::span<const std::byte> key,
                                           AcknowledgementMode mode,
                                           std::chrono::milliseconds timeout) {
    return mutate(storage::Operation::kDelete, key, {}, mode, timeout);
}

ReplicationResult ReplicatedCluster::mutate(storage::Operation operation,
                                            std::span<const std::byte> key,
                                            std::span<const std::byte> value,
                                            AcknowledgementMode mode,
                                            std::chrono::milliseconds timeout) {
    if (key.empty() || key.size() > storage::kMaxKeySize ||
        value.size() > storage::kMaxValueSize ||
        timeout < std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("replication key/value/timeout is invalid");
    }
    std::lock_guard lock(mutex_);
    const auto placement = ring_.placement(key, replication_factor_);
    ReplicationResult result;
    result.required = mode == AcknowledgementMode::kPrimary ? 1 : placement.size();
    Endpoint& primary_endpoint = endpoint(placement.front().id);
    if (!primary_endpoint.available) {
        result.unavailable = 1;
        return result;
    }
    if (primary_endpoint.delay > timeout) {
        result.timed_out = 1;
        return result;
    }
    const std::string stream = placement.front().id + std::string("\0", 1) + key_string(key);
    const std::uint64_t sequence = ++next_sequences_[stream];
    ReplicationMessage message{placement.front().id, sequence, operation, 0,
                               storage::Bytes(key.begin(), key.end()),
                               storage::Bytes(value.begin(), value.end())};
    history_[stream].push_back(message);
    result.sequence = sequence;
    bool primary_acknowledged = false;
    for (const auto& node : placement) {
        Endpoint& target = endpoint(node.id);
        if (!target.available) {
            ++result.unavailable;
            continue;
        }
        if (target.delay > timeout) {
            ++result.timed_out;
            continue;
        }
        const ReplicaApplyResult applied = target.state->apply(message);
        if (applied == ReplicaApplyResult::kApplied || applied == ReplicaApplyResult::kDuplicate) {
            ++result.acknowledgements;
            if (node.id == placement.front().id) primary_acknowledged = true;
        }
    }
    result.acknowledged = mode == AcknowledgementMode::kPrimary
                              ? primary_acknowledged
                              : result.acknowledgements >= result.required;
    return result;
}

std::optional<storage::Bytes> ReplicatedCluster::get_from(
    std::string_view node_id, std::span<const std::byte> key) const {
    std::lock_guard lock(mutex_);
    const Endpoint& target = endpoint(node_id);
    if (!target.available) throw RoutingError("replica is unavailable: " + std::string(node_id));
    return target.state->get(key);
}

void ReplicatedCluster::set_available(std::string_view node_id, bool available) {
    std::lock_guard lock(mutex_);
    endpoint(node_id).available = available;
}

void ReplicatedCluster::set_delay(std::string_view node_id, std::chrono::milliseconds delay) {
    if (delay < std::chrono::milliseconds::zero()) throw std::invalid_argument("delay is negative");
    std::lock_guard lock(mutex_);
    endpoint(node_id).delay = delay;
}

std::uint64_t ReplicatedCluster::replica_lag(std::string_view node_id,
                                             std::span<const std::byte> key) const {
    std::lock_guard lock(mutex_);
    const auto placement = ring_.placement(key, replication_factor_);
    const std::string stream = placement.front().id + std::string("\0", 1) + key_string(key);
    const auto latest = next_sequences_.find(stream);
    if (latest == next_sequences_.end()) return 0;
    const std::uint64_t applied = endpoint(node_id).state->last_sequence(placement.front().id, key);
    return latest->second - std::min(latest->second, applied);
}

void ReplicatedCluster::recover_node(std::string_view node_id) {
    std::lock_guard lock(mutex_);
    Endpoint& target = endpoint(node_id);
    for (const auto& [stream, messages] : history_) {
        static_cast<void>(stream);
        for (const auto& message : messages) {
            const auto placement = ring_.placement(message.key, replication_factor_);
            if (std::none_of(placement.begin(), placement.end(), [&](const Node& node) {
                    return node.id == node_id;
                })) {
                continue;
            }
            const auto result = target.state->apply(message);
            if (result == ReplicaApplyResult::kGap) {
                throw std::runtime_error("replica recovery encountered a sequence gap");
            }
        }
    }
}

const ConsistentHashRing& ReplicatedCluster::ring() const noexcept { return ring_; }

ReplicatedCluster::Endpoint& ReplicatedCluster::endpoint(std::string_view node_id) {
    const auto iterator = endpoints_.find(std::string(node_id));
    if (iterator == endpoints_.end()) throw std::invalid_argument("unknown cluster node");
    return iterator->second;
}

const ReplicatedCluster::Endpoint& ReplicatedCluster::endpoint(std::string_view node_id) const {
    const auto iterator = endpoints_.find(std::string(node_id));
    if (iterator == endpoints_.end()) throw std::invalid_argument("unknown cluster node");
    return iterator->second;
}

std::string ReplicatedCluster::key_string(std::span<const std::byte> key) {
    return std::string(reinterpret_cast<const char*>(key.data()), key.size());
}

}  // namespace forgekv::cluster
