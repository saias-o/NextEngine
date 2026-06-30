#include "generated/GunBehaviour.hpp"

#include "scene/Node.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneTree.hpp"
#include "scene/HealthBehaviour.hpp"
#include "physics/PhysicsWorld.hpp"
#include "core/Input.hpp"
#include "core/Log.hpp"

#include <glm/glm.hpp>

namespace saida {

namespace {
constexpr glm::vec3 kWorldUp{0.0f, 1.0f, 0.0f};
}

void GunBehaviour::onReady() {
    Input::bindMouse("Fire", MouseButton::Left);
}

void GunBehaviour::onUpdate(float dt) {
    if (cooldownTimer_ > 0.0f) cooldownTimer_ -= dt;
    if (cooldownTimer_ <= 0.0f && Input::isActionJustPressed("Fire")) {
        fire();
        cooldownTimer_ = cooldown;
    }
}

void GunBehaviour::fire() {
    SceneTree* t = tree();
    if (!t) return;

    // Aim along the active camera's facing (third-person crosshair).
    Node* cam = t->firstInGroup("camera");
    glm::vec3 origin = cam ? glm::vec3(cam->worldTransform()[3])
                           : glm::vec3(node()->worldTransform()[3]) + kWorldUp * 1.2f;
    glm::vec3 dir = cam ? glm::vec3(cam->worldTransform() * glm::vec4(0, 0, -1, 0))
                        : node()->transform().rotation * glm::vec3(0, 0, -1);

    // Nearest NPC along the ray (node-level query: characters aren't ray-hittable
    // by the physics raycast, so the engine handles them here).
    SceneTree::NodeRayHit hit = t->raycastNodes(origin, dir, range, hitRadius, targetGroup);
    if (!hit.node) return;

    // Reject the shot if a building stands between the muzzle and the target. Real
    // bodies (StaticBody buildings) are raycast-hittable; characters are not, so
    // this only trips on walls.
    if (PhysicsWorld* physics = t->world().physics()) {
        if (physics->raycast(origin, glm::normalize(dir), hit.distance - 0.1f).hit) return;
    }

    if (applyDamage(hit.node, damage))
        Log::info("Shot '", hit.node->name(), "'");
}

void GunBehaviour::describe(reflect::TypeBuilder<GunBehaviour>& t) {
    t.doc("Hitscan gun: left mouse fires from the camera and damages the first NPC "
          "(group 'targetGroup', with a Health behaviour) in line, unless a wall blocks it.");
    t.property("damage", &GunBehaviour::damage).range(0.0, 1000.0).tooltip("per shot");
    t.property("range", &GunBehaviour::range).range(1.0, 1000.0);
    t.property("cooldown", &GunBehaviour::cooldown).range(0.0, 5.0).tooltip("seconds between shots");
    t.property("hitRadius", &GunBehaviour::hitRadius).range(0.05, 3.0).tooltip("aim forgiveness");
    t.property("targetGroup", &GunBehaviour::targetGroup);
}

} // namespace saida
