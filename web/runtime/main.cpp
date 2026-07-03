// Web runtime — Étape 16: the real engine Renderer running on the rhi::webgpu
// backend, loading the actual BeachDemo scene (scenes/beach.scene, preloaded
// into MEMFS at /project). This is the desktop-vs-web parity check: same scene
// data, same drawFrame — shadows + DDGI + water + skybox + AO + bloom + tonemap.
//
// A focused JSON loader (nlohmann) builds the node types the scene uses —
// MeshNode / LightNode / WaterNode / Camera — so the web build stays free of the
// editor's full SceneSerializer/reflection/GLTF stack.

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
#include "scene/WaterNode.hpp"
#include "scene/Scene.hpp"

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <GLFW/glfw3.h>
#include <emscripten.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdio>
#include <fstream>
#include <memory>
#include <vector>

using namespace saida;
using json = nlohmann::json;

namespace {

constexpr uint32_t kWidth = 1280, kHeight = 720;

struct App {
    std::unique_ptr<rhi::Device> device;
    std::unique_ptr<rhi::Surface> surface;
    std::unique_ptr<ResourceManager> resources;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<Scene> scene;
    Camera camera;
    Mesh* cubeMesh = nullptr;
    AssetID whiteTex = kAssetInvalid;
    double time = 0.0;
};

App gApp;

// ---- JSON readers (arrays → glm) ----

glm::vec3 readVec3(const json& a, glm::vec3 fallback = glm::vec3(0.0f)) {
    if (!a.is_array() || a.size() < 3) return fallback;
    return {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
}
glm::vec4 readVec4(const json& a, glm::vec4 fallback = glm::vec4(0.0f)) {
    if (!a.is_array() || a.size() < 4) return fallback;
    return {a[0].get<float>(), a[1].get<float>(), a[2].get<float>(), a[3].get<float>()};
}
// Scene stores quaternions as [x, y, z, w]; glm::quat is (w, x, y, z).
glm::quat readQuat(const json& a, glm::quat fallback = glm::quat(1, 0, 0, 0)) {
    if (!a.is_array() || a.size() < 4) return fallback;
    return {a[3].get<float>(), a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
}
float readF(const json& o, const char* key, float d) {
    return o.contains(key) ? o[key].get<float>() : d;
}

void applyTransform(Node& n, const json& node) {
    if (!node.contains("transform")) return;
    const json& t = node["transform"];
    n.transform().position = readVec3(t.value("position", json::array()), glm::vec3(0.0f));
    n.transform().rotation = readQuat(t.value("rotation", json::array()));
    n.transform().scale = readVec3(t.value("scale", json::array()), glm::vec3(1.0f));
}

// Build a Lit material from a MeshNode's inline PBR fields.
Material* materialFromNode(const json& node) {
    MaterialDesc d;
    d.albedoId = gApp.whiteTex;                        // texture:0 in the scene = white
    d.baseColor = readVec4(node.value("baseColor", json::array()), glm::vec4(1.0f));
    d.emissiveColor = readVec4(node.value("emissive", json::array()), glm::vec4(0.0f));
    d.metallic = readF(node, "metallic", 0.0f);
    d.roughness = readF(node, "roughness", 0.5f);
    d.ao = readF(node, "ao", 1.0f);
    return gApp.resources->getMaterial(d);
}

void loadWater(const json& node, Node& parent) {
    auto w = std::make_unique<WaterNode>();
    applyTransform(*w, node);
    w->size = readF(node, "size", w->size);
    w->deepColor = readVec3(node.value("deepColor", json::array()), w->deepColor);
    w->foamColor = readVec3(node.value("foamColor", json::array()), w->foamColor);
    w->shallowColor = readVec3(node.value("shallowColor", json::array()), w->shallowColor);
    w->roughness = readF(node, "roughness", w->roughness);
    w->reflectivity = readF(node, "reflectivity", w->reflectivity);
    w->amplitude = readF(node, "amplitude", w->amplitude);
    w->wavelength = readF(node, "wavelength", w->wavelength);
    w->waveSpeed = readF(node, "waveSpeed", w->waveSpeed);
    w->choppiness = readF(node, "choppiness", w->choppiness);
    w->detailScale = readF(node, "detailScale", w->detailScale);
    w->detailSpeed = readF(node, "detailSpeed", w->detailSpeed);
    w->detailStrength = readF(node, "detailStrength", w->detailStrength);
    w->detailAngle = readF(node, "detailAngle", w->detailAngle);
    w->detail2Scale = readF(node, "detail2Scale", w->detail2Scale);
    w->detail2Speed = readF(node, "detail2Speed", w->detail2Speed);
    w->detail2Strength = readF(node, "detail2Strength", w->detail2Strength);
    w->detail2Angle = readF(node, "detail2Angle", w->detail2Angle);
    w->fresnelPower = readF(node, "fresnelPower", w->fresnelPower);
    w->specularPower = readF(node, "specularPower", w->specularPower);
    w->specularIntensity = readF(node, "specularIntensity", w->specularIntensity);
    w->foamThreshold = readF(node, "foamThreshold", w->foamThreshold);
    w->foamIntensity = readF(node, "foamIntensity", w->foamIntensity);
    w->warpAmount = readF(node, "warpAmount", w->warpAmount);
    w->detailFadeDistance = readF(node, "detailFadeDistance", w->detailFadeDistance);
    w->depthColorFalloff = readF(node, "depthColorFalloff", w->depthColorFalloff);
    w->edgeFade = readF(node, "edgeFade", w->edgeFade);
    w->shoreSlope = readF(node, "shoreSlope", w->shoreSlope);
    w->shoreMode = static_cast<WaterNode::ShoreMode>(
        static_cast<int>(readF(node, "shoreMode", 0.0f)));
    w->shoreAngle = readF(node, "shoreAngle", w->shoreAngle);
    w->shoreWaterline = readF(node, "shoreWaterline", w->shoreWaterline);
    w->lakeRadius = readF(node, "lakeRadius", w->lakeRadius);
    w->shoreFoam = readF(node, "shoreFoam", w->shoreFoam);
    w->foamWidth = readF(node, "foamWidth", w->foamWidth);
    w->swashSpeed = readF(node, "swashSpeed", w->swashSpeed);
    w->swashAmount = readF(node, "swashAmount", w->swashAmount);
    w->waveFlatten = readF(node, "waveFlatten", w->waveFlatten);
    parent.addChild(std::move(w));
}

void loadLight(const json& node, Node& parent) {
    const auto type = static_cast<LightType>(node.value("lightType", 0));
    auto l = std::make_unique<LightNode>(node.value("name", std::string("Light")), type);
    applyTransform(*l, node);
    l->color = readVec3(node.value("color", json::array()), glm::vec3(1.0f));
    l->intensity = readF(node, "intensity", 1.0f);
    l->range = readF(node, "range", l->range);
    l->direction = readVec3(node.value("direction", json::array()), glm::vec3(0, -1, 0));
    l->castShadows = node.value("castShadows", false);
    parent.addChild(std::move(l));
}

void applyCamera(const json& node) {
    glm::vec3 pos(0.0f);
    glm::quat rot(1, 0, 0, 0);
    if (node.contains("transform")) {
        pos = readVec3(node["transform"].value("position", json::array()), pos);
        rot = readQuat(node["transform"].value("rotation", json::array()));
    }
    gApp.camera.position = pos;
    // Camera looks down -Z in its local frame; rotate that into world space.
    const glm::vec3 forward = glm::normalize(rot * glm::vec3(0.0f, 0.0f, -1.0f));
    gApp.camera.lookAt(pos + forward);
    gApp.camera.fovDegrees = readF(node, "fovDegrees", 60.0f);
    gApp.camera.nearZ = readF(node, "nearZ", 0.1f);
    gApp.camera.farZ = readF(node, "farZ", 600.0f);
}

void loadNode(const json& node, Node& parent) {
    const std::string type = node.value("type", "");
    if (!node.value("enabled", true)) return;

    if (type == "MeshNode") {
        auto m = std::make_unique<MeshNode>(node.value("name", std::string("Mesh")),
                                            gApp.cubeMesh, materialFromNode(node));
        applyTransform(*m, node);
        m->castShadows() = node.value("castShadows", true);
        Node& ref = *m;
        parent.addChild(std::move(m));
        for (const json& c : node.value("children", json::array())) loadNode(c, ref);
        return;
    }
    if (type == "Water")  { loadWater(node, parent); return; }
    if (type == "LightNode") { loadLight(node, parent); return; }
    if (type == "Camera")  { applyCamera(node); return; }

    // Unknown/plain node: keep the hierarchy, recurse into children.
    for (const json& c : node.value("children", json::array())) loadNode(c, parent);
}

void applySettings(const json& s) {
    SceneSettings& out = gApp.scene->settings();
    out.ambientLight = glm::vec4(readVec3(s.value("ambient", json::array()), glm::vec3(0.1f)), 1.0f);
    out.clearColor = glm::vec4(readVec3(s.value("clearColor", json::array()), glm::vec3(0.0f)), 1.0f);
    out.aoEnabled = s.value("aoEnabled", true);
    out.aoRadius = readF(s, "aoRadius", out.aoRadius);
    out.aoIntensity = readF(s, "aoIntensity", out.aoIntensity);
    out.aoPower = readF(s, "aoPower", out.aoPower);
    out.bloomEnabled = s.value("bloomEnabled", true);
    out.bloomThreshold = readF(s, "bloomThreshold", out.bloomThreshold);
    out.bloomIntensity = readF(s, "bloomIntensity", out.bloomIntensity);
    out.bloomRadius = readF(s, "bloomRadius", out.bloomRadius);
    out.fogEnabled = s.value("fogEnabled", false);
    out.fogColor = glm::vec4(readVec3(s.value("fogColor", json::array()), glm::vec3(0.5f)), 1.0f);
    out.fogStart = readF(s, "fogStart", out.fogStart);
    out.fogDensity = readF(s, "fogDensity", out.fogDensity);
    out.lightingMode = static_cast<LightingMode>(s.value("lightingMode", 0));
    out.giMode = static_cast<GIMode>(s.value("giMode", 0));
    out.giEnabled = s.value("giEnabled", true);
    out.giIntensity = readF(s, "giIntensity", out.giIntensity);
    out.iblEnabled = s.value("iblEnabled", true);
    out.iblDiffuseIntensity = readF(s, "iblDiffuseIntensity", out.iblDiffuseIntensity);
    out.iblSpecularIntensity = readF(s, "iblSpecularIntensity", out.iblSpecularIntensity);
    out.skyboxExposure = readF(s, "skyboxExposure", 1.0f);
    out.skyboxRotation = readF(s, "skyboxRotation", 0.0f);
}

// Loads /project/sunset.png (preloaded into MEMFS) as the skybox + IBL source.
AssetID loadSkyTexture() {
    int w = 0, h = 0, ch = 0;
    stbi_uc* px = stbi_load("/project/sunset.png", &w, &h, &ch, 4);
    if (!px) {
        std::printf("saida-web: sunset.png not found — sky stays black\n");
        return kAssetInvalid;
    }
    const AssetID id = gApp.resources->registerGeneratedTexture(
        px, uint32_t(w), uint32_t(h), rhi::Format::RGBA8Srgb, true);
    stbi_image_free(px);
    std::printf("saida-web: loaded sunset.png %dx%d\n", w, h);
    return id;
}

void initRuntime() {
    rhi::Device& device = *gApp.device;
    registerBuiltinRenderFeatures();  // water + skybox + particles on web

    gApp.resources = std::make_unique<ResourceManager>(device);
    gApp.cubeMesh = gApp.resources->getMesh(kAssetBuiltinCube);

    const uint8_t white[4] = {255, 255, 255, 255};
    gApp.whiteTex = gApp.resources->registerGeneratedTexture(white, 1, 1, rhi::Format::RGBA8Srgb, false);

    gApp.scene = std::make_unique<Scene>();

    std::ifstream in("/project/beach.scene");
    if (!in) { std::printf("saida-web: beach.scene missing\n"); return; }
    json doc;
    in >> doc;
    const json& root = doc["scene"];

    if (root.contains("settings")) applySettings(root["settings"]);
    const AssetID skyId = loadSkyTexture();
    if (skyId != kAssetInvalid) gApp.scene->settings().skyboxTexture = skyId;

    for (const json& c : root.value("children", json::array())) loadNode(c, *gApp.scene);

    gApp.scene->update(0.0f);
    gApp.renderer = std::make_unique<Renderer>(device, *gApp.surface, *gApp.resources);
    std::printf("saida-web: BeachDemo loaded — %zu meshes, %zu waters, %zu lights\n",
                gApp.scene->meshes().size(), gApp.scene->waterNodes().size(),
                gApp.scene->lights().size());
}

void frame() {
    constexpr float kDt = 1.0f / 60.0f;
    gApp.time += kDt;
    Time::advance(kDt);  // the water/skybox read Time::elapsed() for animation
    if (gApp.scene && gApp.renderer) {
        gApp.scene->update(kDt);
        gApp.renderer->drawFrame(*gApp.scene, gApp.camera, nullptr);
    }
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
