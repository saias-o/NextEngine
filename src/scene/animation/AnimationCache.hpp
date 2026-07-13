#pragma once

// AnimationCache — cache disque des clips cuits, indexé par hash de contenu
// (sources + réglages + versions du cooker/format). Les fichiers .sanimc sont
// dérivés et régénérables : ils ne sont jamais une source de vérité. Écriture
// atomique (fichier temporaire puis rename) pour survivre aux interruptions.

#include "scene/animation/AnimationCooker.hpp"

#include <memory>
#include <string>

namespace saida {

class AnimationCache {
public:
    // `cacheDir` est créé à la demande (ex. <projet>/.saida/cache/animation).
    explicit AnimationCache(std::string cacheDir);

    struct Result {
        std::shared_ptr<const CookedClip> clip;
        bool fromCache = false;
        CookReport report;  // rempli uniquement quand le clip vient d'être cuit
    };

    Result getOrCook(const AnimationClip& clip, const Rig& rig,
                     const CookSettings& settings = {},
                     const RetargetMap* retarget = nullptr);

    std::string cachePath(uint64_t contentHash) const;

private:
    std::string cacheDir_;
};

} // namespace saida
