#pragma once

#include "scene/Behaviour.hpp"

namespace ne {

class XRController;

// Near-field interactor: attach to an XRController to let that hand grab
// XRGrabbables and poke XRTouchables it gets close to. It owns no interactable
// pointers across frames — each frame it scans the "xr_grabbable"/"xr_touchable"
// groups (the engine's sanctioned, name-free lookup), so freeing an interactable
// node mid-grab is safe. Selection range = the interactable's own radius (refined
// by its mesh AABB) plus `reach`.
class XRDirectInteractor : public Behaviour {
public:
    void onReady() override;
    void onUpdate(float dt) override;
    void onDestroy() override;

    float reach = 0.0f;  // extra range added to each interactable's radius (m)

    const char* typeName() const override { return "XRDirectInteractor"; }
    void save(nlohmann::json& j) const override;
    void load(const nlohmann::json& j) override;

private:
    XRController* controller_ = nullptr;  // cached: this interactor's hand
};

} // namespace ne
