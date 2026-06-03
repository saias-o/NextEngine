#pragma once

#include "scene/Behaviour.hpp"
#include <glm/glm.hpp>

namespace ne {

class SceneSettingsBehaviour : public Behaviour {
public:
    glm::vec4 ambientLight{0.04f, 0.04f, 0.05f, 0.0f};
    glm::vec4 clearColor{0.1f, 0.1f, 0.1f, 1.0f};
    bool enablePostProcessing = true;

    void onDrawInspector() override;

    const char* typeName() const override { return "SceneSettings"; }
    void save(nlohmann::json& j) const override;
    void load(const nlohmann::json& j) override;
};

} // namespace ne
