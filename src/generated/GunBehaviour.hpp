#pragma once

#include "scene/Behaviour.hpp"
#include "core/Reflection.hpp"

#include <string>

namespace ne {

// Hitscan weapon. CharacterBody NPCs are checked manually; physics raycast is
// used only for wall occlusion.
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
