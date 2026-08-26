#include "forgekv/network/tcp.hpp"

#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

namespace {
volatile std::sig_atomic_t stop_requested = 0;
void handle_signal(int) { stop_requested = 1; }

std::uint16_t parse_port(const std::string& text) {
    unsigned value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value > 65535U) {
        throw std::invalid_argument("invalid port: " + text);
    }
    return static_cast<std::uint16_t>(value);
}
}  // namespace

int main(int argc, char** argv) {
    forgekv::network::ServerConfig config;
    try {
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--host" && index + 1 < argc) config.bind_address = argv[++index];
            else if (argument == "--port" && index + 1 < argc) config.port = parse_port(argv[++index]);
            else if (argument == "--data" && index + 1 < argc) config.database_directory = argv[++index];
            else {
                std::cerr << "usage: forgekv-server [--host ADDRESS] [--port PORT] [--data PATH]\n";
                return 2;
            }
        }
        forgekv::network::TcpServer server(config);
        std::cout << "ForgeKV listening on " << config.bind_address << ':' << server.port()
                  << " using " << config.database_directory << '\n';
        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);
        std::atomic_bool finished = false;
        std::exception_ptr server_error;
        std::jthread thread([&](std::stop_token token) {
            try { server.serve(token); }
            catch (...) { server_error = std::current_exception(); }
            finished.store(true);
        });
        while (stop_requested == 0 && !finished.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        thread.request_stop();
        thread.join();
        if (server_error) std::rethrow_exception(server_error);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "forgekv-server: " << error.what() << '\n';
        return 1;
    }
}
