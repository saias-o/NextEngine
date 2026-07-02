#include "scene/Behaviour.hpp"

#include "scene/Node.hpp"
#ifndef SAIDA_RHI_WEBGPU
#include "scene/SceneTree.hpp"
#endif

namespace saida {

Behaviour::~Behaviour() { cancelTimers(); }

SceneTree* Behaviour::tree() const { return node_ ? node_->tree() : nullptr; }

uint64_t Behaviour::wait(float seconds, std::function<void()> fn) {
#ifdef SAIDA_RHI_WEBGPU
    (void)seconds;
    (void)fn;
    return 0;
#else
    SceneTree* t = tree();
    if (!t) return 0;
    timerTree_ = t;
    uint64_t id = t->after(this, seconds, std::move(fn));
    timerIds_.push_back(id);
    return id;
#endif
}

uint64_t Behaviour::every(float interval, std::function<void()> fn) {
#ifdef SAIDA_RHI_WEBGPU
    (void)interval;
    (void)fn;
    return 0;
#else
    SceneTree* t = tree();
    if (!t) return 0;
    timerTree_ = t;
    uint64_t id = t->every(this, interval, std::move(fn));
    timerIds_.push_back(id);
    return id;
#endif
}

uint64_t Behaviour::tween(float duration, Easing easing, std::function<void(float)> fn) {
#ifdef SAIDA_RHI_WEBGPU
    (void)duration;
    (void)easing;
    (void)fn;
    return 0;
#else
    SceneTree* t = tree();
    if (!t) return 0;
    timerTree_ = t;
    uint64_t id = t->tween(this, duration, easing, std::move(fn));
    timerIds_.push_back(id);
    return id;
#endif
}

void Behaviour::cancelTimers() {
#ifndef SAIDA_RHI_WEBGPU
    if (timerTree_)
        for (uint64_t id : timerIds_) timerTree_->cancelTimer(id);
#endif
    timerIds_.clear();
    timerTree_ = nullptr;
}

} // namespace saida
