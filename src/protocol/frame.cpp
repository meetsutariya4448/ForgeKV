#include "forgekv/protocol/frame.hpp"

#include "forgekv/storage/crc32c.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <string_view>

namespace forgekv::protocol {
namespace {

constexpr std::array<std::byte, 4> kMagic = {
    std::byte{'F'}, std::byte{'K'}, std::byte{'V'}, std::byte{'P'}};
constexpr std::uint16_t kProtocolVersion = 1;

[[noreturn]] void fail(ProtocolErrorCode code, std::string_view message) {
    throw ProtocolError(code, std::string(message));
}

std::uint16_t read_u16(std::span<const std::byte> data, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(data[offset])) << 8U) |
        std::to_integer<std::uint8_t>(data[offset + 1]));
}

std::uint32_t read_u32(std::span<const std::byte> data, std::size_t offset) noexcept {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value = (value << 8U) | std::to_integer<std::uint8_t>(data[offset + index]);
    }
    return value;
}

std::uint64_t read_u64(std::span<const std::byte> data, std::size_t offset) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value = (value << 8U) | std::to_integer<std::uint8_t>(data[offset + index]);
    }
    return value;
}

void write_u16(std::span<std::byte> data, std::size_t offset, std::uint16_t value) noexcept {
    data[offset] = static_cast<std::byte>((value >> 8U) & 0xffU);
    data[offset + 1] = static_cast<std::byte>(value & 0xffU);
}

void write_u32(std::span<std::byte> data, std::size_t offset, std::uint32_t value) noexcept {
    for (std::size_t index = 0; index < 4; ++index) {
        const auto shift = static_cast<unsigned>((3U - index) * 8U);
        data[offset + index] = static_cast<std::byte>((value >> shift) & 0xffU);
    }
}

void write_u64(std::span<std::byte> data, std::size_t offset, std::uint64_t value) noexcept {
    for (std::size_t index = 0; index < 8; ++index) {
        const auto shift = static_cast<unsigned>((7U - index) * 8U);
        data[offset + index] = static_cast<std::byte>((value >> shift) & 0xffU);
    }
}

FrameKind parse_kind(std::uint8_t value) {
    if (value == 1) return FrameKind::kRequest;
    if (value == 2) return FrameKind::kResponse;
    fail(ProtocolErrorCode::kUnknownKind, "unknown frame kind");
}

Opcode parse_opcode(std::uint8_t value) {
    if (value >= 1 && value <= 8) return static_cast<Opcode>(value);
    fail(ProtocolErrorCode::kUnknownOpcode, "unknown opcode");
}

Status parse_status(std::uint16_t value) {
    if (value <= 6) return static_cast<Status>(value);
    fail(ProtocolErrorCode::kUnknownStatus, "unknown status");
}

std::size_t checked_size(std::uint32_t key_length, std::uint32_t value_length) {
    if (key_length > storage::kMaxKeySize) {
        fail(ProtocolErrorCode::kInvalidKeyLength, "key length exceeds protocol limit");
    }
    if (value_length > storage::kMaxValueSize) {
        fail(ProtocolErrorCode::kInvalidValueLength, "value length exceeds protocol limit");
    }
    const std::uint64_t total = kFrameHeaderSize + static_cast<std::uint64_t>(key_length) +
                                static_cast<std::uint64_t>(value_length);
    if (total > kMaxFrameSize || total > std::numeric_limits<std::size_t>::max()) {
        fail(ProtocolErrorCode::kInvalidFrameSize, "frame size is invalid");
    }
    return static_cast<std::size_t>(total);
}

struct Header {
    FrameKind kind;
    Opcode opcode;
    Status status;
    std::uint64_t request_id;
    std::uint32_t key_length;
    std::uint32_t value_length;
    std::uint32_t payload_checksum;
    std::size_t encoded_size;
};

