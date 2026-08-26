#pragma once
#include <cstdint>
namespace forgekv::storage {
struct RecordLocation {
    std::uint64_t segment_id;
    std::uint64_t record_offset;
    std::uint64_t value_offset;
    std::uint32_t key_length;
    std::uint32_t value_length;
    std::uint64_t sequence;
    std::uint32_t payload_checksum;
};
}  // namespace forgekv::storage
