#pragma once

// SceneSnapshot - serialisation de l'etat courant pour resync/collaboration.
//
// Quand ResourceManager est disponible, utilise le chemin canonique
// Scene::serialize, identique au coeur de SceneSerializer::saveToFile. Le mode
// sans resources existe pour tests/headless limites et ne doit pas devenir un
// format durable concurrent.

#include <string>

#include <nlohmann/json_fwd.hpp>

namespace saida {

class ResourceManager;
class Scene;

namespace authoring {

std::string serializeSceneSnapshot(Scene& scene, ResourceManager& resources);
std::string serializeSceneSnapshot(Scene& scene, ResourceManager* resources);

// Rebuild a Scene from a headless snapshot document — the inverse of
// serializeSceneSnapshot(scene, nullptr). Reconstructs the node hierarchy,
// transforms, reflected node properties, groups, scene settings and signal
// connections WITHOUT any GPU resource (MeshNode mesh/material refs stay null).
// This is the structural/authoring representation collaboration and headless
// apply-ops (Phase B3) operate on; it deliberately does not load meshes,
// materials or behaviours.
//
// Accepts any {version, scene, ...} document — a headless snapshot or a real
// .saidaproj — reading only the resource-free fields. Returns false + *error on
// a malformed document and leaves `out` cleared; never throws.
bool deserializeSceneSnapshot(const nlohmann::json& doc, Scene& out,
                              std::string* error = nullptr);
bool deserializeSceneSnapshot(const std::string& text, Scene& out,
                              std::string* error = nullptr);

} // namespace authoring
} // namespace saida
