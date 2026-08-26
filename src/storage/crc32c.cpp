#include "forgekv/storage/crc32c.hpp"

namespace forgekv::storage {

std::uint32_t crc32c(std::span<const std::byte> bytes) noexcept {
    constexpr std::uint32_t kReversedCastagnoliPolynomial = 0x82f63b78U;
    std::uint32_t crc = 0xffffffffU;

    for (const std::byte byte : bytes) {
        crc ^= std::to_integer<std::uint8_t>(byte);
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t low_bit_mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (kReversedCastagnoliPolynomial & low_bit_mask);
        }
    }

    return crc ^ 0xffffffffU;
}

}  // namespace forgekv::storage
