# Cluster Routing

The cluster library implements deterministic consistent hashing after the single-node path. Nodes
have stable IDs plus host/port metadata. Membership is sorted by ID, and each node contributes a
configurable number of virtual-node tokens. Token and key hashes use the same deterministic
64-bit FNV-derived mixer; no `std::hash` implementation detail crosses processes.

Primary placement selects the first token clockwise from a key hash. RF placement walks clockwise
and returns distinct physical nodes until `min(RF, membership)` nodes are selected. Membership add
and remove rebuild the immutable token vector. Tests compare independently constructed rings,
verify that keys changed by an add move only to the added node, and that keys changed by a removal
were formerly owned by the removed node. `remap_fraction` measures changed primary assignments over
an explicit key sample.

`route` checks only the selected primary. If it is unreachable, routing fails; it does not silently
choose a replica. This is intentional because safe failover needs authority and consistency rules
that consistent hashing cannot provide.

Complexity is `O(NV log(NV))` to rebuild for `N` nodes and `V` virtual nodes, `O(log(NV))` primary
lookup, and `O(log(NV) + scanned tokens)` RF placement. The current ring is a reusable library
component and is not connected to `forgekv-server`, membership discovery, rebalancing data transfer,
or a control plane.
