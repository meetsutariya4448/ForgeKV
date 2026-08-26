#include "forgekv/network/tcp.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace {
forgekv::protocol::Bytes bytes(std::string_view value) {
    const auto* begin = reinterpret_cast<const std::byte*>(value.data());
    return forgekv::protocol::Bytes(begin, begin + value.size());
}
std::uint16_t parse_port(const std::string& text) {
    unsigned value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value > 65535U) {
        throw std::invalid_argument("invalid port: " + text);
    }
    return static_cast<std::uint16_t>(value);
}
forgekv::protocol::Opcode parse_opcode(const std::string& command) {
    if (command == "PUT") return forgekv::protocol::Opcode::kPut;
    if (command == "GET") return forgekv::protocol::Opcode::kGet;
    if (command == "DELETE") return forgekv::protocol::Opcode::kDelete;
    if (command == "EXISTS") return forgekv::protocol::Opcode::kExists;
    throw std::invalid_argument("unknown command: " + command);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "usage: forgekv-cli HOST PORT PUT KEY VALUE\n"
                     "       forgekv-cli HOST PORT GET|DELETE|EXISTS KEY\n";
        return 2;
    }
    try {
        const std::string command = argv[3];
        const auto opcode = parse_opcode(command);
        if ((opcode == forgekv::protocol::Opcode::kPut && argc != 6) ||
            (opcode != forgekv::protocol::Opcode::kPut && argc != 5)) {
            throw std::invalid_argument("wrong argument count for " + command);
        }
        const auto value = opcode == forgekv::protocol::Opcode::kPut ? bytes(argv[5]) : bytes("");
        forgekv::protocol::Frame request{forgekv::protocol::FrameKind::kRequest, opcode,
                                         forgekv::protocol::Status::kOk, 1, bytes(argv[4]), value};
        auto client = forgekv::network::TcpClient::connect(argv[1], parse_port(argv[2]));
        const auto response = client.request(request);
        if (response.status == forgekv::protocol::Status::kNotFound) {
            std::cout << "NOT_FOUND\n";
            return 3;
        }
        if (response.status != forgekv::protocol::Status::kOk) {
            std::cerr << "ERROR ";
            std::cerr.write(reinterpret_cast<const char*>(response.value.data()),
                            static_cast<std::streamsize>(response.value.size()));
            std::cerr << '\n';
            return 1;
        }
        if (opcode == forgekv::protocol::Opcode::kGet) {
            std::cout.write(reinterpret_cast<const char*>(response.value.data()),
                            static_cast<std::streamsize>(response.value.size()));
            std::cout << '\n';
        } else if (opcode == forgekv::protocol::Opcode::kExists) {
            std::cout << ((!response.value.empty() && response.value.front() == std::byte{1}) ? "true"
                                                                                              : "false")
                      << '\n';
        } else {
            std::cout << "OK\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "forgekv-cli: " << error.what() << '\n';
        return 1;
    }
}
