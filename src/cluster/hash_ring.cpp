#include "forgekv/cluster/hash_ring.hpp"

#include <algorithm>
#include <limits>
#include <set>

namespace forgekv::cluster {
namespace {

std::span<const std::byte> as_bytes(std::string_view value) {
    return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
}

}  // namespace

ConsistentHashRing::ConsistentHashRing(std::size_t virtual_nodes)
    : virtual_nodes_(virtual_nodes) {
    if (virtual_nodes_ == 0) throw std::invalid_argument("virtual-node count must be positive");
}

void ConsistentHashRing::set_nodes(std::vector<Node> nodes) {
    std::sort(nodes.begin(), nodes.end(), [](const Node& left, const Node& right) {
        return left.id < right.id;
    });
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        if (nodes[index].id.empty()) throw std::invalid_argument("node id must not be empty");
        if (index != 0 && nodes[index - 1].id == nodes[index].id) {
            throw std::invalid_argument("node ids must be unique");
        }
    }
    nodes_ = std::move(nodes);
    rebuild();
}

void ConsistentHashRing::add_node(Node node) {
    if (node.id.empty()) throw std::invalid_argument("node id must not be empty");
    if (std::any_of(nodes_.begin(), nodes_.end(), [&](const Node& existing) {
            return existing.id == node.id;
        })) {
        throw std::invalid_argument("node id already exists");
    }
    nodes_.push_back(std::move(node));
    std::sort(nodes_.begin(), nodes_.end(), [](const Node& left, const Node& right) {
        return left.id < right.id;
    });
    rebuild();
}

bool ConsistentHashRing::remove_node(std::string_view node_id) {
    const auto old_size = nodes_.size();
    std::erase_if(nodes_, [&](const Node& node) { return node.id == node_id; });
    if (nodes_.size() == old_size) return false;
    rebuild();
    return true;
}

const Node& ConsistentHashRing::primary(std::span<const std::byte> key) const {
    if (nodes_.empty()) throw RoutingError("cannot route without cluster nodes");
    return nodes_[tokens_[first_token(key)].node_index];
}

std::vector<Node> ConsistentHashRing::placement(std::span<const std::byte> key,
                                                std::size_t replication_factor) const {
    if (replication_factor == 0) throw std::invalid_argument("replication factor must be positive");
    if (nodes_.empty()) throw RoutingError("cannot place replicas without cluster nodes");
    const std::size_t wanted = std::min(replication_factor, nodes_.size());
    std::vector<Node> result;
    result.reserve(wanted);
    std::set<std::size_t> selected;
    std::size_t token_index = first_token(key);
    while (result.size() < wanted) {
        const std::size_t node_index = tokens_[token_index].node_index;
        if (selected.insert(node_index).second) result.push_back(nodes_[node_index]);
        token_index = (token_index + 1) % tokens_.size();
    }
    return result;
}

Node ConsistentHashRing::route(
    std::span<const std::byte> key,
    const std::function<bool(const Node&)>& reachable) const {
    const Node& selected = primary(key);
    if (!reachable(selected)) {
        throw RoutingError("selected node is unavailable: " + selected.id);
    }
    return selected;
}

std::size_t ConsistentHashRing::node_count() const noexcept { return nodes_.size(); }
std::size_t ConsistentHashRing::virtual_nodes() const noexcept { return virtual_nodes_; }

std::uint64_t ConsistentHashRing::hash(std::span<const std::byte> value) noexcept {
    std::uint64_t result = 14695981039346656037ULL;
    for (const std::byte byte : value) {
        result ^= std::to_integer<std::uint8_t>(byte);
        result *= 1099511628211ULL;
    }
    result ^= result >> 32U;
    result *= 0xd6e8feb86659fd93ULL;
    result ^= result >> 32U;
    return result;
}

double ConsistentHashRing::remap_fraction(const ConsistentHashRing& before,
                                          const ConsistentHashRing& after,
                                          const std::vector<std::string>& keys) {
    if (keys.empty()) return 0;
    std::size_t changed = 0;
    for (const auto& key : keys) {
        if (before.primary(as_bytes(key)).id != after.primary(as_bytes(key)).id) ++changed;
    }
    return static_cast<double>(changed) / static_cast<double>(keys.size());
}

void ConsistentHashRing::rebuild() {
    tokens_.clear();
    if (nodes_.empty()) return;
    if (virtual_nodes_ > std::numeric_limits<std::size_t>::max() / nodes_.size()) {
        throw std::length_error("hash ring token count is not representable");
    }
    tokens_.reserve(virtual_nodes_ * nodes_.size());
    for (std::size_t node_index = 0; node_index < nodes_.size(); ++node_index) {
        for (std::size_t virtual_node = 0; virtual_node < virtual_nodes_; ++virtual_node) {
            const std::string identity =
                nodes_[node_index].id + "#" + std::to_string(virtual_node);
            tokens_.push_back(Token{hash(as_bytes(identity)), node_index});
        }
    }
    std::sort(tokens_.begin(), tokens_.end(), [](const Token& left, const Token& right) {
        if (left.value != right.value) return left.value < right.value;
        return left.node_index < right.node_index;
    });
}

std::size_t ConsistentHashRing::first_token(std::span<const std::byte> key) const {
    const std::uint64_t key_hash = hash(key);
    const auto iterator = std::lower_bound(
        tokens_.begin(), tokens_.end(), key_hash,
        [](const Token& token, std::uint64_t value) { return token.value < value; });
    return iterator == tokens_.end() ? 0 : static_cast<std::size_t>(iterator - tokens_.begin());
}

}  // namespace forgekv::cluster
