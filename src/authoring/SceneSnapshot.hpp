#pragma once

// SceneSnapshot - serialisation de l'etat courant pour resync/collaboration.
//
// Quand ResourceManager est disponible, utilise le chemin canonique
// Scene::serialize, identique au coeur de SceneSerializer::saveToFile. Le mode
// sans resources existe pour tests/headless limites et ne doit pas devenir un
// format durable concurrent.

#include <string>

namespace saida {

class ResourceManager;
class Scene;

namespace authoring {

std::string serializeSceneSnapshot(Scene& scene, ResourceManager& resources);
std::string serializeSceneSnapshot(Scene& scene, ResourceManager* resources);

} // namespace authoring
} // namespace saida
