#pragma once

// RetargetProfile — l'asset persistant de retargeting (.sretarget, JSON).
// Schéma 1 : correspondance par noms (RetargetMap). Schéma 2 : corrections de
// rest pose par os (pré-rotation), échelle de translation de la racine, et
// mapping sémantique via deux RigAsset. Les fichiers schéma 1 restent lisibles
// tels quels (champs additionnels optionnels).
//
// Schéma JSON (schema == kRetargetProfileSchema) :
//   {
//     "schema": 2,
//     "name": "MixamoToSaida",
//     "map": { "Hips": "mixamorig:Hips", "Spine": "mixamorig:Spine" },
//     "corrections": { "Hips": { "preRotation": [0, 0, 0, 1] } },
//     "translationScale": 1.15
//   }
// Les clés sont les os du rig CIBLE, les valeurs les pistes du clip SOURCE.

#include "scene/animation/ClipView.hpp"  // AssetDiagnostic
#include "scene/animation/Pose.hpp"
#include "scene/animation/Retarget.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace saida {

class AnimationClip;
class Rig;
class RigAsset;

constexpr int kRetargetProfileSchema = 2;

// Correction d'authoring d'un os cible (persistée dans le .sretarget).
struct RetargetBoneCorrection {
    std::string bone;
    glm::quat preRotation{1.0f, 0.0f, 0.0f, 0.0f};
};

// Forme compilée des corrections, indexée par os du rig cible : appliquée par
// l'Animator sur la pose échantillonnée (aucune recherche par nom au runtime).
struct RetargetCorrections {
    struct Bone {
        glm::quat preRotation{1.0f, 0.0f, 0.0f, 0.0f};
        bool active = false;
        bool scaleTranslation = false;  // racine : translation source × échelle
        glm::vec3 restPosition{0.0f};   // autres os : translation de repos cible
    };
    std::vector<Bone> bones;
    float translationScale = 1.0f;

    bool empty() const { return bones.empty(); }
    void apply(LocalPose& pose) const;
};

struct RetargetProfileParseResult;

class RetargetProfile {
public:
    static RetargetProfileParseResult parse(const nlohmann::json& j);
    static RetargetProfileParseResult loadFile(const std::string& path);

    nlohmann::json toJson() const;
    bool saveFile(const std::string& path) const;

    // Couverture contre le rig cible et le clip source : entrées dont
    // l'os n'existe pas dans le rig, pistes source inexistantes, et os du rig
    // laissés sans piste (warnings — un rig partiellement animé est valide).
    std::vector<AssetDiagnostic> validate(const Rig* targetRig,
                                          const AnimationClip* sourceClip) const;

    RetargetMap toRetargetMap() const;

    // Corrections compilées pour le rig cible (indexation par os). Vide si le
    // profil n'a ni correction ni échelle.
    RetargetCorrections compileCorrections(const Rig& targetRig) const;

    // Point de départ suggéré : l'auto-mapping par noms existant, converti en
    // profil éditable (l'auto-map devient une suggestion).
    static RetargetProfile fromAutoMap(const Rig& targetRig, const AnimationClip& sourceClip);

    // Mapping par identifiants sémantiques partagés entre deux .srig : chaque
    // sémantique présente des deux côtés produit une entrée cible → source.
    static RetargetProfile fromSemantics(const RigAsset& target, const RigAsset& source);

    // Remplit les corrections de rest pose : pré-rotation = repos cible ×
    // inverse(repos source) par os mappé, échelle = rapport des hauteurs
    // (celles des .srig si connues, sinon la hauteur de repos des racines).
    void computeRestPoseCorrections(const Rig& targetRig, const Rig& sourceRig,
                                    float targetHeight = 0.0f, float sourceHeight = 0.0f);

    std::string name;
    // Paires (os du rig cible → piste du clip source), ordre du fichier conservé.
    std::vector<std::pair<std::string, std::string>> entries;
    std::vector<RetargetBoneCorrection> corrections;
    float translationScale = 1.0f;
};

struct RetargetProfileParseResult {
    bool ok = false;
    RetargetProfile profile;
    std::vector<AssetDiagnostic> diagnostics;
};

} // namespace saida
