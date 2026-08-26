#include "forgekv/protocol/frame.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0) return 0;
    const std::size_t chunk_size = static_cast<std::size_t>(data[0] % 64U) + 1U;
    const auto* bytes = reinterpret_cast<const std::byte*>(data + 1);
    std::span<const std::byte> remaining(bytes, size - 1);
    forgekv::protocol::FrameParser parser;
    try {
        while (!remaining.empty()) {
            const std::size_t take = std::min(chunk_size, remaining.size());
            static_cast<void>(parser.feed(remaining.first(take)));
            remaining = remaining.subspan(take);
        }
    } catch (const forgekv::protocol::ProtocolError&) {
        // Malformed input is expected. Memory errors and other exceptions remain visible.
    }
    return 0;
}
