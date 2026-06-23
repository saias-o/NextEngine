#pragma once

#include "core/Signal.hpp"
#include "scene/NodeId.hpp"

#include <string>
#include <vector>

namespace ne {

class Node;

// One data-driven signal→slot link, as stored in a scene's "connections" block.
struct SignalConnectionDef {
    NodeId from = kNodeInvalid;
    std::string signal;          // reflected signal name on the `from` node/behaviour
    NodeId to = kNodeInvalid;
    std::string slot;            // reflected slot name on the `to` node/behaviour
};

// Wires reflected named signals to reflected named slots across a node subtree,
// driven entirely by data (no C++ glue). This is the "brancher des signaux →
// behaviours" layer: an emitter declared in scene JSON triggers a target's slot.
//
// Lifetime-safe both ways:
//   • emitter side — guarded by the Signal's weak control block (Connection);
//   • target side  — re-resolved by NodeId at emit time, so a freed target node
//     is a silent no-op (never a dangling call).
// All Connections drop when this object is cleared/destroyed (owned by the Scene).
class SignalWiring {
public:
    // (Re)wire every def against the subtree rooted at `root`. Clears any prior
    // connections first, so calling twice is safe.
    void apply(Node& root, const std::vector<SignalConnectionDef>& defs);
    void clear() { connections_.clear(); }
    std::size_t count() const { return connections_.size(); }

private:
    std::vector<Connection> connections_;
};

} // namespace ne
