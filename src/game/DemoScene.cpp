#include "game/DemoScene.hpp"

#include "core/Paths.hpp"
#include "graphics/Material.hpp"
#include "graphics/Mesh.hpp"
#include "graphics/ResourceManager.hpp"
#include "graphics/Texture.hpp"
#include "scene/Behaviour.hpp"
#include "scene/LightNode.hpp"
#include "scene/MeshNode.hpp"
#include "scene/Scene.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

namespace ne {

namespace {

// A unit cube with per-face normals and UVs. Color is white so the fragment
// shader shows texture * lighting unmodified.
const std::vector<Vertex> kCubeVertices = {
    // +Z
    {{-0.5f, -0.5f,  0.5f}, {0, 0, 1}, {1, 1, 1}, {0, 1}}, {{ 0.5f, -0.5f,  0.5f}, {0, 0, 1}, {1, 1, 1}, {1, 1}},
    {{ 0.5f,  0.5f,  0.5f}, {0, 0, 1}, {1, 1, 1}, {1, 0}}, {{-0.5f,  0.5f,  0.5f}, {0, 0, 1}, {1, 1, 1}, {0, 0}},
    // -Z
    {{ 0.5f, -0.5f, -0.5f}, {0, 0, -1}, {1, 1, 1}, {0, 1}}, {{-0.5f, -0.5f, -0.5f}, {0, 0, -1}, {1, 1, 1}, {1, 1}},
    {{-0.5f,  0.5f, -0.5f}, {0, 0, -1}, {1, 1, 1}, {1, 0}}, {{ 0.5f,  0.5f, -0.5f}, {0, 0, -1}, {1, 1, 1}, {0, 0}},
    // +Y
    {{-0.5f,  0.5f,  0.5f}, {0, 1, 0}, {1, 1, 1}, {0, 1}}, {{ 0.5f,  0.5f,  0.5f}, {0, 1, 0}, {1, 1, 1}, {1, 1}},
    {{ 0.5f,  0.5f, -0.5f}, {0, 1, 0}, {1, 1, 1}, {1, 0}}, {{-0.5f,  0.5f, -0.5f}, {0, 1, 0}, {1, 1, 1}, {0, 0}},
    // -Y
    {{-0.5f, -0.5f, -0.5f}, {0, -1, 0}, {1, 1, 1}, {0, 1}}, {{ 0.5f, -0.5f, -0.5f}, {0, -1, 0}, {1, 1, 1}, {1, 1}},
    {{ 0.5f, -0.5f,  0.5f}, {0, -1, 0}, {1, 1, 1}, {1, 0}}, {{-0.5f, -0.5f,  0.5f}, {0, -1, 0}, {1, 1, 1}, {0, 0}},
    // +X
    {{ 0.5f, -0.5f,  0.5f}, {1, 0, 0}, {1, 1, 1}, {0, 1}}, {{ 0.5f, -0.5f, -0.5f}, {1, 0, 0}, {1, 1, 1}, {1, 1}},
    {{ 0.5f,  0.5f, -0.5f}, {1, 0, 0}, {1, 1, 1}, {1, 0}}, {{ 0.5f,  0.5f,  0.5f}, {1, 0, 0}, {1, 1, 1}, {0, 0}},
    // -X
    {{-0.5f, -0.5f, -0.5f}, {-1, 0, 0}, {1, 1, 1}, {0, 1}}, {{-0.5f, -0.5f,  0.5f}, {-1, 0, 0}, {1, 1, 1}, {1, 1}},
    {{-0.5f,  0.5f,  0.5f}, {-1, 0, 0}, {1, 1, 1}, {1, 0}}, {{-0.5f,  0.5f, -0.5f}, {-1, 0, 0}, {1, 1, 1}, {0, 0}},
};

const std::vector<uint32_t> kCubeIndices = {
     0,  1,  2,  2,  3,  0,   4,  5,  6,  6,  7,  4,
     8,  9, 10, 10, 11,  8,  12, 13, 14, 14, 15, 12,
    16, 17, 18, 18, 19, 16,  20, 21, 22, 22, 23, 20,
};

// Sample behaviour: continuously spins its node around an axis. Children inherit
// the rotation through the transform hierarchy.
class RotatorBehaviour : public Behaviour {
public:
    RotatorBehaviour(glm::vec3 axis, float degreesPerSecond)
        : axis_(glm::normalize(axis)), speed_(glm::radians(degreesPerSecond)) {}

    void onUpdate(float dt) override {
        angle_ += dt * speed_;
        node()->transform().rotation = glm::angleAxis(angle_, axis_);
    }

private:
    glm::vec3 axis_;
    float speed_;
    float angle_ = 0.0f;
};

} // namespace

void buildDemoScene(Scene& scene, ResourceManager& resources) {
    // Shared resources, loaded once and cached by the ResourceManager.
    // (To load the bugatti instead — untextured, no UVs — use:
    //  resources.loadMesh(assetPath("models/bugatti/bugatti.obj")).)
    Mesh* cube = resources.createMesh("cube", kCubeVertices, kCubeIndices);
    Texture* checker = resources.loadTexture(assetPath("assets/textures/checker.png"));
    // Three materials sharing the texture but tinted differently (per-material data).
    Material* matWhite = resources.createMaterial("white", checker, glm::vec4(1.0f));
    Material* matWarm = resources.createMaterial("warm", checker, glm::vec4(1.0f, 0.55f, 0.4f, 1.0f));
    Material* matCool = resources.createMaterial("cool", checker, glm::vec4(0.45f, 0.7f, 1.0f, 1.0f));

    // A small hierarchy to show transform inheritance: a central "planet" with
    // an orbiting "moon", which itself has an orbiting "sub-moon". They share the
    // cube mesh but each has its own material. The orbiting motion comes entirely
    // from RotatorBehaviours on the parents — children inherit it.
    Node* planet = scene.createChild<MeshNode>("planet", cube, matWhite);
    planet->addBehaviour<RotatorBehaviour>(glm::vec3(0, 1, 0), 40.0f);

    Node* moon = planet->createChild<MeshNode>("moon", cube, matWarm);
    moon->transform().position = {2.0f, 0.0f, 0.0f};
    moon->transform().scale = glm::vec3(0.45f);
    moon->addBehaviour<RotatorBehaviour>(glm::vec3(0, 1, 0), 90.0f);

    Node* subMoon = moon->createChild<MeshNode>("sub-moon", cube, matCool);
    subMoon->transform().position = {2.0f, 0.0f, 0.0f};
    subMoon->transform().scale = glm::vec3(0.5f);

    // A directional "sun" (warm, slightly overhead).
    LightNode* sun = scene.createChild<LightNode>("sun", LightType::Directional);
    sun->direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));
    sun->color = {1.0f, 0.95f, 0.85f};
    sun->intensity = 1.0f;

    // A point light orbiting the scene: a rotating pivot with the light offset
    // from it, so the RotatorBehaviour makes it circle the objects.
    Node* lightPivot = scene.createChild<Node>("light-pivot");
    lightPivot->addBehaviour<RotatorBehaviour>(glm::vec3(0, 1, 0), 70.0f);
    LightNode* lamp = lightPivot->createChild<LightNode>("lamp", LightType::Point);
    lamp->transform().position = {3.5f, 1.5f, 0.0f};
    lamp->color = {0.3f, 0.5f, 1.0f};
    lamp->intensity = 4.0f;
    lamp->range = 8.0f;
}

} // namespace ne