Header decode_header(std::span<const std::byte> bytes) {
    if (bytes.size() < kFrameHeaderSize) fail(ProtocolErrorCode::kTruncatedHeader, "truncated header");
    if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        fail(ProtocolErrorCode::kBadMagic, "frame magic does not match FKVP");
    }
    if (read_u16(bytes, 4) != kProtocolVersion) {
        fail(ProtocolErrorCode::kUnsupportedVersion, "unsupported protocol version");
    }
    if (read_u16(bytes, 6) != kFrameHeaderSize) {
        fail(ProtocolErrorCode::kUnsupportedHeaderSize, "unsupported header size");
    }
    if (read_u32(bytes, 32) != storage::crc32c(bytes.first(32))) {
        fail(ProtocolErrorCode::kHeaderChecksumMismatch, "header checksum mismatch");
    }
    if (read_u16(bytes, 12) != 0) fail(ProtocolErrorCode::kUnsupportedFlags, "unsupported flags");
    if (read_u16(bytes, 14) != 0) fail(ProtocolErrorCode::kNonzeroReserved, "reserved field is nonzero");
    const std::uint64_t request_id = read_u64(bytes, 16);
    if (request_id == 0) fail(ProtocolErrorCode::kInvalidRequestId, "request id is zero");
    const std::uint32_t key_length = read_u32(bytes, 24);
    const std::uint32_t value_length = read_u32(bytes, 28);
    return Header{parse_kind(std::to_integer<std::uint8_t>(bytes[8])),
                  parse_opcode(std::to_integer<std::uint8_t>(bytes[9])),
                  parse_status(read_u16(bytes, 10)), request_id, key_length, value_length,
                  read_u32(bytes, 36), checked_size(key_length, value_length)};
}

}  // namespace

ProtocolError::ProtocolError(ProtocolErrorCode code, std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}
ProtocolErrorCode ProtocolError::code() const noexcept { return code_; }

Bytes encode_frame(const Frame& frame) {
    if (frame.request_id == 0) throw std::invalid_argument("request id must be nonzero");
    if (frame.key.size() > storage::kMaxKeySize || frame.value.size() > storage::kMaxValueSize) {
        throw std::invalid_argument("frame payload exceeds protocol limits");
    }
    const auto key_length = static_cast<std::uint32_t>(frame.key.size());
    const auto value_length = static_cast<std::uint32_t>(frame.value.size());
    Bytes encoded(checked_size(key_length, value_length));
    std::copy(kMagic.begin(), kMagic.end(), encoded.begin());
    write_u16(encoded, 4, kProtocolVersion);
    write_u16(encoded, 6, static_cast<std::uint16_t>(kFrameHeaderSize));
    encoded[8] = static_cast<std::byte>(static_cast<std::uint8_t>(frame.kind));
    encoded[9] = static_cast<std::byte>(static_cast<std::uint8_t>(frame.opcode));
    write_u16(encoded, 10, static_cast<std::uint16_t>(frame.status));
    write_u16(encoded, 12, 0);
    write_u16(encoded, 14, 0);
    write_u64(encoded, 16, frame.request_id);
    write_u32(encoded, 24, key_length);
    write_u32(encoded, 28, value_length);
    write_u32(encoded, 32, storage::crc32c(std::span<const std::byte>(encoded).first(32)));
    auto payload = std::span<std::byte>(encoded).subspan(kFrameHeaderSize);
    std::copy(frame.key.begin(), frame.key.end(), payload.begin());
    std::copy(frame.value.begin(), frame.value.end(), payload.subspan(frame.key.size()).begin());
    write_u32(encoded, 36, storage::crc32c(payload));
    return encoded;
}

Frame decode_frame(std::span<const std::byte> bytes) {
    const Header header = decode_header(bytes);
    if (bytes.size() < header.encoded_size) fail(ProtocolErrorCode::kTruncatedPayload, "truncated payload");
    if (bytes.size() != header.encoded_size) {
        fail(ProtocolErrorCode::kInvalidFrameSize, "frame contains trailing bytes");
    }
    const std::size_t payload_size = static_cast<std::size_t>(header.key_length) + header.value_length;
    const auto payload = bytes.subspan(kFrameHeaderSize, payload_size);
    if (storage::crc32c(payload) != header.payload_checksum) {
        fail(ProtocolErrorCode::kPayloadChecksumMismatch, "payload checksum mismatch");
    }
    const auto key_end = payload.begin() + header.key_length;
    return Frame{header.kind, header.opcode, header.status, header.request_id,
                 Bytes(payload.begin(), key_end), Bytes(key_end, payload.end())};
}

