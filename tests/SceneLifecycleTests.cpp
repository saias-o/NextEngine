#include "scene/Behaviour.hpp"
#include "scene/Node.hpp"

#include <cassert>

namespace {

class ProbeBehaviour final : public ne::Behaviour {
public:
    ProbeBehaviour(bool& ready, bool& destroyed)
        : ready_(ready), destroyed_(destroyed) {}

    void onReady() override { ready_ = true; }
    void onDestroy() override { destroyed_ = true; }

private:
    bool& ready_;
    bool& destroyed_;
};

} // namespace

int main() {
    ne::Node node("LifecycleProbe");
    ne::Node another("IdentityProbe");
    assert(node.id() != ne::kNodeInvalid);
    assert(another.id() != ne::kNodeInvalid);
    assert(node.id() != another.id());

    const ne::NodeId preserved = node.id();
    node.assignSerializedId(preserved);
    assert(node.id() == preserved);
    node.regenerateId();
    assert(node.id() != preserved);

    bool ready = false;
    bool destroyed = false;
    auto* behaviour = node.addBehaviour<ProbeBehaviour>(ready, destroyed);

    node.updateTree(0.0f);
    assert(ready);
    assert(!destroyed);

    node.removeBehaviour(behaviour);
    assert(destroyed);
    assert(node.behaviourCount() == 0);
    return 0;
}
