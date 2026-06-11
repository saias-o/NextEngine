#pragma once

#include "physics/CollisionObjectNode.hpp"

#include <Jolt/Core/Reference.h>

#include <glm/glm.hpp>

namespace JPH {
class CharacterVirtual;
}

namespace ne {

// A character controller (cf. Godot CharacterBody3D), backed by Jolt's
// CharacterVirtual: a kinematic capsule that slides along geometry, walks up
// stairs, sticks to the floor and pushes dynamic bodies. The collider is a child
// CollisionShape (Auto → capsule), like the other body nodes.
//
// A controller behaviour drives it by ONLY writing `velocity` and reading
// `isOnFloor()` — the engine performs the actual move inside the physics step
// (there is no moveAndSlide() to call or forget). After the step, `velocity`
// reflects the slide result and `isOnFloor()` is up to date.
class CharacterBodyNode : public CollisionObjectNode {
public:
    CharacterBodyNode();   // out-of-line (Ref<CharacterVirtual> member needs the complete type)
    ~CharacterBodyNode() override;

    const char* typeName() const override { return "CharacterBody"; }
    BodyMotion motion() const override { return BodyMotion::Kinematic; }
    CharacterBodyNode* asCharacterBody() override { return this; }

    void serialize(nlohmann::json& j, ResourceManager& resources) const override;
    void deserialize(const nlohmann::json& j, ResourceManager& resources) override;

    // Engine hooks (not called by gameplay code).
    void syncToPhysics(PhysicsWorld& world) override;     // lazily create the character
    void prePhysicsStep(PhysicsWorld& world, float dt) override;  // move/slide
    void syncFromPhysics(PhysicsWorld& world) override;   // write pose back to the node

    // ── Gameplay API ─────────────────────────────────────────────────────────
    glm::vec3 velocity{0.0f};     // read/written by the controller behaviour
    float maxSlopeAngle = 50.0f;  // degrees; steeper ground → isOnSteepSlope(), slide off
    float mass = 70.0f;           // how hard the character pushes dynamic bodies

    bool isOnFloor() const;
    bool isOnSteepSlope() const;
    glm::vec3 groundNormal() const;
    glm::vec3 groundVelocity() const;  // velocity of the floor (moving platforms)

private:
    JPH::Ref<JPH::CharacterVirtual> character_;
};

} // namespace ne
