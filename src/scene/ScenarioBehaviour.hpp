#pragma once

#include "core/ReflectionFwd.hpp"
#include "core/Signal.hpp"
#include "scene/Behaviour.hpp"

#include "nlohmann/json_fwd.hpp"

#include <string>
#include <vector>

namespace ne {

class Blackboard;

// A declarative timeline of gameplay steps (cutscene / scripted sequence), run in
// order. Cross-behaviour effects go through the Blackboard (which a StateMachine
// can react to), keeping everything in the data-driven model. Emits stepReached
// and finished. Authored via configure_behaviour:
//   { "autoStart":true,
//     "steps":[ {"wait":2.0}, {"set":{"key":"doorOpen","value":1}}, {"goto":0} ] }
class ScenarioBehaviour : public Behaviour {
public:
    void onReady() override;
    void onUpdate(float dt) override;

    void start();  // slot
    void stop();   // slot

    Signal<int> stepReached;  // index of the step just entered
    Signal<> finished;        // emitted when the last step completes

    bool autoStart = true;  // reflected property

    static constexpr const char* reflectName() { return "Scenario"; }
    const char* typeName() const override { return "Scenario"; }
    static void describe(reflect::TypeBuilder<ScenarioBehaviour>& t);
    void save(nlohmann::json& j) const override;
    void load(const nlohmann::json& j) override;

private:
    enum class Kind { Wait, Set, Goto };
    struct Step {
        Kind kind = Kind::Wait;
        float seconds = 0.0f;  // Wait
        std::string key;       // Set
        double value = 0.0;    // Set
        int target = 0;        // Goto (step index)
    };

    Blackboard* blackboard() const;
    void enterStep(int index);

    std::vector<Step> steps_;
    int index_ = -1;
    bool running_ = false;
    float waitLeft_ = 0.0f;
};

} // namespace ne
