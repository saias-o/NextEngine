#pragma once

#include <openxr/openxr.h>

#include <vector>

namespace ne::xr {

class Instance;

// OpenXR action-set input layer — the producer that "lights up" NEXRTK. It
// declares the interaction actions (grip + aim pose, trigger, squeeze, thumbstick,
// A/B), suggests bindings for the common controllers (Oculus Touch, Khronos
// simple), creates per-hand action spaces, and each frame syncs + locates them,
// pushing the result into the toolkit's ne::XRInput service. Owned by Session.
class Actions {
public:
    Actions(Instance& instance, XrSession session);
    ~Actions();
    Actions(const Actions&) = delete;
    Actions& operator=(const Actions&) = delete;

    // Poll the runtime and feed ne::XRInput. Poses are located in `space` (the app
    // reference space — already recentred to world) at `time`. Safe to call every
    // frame; when the session isn't focused the actions read inactive.
    void sync(XrSession session, XrSpace space, XrTime time);

private:
    XrPath path(const char* str) const;
    XrAction makeAction(const char* name, XrActionType type);
    void suggest(const char* profile, const std::vector<XrActionSuggestedBinding>& bindings);

    XrInstance instance_ = XR_NULL_HANDLE;   // borrowed
    XrActionSet actionSet_ = XR_NULL_HANDLE;
    XrPath handPaths_[2]{};                   // /user/hand/left, /right

    XrAction gripPose_ = XR_NULL_HANDLE;
    XrAction aimPose_ = XR_NULL_HANDLE;
    XrAction squeeze_ = XR_NULL_HANDLE;
    XrAction trigger_ = XR_NULL_HANDLE;
    XrAction thumbstick_ = XR_NULL_HANDLE;
    XrAction primary_ = XR_NULL_HANDLE;
    XrAction secondary_ = XR_NULL_HANDLE;

    XrSpace gripSpace_[2]{};
    XrSpace aimSpace_[2]{};
};

} // namespace ne::xr
