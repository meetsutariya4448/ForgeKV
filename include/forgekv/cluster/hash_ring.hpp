#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace forgekv::cluster {

struct Node {
    std::string id;
    std::string host;
    std::uint16_t port = 0;

    bool operator==(const Node&) const = default;
};

class RoutingError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ConsistentHashRing {
public:
    explicit ConsistentHashRing(std::size_t virtual_nodes = 128);

    void set_nodes(std::vector<Node> nodes);
    void add_node(Node node);
    [[nodiscard]] bool remove_node(std::string_view node_id);
    [[nodiscard]] const Node& primary(std::span<const std::byte> key) const;
    [[nodiscard]] std::vector<Node> placement(std::span<const std::byte> key,
                                              std::size_t replication_factor) const;
    [[nodiscard]] Node route(
        std::span<const std::byte> key,
        const std::function<bool(const Node&)>& reachable) const;
    [[nodiscard]] std::size_t node_count() const noexcept;
    [[nodiscard]] std::size_t virtual_nodes() const noexcept;

    [[nodiscard]] static std::uint64_t hash(std::span<const std::byte> value) noexcept;
    [[nodiscard]] static double remap_fraction(const ConsistentHashRing& before,
                                               const ConsistentHashRing& after,
                                               const std::vector<std::string>& keys);

private:
    struct Token {
        std::uint64_t value;
        std::size_t node_index;
    };

    [[nodiscard]] std::vector<Token> build_tokens(const std::vector<Node>& nodes) const;
    [[nodiscard]] std::size_t first_token(std::span<const std::byte> key) const;

    std::size_t virtual_nodes_;
    std::vector<Node> nodes_;
    std::vector<Token> tokens_;
};

}  // namespace forgekv::cluster
