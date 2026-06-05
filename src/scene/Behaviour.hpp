#pragma once

#include "nlohmann/json_fwd.hpp"  // forward decl only; full header in .cpp

namespace ne {

class Node;

// A piece of logic attached to a Node (cf. Unity MonoBehaviour / Godot script).
// The owning node calls onReady() once before the first update, then onUpdate()
// every frame. A behaviour can reach its node (and thus its transform, children,
// siblings) via node().
//
// To make a behaviour serializable, override typeName() (a stable id), save()
// and load(), and register a factory with the BehaviourRegistry. Behaviours
// without a typeName are skipped on save.
class Behaviour {
public:
    virtual ~Behaviour() = default;
    virtual void onReady() {}
    virtual void onUpdate(float /*dt*/) {}
    virtual void onDrawInspector() {}

    virtual const char* typeName() const { return nullptr; }
    virtual void save(nlohmann::json&) const {}
    virtual void load(const nlohmann::json&) {}

    Node* node() const { return node_; }

    bool enabled() const { return enabled_; }
    void setEnabled(bool enabled) { enabled_ = enabled; }

private:
    friend class Node;  // sets node_ on attach, drives ready_/lifecycle
    friend class Scene; // flattens behaviours and drives lifecycle
    Node* node_ = nullptr;
    bool ready_ = false;
    bool enabled_ = true;
};

} // namespace ne
