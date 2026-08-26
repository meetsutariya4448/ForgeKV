#include "forgekv/storage/record.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto* bytes = reinterpret_cast<const std::byte*>(data);
    const std::span<const std::byte> input(bytes, size);

    try {
        static_cast<void>(forgekv::storage::decode_record(input));
    } catch (const forgekv::storage::DecodeError&) {
        // Invalid records are expected inputs. Other exception types should remain visible.
    }
    return 0;
}
