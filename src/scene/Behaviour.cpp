#include "scene/Behaviour.hpp"

#include "scene/Node.hpp"
#include "scene/SceneTree.hpp"

namespace saida {

Behaviour::~Behaviour() { cancelTimers(); }

SceneTree* Behaviour::tree() const { return node_ ? node_->tree() : nullptr; }

uint64_t Behaviour::wait(float seconds, std::function<void()> fn) {
    SceneTree* t = tree();
    if (!t) return 0;
    timerTree_ = t;
    uint64_t id = t->after(this, seconds, std::move(fn));
    timerIds_.push_back(id);
    return id;
}

uint64_t Behaviour::every(float interval, std::function<void()> fn) {
    SceneTree* t = tree();
    if (!t) return 0;
    timerTree_ = t;
    uint64_t id = t->every(this, interval, std::move(fn));
    timerIds_.push_back(id);
    return id;
}

uint64_t Behaviour::tween(float duration, Easing easing, std::function<void(float)> fn) {
    SceneTree* t = tree();
    if (!t) return 0;
    timerTree_ = t;
    uint64_t id = t->tween(this, duration, easing, std::move(fn));
    timerIds_.push_back(id);
    return id;
}

void Behaviour::cancelTimers() {
    if (timerTree_)
        for (uint64_t id : timerIds_) timerTree_->cancelTimer(id);
    timerIds_.clear();
    timerTree_ = nullptr;
}

} // namespace saida
