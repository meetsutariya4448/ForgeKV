#include "forgekv/cluster/hash_ring.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace forgekv::cluster {
namespace {

std::span<const std::byte> key_bytes(std::string_view value) {
    return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
}

std::vector<Node> three_nodes() {
    return {{"node-a", "127.0.0.1", 7001},
            {"node-b", "127.0.0.1", 7002},
            {"node-c", "127.0.0.1", 7003}};
}

TEST(ConsistentHashRingTest, PlacementIsDeterministicAndReplicaDistinct) {
    ConsistentHashRing first(64);
    ConsistentHashRing second(64);
    first.set_nodes(three_nodes());
    auto reversed = three_nodes();
    std::reverse(reversed.begin(), reversed.end());
    second.set_nodes(reversed);

    for (int index = 0; index < 1'000; ++index) {
        const std::string key = "key-" + std::to_string(index);
        EXPECT_EQ(first.primary(key_bytes(key)), second.primary(key_bytes(key)));
        const auto placement = first.placement(key_bytes(key), 3);
        ASSERT_EQ(placement.size(), 3U);
        EXPECT_NE(placement[0].id, placement[1].id);
        EXPECT_NE(placement[1].id, placement[2].id);
        EXPECT_NE(placement[0].id, placement[2].id);
    }
}

TEST(ConsistentHashRingTest, AddingNodeOnlyMovesKeysToAddedNode) {
    ConsistentHashRing before(128);
    before.set_nodes(three_nodes());
    ConsistentHashRing after = before;
    after.add_node({"node-d", "127.0.0.1", 7004});
    std::vector<std::string> keys;
    std::size_t changed = 0;
    for (int index = 0; index < 10'000; ++index) {
        keys.push_back("key-" + std::to_string(index));
        const auto key = key_bytes(keys.back());
        if (before.primary(key).id != after.primary(key).id) {
            ++changed;
            EXPECT_EQ(after.primary(key).id, "node-d");
        }
    }
    EXPECT_GT(changed, 0U);
    EXPECT_LT(ConsistentHashRing::remap_fraction(before, after, keys), 0.5);
}

TEST(ConsistentHashRingTest, RemovingNodeOnlyMovesItsFormerKeys) {
    ConsistentHashRing before(128);
    before.set_nodes(three_nodes());
    ConsistentHashRing after = before;
    ASSERT_TRUE(after.remove_node("node-b"));
    for (int index = 0; index < 5'000; ++index) {
        const std::string key = "key-" + std::to_string(index);
        const std::string old_node = before.primary(key_bytes(key)).id;
        const std::string new_node = after.primary(key_bytes(key)).id;
        if (old_node != new_node) EXPECT_EQ(old_node, "node-b");
    }
}

TEST(ConsistentHashRingTest, UnreachableSelectedNodeFailsWithoutImplicitFailover) {
    ConsistentHashRing ring(32);
    ring.set_nodes(three_nodes());
    const Node selected = ring.primary(key_bytes("important-key"));
    EXPECT_THROW(static_cast<void>(ring.route(key_bytes("important-key"),
                                              [&](const Node& node) {
                                                  return node.id != selected.id;
                                              })),
                 RoutingError);
}

}  // namespace
}  // namespace forgekv::cluster
