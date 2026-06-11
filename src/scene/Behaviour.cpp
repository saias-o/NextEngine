#include "scene/Behaviour.hpp"

#include "scene/Node.hpp"
#include "scene/SceneTree.hpp"

namespace ne {

SceneTree* Behaviour::tree() const { return node_ ? node_->tree() : nullptr; }

uint64_t Behaviour::wait(float seconds, std::function<void()> fn) {
    SceneTree* t = tree();
    return t ? t->after(node_, seconds, std::move(fn)) : 0;
}

uint64_t Behaviour::every(float interval, std::function<void()> fn) {
    SceneTree* t = tree();
    return t ? t->every(node_, interval, std::move(fn)) : 0;
}

uint64_t Behaviour::tween(float duration, Easing easing, std::function<void(float)> fn) {
    SceneTree* t = tree();
    return t ? t->tween(node_, duration, easing, std::move(fn)) : 0;
}

} // namespace ne
