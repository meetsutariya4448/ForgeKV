#pragma once

#include "forgekv/cluster/hash_ring.hpp"
#include "forgekv/storage/record.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace forgekv::cluster {

struct ReplicationMessage {
    std::string primary_id;
    std::uint64_t sequence;
    storage::Operation operation;
    std::uint64_t expires_at_unix_ms = 0;
    storage::Bytes key;
    storage::Bytes value;
};

[[nodiscard]] storage::Bytes encode_replication_message(const ReplicationMessage& message);
[[nodiscard]] ReplicationMessage decode_replication_message(std::span<const std::byte> bytes);

enum class ReplicaApplyResult { kApplied, kDuplicate, kGap };

class ReplicaState {
public:
    [[nodiscard]] ReplicaApplyResult apply(const ReplicationMessage& message);
    [[nodiscard]] std::optional<storage::Bytes> get(std::span<const std::byte> key) const;
    [[nodiscard]] std::uint64_t last_sequence(std::string_view primary_id,
                                              std::span<const std::byte> key) const;
    [[nodiscard]] std::vector<ReplicationMessage> snapshot() const;
    void install_snapshot(std::span<const ReplicationMessage> messages);

private:
    struct StoredValue {
        std::string primary_id;
        std::uint64_t sequence;
        std::uint64_t expires_at_unix_ms;
        storage::Bytes value;
    };

    [[nodiscard]] static std::string bytes_string(std::span<const std::byte> value);
    [[nodiscard]] static std::string stream_key(std::string_view primary_id,
                                                std::span<const std::byte> key);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, StoredValue> values_;
    std::unordered_map<std::string, std::uint64_t> stream_sequences_;
};

enum class AcknowledgementMode { kPrimary, kAll };

struct ReplicationResult {
    bool acknowledged = false;
    std::uint64_t sequence = 0;
    std::size_t acknowledgements = 0;
    std::size_t required = 0;
    std::size_t unavailable = 0;
    std::size_t timed_out = 0;
};

class ReplicatedCluster {
public:
    ReplicatedCluster(std::vector<Node> nodes, std::size_t replication_factor,
                      std::size_t virtual_nodes = 128);

    [[nodiscard]] ReplicationResult put(
        std::span<const std::byte> key, std::span<const std::byte> value,
        AcknowledgementMode mode,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{100});
    [[nodiscard]] ReplicationResult erase(
        std::span<const std::byte> key, AcknowledgementMode mode,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{100});
    [[nodiscard]] std::optional<storage::Bytes> get_from(
        std::string_view node_id, std::span<const std::byte> key) const;
    void set_available(std::string_view node_id, bool available);
    void set_delay(std::string_view node_id, std::chrono::milliseconds delay);
    [[nodiscard]] std::uint64_t replica_lag(std::string_view node_id,
                                            std::span<const std::byte> key) const;
    void recover_node(std::string_view node_id);
    [[nodiscard]] const ConsistentHashRing& ring() const noexcept;

private:
    struct Endpoint {
        std::unique_ptr<ReplicaState> state;
        bool available = true;
        std::chrono::milliseconds delay{0};
    };

    [[nodiscard]] ReplicationResult mutate(storage::Operation operation,
                                           std::span<const std::byte> key,
                                           std::span<const std::byte> value,
                                           AcknowledgementMode mode,
                                           std::chrono::milliseconds timeout);
    [[nodiscard]] Endpoint& endpoint(std::string_view node_id);
    [[nodiscard]] const Endpoint& endpoint(std::string_view node_id) const;
    [[nodiscard]] static std::string key_string(std::span<const std::byte> key);

    ConsistentHashRing ring_;
    std::size_t replication_factor_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Endpoint> endpoints_;
    std::unordered_map<std::string, std::uint64_t> next_sequences_;
    std::unordered_map<std::string, std::vector<ReplicationMessage>> history_;
};

}  // namespace forgekv::cluster
