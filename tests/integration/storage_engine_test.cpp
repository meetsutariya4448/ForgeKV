#include "forgekv/storage/engine.hpp"
#include "forgekv/storage/record.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <span>
#include <string>
#include <string_view>

namespace forgekv::storage {
namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        static std::atomic<std::uint64_t> counter = 0;
        std::random_device random;
        path_ = std::filesystem::temp_directory_path() /
                ("forgekv-storage-test-" + std::to_string(random()) + "-" +
                 std::to_string(++counter));
        std::filesystem::remove_all(path_);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() { std::filesystem::remove_all(path_); }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

[[nodiscard]] Bytes bytes(std::string_view text) {
    const auto* begin = reinterpret_cast<const std::byte*>(text.data());
    return Bytes(begin, begin + text.size());
}

void append_bytes(const std::filesystem::path& path, std::span<const std::byte> data) {
    std::ofstream stream(path, std::ios::binary | std::ios::app);
    ASSERT_TRUE(stream);
    stream.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(data.size()));
    ASSERT_TRUE(stream);
}

void overwrite_byte(const std::filesystem::path& path, std::uint64_t offset, std::byte value) {
    std::fstream stream(path, std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(stream);
    stream.seekp(static_cast<std::streamoff>(offset));
    stream.put(static_cast<char>(std::to_integer<unsigned char>(value)));
    ASSERT_TRUE(stream);
}

TEST(StorageEngineTest, CreatesAndOpensEmptyDatabase) {
    TemporaryDirectory temporary;
    {
        StorageEngine engine = StorageEngine::open(temporary.path());
        EXPECT_EQ(engine.size(), 0U);
        EXPECT_EQ(engine.last_sequence(), 0U);
        EXPECT_TRUE(std::filesystem::exists(
            StorageEngine::segment_path_for_id(temporary.path(), kInitialSegmentId)));
        EXPECT_EQ(std::filesystem::file_size(
                      StorageEngine::segment_path_for_id(temporary.path(), kInitialSegmentId)),
                  0U);
    }

    StorageEngine reopened = StorageEngine::open(temporary.path());
    EXPECT_EQ(reopened.size(), 0U);
}

TEST(StorageEngineTest, PutsAndGetsBinaryKeyValue) {
    TemporaryDirectory temporary;
    StorageEngine engine = StorageEngine::open(temporary.path());
    const Bytes key = {std::byte{0x00}, std::byte{0xff}, std::byte{0x6b}};
    const Bytes value = {std::byte{0x00}, std::byte{0x01}, std::byte{0xfe}};

    EXPECT_EQ(engine.put(key, value), 1U);
    EXPECT_EQ(engine.get(key), std::optional<Bytes>(value));
    EXPECT_EQ(engine.size(), 1U);
}

TEST(StorageEngineTest, OverwritePublishesLatestValue) {
    TemporaryDirectory temporary;
    StorageEngine engine = StorageEngine::open(temporary.path());
    const Bytes key = bytes("key");

    EXPECT_EQ(engine.put(key, bytes("old")), 1U);
    EXPECT_EQ(engine.put(key, bytes("new")), 2U);
    EXPECT_EQ(engine.get(key), std::optional<Bytes>(bytes("new")));
    EXPECT_EQ(engine.size(), 1U);
}

TEST(StorageEngineTest, DeleteRemovesExistingKey) {
    TemporaryDirectory temporary;
    StorageEngine engine = StorageEngine::open(temporary.path());
    const Bytes key = bytes("key");
    static_cast<void>(engine.put(key, bytes("value")));

    const EraseResult result = engine.erase(key);
    EXPECT_TRUE(result.existed);
    EXPECT_EQ(result.sequence, 2U);
    EXPECT_FALSE(engine.get(key).has_value());
    EXPECT_EQ(engine.size(), 0U);
}

TEST(StorageEngineTest, DeleteNonexistentKeyStillAppendsTombstone) {
    TemporaryDirectory temporary;
    const Bytes key = bytes("missing");
    {
        StorageEngine engine = StorageEngine::open(temporary.path());
        const EraseResult result = engine.erase(key);
        EXPECT_FALSE(result.existed);
        EXPECT_EQ(result.sequence, 1U);
    }

    StorageEngine reopened = StorageEngine::open(temporary.path());
    EXPECT_EQ(reopened.last_sequence(), 1U);
    EXPECT_FALSE(reopened.get(key).has_value());
}

TEST(StorageEngineTest, PutRestartGetRecoversValue) {
    TemporaryDirectory temporary;
    const Bytes key = bytes("key");
    const Bytes value = bytes("persisted");
    {
        StorageEngine engine = StorageEngine::open(temporary.path());
        static_cast<void>(engine.put(key, value));
        engine.close();
    }

    StorageEngine reopened = StorageEngine::open(temporary.path());
    EXPECT_EQ(reopened.get(key), std::optional<Bytes>(value));
}

TEST(StorageEngineTest, PutDeleteRestartRemainsDeleted) {
    TemporaryDirectory temporary;
    const Bytes key = bytes("key");
    {
        StorageEngine engine = StorageEngine::open(temporary.path());
        static_cast<void>(engine.put(key, bytes("value")));
        static_cast<void>(engine.erase(key));
    }

    StorageEngine reopened = StorageEngine::open(temporary.path());
    EXPECT_FALSE(reopened.get(key).has_value());
    EXPECT_EQ(reopened.last_sequence(), 2U);
}

TEST(StorageEngineTest, MultipleOverwritesRecoverLatestSequence) {
    TemporaryDirectory temporary;
    const Bytes key = bytes("key");
    {
        StorageEngine engine = StorageEngine::open(temporary.path());
        static_cast<void>(engine.put(key, bytes("one")));
        static_cast<void>(engine.put(key, bytes("two")));
        static_cast<void>(engine.put(key, bytes("three")));
    }

    StorageEngine reopened = StorageEngine::open(temporary.path());
    EXPECT_EQ(reopened.get(key), std::optional<Bytes>(bytes("three")));
    EXPECT_EQ(reopened.last_sequence(), 3U);
    EXPECT_EQ(reopened.put(bytes("next"), bytes("value")), 4U);
}

TEST(StorageEngineTest, TruncatedFinalHeaderIsRemovedAndPrecedingStateSurvives) {
    const Bytes first = encode_record(Operation::kPut, 1, bytes("key"), bytes("value"));
    const Bytes second = encode_record(Operation::kPut, 2, bytes("other"), bytes("later"));
    for (std::size_t tail_size = 1; tail_size < kRecordHeaderSize; ++tail_size) {
        SCOPED_TRACE(tail_size);
        TemporaryDirectory temporary;
        const auto segment =
            StorageEngine::segment_path_for_id(temporary.path(), kInitialSegmentId);
        std::filesystem::create_directories(temporary.path());
        append_bytes(segment, first);
        append_bytes(segment, std::span<const std::byte>(second).first(tail_size));

        StorageEngine engine = StorageEngine::open(temporary.path());
        EXPECT_EQ(engine.get(bytes("key")), std::optional<Bytes>(bytes("value")));
        EXPECT_EQ(engine.last_sequence(), 1U);
        EXPECT_EQ(std::filesystem::file_size(segment), first.size());
    }
}

TEST(StorageEngineTest, TruncatedFinalPayloadIsRemovedAndPrecedingStateSurvives) {
    TemporaryDirectory temporary;
    const Bytes first = encode_record(Operation::kPut, 1, bytes("key"), bytes("value"));
    const Bytes second = encode_record(Operation::kPut, 2, bytes("other"), bytes("later"));
    const auto segment = StorageEngine::segment_path_for_id(temporary.path(), kInitialSegmentId);
    std::filesystem::create_directories(temporary.path());
    append_bytes(segment, first);
    append_bytes(segment, std::span<const std::byte>(second).first(second.size() - 2));

    StorageEngine engine = StorageEngine::open(temporary.path());
    EXPECT_EQ(engine.get(bytes("key")), std::optional<Bytes>(bytes("value")));
    EXPECT_EQ(std::filesystem::file_size(segment), first.size());
}

TEST(StorageEngineTest, CompleteFinalRecordChecksumFailureIsCorruption) {
    TemporaryDirectory temporary;
    const Bytes encoded = encode_record(Operation::kPut, 1, bytes("key"), bytes("value"));
    const auto segment = StorageEngine::segment_path_for_id(temporary.path(), kInitialSegmentId);
    std::filesystem::create_directories(temporary.path());
    append_bytes(segment, encoded);
    overwrite_byte(segment, encoded.size() - 1, std::byte{0x00});

    EXPECT_THROW(static_cast<void>(StorageEngine::open(temporary.path())), CorruptionError);
}

TEST(StorageEngineTest, MidFileCorruptionFailsWithoutDiscardingLaterBytes) {
    TemporaryDirectory temporary;
    const Bytes first = encode_record(Operation::kPut, 1, bytes("first"), bytes("value-one"));
    const Bytes second = encode_record(Operation::kPut, 2, bytes("second"), bytes("value-two"));
    const auto segment = StorageEngine::segment_path_for_id(temporary.path(), kInitialSegmentId);
    std::filesystem::create_directories(temporary.path());
    append_bytes(segment, first);
    append_bytes(segment, second);
    overwrite_byte(segment, kRecordHeaderSize + 1, std::byte{0xff});
    const auto original_size = std::filesystem::file_size(segment);

    EXPECT_THROW(static_cast<void>(StorageEngine::open(temporary.path())), CorruptionError);
    EXPECT_EQ(std::filesystem::file_size(segment), original_size);
}

TEST(StorageEngineTest, GetDetectsPayloadCorruptionAfterOpen) {
    TemporaryDirectory temporary;
    const Bytes key = bytes("key");
    const Bytes value = bytes("value");
    StorageEngine engine = StorageEngine::open(temporary.path());
    static_cast<void>(engine.put(key, value));
    const auto segment = StorageEngine::segment_path_for_id(temporary.path(), kInitialSegmentId);
    overwrite_byte(segment, kRecordHeaderSize + key.size(), std::byte{0xff});

    EXPECT_THROW(static_cast<void>(engine.get(key)), CorruptionError);
}

TEST(StorageEngineTest, DuplicateSequenceIsCorruption) {
    TemporaryDirectory temporary;
    const Bytes first = encode_record(Operation::kPut, 1, bytes("first"), bytes("one"));
    const Bytes second = encode_record(Operation::kPut, 1, bytes("second"), bytes("two"));
    const auto segment = StorageEngine::segment_path_for_id(temporary.path(), kInitialSegmentId);
    std::filesystem::create_directories(temporary.path());
    append_bytes(segment, first);
    append_bytes(segment, second);

    EXPECT_THROW(static_cast<void>(StorageEngine::open(temporary.path())), CorruptionError);
}

TEST(StorageEngineTest, OperationsAfterCloseAreRejected) {
    TemporaryDirectory temporary;
    StorageEngine engine = StorageEngine::open(temporary.path());
    engine.close();

    EXPECT_THROW(static_cast<void>(engine.get(bytes("key"))), StorageError);
    EXPECT_THROW(static_cast<void>(engine.put(bytes("key"), bytes("value"))), StorageError);
}

}  // namespace
}  // namespace forgekv::storage
