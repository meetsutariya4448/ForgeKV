#include "forgekv/protocol/frame.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <string_view>

namespace forgekv::protocol {
namespace {

Bytes bytes(std::string_view value) {
    const auto* begin = reinterpret_cast<const std::byte*>(value.data());
    return Bytes(begin, begin + value.size());
}

Frame request(Opcode opcode, Bytes key = bytes("key"), Bytes value = {}) {
    return Frame{FrameKind::kRequest, opcode, Status::kOk, 42, std::move(key), std::move(value)};
}

TEST(FrameCodecTest, RoundTripsBinaryPut) {
    Frame original = request(Opcode::kPut, {std::byte{0}, std::byte{0xff}},
                             {std::byte{1}, std::byte{0}});
    const Frame decoded = decode_frame(encode_frame(original));
    EXPECT_EQ(decoded.kind, original.kind);
    EXPECT_EQ(decoded.opcode, original.opcode);
    EXPECT_EQ(decoded.request_id, 42U);
    EXPECT_EQ(decoded.key, original.key);
    EXPECT_EQ(decoded.value, original.value);
}

TEST(FrameCodecTest, RoundTripsOverloadedResponse) {
    const Frame original{FrameKind::kResponse, Opcode::kGet, Status::kOverloaded, 7, {},
                         bytes("request queue is full")};
    const Frame decoded = decode_frame(encode_frame(original));
    EXPECT_EQ(decoded.status, Status::kOverloaded);
    EXPECT_EQ(decoded.value, original.value);
}

TEST(FrameCodecTest, RejectsZeroRequestIdAndOversizedPayload) {
    Frame frame = request(Opcode::kGet);
    frame.request_id = 0;
    EXPECT_THROW(static_cast<void>(encode_frame(frame)), std::invalid_argument);
    frame.request_id = 1;
    frame.value.resize(storage::kMaxValueSize + 1);
    EXPECT_THROW(static_cast<void>(encode_frame(frame)), std::invalid_argument);
}

TEST(FrameCodecTest, DetectsHeaderAndPayloadCorruption) {
    Bytes encoded = encode_frame(request(Opcode::kPut, bytes("key"), bytes("value")));
    encoded[16] ^= std::byte{1};
    EXPECT_THROW(static_cast<void>(decode_frame(encoded)), ProtocolError);
    encoded = encode_frame(request(Opcode::kPut, bytes("key"), bytes("value")));
    encoded.back() ^= std::byte{1};
    EXPECT_THROW(static_cast<void>(decode_frame(encoded)), ProtocolError);
}

TEST(FrameCodecTest, ReportsTruncatedHeaderAndPayload) {
    const Bytes encoded = encode_frame(request(Opcode::kPut, bytes("key"), bytes("value")));
    for (std::size_t size = 0; size < kFrameHeaderSize; ++size) {
        EXPECT_THROW(static_cast<void>(decode_frame(std::span<const std::byte>(encoded).first(size))),
                     ProtocolError);
    }
    EXPECT_THROW(static_cast<void>(decode_frame(
                     std::span<const std::byte>(encoded).first(encoded.size() - 1))),
                 ProtocolError);
}

TEST(FrameParserTest, HandlesEveryTwoPartSplit) {
    const Bytes encoded = encode_frame(request(Opcode::kPut, bytes("key"), bytes("value")));
    for (std::size_t split = 0; split <= encoded.size(); ++split) {
        FrameParser parser;
        const auto first = parser.feed(std::span<const std::byte>(encoded).first(split));
        if (split == encoded.size()) {
            ASSERT_EQ(first.size(), 1U);
            continue;
        }
        EXPECT_TRUE(first.empty());
        const auto frames = parser.feed(std::span<const std::byte>(encoded).subspan(split));
        ASSERT_EQ(frames.size(), 1U);
        EXPECT_EQ(frames.front().value, bytes("value"));
    }
}

TEST(FrameParserTest, ParsesMultipleFramesFromOneRead) {
    Bytes input = encode_frame(request(Opcode::kGet, bytes("one")));
    const Bytes second = encode_frame(request(Opcode::kDelete, bytes("two")));
    input.insert(input.end(), second.begin(), second.end());
    FrameParser parser;
    const auto frames = parser.feed(input);
    ASSERT_EQ(frames.size(), 2U);
    EXPECT_EQ(frames[0].key, bytes("one"));
    EXPECT_EQ(frames[1].key, bytes("two"));
    EXPECT_EQ(parser.buffered_bytes(), 0U);
}

TEST(FrameParserTest, IsPoisonedAfterMalformedHeader) {
    Bytes encoded = encode_frame(request(Opcode::kGet));
    encoded[0] = std::byte{'X'};
    FrameParser parser;
    EXPECT_THROW(static_cast<void>(parser.feed(encoded)), ProtocolError);
    EXPECT_TRUE(parser.failed());
    EXPECT_THROW(static_cast<void>(parser.feed({})), ProtocolError);
}

TEST(RequestSemanticsTest, EnforcesOpcodePayloadRules) {
    EXPECT_TRUE(request_semantics_valid(request(Opcode::kPut, bytes("key"), {})));
    EXPECT_TRUE(request_semantics_valid(request(Opcode::kGet)));
    EXPECT_FALSE(request_semantics_valid(request(Opcode::kGet, bytes("key"), bytes("bad"))));
    EXPECT_FALSE(request_semantics_valid(request(Opcode::kDelete, {}, {})));
    Frame response{FrameKind::kResponse, Opcode::kGet, Status::kOk, 1, {}, {}};
    EXPECT_FALSE(request_semantics_valid(response));
}

}  // namespace
}  // namespace forgekv::protocol
