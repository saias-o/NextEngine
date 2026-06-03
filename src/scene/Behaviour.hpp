#pragma once

namespace ne {

class Node;

// A piece of logic attached to a Node (cf. Unity MonoBehaviour / Godot script).
// The owning node calls onReady() once before the first update, then onUpdate()
// every frame. A behaviour can reach its node (and thus its transform, children,
// siblings) via node().
class Behaviour {
public:
    virtual ~Behaviour() = default;
    virtual void onReady() {}
    virtual void onUpdate(float /*dt*/) {}
    virtual void onDrawInspector() {}

    Node* node() const { return node_; }

private:
    friend class Node;  // sets node_ on attach, drives ready_/lifecycle
    Node* node_ = nullptr;
    bool ready_ = false;
};

} // namespace ne
