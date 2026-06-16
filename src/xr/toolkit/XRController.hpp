#pragma once

#include "scene/Node.hpp"
#include "xr/toolkit/XRTypes.hpp"

namespace ne {

// A tracked hand/controller in the scene. Its transform follows the matching
// XRInput grip pose every frame (via an internal tracker behaviour added in the
// constructor — no setup required). Attach interactors to it (e.g.
// XRDirectInteractor) and parent a controller model / collider as children.
//
// Pose is written as the node's *local* transform: with no XR origin that equals
// world space; once an XROrigin node exists and the controller is its child, the
// hierarchy composes grip→world automatically, so locomotion/teleport "just work".
class XRController : public Node {
public:
    explicit XRController(XRHand hand = XRHand::Right);

    XRHand hand() const { return hand_; }
    void setHand(XRHand h) { hand_ = h; }

    // True while the underlying device reported tracking this frame.
    bool isTracked() const;

    const char* typeName() const override { return "XRController"; }
    void serialize(nlohmann::json& j, ResourceManager& resources) const override;
    void deserialize(const nlohmann::json& j, ResourceManager& resources) override;

private:
    XRHand hand_;
};

} // namespace ne
