#pragma once

// RigAsset — l'asset persistant d'un squelette (.srig, JSON). Il ne duplique
// pas la hiérarchie (elle vit dans la source glTF/BVH) : il porte ce que la
// source ne connaît pas — les identifiants sémantiques des os (hips, spine,
// head, left_hand…), les métriques utiles au retargeting et le hash de
// compatibilité du squelette. Premier consommateur : le mapping sémantique de
// RetargetProfile::fromSemantics.
//
// Schéma JSON (schema == kRigAssetSchema) :
//   {
//     "schema": 1,
//     "name": "Hero",
//     "semantics": { "hips": "mixamorig:Hips", "head": "mixamorig:Head" },
//     "height": 1.8,
//     "skeletonHash": "9f2c..."   // optionnel : détecte un reimport divergent
//   }

#include "scene/animation/ClipView.hpp"  // AssetDiagnostic
#include "scene/animation/Rig.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

namespace saida {

constexpr int kRigAssetSchema = 1;

struct RigAssetParseResult;

class RigAsset {
public:
    static RigAssetParseResult parse(const nlohmann::json& j);
    static RigAssetParseResult loadFile(const std::string& path);

    nlohmann::json toJson() const;
    bool saveFile(const std::string& path) const;

    // Sémantiques dupliquées, os absents du rig, hash divergent (warning).
    std::vector<AssetDiagnostic> validate(const Rig* rig) const;

    const std::string* boneForSemantic(const std::string& semantic) const;

    // Empreinte stable du squelette (noms + parents) : deux rigs au même hash
    // partagent leurs palettes et programmes sans revalidation.
    static uint64_t skeletonHash(const Rig& rig);

    // Point de départ suggéré : détecte hips/spine/head/mains/pieds par les
    // conventions de nommage usuelles. Éditable ensuite, comme l'auto-map.
    static RigAsset fromRig(const Rig& rig, std::string assetName);

    std::string name;
    // Paires (sémantique → nom d'os), ordre du fichier conservé.
    std::vector<std::pair<std::string, std::string>> semantics;
    float height = 0.0f;         // 0 = inconnue
    uint64_t storedHash = 0;     // 0 = non renseigné
};

struct RigAssetParseResult {
    bool ok = false;
    RigAsset asset;
    std::vector<AssetDiagnostic> diagnostics;
};

} // namespace saida
