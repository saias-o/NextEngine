#pragma once

// Le mode sans ressources reste limite au headless pour eviter un second format.

#include <string>

#include <nlohmann/json_fwd.hpp>

namespace saida {

class ResourceManager;
class Scene;

namespace authoring {

std::string serializeSceneSnapshot(Scene& scene, ResourceManager& resources);
std::string serializeSceneSnapshot(Scene& scene, ResourceManager* resources);

// Rebuilds only resource-free scene data; mesh, material and behaviour refs stay null.
bool deserializeSceneSnapshot(const nlohmann::json& doc, Scene& out,
                              std::string* error = nullptr);
bool deserializeSceneSnapshot(const std::string& text, Scene& out,
                              std::string* error = nullptr);

} // namespace authoring
} // namespace saida
