#include "scene/Scene.hpp"
#include "scene/SceneSettings.hpp"

namespace ne {

Scene::Scene() : Node("Scene") {
    createChild<Node>("Settings")->addBehaviour<SceneSettingsBehaviour>();
}

void Scene::update(float dt) {
    updateTree(dt);
}

SceneSettingsBehaviour* Scene::getActiveSettings() {
    SceneSettingsBehaviour* active = nullptr;
    traverse([&](Node& node, const glm::mat4&) {
        if (!active) {
            active = node.getBehaviour<SceneSettingsBehaviour>();
        }
    });
    return active;
}

} // namespace ne
