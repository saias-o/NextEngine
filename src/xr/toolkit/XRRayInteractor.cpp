#include "xr/toolkit/XRRayInteractor.hpp"
#include "xr/toolkit/XRController.hpp"
#include "xr/toolkit/XROrigin.hpp"
#include "xr/toolkit/TeleportArea.hpp"
#include "xr/toolkit/XRInput.hpp"
#include "xr/toolkit/XRTypes.hpp"

#include "scene/Node.hpp"
#include "scene/SceneTree.hpp"
#include "core/Log.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

namespace ne {

void XRRayInteractor::onReady() {
    controller_ = dynamic_cast<XRController*>(node());
    if (!controller_)
        Log::warn("XRRayInteractor must be attached to an XRController node");
}

void XRRayInteractor::onUpdate(float) {
    if (!controller_) return;
    SceneTree* t = tree();
    if (!t) return;

    const XRHand hand = controller_->hand();
    const XRHandState& s = XRInput::hand(hand);

    // Ray from the aim pose (fallback to grip); forward is -Z in pose space.
    const XRPose& aim = s.aim.tracked ? s.aim : s.grip;
    rayOrigin_ = aim.position;
    rayDir_ = glm::normalize(aim.orientation * glm::vec3(0.0f, 0.0f, -1.0f));

    const bool wantAim = s.active && s.thumbstick.y > aimThreshold;

    if (wantAim) {
        aiming_ = true;
        hasTarget_ = false;
        targetArea_ = nullptr;
        float bestDist = maxDistance;
        for (Node* n : t->group(kXRTeleportAreaGroup)) {
            auto* area = dynamic_cast<TeleportArea*>(n);
            if (!area) continue;
            glm::vec3 hit;
            if (area->raycast(rayOrigin_, rayDir_, maxDistance, hit)) {
                const float d = glm::distance(rayOrigin_, hit);
                if (d < bestDist) {
                    bestDist = d;
                    target_ = hit;
                    hasTarget_ = true;
                    targetArea_ = area;
                }
            }
        }
    } else if (aiming_) {
        // Stick released → commit the teleport to the last valid target.
        if (hasTarget_) commitTeleport();
        aiming_ = false;
        hasTarget_ = false;
        targetArea_ = nullptr;
    }
}

void XRRayInteractor::commitTeleport() {
    SceneTree* t = tree();
    if (!t) return;

    XROrigin* origin = dynamic_cast<XROrigin*>(t->firstInGroup(kXROriginGroup));
    if (!origin) {
        Log::warn("XRRayInteractor: no XROrigin in the scene to teleport");
        return;
    }
    origin->teleportTo(target_);

    // Re-validate the area is still alive (it may have been freed mid-aim) before
    // emitting its landing signal — no dangling pointer deref.
    for (Node* n : t->group(kXRTeleportAreaGroup)) {
        if (n == static_cast<Node*>(targetArea_)) {
            targetArea_->teleported.emit(target_);
            break;
        }
    }
    teleported.emit(target_);
}

void XRRayInteractor::save(nlohmann::json& j) const {
    j["maxDistance"] = maxDistance;
    j["aimThreshold"] = aimThreshold;
}

void XRRayInteractor::load(const nlohmann::json& j) {
    if (j.contains("maxDistance")) maxDistance = j["maxDistance"].get<float>();
    if (j.contains("aimThreshold")) aimThreshold = j["aimThreshold"].get<float>();
}

} // namespace ne
