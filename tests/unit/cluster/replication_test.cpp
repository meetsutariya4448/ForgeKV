#include "forgekv/cluster/replication.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace forgekv::cluster {
namespace {

storage::Bytes replica_bytes(std::string_view value) {
    const auto* begin = reinterpret_cast<const std::byte*>(value.data());
    return storage::Bytes(begin, begin + value.size());
}

std::vector<Node> replica_nodes() {
    return {{"node-a", "127.0.0.1", 7101},
            {"node-b", "127.0.0.1", 7102},
            {"node-c", "127.0.0.1", 7103}};
}

TEST(ReplicationProtocolTest, RoundTripsAndDetectsCorruption) {
    const ReplicationMessage message{"node-a", 42, storage::Operation::kPut, 123456,
                                     replica_bytes("key"), replica_bytes("value")};
    auto encoded = encode_replication_message(message);
    const auto decoded = decode_replication_message(encoded);
    EXPECT_EQ(decoded.primary_id, message.primary_id);
    EXPECT_EQ(decoded.sequence, message.sequence);
    EXPECT_EQ(decoded.operation, message.operation);
    EXPECT_EQ(decoded.expires_at_unix_ms, message.expires_at_unix_ms);
    EXPECT_EQ(decoded.key, message.key);
    EXPECT_EQ(decoded.value, message.value);

    encoded.back() ^= std::byte{1};
    EXPECT_THROW(static_cast<void>(decode_replication_message(encoded)), std::invalid_argument);
}

TEST(ReplicaStateTest, DetectsGapsDuplicatesAndRestoresSnapshot) {
    ReplicaState state;
    const auto key = replica_bytes("key");
    const ReplicationMessage second{"node-a", 2, storage::Operation::kPut, 0, key,
                                    replica_bytes("two")};
    EXPECT_EQ(state.apply(second), ReplicaApplyResult::kGap);
    const ReplicationMessage first{"node-a", 1, storage::Operation::kPut, 0, key,
                                   replica_bytes("one")};
    EXPECT_EQ(state.apply(first), ReplicaApplyResult::kApplied);
    EXPECT_EQ(state.apply(first), ReplicaApplyResult::kDuplicate);
    EXPECT_EQ(state.apply(second), ReplicaApplyResult::kApplied);
    EXPECT_EQ(state.get(key), std::optional<storage::Bytes>(replica_bytes("two")));

    ReplicaState restarted;
    const auto snapshot = state.snapshot();
    restarted.install_snapshot(snapshot);
    EXPECT_EQ(restarted.last_sequence("node-a", key), 2U);
    EXPECT_EQ(restarted.get(key), std::optional<storage::Bytes>(replica_bytes("two")));
}

TEST(ReplicatedClusterTest, PrimaryAndAllAcknowledgementModesAreExplicit) {
    ReplicatedCluster cluster(replica_nodes(), 3, 64);
    const auto key = replica_bytes("account");
    const auto placement = cluster.ring().placement(key, 3);
    cluster.set_available(placement[2].id, false);

    const auto primary = cluster.put(key, replica_bytes("v1"), AcknowledgementMode::kPrimary);
    EXPECT_TRUE(primary.acknowledged);
    EXPECT_EQ(primary.acknowledgements, 2U);
    const auto all = cluster.put(key, replica_bytes("v2"), AcknowledgementMode::kAll);
    EXPECT_FALSE(all.acknowledged);
    EXPECT_EQ(all.required, 3U);
    EXPECT_EQ(all.unavailable, 1U);
}

TEST(ReplicatedClusterTest, ReplicaLagAndRecoveryAreMeasuredPerKeyStream) {
    ReplicatedCluster cluster(replica_nodes(), 3, 64);
    const auto key = replica_bytes("recoverable");
    const auto placement = cluster.ring().placement(key, 3);
    const std::string lagging = placement[1].id;
    cluster.set_available(lagging, false);
    EXPECT_TRUE(cluster.put(key, replica_bytes("one"), AcknowledgementMode::kPrimary)
                    .acknowledged);
    EXPECT_TRUE(cluster.put(key, replica_bytes("two"), AcknowledgementMode::kPrimary)
                    .acknowledged);
    EXPECT_EQ(cluster.replica_lag(lagging, key), 2U);

    cluster.set_available(lagging, true);
    cluster.recover_node(lagging);
    EXPECT_EQ(cluster.replica_lag(lagging, key), 0U);
    EXPECT_EQ(cluster.get_from(lagging, key),
              std::optional<storage::Bytes>(replica_bytes("two")));
}

TEST(ReplicatedClusterTest, SlowReplicaTimesOutAndUnavailablePrimaryRejects) {
    ReplicatedCluster cluster(replica_nodes(), 2, 64);
    const auto key = replica_bytes("timed");
    const auto placement = cluster.ring().placement(key, 2);
    cluster.set_delay(placement[1].id, std::chrono::milliseconds{500});
    const auto timeout = cluster.put(key, replica_bytes("value"), AcknowledgementMode::kAll,
                                     std::chrono::milliseconds{10});
    EXPECT_FALSE(timeout.acknowledged);
    EXPECT_EQ(timeout.timed_out, 1U);

    cluster.set_available(placement.front().id, false);
    const auto unavailable = cluster.put(key, replica_bytes("lost"),
                                         AcknowledgementMode::kPrimary);
    EXPECT_FALSE(unavailable.acknowledged);
    EXPECT_EQ(unavailable.sequence, 0U);
    EXPECT_EQ(unavailable.unavailable, 1U);
}

}  // namespace
}  // namespace forgekv::cluster
