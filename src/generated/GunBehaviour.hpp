#pragma once

#include "scene/Behaviour.hpp"
#include "core/Reflection.hpp"

#include <string>

namespace ne {

// A hitscan gun for the player. Left mouse fires a ray from the active camera; the
// first NPC (a node in `targetGroup` carrying a Health behaviour) along it takes
// `damage`, unless a wall is in the way. Attach to the player node.
//
// NPCs are kinematic CharacterVirtual controllers, which the physics raycast does
// NOT see — so we intersect the ray against the NPCs' positions ourselves and use
// the physics raycast only to check that a building isn't blocking the shot.
class GunBehaviour : public Behaviour {
public:
    void onReady() override;
    void onUpdate(float dt) override;

    NE_REFLECT_BEHAVIOUR(GunBehaviour, "Gun")

    float damage = 60.0f;       // per shot
    float range = 120.0f;       // max hit distance (m)
    float cooldown = 0.2f;      // seconds between shots
    float hitRadius = 0.6f;     // how forgiving aim is (NPC treated as this sphere)
    std::string targetGroup = "npc";

private:
    void fire();

    float cooldownTimer_ = 0.0f;
};

} // namespace ne
