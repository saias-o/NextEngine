#pragma once

// SaidaOpApplier — applique une SaidaOp (JSON) a une Scene vivante.
// Code PARTAGE (invariant 0.2) : destine a etre linke dans l'editeur desktop,
// les outils headless ET le runtime d'edition web. Aucune dependance ImGui,
// SceneSerializer ou reseau ; il ne touche que Node/Scene/ResourceManager.
//
// Spike S0/S2 (PLAN_LIVE_EDIT_WEB.md) : couvre set_transform / create_node /
// delete_node / rename_node, plus set_property via la reflection des nodes web
// reflechis (LightNode, Water, ParticleSystem). La validation stricte complete
// contre l'EngineManifest releve de la Phase A du mandat.

#include <string>

namespace saida {

class Scene;
class ResourceManager;

namespace authoring {

// Applique une op JSON a la scene. Retourne un JSON serialise :
//   {"ok":true,"applied":"set_transform","diff":{...}}
//   {"ok":false,"error":"unknown node 'Foo'"}
// Ne mute jamais la scene si l'op est invalide.
std::string applyOpJson(Scene& scene, ResourceManager& resources,
                        const std::string& opJson);

// Test/headless-friendly overload. Ops that need GPU resources (for example
// create_node MeshNode) return an error when resources is null.
std::string applyOpJson(Scene& scene, ResourceManager* resources,
                        const std::string& opJson);

} // namespace authoring
} // namespace saida
