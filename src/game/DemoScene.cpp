#include "game/DemoScene.hpp"

#include "core/Paths.hpp"
#include "graphics/Material.hpp"
#include "graphics/Mesh.hpp"
#include "graphics/ResourceManager.hpp"
#include "graphics/Texture.hpp"
#include "scene/Behaviour.hpp"
#include "scene/BehaviourRegistry.hpp"
#include "scene/LightNode.hpp"
#include "scene/MeshNode.hpp"
#include "scene/Scene.hpp"
#include "project/AssetRegistry.hpp"
#include "scene/GLTFLoader.hpp"

#include "nlohmann/json.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

namespace ne {

namespace {

constexpr float kDemoIblDiffuseIntensity = 0.5f;
constexpr float kDemoIblSpecularIntensity = 1.5f;

// Sample behaviour: continuously spins its node around an axis. Children inherit
// the rotation through the transform hierarchy. Serializable (registered below).
class RotatorBehaviour : public Behaviour {
public:
    RotatorBehaviour() = default;  // for the registry factory
    RotatorBehaviour(glm::vec3 axis, float degreesPerSecond)
        : axis_(glm::normalize(axis)), degreesPerSecond_(degreesPerSecond) {}

    void onUpdate(float dt) override {
        angle_ += dt * glm::radians(degreesPerSecond_);
        node()->transform().rotation = glm::angleAxis(angle_, axis_);
    }

    const char* typeName() const override { return "Rotator"; }
    void save(nlohmann::json& j) const override {
        j["axis"] = {axis_.x, axis_.y, axis_.z};
        j["speed"] = degreesPerSecond_;
    }
    void load(const nlohmann::json& j) override {
        if (auto it = j.find("axis"); it != j.end() && it->is_array() && it->size() == 3)
            axis_ = glm::normalize(glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()));
        degreesPerSecond_ = j.value("speed", 0.0f);
    }

private:
    glm::vec3 axis_{0.0f, 1.0f, 0.0f};
    float degreesPerSecond_ = 0.0f;
    float angle_ = 0.0f;
};

} // namespace

void buildDemoScene(Scene& scene, ResourceManager& resources) {
    // Register the game's serializable behaviours so saved scenes round-trip.
    BehaviourRegistry::instance().registerType<RotatorBehaviour>("Rotator");

    // Content-addressed assets: a built-in cube primitive and materials described
    // by (texture path, tint). The serialized scene fully describes these, so it
    // reloads without the game re-registering anything.
    // (To use the bugatti instead — untextured, no UVs — use getMesh(path).)
    Mesh* cube = resources.getMesh(kAssetBuiltinCube);
    const std::string checker = assetPath("assets/textures/checker.png");
    AssetID checkerId = resources.getOrRegister(checker, AssetType::Texture);
    const std::string sky = assetPath("MyGame/assets/textures/hdri_sky_809.jpg");
    scene.settings().skyboxTexture = resources.getOrRegister(sky, AssetType::Texture);
    scene.settings().iblDiffuseIntensity = kDemoIblDiffuseIntensity;
    scene.settings().iblSpecularIntensity = kDemoIblSpecularIntensity;

    MaterialDesc descWhite; descWhite.albedoId = checkerId; descWhite.baseColor = glm::vec4(1.0f);
    Material* matWhite = resources.getMaterial(descWhite);

    MaterialDesc descWarm; descWarm.albedoId = checkerId; descWarm.baseColor = glm::vec4(1.0f, 0.55f, 0.4f, 1.0f);
    Material* matWarm = resources.getMaterial(descWarm);

    MaterialDesc descCool; descCool.albedoId = checkerId; descCool.baseColor = glm::vec4(0.45f, 0.7f, 1.0f, 1.0f);
    Material* matCool = resources.getMaterial(descCool);

    // A large flat ground plane (a scaled cube) so the directional/spot shadows
    // have a surface to land on. Static receiver: it doesn't cast and is a good
    // candidate for the light bake.
    MaterialDesc descGround; descGround.albedoId = checkerId; descGround.baseColor = glm::vec4(0.8f, 0.8f, 0.85f, 1.0f);
    Material* matGround = resources.getMaterial(descGround);
    MeshNode* ground = scene.createChild<MeshNode>("ground", cube, matGround);
    ground->transform().position = {0.0f, -2.5f, 0.0f};
    ground->transform().scale = {20.0f, 0.2f, 20.0f};
    ground->castShadows() = false;
    ground->includeInLightBaking() = true;

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

    // Load glTF model
    std::string gltfPath = assetPath("assets/models/DamagedHelmet.glb");
    GLTFLoader::load(gltfPath, scene, resources);

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
