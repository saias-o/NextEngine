// Web runtime — Étape 16.5: the real Renderer running on the rhi::webgpu
// backend. The scene below is a placeholder (spinning lit cube + ground +
// shadow-casting sun); the frame itself is the engine's own drawFrame —
// shadows, DDGI voxelize/trace/blend, HDR scene pass, bloom, tonemap — the
// same code path as desktop, recorded through rhi::* onto WebGPU.

#include "rhi/webgpu/RhiWeb.hpp"
#include "graphics/Material.hpp"
#include "graphics/Mesh.hpp"
#include "graphics/ResourceManager.hpp"
#include "render/Renderer.hpp"
#include "render/RenderFeatureRegistry.hpp"
#include "core/Camera.hpp"
#include "core/Time.hpp"
#include "scene/LightNode.hpp"
#include "scene/MeshNode.hpp"
#include "scene/ParticleSystemNode.hpp"
#include "scene/Scene.hpp"

#include <GLFW/glfw3.h>
#include <emscripten.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdio>
#include <memory>
#include <vector>

using namespace saida;

namespace {

constexpr uint32_t kWidth = 1280, kHeight = 720;

struct App {
    std::unique_ptr<rhi::Device> device;
    std::unique_ptr<rhi::Surface> surface;
    std::unique_ptr<ResourceManager> resources;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<Scene> scene;
    Camera camera;
    MeshNode* cubeNode = nullptr;
    double time = 0.0;
};

App gApp;

std::vector<uint8_t> checkerPixels(uint32_t size) {
    std::vector<uint8_t> px(size_t(size) * size * 4);
    for (uint32_t y = 0; y < size; ++y)
        for (uint32_t x = 0; x < size; ++x) {
            const bool a = ((x / 8) + (y / 8)) % 2 == 0;
            uint8_t* p = &px[(size_t(y) * size + x) * 4];
            p[0] = a ? 230 : 60;
            p[1] = a ? 90 : 160;
            p[2] = a ? 60 : 230;
            p[3] = 255;
        }
    return px;
}

// Equirectangular gradient sky: zenith (V=0, up) deep blue → horizon warm.
// The SkyboxFeature samples this exact 2D layout (skybox.frag, asin(-dir.y)).
std::vector<uint8_t> skyEquirect(uint32_t w, uint32_t h) {
    std::vector<uint8_t> px(size_t(w) * h * 4);
    auto mix = [](float a, float b, float t) { return a + (b - a) * t; };
    for (uint32_t y = 0; y < h; ++y) {
        const float v = float(y) / float(h - 1);           // 0 = up, 1 = down
        // Zenith blue -> horizon pale -> nadir dim.
        float r, g, b;
        if (v < 0.5f) { const float t = v / 0.5f;
            r = mix(40, 150, t); g = mix(90, 190, t); b = mix(200, 225, t);
        } else { const float t = (v - 0.5f) / 0.5f;
            r = mix(150, 60, t); g = mix(190, 70, t); b = mix(225, 80, t);
        }
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t* p = &px[(size_t(y) * w + x) * 4];
            p[0] = uint8_t(r); p[1] = uint8_t(g); p[2] = uint8_t(b); p[3] = 255;
        }
    }
    return px;
}

