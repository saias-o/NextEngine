#pragma once

#include "project/AssetRegistry.hpp"  // AssetID

#include <string>

namespace ne {

class ResourceManager;

// Loads a Biovision .bvh motion-capture file into an AnimationClip and registers
// it with the ResourceManager (keyed by the file path). Returns the clip's AssetID
// (kAssetInvalid on failure).
//
// The clip's tracks are keyed by joint name, so it can be played on any skeleton
// whose bone names match (e.g. another Mixamo rig) via the Animator — no explicit
// retargeting. Cross-skeleton / cross-convention use (differing rest poses or up
// axes) still needs a proper retargeting pass (not implemented yet).
class BVHLoader {
public:
    static AssetID load(const std::string& path, ResourceManager& resources);
};

} // namespace ne
