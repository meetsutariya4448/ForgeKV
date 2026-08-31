#include "forgekv/cluster/replication.hpp"

#include <gtest/gtest.h>

#include <algorithm>
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

TEST(ReplicationProtocolTest, RejectsAmbiguousPrimaryIdentifiers) {
    const ReplicationMessage message{std::string("node\0a", 6), 1,
                                     storage::Operation::kPut, 0,
                                     replica_bytes("key"), replica_bytes("value")};

    EXPECT_THROW(static_cast<void>(encode_replication_message(message)),
                 std::invalid_argument);
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

TEST(ReplicaStateTest, InvalidSnapshotLeavesExistingStateIntact) {
    ReplicaState state;
    const auto key = replica_bytes("key");
    ASSERT_EQ(state.apply({"node-a", 1, storage::Operation::kPut, 0, key,
                           replica_bytes("original")}),
              ReplicaApplyResult::kApplied);
    const std::vector<ReplicationMessage> invalid_snapshot = {
        {"node-a", 2, storage::Operation::kPut, 0, key, replica_bytes("replacement")},
        {"node-a", 3, storage::Operation::kDelete, 0, key, {}},
    };

    EXPECT_THROW(state.install_snapshot(invalid_snapshot), std::invalid_argument);
    EXPECT_EQ(state.get(key), std::optional<storage::Bytes>(replica_bytes("original")));
    EXPECT_EQ(state.last_sequence("node-a", key), 1U);
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

TEST(ReplicatedClusterTest, RejectsUnknownAcknowledgementModeWithoutConsumingSequence) {
    ReplicatedCluster cluster(replica_nodes(), 3, 64);
    const auto key = replica_bytes("account");

    EXPECT_THROW(static_cast<void>(cluster.put(
                     key, replica_bytes("rejected"), static_cast<AcknowledgementMode>(255))),
                 std::invalid_argument);
    const auto accepted =
        cluster.put(key, replica_bytes("accepted"), AcknowledgementMode::kAll);
    EXPECT_TRUE(accepted.acknowledged);
    EXPECT_EQ(accepted.sequence, 1U);
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

TEST(ReplicatedClusterTest, ReplicaLagRejectsNodesOutsideKeyPlacement) {
    ReplicatedCluster cluster(replica_nodes(), 2, 64);
    const auto key = replica_bytes("placement-bound-lag");
    const auto placement = cluster.ring().placement(key, 2);
    const auto nodes = replica_nodes();
    const auto outside = std::find_if(nodes.begin(), nodes.end(), [&](const Node& node) {
        return std::none_of(placement.begin(), placement.end(), [&](const Node& replica) {
            return replica.id == node.id;
        });
    });
    ASSERT_NE(outside, nodes.end());

    EXPECT_EQ(cluster.replica_lag(placement.front().id, key), 0U);
    EXPECT_THROW(static_cast<void>(cluster.replica_lag(outside->id, key)),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(cluster.replica_lag("unknown-node", key)),
                 std::invalid_argument);
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

TEST(ReplicatedClusterTest, OversizedMutationDoesNotConsumeSequence) {
    ReplicatedCluster cluster(replica_nodes(), 3, 64);
    const storage::Bytes oversized_key(storage::kMaxKeySize + 1, std::byte{0x6b});
    EXPECT_THROW(static_cast<void>(cluster.put(oversized_key, replica_bytes("rejected"),
                                               AcknowledgementMode::kAll)),
                 std::invalid_argument);

    const auto accepted = cluster.put(replica_bytes("valid"), replica_bytes("value"),
                                      AcknowledgementMode::kAll);
    EXPECT_TRUE(accepted.acknowledged);
    EXPECT_EQ(accepted.sequence, 1U);
}

}  // namespace
}  // namespace forgekv::cluster