void initRuntime() {
    rhi::Device& device = *gApp.device;

    // Populate the render-feature registry (skybox + GPU particles on web).
    registerBuiltinRenderFeatures();

    gApp.resources = std::make_unique<ResourceManager>(device);
    Mesh* cubeMesh = gApp.resources->getMesh(kAssetBuiltinCube);

    auto checker = checkerPixels(64);
    const AssetID checkerId = gApp.resources->registerGeneratedTexture(
        checker.data(), 64, 64, rhi::Format::RGBA8Srgb, true);
    MaterialDesc materialDesc;
    materialDesc.albedoId = checkerId;
    materialDesc.roughness = 0.55f;
    Material* material = gApp.resources->getMaterial(materialDesc);

    auto sky = skyEquirect(256, 128);
    const AssetID skyId = gApp.resources->registerGeneratedTexture(
        sky.data(), 256, 128, rhi::Format::RGBA8Srgb, true);

    gApp.scene = std::make_unique<Scene>();
    auto cube = std::make_unique<MeshNode>("Cube", cubeMesh, material);
    gApp.cubeNode = cube.get();
    gApp.scene->addChild(std::move(cube));

    auto ground = std::make_unique<MeshNode>("Ground", cubeMesh, material);
    ground->transform().position = glm::vec3(0.0f, -1.6f, 0.0f);
    ground->transform().scale = glm::vec3(6.0f, 0.1f, 6.0f);
    gApp.scene->addChild(std::move(ground));

    auto sun = std::make_unique<LightNode>("Sun", LightType::Directional);
    sun->direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));
    sun->intensity = 3.0f;
    sun->castShadows = true;
    gApp.scene->addChild(std::move(sun));

    // GPU-particle emitter (feature-registry path): a small additive fire
    // fountain above the cube, simulated + drawn by ParticleFeature on WebGPU.
    auto fire = std::make_unique<ParticleSystemNode>();
    fire->transform().position = glm::vec3(0.0f, 0.9f, 0.0f);
    fire->effectClass = ParticleSystemNode::EffectClass::Fire;
    fire->blendMode = ParticleSystemNode::BlendMode::Additive;
    fire->shape = ParticleSystemNode::Shape::Cone;
    fire->maxParticles = 512;
    fire->spawnRate = 120.0f;
    fire->lifetime = 1.6f;
    fire->startSpeed = 1.4f;
    fire->startSize = 0.18f;
    fire->radius = 0.12f;
    fire->coneAngle = 22.0f;
    fire->emissive = 3.0f;
    fire->startColor = glm::vec4(4.0f, 1.6f, 0.4f, 0.9f);
    fire->endColor = glm::vec4(0.6f, 0.15f, 0.05f, 0.0f);
    fire->gravity = glm::vec3(0.0f, 0.6f, 0.0f);  // rises
    gApp.scene->addChild(std::move(fire));

    gApp.scene->settings().clearColor = glm::vec4(0.05f, 0.07f, 0.12f, 1.0f);
    gApp.scene->settings().ambientLight = glm::vec4(0.16f, 0.17f, 0.2f, 1.0f);
    gApp.scene->settings().skyboxTexture = skyId;   // SkyboxFeature + IBL environment
    gApp.scene->settings().skyboxExposure = 1.0f;
    gApp.scene->settings().aoEnabled = true;        // depth-based AO in the tonemap pass
    gApp.scene->settings().aoRadius = 0.6f;
    gApp.scene->settings().aoIntensity = 0.9f;
    gApp.scene->settings().aoPower = 1.35f;
    gApp.scene->settings().bloomEnabled = true;
    gApp.scene->settings().bloomThreshold = 1.0f;
    gApp.scene->settings().bloomIntensity = 0.25f;
    gApp.scene->settings().bloomRadius = 3.0f;

    gApp.camera.position = glm::vec3(3.5f, 2.5f, 3.5f);
    gApp.camera.lookAt(glm::vec3(0.0f));
    gApp.camera.fovDegrees = 55.0f;
    gApp.camera.nearZ = 0.1f;
    gApp.camera.farZ = 100.0f;

    gApp.scene->update(0.0f);
    gApp.renderer = std::make_unique<Renderer>(device, *gApp.surface, *gApp.resources);
    std::printf("saida-web: renderer runtime ready\n");
}

void frame() {
    constexpr float kDt = 1.0f / 60.0f;
    gApp.time += kDt;
    Time::advance(kDt);  // the renderer reads Time::elapsed() for feature timing
    if (gApp.cubeNode) {
        gApp.cubeNode->transform().rotation =
            glm::angleAxis(float(gApp.time) * 0.8f, glm::vec3(0.0f, 1.0f, 0.0f));
    }
    gApp.scene->update(kDt);
    gApp.renderer->drawFrame(*gApp.scene, gApp.camera, nullptr);
}

} // namespace

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwCreateWindow(int(kWidth), int(kHeight), "SaidaEngine web", nullptr, nullptr);

    rhi::Device::requestAsync([](std::unique_ptr<rhi::Device> device) {
        if (!device) {
            std::printf("saida-web: WebGPU unavailable\n");
            return;
        }
        gApp.device = std::move(device);
        gApp.surface = std::make_unique<rhi::Surface>(*gApp.device, "#canvas", kWidth, kHeight);
        initRuntime();
        emscripten_set_main_loop(frame, 0, false);
    });

    emscripten_exit_with_live_runtime();
    return 0;
}
