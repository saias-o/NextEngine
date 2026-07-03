#pragma once

// EngineManifest — description stable du moteur exposee au SaaS et aux agents IA.
// Voir ARCHITECTURE_PRODUCTION_CLAUDE.md §2 (EngineManifest) et §3.3.
//
// Spike S0 (PLAN_LIVE_EDIT_WEB.md) : version minimale. Elle sert surtout, ici, a
// faire referencer l'authoring-core par le binding web pour que le linker ne le
// strippe pas — c'est ce qui rend la mesure de taille wasm honnete. La version
// complete (proprietes reflechies, signaux, actions scenario) releve de la
// Phase A du mandat.

#include <nlohmann/json.hpp>

namespace saida::authoring {

// { engineVersion, opVersion, nodes:[...], ops:[...] }
nlohmann::json buildEngineManifest();

// Versions de contrat (invariant 0.6 : le pont inter-versions est le snapshot).
constexpr const char* kEngineVersion = "0.1.0";
constexpr int kOpVersion = 1;

} // namespace saida::authoring
