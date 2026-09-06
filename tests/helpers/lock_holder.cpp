#include "forgekv/storage/engine.hpp"

#include <unistd.h>

#include <array>
#include <cstddef>
#include <exception>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        auto engine = forgekv::storage::StorageEngine::open(argv[1]);
        constexpr std::array<std::byte, 1> ready{std::byte{'R'}};
        if (::write(STDOUT_FILENO, ready.data(), ready.size()) !=
            static_cast<ssize_t>(ready.size())) {
            return 3;
        }
        for (;;) ::pause();
    } catch (const std::exception&) {
        return 1;
    }
}
