#pragma once

// Description stable du moteur pour les outils externes.
// Garde l'authoring-core linke dans le build web afin que sa taille WASM soit reelle.

#include "core/EngineVersion.hpp"

#include <nlohmann/json.hpp>

namespace saida::authoring {

// { engineVersion, opVersion, nodes:[...], behaviours:[...], ops:[...],
//   scenario:{ actions:[...], conditions:[...] } }
nlohmann::json buildEngineManifest();

// Les snapshots matérialisent la version courante du contrat d'authoring.
constexpr const char* kEngineVersion = ::saida::kEngineVersion;
constexpr int kOpVersion = 2;

} // namespace saida::authoring
