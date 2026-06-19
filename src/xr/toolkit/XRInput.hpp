#pragma once

#include "xr/toolkit/XRTypes.hpp"

namespace ne {

// Global XR input service — the XR analogue of `Input` (keyboard/mouse) and
// `Time`. It holds the per-hand state sampled once per frame and derives button
// edges (pressed/released) from the analog values. It is a *service*, not game
// state, so a static API is the sanctioned shape here (same as Input/Time).
//
// Producer side: the OpenXR action layer (Étape C) calls beginFrame() once per
// frame, then submitHand() for each tracked controller. Until that layer exists,
// no one submits and every hand reads back inactive — so all toolkit behaviours
// are safely inert rather than acting on garbage. A desktop emulator or a test
// can drive the exact same submitHand() entry point.
//
// Consumer side: behaviours read hand(), and the edge/level helpers.
class XRInput {
public:
    // ---- Consumer API ----
    static const XRHandState& hand(XRHand h);
    static const XRHandSkeletonState& skeleton(XRHand h);
    static bool anyActive();
    static bool handTrackingActive(XRHand h);

    // Head (HMD) pose in world space — fed by the engine each XR frame. Used e.g.
    // by teleport (head-relative placement) and head-facing UI.
    static const XRPose& head();

    // Grip "squeeze" (the grab button) — level + edges.
    static bool squeezeDown(XRHand h);
    static bool squeezePressed(XRHand h);
    static bool squeezeReleased(XRHand h);

    // Index "trigger" (select/poke/use) — level + edges.
    static bool triggerDown(XRHand h);
    static bool triggerPressed(XRHand h);
    static bool triggerReleased(XRHand h);

    // The analog value beyond which a squeeze/trigger counts as "down".
    static void setPressThreshold(float t);
    static float pressThreshold();

    // ---- Producer API (OpenXR action layer / emulator / tests) ----
    // Snapshot the current state as "previous" so edges can be derived; call once
    // at the start of an XR frame, before submitHand().
    static void beginFrame();
    // Provide a freshly polled state for one hand this frame.
    static void submitHand(XRHand h, const XRHandState& state);
    static void submitSkeleton(XRHand h, const XRHandSkeletonState& state);
    // Mark a hand as not present this frame (e.g. controller dropped).
    static void clearHand(XRHand h);
    // Provide the head pose (world space) for this frame.
    static void submitHead(const XRPose& pose);
};

} // namespace ne
