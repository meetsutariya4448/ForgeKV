#include "forgekv/index/sharded_index.hpp"
#include <gtest/gtest.h>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>
namespace forgekv::index {
namespace {
storage::RecordLocation location(std::uint64_t sequence) {
    return storage::RecordLocation{1, sequence * 100, sequence * 100 + 40, 3, 5, sequence, 0};
}
TEST(ShardedIndexTest, InsertsFindsReplacesAndErases) {
    ShardedIndex index(4);
    index.insert_or_assign("key", location(1));
    ASSERT_TRUE(index.find("key"));
    EXPECT_EQ(index.find("key")->sequence, 1U);
    index.insert_or_assign("key", location(2));
    EXPECT_EQ(index.size(), 1U);
    EXPECT_EQ(index.find("key")->sequence, 2U);
    EXPECT_TRUE(index.erase("key"));
    EXPECT_FALSE(index.find("key"));
}
TEST(ShardedIndexTest, ConcurrentDifferentKeysRemainVisible) {
    ShardedIndex index(16);
    std::vector<std::jthread> threads;
    for (std::uint64_t id = 1; id <= 32; ++id) {
        threads.emplace_back([&index, id] { index.insert_or_assign("key-" + std::to_string(id), location(id)); });
    }
    threads.clear();
    EXPECT_EQ(index.size(), 32U);
    for (std::uint64_t id = 1; id <= 32; ++id) {
        ASSERT_TRUE(index.find("key-" + std::to_string(id)));
    }
}
TEST(ShardedIndexTest, ConcurrentSameKeyPublishesWholeLocations) {
    ShardedIndex index(8);
    std::vector<std::jthread> threads;
    for (std::uint64_t id = 1; id <= 32; ++id) {
        threads.emplace_back([&index, id] { index.insert_or_assign("same", location(id)); });
    }
    threads.clear();
    const auto result = index.find("same");
    ASSERT_TRUE(result);
    EXPECT_GE(result->sequence, 1U);
    EXPECT_LE(result->sequence, 32U);
    EXPECT_EQ(result->record_offset, result->sequence * 100);
    EXPECT_EQ(index.size(), 1U);
}
}  // namespace
}  // namespace forgekv::index
