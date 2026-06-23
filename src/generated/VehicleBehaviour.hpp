#pragma once

#include "scene/Behaviour.hpp"
#include "core/Reflection.hpp"

#include <glm/glm.hpp>

#include <string>

namespace ne {

class Node;

// A drivable, GTA-style arcade vehicle. Attach to a CharacterBody node (the car),
// tagged in the `vehicle` group, with a box CollisionShape child. The body's
// CharacterVirtual gives us free collision/sliding against the world, exactly like
// the player — we only feed it a velocity and a steering rotation each frame.
//
// Enter/exit: when no one is driving, pressing the "EnterVehicle" action (F) while
// an on-foot driver (the node in `driverGroup`, default "driver") stands within
// `enterRadius` seats them — the driver node is disabled (hidden + on-foot control
// stops) and the camera's follow target (the "player" group) is moved onto the
// car. Pressing it again drops the driver back out beside the car.
//
// Kept self-contained: the car owns its driver relationship, so there is no
// cross-node "manager". The camera switch is just a group tag moving between two
// nodes, which CameraFollow re-resolves every frame.
class VehicleBehaviour : public Behaviour {
public:
    void onReady() override;
    void onUpdate(float dt) override;
    void onDisable() override;

    NE_REFLECT_BEHAVIOUR(VehicleBehaviour, "Vehicle")

    // Driving feel.
    float maxSpeed = 16.0f;       // m/s forward top speed
    float reverseSpeed = 6.0f;    // m/s reverse top speed
    float accel = 14.0f;          // m/s^2 throttle acceleration
    float brakeDecel = 26.0f;     // m/s^2 deceleration when braking / no throttle
    float turnRate = 120.0f;      // deg/s steering at full effectiveness
    float gravity = 22.0f;        // m/s^2 downward pull when airborne

    // Enter/exit.
    float enterRadius = 3.5f;     // how close the driver must be to board
    float exitDistance = 2.2f;    // how far to the car's right the driver pops out
    std::string driverGroup = "driver";  // the on-foot character to seat
    std::string cameraGroup = "player";  // follow-camera target tag to move on board

    // Running over pedestrians.
    float killSpeed = 4.0f;       // min |speed| (m/s) to be lethal
    float runOverRadius = 2.6f;   // NPCs within this of the car get hit
    float runOverDamage = 1000.0f;// damage dealt per hit (high = instant kill)
    std::string npcGroup = "npc"; // who can be run over (needs a Health behaviour)

private:
    void enterVehicle(Node* driver);
    void exitVehicle();
    float yawFromRotation() const;

    Node* occupant_ = nullptr;  // current driver (disabled while seated; not freed)
    float yaw_ = 0.0f;          // heading, radians (rotation about +Y)
    float speed_ = 0.0f;        // signed forward speed, m/s
    bool yawInit_ = false;
    bool warned_ = false;
};

} // namespace ne
