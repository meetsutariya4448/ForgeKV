#include "forgekv/storage/crc32c.hpp"
#include "forgekv/storage/record.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace forgekv::storage {
namespace {

[[nodiscard]] Bytes bytes(std::string_view text) {
    const auto* begin = reinterpret_cast<const std::byte*>(text.data());
    return Bytes(begin, begin + text.size());
}

void write_u32_be(Bytes& target, std::size_t offset, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        const auto shift = static_cast<unsigned>((3U - index) * 8U);
        target[offset + index] = static_cast<std::byte>((value >> shift) & 0xffU);
    }
}

void write_u16_be(Bytes& target, std::size_t offset, std::uint16_t value) {
    target[offset] = static_cast<std::byte>((value >> 8U) & 0xffU);
    target[offset + 1] = static_cast<std::byte>(value & 0xffU);
}

void write_u64_be(Bytes& target, std::size_t offset, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        const auto shift = static_cast<unsigned>((7U - index) * 8U);
        target[offset + index] = static_cast<std::byte>((value >> shift) & 0xffU);
    }
}

void refresh_header_checksum(Bytes& encoded) {
    const std::size_t checksum_offset = encoded[5] == std::byte{1} ? 28 : 36;
    write_u32_be(encoded, checksum_offset,
                 crc32c(std::span<const std::byte>(encoded).first(checksum_offset)));
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

void expect_decode_error(std::span<const std::byte> encoded, DecodeErrorCode expected) {
    try {
        static_cast<void>(decode_record(encoded));
        FAIL() << "expected DecodeError";
    } catch (const DecodeError& error) {
        EXPECT_EQ(error.code(), expected);
    }
}

TEST(Crc32cTest, MatchesStandardCheckValue) {
    EXPECT_EQ(crc32c(bytes("123456789")), 0xe3069283U);
}

TEST(RecordCodecTest, PutRoundTrips) {
    const Bytes key = bytes(std::string_view("binary\0key", 10));
    const Bytes value = {std::byte{0x00}, std::byte{0xff}, std::byte{0x7f}};
    const DecodedRecord decoded = decode_record(encode_record(Operation::kPut, 42, key, value));

    EXPECT_EQ(decoded.record.operation, Operation::kPut);
    EXPECT_EQ(decoded.record.sequence, 42U);
    EXPECT_EQ(decoded.record.expires_at_unix_ms, 0U);
    EXPECT_EQ(decoded.record.key, key);
    EXPECT_EQ(decoded.record.value, value);
    EXPECT_EQ(decoded.encoded_size, kRecordHeaderSize + key.size() + value.size());
}

TEST(RecordCodecTest, PutWithExpirationRoundTrips) {
    const DecodedRecord decoded = decode_record(
        encode_record(Operation::kPut, 42, bytes("key"), bytes("value"), 1'700'000'000'123ULL));
    EXPECT_EQ(decoded.record.expires_at_unix_ms, 1'700'000'000'123ULL);
}

TEST(RecordCodecTest, DecodesLegacyVersionOneAsPersistent) {
    const DecodedRecord decoded =
        decode_record(encode_legacy_v1(9, bytes("legacy"), bytes("value")));
    EXPECT_EQ(decoded.record.sequence, 9U);
    EXPECT_EQ(decoded.record.expires_at_unix_ms, 0U);
    EXPECT_EQ(decoded.record.value, bytes("value"));
    EXPECT_EQ(decoded.encoded_size, kRecordHeaderSizeV1 + 11U);
}

TEST(RecordCodecTest, DeleteRoundTrips) {
    const Bytes key = bytes("gone");
    const DecodedRecord decoded = decode_record(encode_record(Operation::kDelete, 7, key, {}));

    EXPECT_EQ(decoded.record.operation, Operation::kDelete);
    EXPECT_EQ(decoded.record.key, key);
    EXPECT_TRUE(decoded.record.value.empty());
}

TEST(RecordCodecTest, RejectsEmptyKey) {
    EXPECT_THROW(static_cast<void>(encode_record(Operation::kPut, 1, {}, {})),
                 std::invalid_argument);
}

TEST(RecordCodecTest, AllowsEmptyPutValue) {
    const Bytes key = bytes("key");
    EXPECT_NO_THROW(static_cast<void>(decode_record(encode_record(Operation::kPut, 1, key, {}))));
}

TEST(RecordCodecTest, AcceptsMaximumLengths) {
    const Bytes key(kMaxKeySize, std::byte{0x6b});
    const Bytes value(kMaxValueSize, std::byte{0x76});
    const DecodedRecord decoded = decode_record(encode_record(Operation::kPut, 1, key, value));
    EXPECT_EQ(decoded.encoded_size, kMaxRecordSize);
}

TEST(RecordCodecTest, RejectsOversizedKeyAndValue) {
    const Bytes valid_key = bytes("key");
    const Bytes oversized_key(kMaxKeySize + 1, std::byte{0});
    const Bytes oversized_value(kMaxValueSize + 1, std::byte{0});

    EXPECT_THROW(static_cast<void>(encode_record(Operation::kPut, 1, oversized_key, {})),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(encode_record(Operation::kPut, 1, valid_key, oversized_value)),
                 std::invalid_argument);
}

TEST(RecordCodecTest, RejectsZeroSequenceAndDeleteValue) {
    const Bytes key = bytes("key");
    EXPECT_THROW(static_cast<void>(encode_record(Operation::kPut, 0, key, {})),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(encode_record(Operation::kDelete, 1, key, bytes("value"))),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(encode_record(Operation::kDelete, 1, key, {}, 123)),
                 std::invalid_argument);
}

TEST(RecordCodecTest, RejectsMalformedMagic) {
    Bytes encoded = encode_record(Operation::kPut, 1, bytes("key"), bytes("value"));
    encoded[0] ^= std::byte{0x01};
    expect_decode_error(encoded, DecodeErrorCode::kBadMagic);
}

TEST(RecordCodecTest, RejectsUnsupportedVersion) {
    Bytes encoded = encode_record(Operation::kPut, 1, bytes("key"), bytes("value"));
    encoded[5] = std::byte{0x03};
    expect_decode_error(encoded, DecodeErrorCode::kUnsupportedVersion);
}

TEST(RecordCodecTest, RejectsUnknownOperation) {
    Bytes encoded = encode_record(Operation::kPut, 1, bytes("key"), bytes("value"));
    encoded[8] = std::byte{0x7f};
    refresh_header_checksum(encoded);
    expect_decode_error(encoded, DecodeErrorCode::kUnknownOperation);
}

TEST(RecordCodecTest, RejectsHeaderChecksumMismatch) {
    Bytes encoded = encode_record(Operation::kPut, 1, bytes("key"), bytes("value"));
    encoded[12] ^= std::byte{0x01};
    expect_decode_error(encoded, DecodeErrorCode::kHeaderChecksumMismatch);
}

TEST(RecordCodecTest, RejectsPayloadChecksumMismatch) {
    Bytes encoded = encode_record(Operation::kPut, 1, bytes("key"), bytes("value"));
    encoded.back() ^= std::byte{0x01};
    expect_decode_error(encoded, DecodeErrorCode::kPayloadChecksumMismatch);
}

TEST(RecordCodecTest, ReportsEveryTruncatedHeaderBoundary) {
    const Bytes encoded = encode_record(Operation::kPut, 1, bytes("key"), bytes("value"));
    for (std::size_t size = 0; size < kRecordHeaderSize; ++size) {
        expect_decode_error(std::span<const std::byte>(encoded).first(size),
                            DecodeErrorCode::kTruncatedHeader);
    }
}

TEST(RecordCodecTest, ReportsTruncatedPayload) {
    const Bytes encoded = encode_record(Operation::kPut, 1, bytes("key"), bytes("value"));
    expect_decode_error(std::span<const std::byte>(encoded).first(encoded.size() - 1),
                        DecodeErrorCode::kTruncatedPayload);
}

TEST(RecordCodecTest, RejectsImpossibleLengthFieldsBeforeAllocation) {
    Bytes encoded = encode_record(Operation::kPut, 1, bytes("key"), bytes("value"));
    write_u32_be(encoded, 20, 0xffffffffU);
    write_u32_be(encoded, 24, 0xffffffffU);
    refresh_header_checksum(encoded);
    expect_decode_error(encoded, DecodeErrorCode::kInvalidKeyLength);
}

}  // namespace
}  // namespace forgekv::storage
