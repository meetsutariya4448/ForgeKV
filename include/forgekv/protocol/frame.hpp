#pragma once

#include "forgekv/storage/record.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace forgekv::protocol {

using Bytes = storage::Bytes;
inline constexpr std::size_t kFrameHeaderSize = 40;
inline constexpr std::size_t kMaxFrameSize =
    kFrameHeaderSize + storage::kMaxKeySize + storage::kMaxValueSize;

enum class FrameKind : std::uint8_t { kRequest = 1, kResponse = 2 };
enum class Opcode : std::uint8_t {
    kPut = 1,
    kGet = 2,
    kDelete = 3,
    kExists = 4,
    kPutEx = 5,
    kTtl = 6,
    kPing = 7,
    kStats = 8,
};
enum class Status : std::uint16_t {
    kOk = 0,
    kNotFound = 1,
    kInvalidRequest = 2,
    kInternalError = 3,
    kStorageError = 4,
    kOverloaded = 5,
    kNoExpiry = 6,
};

inline constexpr std::size_t kTtlPayloadPrefixSize = 8;

struct PutExPayload {
    std::uint64_t ttl_ms;
    Bytes value;
};

struct Frame {
    FrameKind kind;
    Opcode opcode;
    Status status;
    std::uint64_t request_id;
    Bytes key;
    Bytes value;
};

enum class ProtocolErrorCode {
    kParserFailed,
    kTruncatedHeader,
    kBadMagic,
    kUnsupportedVersion,
    kUnsupportedHeaderSize,
    kHeaderChecksumMismatch,
    kUnknownKind,
    kUnknownOpcode,
    kUnknownStatus,
    kUnsupportedFlags,
    kNonzeroReserved,
    kInvalidRequestId,
    kInvalidKeyLength,
    kInvalidValueLength,
    kInvalidFrameSize,
    kTruncatedPayload,
    kPayloadChecksumMismatch,
};

class ProtocolError : public std::runtime_error {
public:
    ProtocolError(ProtocolErrorCode code, std::string message);
    [[nodiscard]] ProtocolErrorCode code() const noexcept;

private:
    ProtocolErrorCode code_;
};

[[nodiscard]] Bytes encode_frame(const Frame& frame);
[[nodiscard]] Frame decode_frame(std::span<const std::byte> bytes);
[[nodiscard]] bool request_semantics_valid(const Frame& frame) noexcept;
[[nodiscard]] Bytes encode_put_ex_payload(std::uint64_t ttl_ms,
                                          std::span<const std::byte> value);
[[nodiscard]] PutExPayload decode_put_ex_payload(std::span<const std::byte> payload);
[[nodiscard]] Bytes encode_ttl_payload(std::uint64_t remaining_ms);
[[nodiscard]] std::uint64_t decode_ttl_payload(std::span<const std::byte> payload);

class FrameParser {
public:
    [[nodiscard]] std::vector<Frame> feed(std::span<const std::byte> bytes);
    [[nodiscard]] std::size_t buffered_bytes() const noexcept;
    [[nodiscard]] bool failed() const noexcept;

private:
    Bytes buffer_;
    std::size_t expected_size_ = kFrameHeaderSize;
    bool header_decoded_ = false;
    bool failed_ = false;
};

}  // namespace forgekv::protocol
