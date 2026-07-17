#pragma once

// Description stable du moteur pour les outils externes.
// Garde l'authoring-core linke dans le build web afin que sa taille WASM soit reelle.

#include <nlohmann/json.hpp>

namespace saida::authoring {

// { engineVersion, opVersion, nodes:[...], behaviours:[...], ops:[...],
//   scenario:{ actions:[...], conditions:[...] } }
nlohmann::json buildEngineManifest();

// Les snapshots assurent la compatibilite entre versions de contrat.
constexpr const char* kEngineVersion = "0.1.0";
constexpr int kOpVersion = 2;

} // namespace saida::authoring
