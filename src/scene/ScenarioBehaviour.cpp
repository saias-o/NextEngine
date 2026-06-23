#include "scene/ScenarioBehaviour.hpp"

#include "core/Reflection.hpp"
#include "scene/Blackboard.hpp"
#include "scene/Node.hpp"
#include "scene/SceneTree.hpp"

namespace ne {

Blackboard* ScenarioBehaviour::blackboard() const {
    if (node())
        if (Blackboard* b = node()->getBehaviour<Blackboard>()) return b;  // same node
    SceneTree* t = tree();
    if (!t) return nullptr;
    if (Blackboard* b = t->autoload<Blackboard>()) return b;
    if (Node* n = t->firstInGroup("blackboard"))
        if (Blackboard* b = n->getBehaviour<Blackboard>()) return b;
    return nullptr;
}

void ScenarioBehaviour::onReady() {
    if (autoStart) start();
}

void ScenarioBehaviour::start() {
    running_ = true;
    enterStep(0);
}

void ScenarioBehaviour::stop() {
    running_ = false;
    index_ = -1;
}

void ScenarioBehaviour::enterStep(int index) {
    if (!running_) return;
    if (index < 0 || index >= static_cast<int>(steps_.size())) {
        running_ = false;
        finished.emit();
        return;
    }
    index_ = index;
    stepReached.emit(index_);
    const Step& step = steps_[index_];
    switch (step.kind) {
        case Kind::Wait:
            waitLeft_ = step.seconds;  // counted down in onUpdate
            break;
        case Kind::Set:
            if (Blackboard* bb = blackboard()) bb->setNumber(step.key, step.value);
            enterStep(index_ + 1);  // instantaneous: advance immediately
            break;
        case Kind::Goto:
            enterStep(step.target);
            break;
    }
}

void ScenarioBehaviour::onUpdate(float dt) {
    if (!running_ || index_ < 0 || index_ >= static_cast<int>(steps_.size())) return;
    if (steps_[index_].kind != Kind::Wait) return;
    waitLeft_ -= dt;
    if (waitLeft_ <= 0.0f) enterStep(index_ + 1);
}

void ScenarioBehaviour::describe(reflect::TypeBuilder<ScenarioBehaviour>& t) {
    t.doc("Declarative timeline of steps (wait/set/goto), driving the Blackboard. "
          "Configure with {autoStart, steps[]} via configure_behaviour.");
    t.property("autoStart", &ScenarioBehaviour::autoStart);
    t.signal("stepReached", &ScenarioBehaviour::stepReached);
    t.signal("finished", &ScenarioBehaviour::finished);
    t.slot("start", &ScenarioBehaviour::start);
    t.slot("stop", &ScenarioBehaviour::stop);
}

void ScenarioBehaviour::save(nlohmann::json& j) const {
    j["autoStart"] = autoStart;
    nlohmann::json steps = nlohmann::json::array();
    for (const auto& s : steps_) {
        switch (s.kind) {
            case Kind::Wait: steps.push_back({{"wait", s.seconds}}); break;
            case Kind::Set: steps.push_back({{"set", {{"key", s.key}, {"value", s.value}}}}); break;
            case Kind::Goto: steps.push_back({{"goto", s.target}}); break;
        }
    }
    j["steps"] = std::move(steps);
}

void ScenarioBehaviour::load(const nlohmann::json& j) {
    autoStart = j.value("autoStart", true);
    steps_.clear();
    auto it = j.find("steps");
    if (it == j.end() || !it->is_array()) return;
    for (const auto& sj : *it) {
        Step s;
        if (sj.contains("wait")) {
            s.kind = Kind::Wait;
            s.seconds = sj["wait"].get<float>();
        } else if (sj.contains("set")) {
            s.kind = Kind::Set;
            s.key = sj["set"].value("key", std::string());
            s.value = sj["set"].value("value", 0.0);
        } else if (sj.contains("goto")) {
            s.kind = Kind::Goto;
            s.target = sj["goto"].get<int>();
        }
        steps_.push_back(std::move(s));
    }
}

} // namespace ne
