#include "forgekv/storage/record.hpp"

#include "forgekv/storage/crc32c.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

namespace forgekv::storage {
namespace {

constexpr std::array<std::byte, 4> kMagic = {
    std::byte{'F'}, std::byte{'K'}, std::byte{'V'}, std::byte{'R'}};
constexpr std::uint16_t kFormatVersionV1 = 1;
constexpr std::uint16_t kFormatVersionV2 = 2;
constexpr std::uint8_t kSupportedFlags = 0;

[[nodiscard]] std::uint16_t read_u16_be(std::span<const std::byte> bytes,
                                        std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset])) << 8U) |
        static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])));
}

[[nodiscard]] std::uint32_t read_u32_be(std::span<const std::byte> bytes,
                                        std::size_t offset) noexcept {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value = (value << 8U) | std::to_integer<std::uint8_t>(bytes[offset + index]);
    }
    return value;
}

[[nodiscard]] std::uint64_t read_u64_be(std::span<const std::byte> bytes,
                                        std::size_t offset) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value = (value << 8U) | std::to_integer<std::uint8_t>(bytes[offset + index]);
    }
    return value;
}

void write_u16_be(std::span<std::byte> bytes, std::size_t offset, std::uint16_t value) noexcept {
    bytes[offset] = static_cast<std::byte>((value >> 8U) & 0xffU);
    bytes[offset + 1] = static_cast<std::byte>(value & 0xffU);
}

void write_u32_be(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) noexcept {
    for (std::size_t index = 0; index < 4; ++index) {
        const auto shift = static_cast<unsigned>((3U - index) * 8U);
        bytes[offset + index] = static_cast<std::byte>((value >> shift) & 0xffU);
    }
}

void write_u64_be(std::span<std::byte> bytes, std::size_t offset, std::uint64_t value) noexcept {
    for (std::size_t index = 0; index < 8; ++index) {
        const auto shift = static_cast<unsigned>((7U - index) * 8U);
        bytes[offset + index] = static_cast<std::byte>((value >> shift) & 0xffU);
    }
}

[[noreturn]] void throw_decode(DecodeErrorCode code, std::string_view message) {
    throw DecodeError(code, std::string(message));
}

[[nodiscard]] std::size_t checked_record_size(std::uint16_t header_size,
                                              std::uint32_t key_length,
                                              std::uint32_t value_length) {
    const std::uint64_t payload_size = static_cast<std::uint64_t>(key_length) + value_length;
    const std::uint64_t record_size = header_size + payload_size;
    if (record_size > kMaxRecordSize || record_size > std::numeric_limits<std::size_t>::max()) {
        throw_decode(DecodeErrorCode::kInvalidRecordSize, "record size is not representable");
    }
    return static_cast<std::size_t>(record_size);
}

void validate_record_fields(Operation operation, std::uint32_t key_length,
                            std::uint32_t value_length, std::uint64_t expires_at_unix_ms) {
    if (key_length == 0 || key_length > kMaxKeySize) {
        throw_decode(DecodeErrorCode::kInvalidKeyLength, "key length is outside supported bounds");
    }
    if (value_length > kMaxValueSize) {
        throw_decode(DecodeErrorCode::kInvalidValueLength,
                     "value length is outside supported bounds");
    }
    if (operation == Operation::kDelete && value_length != 0) {
        throw_decode(DecodeErrorCode::kDeleteHasValue, "DELETE record contains a value");
    }
    if (operation == Operation::kDelete && expires_at_unix_ms != 0) {
        throw_decode(DecodeErrorCode::kDeleteHasExpiration,
                     "DELETE record contains an expiration");
    }
}

}  // namespace

DecodeError::DecodeError(DecodeErrorCode code, std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}

DecodeErrorCode DecodeError::code() const noexcept { return code_; }

Bytes encode_record(Operation operation, std::uint64_t sequence, std::span<const std::byte> key,
                    std::span<const std::byte> value, std::uint64_t expires_at_unix_ms) {
    if (operation != Operation::kPut && operation != Operation::kDelete) {
        throw std::invalid_argument("unsupported record operation");
    }
    if (sequence == 0) {
        throw std::invalid_argument("record sequence must be nonzero");
    }
    if (key.empty() || key.size() > kMaxKeySize) {
        throw std::invalid_argument("key length is outside supported bounds");
    }
    if (value.size() > kMaxValueSize) {
        throw std::invalid_argument("value length is outside supported bounds");
    }
    if (operation == Operation::kDelete && !value.empty()) {
        throw std::invalid_argument("DELETE record cannot contain a value");
    }
    if (operation == Operation::kDelete && expires_at_unix_ms != 0) {
        throw std::invalid_argument("DELETE record cannot contain an expiration");
    }

    const auto key_length = static_cast<std::uint32_t>(key.size());
    const auto value_length = static_cast<std::uint32_t>(value.size());
    const std::size_t encoded_size =
        checked_record_size(static_cast<std::uint16_t>(kRecordHeaderSizeV2), key_length,
                            value_length);

    Bytes encoded(encoded_size);
    std::copy(kMagic.begin(), kMagic.end(), encoded.begin());
    write_u16_be(encoded, 4, kFormatVersionV2);
    write_u16_be(encoded, 6, static_cast<std::uint16_t>(kRecordHeaderSizeV2));
    encoded[8] = static_cast<std::byte>(operation);
    encoded[9] = static_cast<std::byte>(kSupportedFlags);
    write_u16_be(encoded, 10, 0);
    write_u64_be(encoded, 12, sequence);
    write_u32_be(encoded, 20, key_length);
    write_u32_be(encoded, 24, value_length);
    write_u64_be(encoded, 28, expires_at_unix_ms);
    write_u32_be(encoded, 36, crc32c(std::span<const std::byte>(encoded).first(36)));

    auto payload = std::span<std::byte>(encoded).subspan(kRecordHeaderSizeV2);
    std::copy(key.begin(), key.end(), payload.begin());
    std::copy(value.begin(), value.end(), payload.subspan(key.size()).begin());
    write_u32_be(encoded, 40, crc32c(payload));
    return encoded;
}

