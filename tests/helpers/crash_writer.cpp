#include "forgekv/storage/engine.hpp"

#include <unistd.h>

#include <cstddef>
#include <exception>
#include <string_view>

namespace {

forgekv::storage::Bytes bytes(std::string_view value) {
    const auto* begin = reinterpret_cast<const std::byte*>(value.data());
    return forgekv::storage::Bytes(begin, begin + value.size());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        forgekv::storage::StorageOptions options;
        options.durability = forgekv::storage::DurabilityMode::kAlways;
        auto engine = forgekv::storage::StorageEngine::open(argv[1], options);
        static_cast<void>(engine.put(bytes("crash-key"), bytes("acknowledged")));
        ::_exit(0);
    } catch (const std::exception&) {
        ::_exit(1);
    }
}
