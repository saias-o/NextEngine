#pragma once

// RetargetProfile — l'asset persistant de retargeting (.sretarget, JSON,
// PLAN_ANIMATION.md §4.1/§8.1). Le schéma 1 couvre ce que le runtime sait
// faire aujourd'hui : la correspondance par noms (RetargetMap). Les corrections
// de rest pose, d'échelle et de racine viendront à l'Étape 6 en étendant le
// schéma (avec migration).
//
// Schéma JSON (schema == kRetargetProfileSchema) :
//   {
//     "schema": 1,
//     "name": "MixamoToSaida",
//     "map": { "Hips": "mixamorig:Hips", "Spine": "mixamorig:Spine" }
//   }
// Les clés sont les os du rig CIBLE, les valeurs les pistes du clip SOURCE.

#include "scene/animation/ClipView.hpp"  // AssetDiagnostic
#include "scene/animation/Retarget.hpp"

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace saida {

class AnimationClip;
class Rig;

constexpr int kRetargetProfileSchema = 1;

struct RetargetProfileParseResult;

class RetargetProfile {
public:
    static RetargetProfileParseResult parse(const nlohmann::json& j);
    static RetargetProfileParseResult loadFile(const std::string& path);

    nlohmann::json toJson() const;
    bool saveFile(const std::string& path) const;

    // Couverture contre le rig cible et le clip source (§8.1) : entrées dont
    // l'os n'existe pas dans le rig, pistes source inexistantes, et os du rig
    // laissés sans piste (warnings — un rig partiellement animé est valide).
    std::vector<AssetDiagnostic> validate(const Rig* targetRig,
                                          const AnimationClip* sourceClip) const;

    RetargetMap toRetargetMap() const;

    // Point de départ suggéré : l'auto-mapping par noms existant, converti en
    // profil éditable (plan §8.1 — l'auto-map devient une suggestion).
    static RetargetProfile fromAutoMap(const Rig& targetRig, const AnimationClip& sourceClip);

    std::string name;
    // Paires (os du rig cible → piste du clip source), ordre du fichier conservé.
    std::vector<std::pair<std::string, std::string>> entries;
};

struct RetargetProfileParseResult {
    bool ok = false;
    RetargetProfile profile;
    std::vector<AssetDiagnostic> diagnostics;
};

} // namespace saida