std::uint16_t encoded_record_header_size(std::span<const std::byte> bytes) {
    if (bytes.size() < 8) {
        throw_decode(DecodeErrorCode::kTruncatedHeader, "record header is truncated");
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        throw_decode(DecodeErrorCode::kBadMagic, "record magic does not match FKVR");
    }
    const std::uint16_t version = read_u16_be(bytes, 4);
    std::uint16_t header_size = 0;
    if (version == kFormatVersionV1) {
        header_size = static_cast<std::uint16_t>(kRecordHeaderSizeV1);
    } else if (version == kFormatVersionV2) {
        header_size = static_cast<std::uint16_t>(kRecordHeaderSizeV2);
    } else {
        throw_decode(DecodeErrorCode::kUnsupportedVersion, "record format version is unsupported");
    }
    if (read_u16_be(bytes, 6) != header_size) {
        throw_decode(DecodeErrorCode::kUnsupportedHeaderSize, "record header size is unsupported");
    }
    return header_size;
}

RecordHeader decode_record_header(std::span<const std::byte> bytes) {
    const std::uint16_t header_size = encoded_record_header_size(bytes);
    const std::uint16_t version = read_u16_be(bytes, 4);
    if (bytes.size() < header_size) {
        throw_decode(DecodeErrorCode::kTruncatedHeader, "record header is truncated");
    }
    const std::size_t header_checksum_offset = version == kFormatVersionV1 ? 28 : 36;
    const std::size_t payload_checksum_offset = version == kFormatVersionV1 ? 32 : 40;
    if (read_u32_be(bytes, header_checksum_offset) !=
        crc32c(bytes.first(header_checksum_offset))) {
        throw_decode(DecodeErrorCode::kHeaderChecksumMismatch, "record header checksum mismatch");
    }
    if (std::to_integer<std::uint8_t>(bytes[9]) != kSupportedFlags) {
        throw_decode(DecodeErrorCode::kUnsupportedFlags, "record flags are unsupported");
    }
    if (read_u16_be(bytes, 10) != 0) {
        throw_decode(DecodeErrorCode::kNonzeroReserved, "record reserved field is nonzero");
    }

    const auto operation_byte = std::to_integer<std::uint8_t>(bytes[8]);
    Operation operation;
    if (operation_byte == static_cast<std::uint8_t>(Operation::kPut)) {
        operation = Operation::kPut;
    } else if (operation_byte == static_cast<std::uint8_t>(Operation::kDelete)) {
        operation = Operation::kDelete;
    } else {
        throw_decode(DecodeErrorCode::kUnknownOperation, "record operation is unknown");
    }

    const std::uint64_t sequence = read_u64_be(bytes, 12);
    if (sequence == 0) {
        throw_decode(DecodeErrorCode::kInvalidSequence, "record sequence is zero");
    }
    const std::uint32_t key_length = read_u32_be(bytes, 20);
    const std::uint32_t value_length = read_u32_be(bytes, 24);
    const std::uint64_t expires_at_unix_ms =
        version == kFormatVersionV1 ? 0 : read_u64_be(bytes, 28);
    validate_record_fields(operation, key_length, value_length, expires_at_unix_ms);

    return RecordHeader{operation,
                        sequence,
                        expires_at_unix_ms,
                        header_size,
                        key_length,
                        value_length,
                        read_u32_be(bytes, payload_checksum_offset),
                        checked_record_size(header_size, key_length, value_length)};
}

DecodedRecord decode_record(std::span<const std::byte> bytes) {
    const RecordHeader header = decode_record_header(bytes);
    if (bytes.size() < header.encoded_size) {
        throw_decode(DecodeErrorCode::kTruncatedPayload, "record payload is truncated");
    }

    const std::size_t payload_size =
        static_cast<std::size_t>(header.key_length) + header.value_length;
    const auto payload = bytes.subspan(header.header_size, payload_size);
    if (crc32c(payload) != header.payload_checksum) {
        throw_decode(DecodeErrorCode::kPayloadChecksumMismatch, "record payload checksum mismatch");
    }

    const auto key_end = payload.begin() + header.key_length;
    Record record{header.operation,
                  header.sequence,
                  header.expires_at_unix_ms,
                  Bytes(payload.begin(), key_end),
                  Bytes(key_end, payload.end())};
    return DecodedRecord{std::move(record), header.encoded_size};
}

}  // namespace forgekv::storage
