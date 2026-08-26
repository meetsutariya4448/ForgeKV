#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace forgekv::storage {

using Bytes = std::vector<std::byte>;

inline constexpr std::size_t kRecordHeaderSize = 36;
inline constexpr std::size_t kMaxKeySize = 64U * 1024U;
inline constexpr std::size_t kMaxValueSize = 16U * 1024U * 1024U;
inline constexpr std::size_t kMaxRecordSize = kRecordHeaderSize + kMaxKeySize + kMaxValueSize;

enum class Operation : std::uint8_t {
    kPut = 1,
    kDelete = 2,
};

struct Record {
    Operation operation;
    std::uint64_t sequence;
    Bytes key;
    Bytes value;
};

struct RecordHeader {
    Operation operation;
    std::uint64_t sequence;
    std::uint32_t key_length;
    std::uint32_t value_length;
    std::uint32_t payload_checksum;
    std::size_t encoded_size;
};

struct DecodedRecord {
    Record record;
    std::size_t encoded_size;
};

enum class DecodeErrorCode {
    kTruncatedHeader,
    kBadMagic,
    kUnsupportedVersion,
    kUnsupportedHeaderSize,
    kHeaderChecksumMismatch,
    kUnknownOperation,
    kUnsupportedFlags,
    kNonzeroReserved,
    kInvalidSequence,
    kInvalidKeyLength,
    kInvalidValueLength,
    kDeleteHasValue,
    kInvalidRecordSize,
    kTruncatedPayload,
    kPayloadChecksumMismatch,
};

class DecodeError : public std::runtime_error {
public:
    DecodeError(DecodeErrorCode code, std::string message);

    [[nodiscard]] DecodeErrorCode code() const noexcept;

private:
    DecodeErrorCode code_;
};

[[nodiscard]] Bytes encode_record(Operation operation, std::uint64_t sequence,
                                  std::span<const std::byte> key,
                                  std::span<const std::byte> value);

[[nodiscard]] RecordHeader decode_record_header(std::span<const std::byte> bytes);
[[nodiscard]] DecodedRecord decode_record(std::span<const std::byte> bytes);

}  // namespace forgekv::storage
