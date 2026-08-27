#include "forgekv/storage/engine.hpp"
#include "forgekv/storage/crc32c.hpp"
#include "forgekv/storage/record.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <mutex>
#include <thread>
#include <vector>
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

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

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

void write_u16_be(Bytes& target, std::size_t offset, std::uint16_t value) {
    target[offset] = static_cast<std::byte>((value >> 8U) & 0xffU);
    target[offset + 1] = static_cast<std::byte>(value & 0xffU);
}

void write_u32_be(Bytes& target, std::size_t offset, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        const auto shift = static_cast<unsigned>((3U - index) * 8U);
        target[offset + index] = static_cast<std::byte>((value >> shift) & 0xffU);
    }
}

void write_u64_be(Bytes& target, std::size_t offset, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        const auto shift = static_cast<unsigned>((7U - index) * 8U);
        target[offset + index] = static_cast<std::byte>((value >> shift) & 0xffU);
    }
}

Bytes encode_legacy_v1(std::uint64_t sequence, std::span<const std::byte> key,
                       std::span<const std::byte> value) {
    Bytes encoded(kRecordHeaderSizeV1 + key.size() + value.size());
    encoded[0] = std::byte{'F'};
    encoded[1] = std::byte{'K'};
    encoded[2] = std::byte{'V'};
    encoded[3] = std::byte{'R'};
    write_u16_be(encoded, 4, 1);
    write_u16_be(encoded, 6, static_cast<std::uint16_t>(kRecordHeaderSizeV1));
    encoded[8] = static_cast<std::byte>(Operation::kPut);
    write_u64_be(encoded, 12, sequence);
    write_u32_be(encoded, 20, static_cast<std::uint32_t>(key.size()));
    write_u32_be(encoded, 24, static_cast<std::uint32_t>(value.size()));
    write_u32_be(encoded, 28, crc32c(std::span<const std::byte>(encoded).first(28)));
    auto payload = std::span<std::byte>(encoded).subspan(kRecordHeaderSizeV1);
    std::copy(key.begin(), key.end(), payload.begin());
    std::copy(value.begin(), value.end(), payload.subspan(key.size()).begin());
    write_u32_be(encoded, 32, crc32c(payload));
    return encoded;
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

TEST(StorageEngineTest, ReplaysMixedVersionOneAndVersionTwoRecords) {
    TemporaryDirectory temporary;
    const auto segment = StorageEngine::segment_path_for_id(temporary.path(), kInitialSegmentId);
    std::filesystem::create_directories(temporary.path());
    append_bytes(segment, encode_legacy_v1(1, bytes("legacy"), bytes("v1")));
    append_bytes(segment, encode_record(Operation::kPut, 2, bytes("current"), bytes("v2"),
                                        std::numeric_limits<std::uint64_t>::max() - 1));

    StorageEngine engine = StorageEngine::open(temporary.path());
    EXPECT_EQ(engine.get(bytes("legacy")), std::optional<Bytes>(bytes("v1")));
    EXPECT_EQ(engine.ttl(bytes("legacy")).state, TtlState::kPersistent);
    EXPECT_EQ(engine.get(bytes("current")), std::optional<Bytes>(bytes("v2")));
    EXPECT_EQ(engine.ttl(bytes("current")).state, TtlState::kExpiring);
    EXPECT_EQ(engine.last_sequence(), 2U);
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

TEST(StorageEngineTest, ConcurrentDifferentKeysPersist) {
    TemporaryDirectory temporary;
    StorageEngine engine = StorageEngine::open(temporary.path(), 16);
    std::vector<std::jthread> threads;
    for (int id = 0; id < 16; ++id) {
        threads.emplace_back([&engine, id] {
            const Bytes key = bytes("key-" + std::to_string(id));
            const Bytes value = bytes("value-" + std::to_string(id));
            static_cast<void>(engine.put(key, value));
        });
    }
    threads.clear();
    EXPECT_EQ(engine.size(), 16U);
    for (int id = 0; id < 16; ++id) {
        EXPECT_EQ(engine.get(bytes("key-" + std::to_string(id))),
                  std::optional<Bytes>(bytes("value-" + std::to_string(id))));
    }
}

TEST(StorageEngineTest, ConcurrentSameKeyMatchesHighestSequence) {
    TemporaryDirectory temporary;
    StorageEngine engine = StorageEngine::open(temporary.path(), 8);
    const Bytes key = bytes("same");
    std::mutex results_mutex;
    std::vector<std::pair<std::uint64_t, Bytes>> results;
    std::vector<std::jthread> threads;
    for (int id = 0; id < 16; ++id) {
        threads.emplace_back([&, id] {
            Bytes value = bytes("value-" + std::to_string(id));
            const std::uint64_t sequence = engine.put(key, value);
            std::lock_guard lock(results_mutex);
            results.emplace_back(sequence, std::move(value));
        });
    }
    threads.clear();
    const auto latest = std::max_element(results.begin(), results.end(),
                                         [](const auto& left, const auto& right) {
                                             return left.first < right.first;
                                         });
    ASSERT_NE(latest, results.end());
    EXPECT_EQ(engine.get(key), std::optional<Bytes>(latest->second));
    EXPECT_EQ(engine.size(), 1U);
}

TEST(StorageEngineTest, PutExExpiresWithoutSchedulerScan) {
    TemporaryDirectory temporary;
    StorageEngine engine = StorageEngine::open(temporary.path());
    const Bytes key = bytes("ephemeral");

    EXPECT_EQ(engine.put_ex(key, bytes("value"), std::chrono::milliseconds{30}), 1U);
    const TtlResult initial = engine.ttl(key);
    EXPECT_EQ(initial.state, TtlState::kExpiring);
    EXPECT_GT(initial.remaining_ms, 0U);
    std::this_thread::sleep_for(std::chrono::milliseconds{60});
    EXPECT_FALSE(engine.get(key).has_value());
    EXPECT_EQ(engine.ttl(key).state, TtlState::kNotFound);
}

TEST(StorageEngineTest, PutExRejectsNonpositiveTtl) {
    TemporaryDirectory temporary;
    StorageEngine engine = StorageEngine::open(temporary.path());
    EXPECT_THROW(static_cast<void>(
                     engine.put_ex(bytes("key"), bytes("value"), std::chrono::milliseconds{0})),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(
                     engine.put_ex(bytes("key"), bytes("value"), std::chrono::milliseconds{-1})),
                 std::invalid_argument);
    EXPECT_EQ(engine.last_sequence(), 0U);
}

TEST(StorageEngineTest, PersistentOverwriteSurvivesStaleExpirationEntry) {
    TemporaryDirectory temporary;
    StorageEngine engine = StorageEngine::open(temporary.path());
    const Bytes key = bytes("key");
    static_cast<void>(engine.put_ex(key, bytes("old"), std::chrono::milliseconds{25}));
    static_cast<void>(engine.put(key, bytes("persistent")));

    std::this_thread::sleep_for(std::chrono::milliseconds{55});
    EXPECT_EQ(engine.get(key), std::optional<Bytes>(bytes("persistent")));
    EXPECT_EQ(engine.ttl(key).state, TtlState::kPersistent);
}

TEST(StorageEngineTest, LaterExpirationSurvivesOlderHeapEntry) {
    TemporaryDirectory temporary;
    StorageEngine engine = StorageEngine::open(temporary.path());
    const Bytes key = bytes("key");
    static_cast<void>(engine.put_ex(key, bytes("old"), std::chrono::milliseconds{25}));
    static_cast<void>(engine.put_ex(key, bytes("new"), std::chrono::milliseconds{200}));

    std::this_thread::sleep_for(std::chrono::milliseconds{55});
    EXPECT_EQ(engine.get(key), std::optional<Bytes>(bytes("new")));
    EXPECT_EQ(engine.ttl(key).state, TtlState::kExpiring);
}

TEST(StorageEngineTest, ExpirationMetadataSurvivesRestart) {
    TemporaryDirectory temporary;
    const Bytes key = bytes("key");
    {
        StorageEngine engine = StorageEngine::open(temporary.path());
        static_cast<void>(engine.put_ex(key, bytes("value"), std::chrono::seconds{5}));
    }
    {
        StorageEngine reopened = StorageEngine::open(temporary.path());
        EXPECT_EQ(reopened.get(key), std::optional<Bytes>(bytes("value")));
        EXPECT_EQ(reopened.ttl(key).state, TtlState::kExpiring);
    }
}

TEST(StorageEngineTest, RecoveryDoesNotResurrectAlreadyExpiredRecord) {
    TemporaryDirectory temporary;
    const auto segment = StorageEngine::segment_path_for_id(temporary.path(), kInitialSegmentId);
    std::filesystem::create_directories(temporary.path());
    append_bytes(segment, encode_record(Operation::kPut, 1, bytes("key"), bytes("old")));
    append_bytes(segment,
                 encode_record(Operation::kPut, 2, bytes("key"), bytes("expired"), 1));

    StorageEngine engine = StorageEngine::open(temporary.path());
    EXPECT_FALSE(engine.get(bytes("key")).has_value());
    EXPECT_EQ(engine.ttl(bytes("key")).state, TtlState::kNotFound);
    EXPECT_EQ(engine.last_sequence(), 2U);
}

TEST(StorageEngineTest, DurabilityModesExposeTheirSyncBoundary) {
    {
        TemporaryDirectory temporary;
        StorageOptions options;
        options.durability = DurabilityMode::kAlways;
        StorageEngine engine = StorageEngine::open(temporary.path(), options);
        EXPECT_EQ(engine.put(bytes("key"), bytes("always")), 1U);
        EXPECT_EQ(engine.last_synced_sequence(), 1U);
    }
    {
        TemporaryDirectory temporary;
        StorageOptions options;
        options.durability = DurabilityMode::kNone;
        StorageEngine engine = StorageEngine::open(temporary.path(), options);
        EXPECT_EQ(engine.put(bytes("key"), bytes("none")), 1U);
        EXPECT_EQ(engine.last_synced_sequence(), 0U);
    }
}

TEST(StorageEngineTest, PeriodicModeSyncsDirtySequenceAndOnClose) {
    TemporaryDirectory temporary;
    StorageOptions options;
    options.durability = DurabilityMode::kPeriodic;
    options.sync_interval = std::chrono::milliseconds{20};
    StorageEngine engine = StorageEngine::open(temporary.path(), options);
    static_cast<void>(engine.put(bytes("first"), bytes("value")));
    for (int attempt = 0; attempt < 100 && engine.last_synced_sequence() != 1; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    EXPECT_EQ(engine.last_synced_sequence(), 1U);

    static_cast<void>(engine.put(bytes("second"), bytes("value")));
    engine.close();
    EXPECT_EQ(engine.last_synced_sequence(), 2U);
}

TEST(StorageEngineTest, ShutdownDoesNotWaitForFutureExpiration) {
    TemporaryDirectory temporary;
    const auto started = std::chrono::steady_clock::now();
    {
        StorageOptions options;
        options.durability = DurabilityMode::kPeriodic;
        StorageEngine engine = StorageEngine::open(temporary.path(), options);
        static_cast<void>(engine.put_ex(bytes("key"), bytes("value"), std::chrono::hours{1}));
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    EXPECT_LT(elapsed, std::chrono::milliseconds{500});
}

TEST(StorageEngineTest, AlwaysModeAcknowledgementSurvivesAbruptProcessExit) {
    TemporaryDirectory temporary;
    const pid_t child = ::fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        ::execl(FORGEKV_CRASH_WRITER_PATH, FORGEKV_CRASH_WRITER_PATH,
                temporary.path().c_str(), static_cast<char*>(nullptr));
        ::_exit(127);
    }

    int status = 0;
    ASSERT_EQ(::waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);

    StorageEngine recovered = StorageEngine::open(temporary.path());
    EXPECT_EQ(recovered.get(bytes("crash-key")),
              std::optional<Bytes>(bytes("acknowledged")));
    EXPECT_EQ(recovered.last_sequence(), 1U);
}

TEST(StorageEngineTest, RotatesSegmentsAndRecoversAcrossThem) {
    TemporaryDirectory temporary;
    StorageOptions options;
    options.segment_max_bytes = 96;
    options.background_compaction = false;
    {
        StorageEngine engine = StorageEngine::open(temporary.path(), options);
        for (int index = 0; index < 6; ++index) {
            static_cast<void>(engine.put(bytes("key-" + std::to_string(index)),
                                         bytes(std::string(48, static_cast<char>('a' + index)))));
        }
        EXPECT_GE(engine.segment_count(), 6U);
    }

    StorageEngine recovered = StorageEngine::open(temporary.path(), options);
    for (int index = 0; index < 6; ++index) {
        EXPECT_EQ(recovered.get(bytes("key-" + std::to_string(index))),
                  std::optional<Bytes>(
                      bytes(std::string(48, static_cast<char>('a' + index)))));
    }
    EXPECT_EQ(recovered.last_sequence(), 6U);
}

TEST(StorageEngineTest, CompactionRemovesObsoleteBytesAndSurvivesRestart) {
    TemporaryDirectory temporary;
    StorageOptions options;
    options.segment_max_bytes = 100;
    options.background_compaction = false;
    {
        StorageEngine engine = StorageEngine::open(temporary.path(), options);
        static_cast<void>(engine.put(bytes("stable"), bytes("kept")));
        for (int index = 0; index < 8; ++index) {
            static_cast<void>(engine.put(bytes("changing"),
                                         bytes(std::string(50, static_cast<char>('a' + index)))));
        }
        ASSERT_GE(engine.segment_count(), 8U);
        const CompactionStats stats = engine.compact();
        EXPECT_TRUE(stats.ran);
        EXPECT_GE(stats.input_segments, 2U);
        EXPECT_LT(stats.bytes_after, stats.bytes_before);
        EXPECT_EQ(stats.bytes_after, stats.bytes_written);
        EXPECT_EQ(engine.segment_count(), 2U);
        EXPECT_EQ(engine.get(bytes("stable")), std::optional<Bytes>(bytes("kept")));
        EXPECT_EQ(engine.get(bytes("changing")),
                  std::optional<Bytes>(bytes(std::string(50, 'h'))));
    }

    StorageEngine recovered = StorageEngine::open(temporary.path(), options);
    EXPECT_EQ(recovered.get(bytes("stable")), std::optional<Bytes>(bytes("kept")));
    EXPECT_EQ(recovered.get(bytes("changing")),
              std::optional<Bytes>(bytes(std::string(50, 'h'))));
    EXPECT_EQ(recovered.last_sequence(), 9U);
}

TEST(StorageEngineTest, ReadsAndWritesRemainSafeDuringCompaction) {
    TemporaryDirectory temporary;
    StorageOptions options;
    options.segment_max_bytes = 128;
    options.background_compaction = false;
    StorageEngine engine = StorageEngine::open(temporary.path(), options);
    for (int index = 0; index < 40; ++index) {
        static_cast<void>(engine.put(bytes("base-" + std::to_string(index)),
                                     bytes(std::string(70, 'x'))));
    }

    std::atomic_bool start = false;
    std::jthread writer([&] {
        while (!start.load()) std::this_thread::yield();
        for (int index = 0; index < 100; ++index) {
            static_cast<void>(engine.put(bytes("live-" + std::to_string(index)), bytes("value")));
        }
    });
    start.store(true);
    const CompactionStats stats = engine.compact();
    writer.join();
    EXPECT_TRUE(stats.ran);
    for (int index = 0; index < 100; ++index) {
        EXPECT_EQ(engine.get(bytes("live-" + std::to_string(index))),
                  std::optional<Bytes>(bytes("value")));
    }
}

TEST(StorageEngineTest, StartupRollsBackAndFinishesInterruptedCompactionPublication) {
    {
        TemporaryDirectory temporary;
        const auto segment =
            StorageEngine::segment_path_for_id(temporary.path(), kInitialSegmentId);
        std::filesystem::create_directories(temporary.path());
        append_bytes(segment, encode_record(Operation::kPut, 1, bytes("key"), bytes("old")));
        std::filesystem::rename(segment, segment.string() + ".old");
        append_bytes(segment.string() + ".compact",
                     encode_record(Operation::kPut, 1, bytes("key"), bytes("new")));
        append_bytes(temporary.path() / "user.compact", bytes("unrelated"));

        StorageEngine recovered = StorageEngine::open(temporary.path());
        EXPECT_EQ(recovered.get(bytes("key")), std::optional<Bytes>(bytes("old")));
        EXPECT_FALSE(std::filesystem::exists(segment.string() + ".old"));
        EXPECT_FALSE(std::filesystem::exists(segment.string() + ".compact"));
        EXPECT_TRUE(std::filesystem::exists(temporary.path() / "user.compact"));
    }
    {
        TemporaryDirectory temporary;
        const auto segment =
            StorageEngine::segment_path_for_id(temporary.path(), kInitialSegmentId);
        std::filesystem::create_directories(temporary.path());
        append_bytes(segment.string() + ".old",
                     encode_record(Operation::kPut, 1, bytes("key"), bytes("old")));
        append_bytes(segment,
                     encode_record(Operation::kPut, 1, bytes("key"), bytes("replacement")));

        StorageEngine recovered = StorageEngine::open(temporary.path());
        EXPECT_EQ(recovered.get(bytes("key")),
                  std::optional<Bytes>(bytes("replacement")));
        EXPECT_FALSE(std::filesystem::exists(segment.string() + ".old"));
    }
}

TEST(StorageEngineTest, BackgroundCompactionRunsAfterRotationThreshold) {
    TemporaryDirectory temporary;
    StorageOptions options;
    options.segment_max_bytes = 96;
    options.compaction_min_segments = 2;
    options.background_compaction = true;
    StorageEngine engine = StorageEngine::open(temporary.path(), options);
    for (int index = 0; index < 8; ++index) {
        static_cast<void>(engine.put(bytes("key-" + std::to_string(index)),
                                     bytes(std::string(48, 'z'))));
    }
    for (int attempt = 0; attempt < 200 && engine.compaction_count() == 0; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    EXPECT_GT(engine.compaction_count(), 0U);
    for (int index = 0; index < 8; ++index) {
        EXPECT_EQ(engine.get(bytes("key-" + std::to_string(index))),
                  std::optional<Bytes>(bytes(std::string(48, 'z'))));
    }
}

}  // namespace
}  // namespace forgekv::storage
