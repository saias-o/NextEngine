#include "scene/SpawnerBehaviour.hpp"

#include "scene/Node.hpp"
#include "scene/SceneTree.hpp"

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

namespace ne {

void SpawnerBehaviour::onReady() {
    if (interval > 0.0f)
        every(interval, [this] { spawn(); });
}

void SpawnerBehaviour::spawn() {
    if (scenePath.empty()) return;
    SceneTree* t = tree();
    if (!t) return;

    Node* n = t->instantiate(scenePath);
    if (!n) return;
    n->transform().position = glm::vec3(node()->worldTransform()[3]);  // at this node

    if (lifetime > 0.0f)
        t->after(n, lifetime, [n] { n->queueFree(); });  // owned by n → cancels if it dies first
}

void SpawnerBehaviour::save(nlohmann::json& json) const {
    json["scenePath"] = scenePath;
    json["interval"] = interval;
    json["lifetime"] = lifetime;
}

void SpawnerBehaviour::load(const nlohmann::json& json) {
    if (json.contains("scenePath")) scenePath = json["scenePath"].get<std::string>();
    if (json.contains("interval")) interval = json["interval"].get<float>();
    if (json.contains("lifetime")) lifetime = json["lifetime"].get<float>();
}

} // namespace ne
