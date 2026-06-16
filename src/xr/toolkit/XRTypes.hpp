#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// NextEngine XR Toolkit (NEXRTK) — shared types.
//
// The toolkit is a thin layer of nodes + behaviours on top of the engine's
// scene/behaviour/signal model. It never reaches into OpenXR directly: it reads
// hand state from the XRInput service (fed by the OpenXR action layer — Étape C),
// so every piece works the same whether the source is a real headset, a desktop
// emulator, or a test. Same engine conventions throughout: logic = behaviour,
// "call down / signal up", nodes located via groups (never by name).

namespace ne {

// Which hand a controller / interactor represents.
enum class XRHand { Left = 0, Right = 1 };
inline constexpr int kXRHandCount = 2;
inline int xrHandIndex(XRHand h) { return static_cast<int>(h); }

// A tracked 6-DoF pose. `tracked` is false when the runtime lost tracking this
// frame (consumers should hold their last good pose rather than snap to origin).
struct XRPose {
    bool tracked = false;
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};

    glm::mat4 matrix() const {
        return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(orientation);
    }
};

// One controller / hand sampled this frame. Poses are expressed in the XR
// reference space (== world while there is no XR origin offset; once an XROrigin
// node exists, the controller node lives under it and the hierarchy composes the
// world transform). Buttons are raw analog values; XRInput derives the edges.
struct XRHandState {
    bool active = false;          // device present & tracked this frame
    XRPose grip;                  // grip pose — for holding objects
    XRPose aim;                   // aim/pointer pose — for ray interactors
    float trigger = 0.0f;         // index trigger, 0..1
    float squeeze = 0.0f;         // grip squeeze, 0..1
    glm::vec2 thumbstick{0.0f};   // primary 2D axis
    bool primaryButton = false;   // A / X
    bool secondaryButton = false; // B / Y
};

// Group tags used to locate interactables without coupling by name (the engine
// has no global find-by-name on purpose). Interactables join these in onReady();
// interactors scan them via tree()->group(...).
inline constexpr const char* kXRGrabbableGroup = "xr_grabbable";
inline constexpr const char* kXRTouchableGroup = "xr_touchable";
inline constexpr const char* kXROriginGroup = "xr_origin";          // the player rig
inline constexpr const char* kXRTeleportAreaGroup = "xr_teleport_area";

} // namespace ne
