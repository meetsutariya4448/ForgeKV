#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace forgekv::storage {

// CRC-32C (Castagnoli), with initial and final XOR of 0xffffffff.
[[nodiscard]] std::uint32_t crc32c(std::span<const std::byte> bytes) noexcept;

}  // namespace forgekv::storage