std::vector<Frame> FrameParser::feed(std::span<const std::byte> bytes) {
    if (failed_) fail(ProtocolErrorCode::kParserFailed, "parser is unusable after an error");
    std::vector<Frame> frames;
    try {
        while (!bytes.empty()) {
            const std::size_t target = header_decoded_ ? expected_size_ : kFrameHeaderSize;
            const std::size_t take = std::min(target - buffer_.size(), bytes.size());
            const auto prefix = bytes.first(take);
            buffer_.insert(buffer_.end(), prefix.begin(), prefix.end());
            bytes = bytes.subspan(take);
            if (buffer_.size() == kFrameHeaderSize && !header_decoded_) {
                expected_size_ = decode_header(buffer_).encoded_size;
                header_decoded_ = true;
            }
            if (header_decoded_ && buffer_.size() == expected_size_) {
                frames.push_back(decode_frame(buffer_));
                buffer_.clear();
                expected_size_ = kFrameHeaderSize;
                header_decoded_ = false;
            }
        }
    } catch (...) {
        failed_ = true;
        throw;
    }
    return frames;
}

std::size_t FrameParser::buffered_bytes() const noexcept { return buffer_.size(); }
bool FrameParser::failed() const noexcept { return failed_; }

bool request_semantics_valid(const Frame& frame) noexcept {
    if (frame.kind != FrameKind::kRequest || frame.status != Status::kOk) return false;
    if (frame.opcode == Opcode::kPing || frame.opcode == Opcode::kStats) {
        return frame.key.empty() && frame.value.empty();
    }
    if (frame.key.empty()) return false;
    switch (frame.opcode) {
        case Opcode::kPut:
            return true;
        case Opcode::kPutEx: {
            if (frame.value.size() < kTtlPayloadPrefixSize) return false;
            const std::uint64_t ttl_ms = read_u64(frame.value, 0);
            return ttl_ms != 0 &&
                   ttl_ms <= static_cast<std::uint64_t>(
                                 std::numeric_limits<std::chrono::milliseconds::rep>::max());
        }
        case Opcode::kGet:
        case Opcode::kDelete:
        case Opcode::kExists:
        case Opcode::kTtl:
            return frame.value.empty();
        case Opcode::kPing:
        case Opcode::kStats:
            return false;
    }
    return false;
}

Bytes encode_put_ex_payload(std::uint64_t ttl_ms, std::span<const std::byte> value) {
    if (ttl_ms == 0 ||
        ttl_ms > static_cast<std::uint64_t>(
                     std::numeric_limits<std::chrono::milliseconds::rep>::max())) {
        throw std::invalid_argument("TTL milliseconds are outside protocol bounds");
    }
    if (value.size() > storage::kMaxValueSize - kTtlPayloadPrefixSize) {
        throw std::invalid_argument("PUTEX value exceeds protocol limit");
    }
    Bytes payload(kTtlPayloadPrefixSize + value.size());
    write_u64(payload, 0, ttl_ms);
    std::copy(value.begin(), value.end(), payload.begin() + kTtlPayloadPrefixSize);
    return payload;
}

PutExPayload decode_put_ex_payload(std::span<const std::byte> payload) {
    if (payload.size() < kTtlPayloadPrefixSize) {
        throw std::invalid_argument("PUTEX payload is missing TTL prefix");
    }
    const std::uint64_t ttl_ms = read_u64(payload, 0);
    if (ttl_ms == 0 ||
        ttl_ms > static_cast<std::uint64_t>(
                     std::numeric_limits<std::chrono::milliseconds::rep>::max())) {
        throw std::invalid_argument("TTL milliseconds are outside protocol bounds");
    }
    return PutExPayload{ttl_ms, Bytes(payload.begin() + kTtlPayloadPrefixSize, payload.end())};
}

Bytes encode_ttl_payload(std::uint64_t remaining_ms) {
    Bytes payload(kTtlPayloadPrefixSize);
    write_u64(payload, 0, remaining_ms);
    return payload;
}

std::uint64_t decode_ttl_payload(std::span<const std::byte> payload) {
    if (payload.size() != kTtlPayloadPrefixSize) {
        throw std::invalid_argument("TTL response payload must contain eight bytes");
    }
    return read_u64(payload, 0);
}

}  // namespace forgekv::protocol
