#pragma once

#include "project/AssetLoader.hpp"
#include "scene/animation/AnimGraphAsset.hpp"
#include "scene/animation/ClipView.hpp"
#include "scene/animation/RigAsset.hpp"

#include <vector>

namespace saida {

// Payloads CPU produits par l'AssetLoader. Les diagnostics sont conservés
// jusqu'au thread principal afin que ResourceManager puisse les journaliser
// sans faire toucher le worker à l'état du moteur.
struct DecodedRigAsset {
    RigAsset asset;
    std::vector<AssetDiagnostic> diagnostics;
};

struct DecodedClipView {
    ClipView view;
    std::vector<AssetDiagnostic> diagnostics;
};

struct DecodedAnimGraph {
    AnimGraphAsset graph;
    std::vector<AssetDiagnostic> diagnostics;
};

AssetDecoder makeRigAssetDecoder();
AssetDecoder makeClipViewDecoder();
AssetDecoder makeAnimGraphDecoder();

} // namespace saida
