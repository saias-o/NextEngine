#pragma once

// AnimationCooker — compile un AnimationClip d'authoring en CookedClip runtime.
// Étapes : liaison des pistes aux os du rig (retargeting par noms optionnel),
// rééchantillonnage des splines cubiques, détection des canaux constants,
// suppression des pistes égales à la rest pose, réduction de clés sous
// tolérance, quantification et pagination. Le résultat et le rapport sont
// déterministes à entrées identiques.

#include "scene/animation/CookedClip.hpp"
#include "scene/animation/Retarget.hpp"
#include "scene/animation/Rig.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace saida {

struct CookSettings {
    // Tolérances par canal (espace local de l'os) : au-delà, une clé est gardée.
    float translationTolerance = 0.0005f;  // unités monde
    float rotationTolerance = 0.001f;      // radians
    float scaleTolerance = 0.0005f;
    // Fréquence de rééchantillonnage des pistes CUBICSPLINE (linéarisées).
    float splineResampleRate = 60.0f;
    // Durée maximale d'une page : borne la précision du temps quantifié
    // (durée de page / 65535) et permet le streaming des longues séquences.
    float maxPageSeconds = 60.0f;

    uint64_t hash() const;
};

struct CookReport {
    std::string clipName;
    uint32_t sourceKeys = 0;
    uint32_t cookedKeys = 0;
    uint32_t cookedTracks = 0;
    uint32_t constantTracks = 0;
    uint32_t restPoseTracksDropped = 0;
    uint32_t unmappedTracks = 0;  // pistes sans os correspondant dans le rig
    size_t sourceBytes = 0;
    size_t cookedBytes = 0;
    float maxTranslationError = 0.0f;
    float maxRotationError = 0.0f;  // radians
    float maxScaleError = 0.0f;

    bool withinTolerance(const CookSettings& settings) const;
    nlohmann::json toJson() const;
};

class AnimationCooker {
public:
    // `retarget` traduit les noms d'os du rig vers les noms de pistes du clip
    // (identité si null). Les pistes du clip sans os cible sont comptées dans
    // le rapport et ignorées.
    static CookedClip cook(const AnimationClip& clip, const Rig& rig,
                           const CookSettings& settings = {},
                           CookReport* report = nullptr,
                           const RetargetMap* retarget = nullptr);

    // Hash de contenu du triplet (clip, rig, réglages) : identifie l'entrée du
    // cache dérivé. Inclut la version du format cuit et celle du cooker.
    static uint64_t contentHash(const AnimationClip& clip, const Rig& rig,
                                const CookSettings& settings,
                                const RetargetMap* retarget = nullptr);
};

} // namespace saida
