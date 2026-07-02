// Web runtime — Étape 16.4 validation target: the real engine shaders (16.2
// WGSL) running on the real rhi::webgpu backend, rendering a Lit scene through
// the engine's two-pass frame (HDR scene pass -> tonemap to canvas) in the
// browser. This is the "chemin minimal Lit + tonemap" of PLAN_WEB_EXPORT §9:
// the full Renderer port on top of this backend is 16.5.
//
// Everything binds exactly like desktop: set 0 = global (camera, lighting,
// shadow array, bones, GI atlases, environment), set 1 = material, set 3 =
// push-constant UBO (web_compat). GI/shadows are bound with neutral dummies —
// the same resources the desktop renderer starts from before its passes run.

#include "rhi/webgpu/RhiWeb.hpp"

#include <GLFW/glfw3.h>
#include <emscripten.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdio>
#include <memory>
#include <vector>

using namespace saida;

namespace {

constexpr uint32_t kWidth = 1280, kHeight = 720;

// ---- CPU-side mirrors of the engine's GPU blocks (Renderer.hpp) ----

struct UniformBufferObject {
    alignas(16) glm::mat4 view[2];
    alignas(16) glm::mat4 proj[2];
};

struct PushConstants {
    glm::mat4 model;
    glm::vec4 params;  // y = bone offset, -1 = unskinned
};

constexpr int kMaxLights = 16;
constexpr int kMaxShadowCasters = 4;

struct GpuLight {
    glm::vec4 posRange{0.0f};
    glm::vec4 colorInt{0.0f};
    glm::vec4 dirType{0.0f};
    glm::vec4 spotShadow{0.0f, 0.0f, -1.0f, 0.0f};
};

struct LightingUBO {
    glm::vec4 ambient{0.0f};
    glm::vec4 cameraPos{0.0f};
    glm::vec4 shadowParams{0.0f};
    glm::ivec4 counts{0};
    GpuLight lights[kMaxLights]{};
    glm::mat4 shadowMatrices[kMaxShadowCasters]{};
    glm::vec4 giOrigin{0.0f};
    glm::vec4 giSpacing{1.0f};
    glm::ivec4 giCounts{0};
    glm::ivec4 giAtlas{0};
    glm::vec4 environmentParams{0.0f};
};

struct MaterialParams {
    glm::vec4 baseColor;
    float metallic;
    float roughness;
    float ao;
    float _pad;
    glm::vec4 emissive;
};

struct TonemapPushConstants {
    glm::mat4 invProjection{1.0f};
    glm::vec4 aoParams{0.0f};
    glm::vec4 fogColor{0.0f};
    glm::vec4 fogParams{0.0f};
    glm::vec4 bloomParams{0.0f};
    glm::vec4 sourceRect{0.0f, 0.0f, 1.0f, 1.0f};
    glm::vec4 projectionParams{0.0f};
    glm::vec4 projectionParams2{0.0f};
};

// Mirror of saida::Vertex (graphics/Mesh.hpp): 100 bytes, 8 attributes.
struct WebVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec2 lightmapUV;
    glm::vec4 tangent;
    int32_t boneIndices[4];
    glm::vec4 boneWeights;
};
static_assert(sizeof(WebVertex) == 100, "WebVertex must mirror saida::Vertex");

struct App {
    std::unique_ptr<rhi::Device> device;
    std::unique_ptr<rhi::Surface> surface;

    // Scene pass
    std::unique_ptr<rhi::BindGroupLayout> globalLayout;
    std::unique_ptr<rhi::BindGroupLayout> materialLayout;
    std::unique_ptr<rhi::Pipeline> scenePipeline;
    std::unique_ptr<rhi::BindGroup> globalGroup;
    std::unique_ptr<rhi::BindGroup> materialGroup;
    std::unique_ptr<rhi::Buffer> cameraUbo;
    std::unique_ptr<rhi::Buffer> lightingUbo;
    std::unique_ptr<rhi::Buffer> bonesSsbo;
    std::unique_ptr<rhi::Buffer> materialUbo;
    std::unique_ptr<rhi::Buffer> vertexBuffer;
    std::unique_ptr<rhi::Buffer> indexBuffer;
    uint32_t indexCount = 0;

    // Neutral global resources (what desktop binds before its passes ran)
    std::unique_ptr<rhi::RenderTexture> shadowArray;   // depth 2D array, zeroed
    std::unique_ptr<rhi::RenderTexture> giIrradiance;  // 1x1 rgba16f
    std::unique_ptr<rhi::RenderTexture> giVisibility;  // 1x1 rg16f
    std::unique_ptr<rhi::RenderTexture> giVoxels;      // tiny 3D grid
    std::unique_ptr<rhi::Texture> environment;         // 1x1 white
    std::unique_ptr<rhi::Texture> albedo;              // checkerboard
    std::unique_ptr<rhi::Texture> normalMap;           // flat normal
    std::unique_ptr<rhi::Texture> white;
    std::unique_ptr<rhi::Texture> black;
    std::unique_ptr<rhi::Sampler> linearSampler;
    std::unique_ptr<rhi::Sampler> nearestSampler;
    std::unique_ptr<rhi::Sampler> shadowSampler;       // comparison (PCF)

    // Tonemap pass
    std::unique_ptr<rhi::RenderTexture> hdr;
    std::unique_ptr<rhi::RenderTexture> depth;
    std::unique_ptr<rhi::BindGroupLayout> tonemapLayout;
    std::unique_ptr<rhi::Pipeline> tonemapPipeline;
    std::unique_ptr<rhi::BindGroup> tonemapGroup;

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

void buildCube(std::vector<WebVertex>& verts, std::vector<uint32_t>& indices) {
    const glm::vec3 positions[8] = {
        {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
        {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}};
    const int faces[6][4] = {{4, 5, 6, 7}, {1, 0, 3, 2}, {5, 1, 2, 6},
                             {0, 4, 7, 3}, {7, 6, 2, 3}, {0, 1, 5, 4}};
    const glm::vec3 normals[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0},
                                  {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (int f = 0; f < 6; ++f) {
        const uint32_t base = uint32_t(verts.size());
        const glm::vec2 uv[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        for (int i = 0; i < 4; ++i) {
            WebVertex v{};
            v.pos = positions[faces[f][i]];
            v.normal = normals[f];
            v.color = glm::vec3(1.0f);
            v.texCoord = uv[i];
            v.lightmapUV = uv[i];
            v.tangent = glm::vec4(1, 0, 0, 1);
            v.boneIndices[0] = v.boneIndices[1] = v.boneIndices[2] = v.boneIndices[3] = 0;
            v.boneWeights = glm::vec4(0.0f);
            verts.push_back(v);
        }
        for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
            indices.push_back(base + i);
    }
}

void initResources() {
    rhi::Device& device = *gApp.device;

    // ---- Geometry: a cube + a ground slab, both in one buffer ----
    std::vector<WebVertex> verts;
    std::vector<uint32_t> indices;
    buildCube(verts, indices);
    gApp.indexCount = uint32_t(indices.size());

    gApp.vertexBuffer = std::make_unique<rhi::Buffer>(
        device, verts.size() * sizeof(WebVertex), rhi::BufferUsage::Vertex,
        rhi::MemoryUsage::HostVisible);
    gApp.vertexBuffer->write(verts.data(), verts.size() * sizeof(WebVertex));
    gApp.indexBuffer = std::make_unique<rhi::Buffer>(
        device, indices.size() * sizeof(uint32_t), rhi::BufferUsage::Index,
        rhi::MemoryUsage::HostVisible);
    gApp.indexBuffer->write(indices.data(), indices.size() * sizeof(uint32_t));

    // ---- Uniforms ----
    gApp.cameraUbo = std::make_unique<rhi::Buffer>(device, sizeof(UniformBufferObject),
                                                   rhi::BufferUsage::Uniform,
                                                   rhi::MemoryUsage::HostVisible);
    gApp.lightingUbo = std::make_unique<rhi::Buffer>(device, sizeof(LightingUBO),
                                                     rhi::BufferUsage::Uniform,
                                                     rhi::MemoryUsage::HostVisible);
    gApp.bonesSsbo = std::make_unique<rhi::Buffer>(device, sizeof(glm::mat4),
                                                   rhi::BufferUsage::Storage,
                                                   rhi::MemoryUsage::HostVisible);
    const glm::mat4 identity(1.0f);
    gApp.bonesSsbo->write(&identity, sizeof(identity));

    MaterialParams material{};
    material.baseColor = glm::vec4(1.0f);
    material.metallic = 0.0f;
    material.roughness = 0.55f;
    material.ao = 1.0f;
    material.emissive = glm::vec4(0.0f);
    gApp.materialUbo = std::make_unique<rhi::Buffer>(device, sizeof(MaterialParams),
                                                     rhi::BufferUsage::Uniform,
                                                     rhi::MemoryUsage::HostVisible);
    gApp.materialUbo->write(&material, sizeof(material));

    // ---- Textures / samplers ----
    auto checker = checkerPixels(64);
    gApp.albedo = std::make_unique<rhi::Texture>(device, checker.data(), 64, 64);
    const uint8_t flatNormal[4] = {128, 128, 255, 255};
    gApp.normalMap = std::make_unique<rhi::Texture>(device, flatNormal, 1, 1,
                                                    rhi::Format::RGBA8Unorm, false);
    const uint8_t whitePx[4] = {255, 255, 255, 255};
    const uint8_t blackPx[4] = {0, 0, 0, 255};
    gApp.white = std::make_unique<rhi::Texture>(device, whitePx, 1, 1,
                                                rhi::Format::RGBA8Srgb, false);
    gApp.black = std::make_unique<rhi::Texture>(device, blackPx, 1, 1,
                                                rhi::Format::RGBA8Srgb, false);
    gApp.environment = std::make_unique<rhi::Texture>(device, whitePx, 1, 1,
                                                      rhi::Format::RGBA8Srgb, false);

    gApp.linearSampler = std::make_unique<rhi::Sampler>(device, rhi::SamplerDesc{});
    rhi::SamplerDesc nearest;
    nearest.magFilter = rhi::FilterMode::Nearest;
    nearest.minFilter = rhi::FilterMode::Nearest;
    gApp.nearestSampler = std::make_unique<rhi::Sampler>(device, nearest);
    rhi::SamplerDesc shadow;
    shadow.compareEnabled = true;
    gApp.shadowSampler = std::make_unique<rhi::Sampler>(device, shadow);

    // ---- Neutral global targets (WebGPU zero-initialises textures) ----
    rhi::RenderTextureDesc shadowDesc;
    shadowDesc.format = rhi::Format::Depth32Float;
    shadowDesc.width = shadowDesc.height = 1;
    shadowDesc.layers = kMaxShadowCasters;
    shadowDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::DepthAttachment;
    gApp.shadowArray = std::make_unique<rhi::RenderTexture>(device, shadowDesc);

    rhi::RenderTextureDesc atlasDesc;
    atlasDesc.width = atlasDesc.height = 1;
    atlasDesc.usage = rhi::TextureUsage::Sampled;
    atlasDesc.format = rhi::Format::RGBA16Float;
    gApp.giIrradiance = std::make_unique<rhi::RenderTexture>(device, atlasDesc);
    atlasDesc.format = rhi::Format::RG16Float;
    gApp.giVisibility = std::make_unique<rhi::RenderTexture>(device, atlasDesc);

    rhi::RenderTextureDesc voxelDesc;
    voxelDesc.format = rhi::Format::RGBA16Float;
    voxelDesc.width = voxelDesc.height = 1;
    voxelDesc.depth = 2;  // > 1 → 3D, as the shader's texture_3d expects
    voxelDesc.usage = rhi::TextureUsage::Sampled;
    gApp.giVoxels = std::make_unique<rhi::RenderTexture>(device, voxelDesc);

    // ---- HDR + depth targets ----
    rhi::RenderTextureDesc hdrDesc;
    hdrDesc.format = rhi::Format::RGBA16Float;
    hdrDesc.width = kWidth;
    hdrDesc.height = kHeight;
    hdrDesc.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled;
    gApp.hdr = std::make_unique<rhi::RenderTexture>(device, hdrDesc);

    rhi::RenderTextureDesc depthDesc;
    depthDesc.format = rhi::Format::Depth32Float;
    depthDesc.width = kWidth;
    depthDesc.height = kHeight;
    depthDesc.usage = rhi::TextureUsage::DepthAttachment | rhi::TextureUsage::Sampled;
    gApp.depth = std::make_unique<rhi::RenderTexture>(device, depthDesc);

    // ---- Bind group layouts: 1:1 with the WGSL @group/@binding tables ----
    using WE = rhi::webgpu::BindGroupLayoutEntry;
    using Dim = rhi::webgpu::TextureDim;
    const auto VF = rhi::ShaderStages::VertexFragment;
    const auto F = rhi::ShaderStages::Fragment;
    const auto V = rhi::ShaderStages::Vertex;

    std::vector<WE> globalEntries;
    {
        WE e{};
        e.binding = 0; e.type = rhi::BindingType::UniformBuffer; e.visibility = V;
        globalEntries.push_back(e);                       // camera
        e = {}; e.binding = 1; e.type = rhi::BindingType::UniformBuffer; e.visibility = F;
        globalEntries.push_back(e);                       // lighting
        e = {}; e.binding = 2; e.type = rhi::BindingType::SampledTexture; e.visibility = F;
        e.dim = Dim::Dim2DArray; e.depthTexture = true;
        globalEntries.push_back(e);                       // shadow array
        e = {}; e.binding = 3; e.type = rhi::BindingType::StorageBuffer; e.visibility = V;
        e.readOnlyStorage = true;
        globalEntries.push_back(e);                       // bones
        e = {}; e.binding = 4; e.type = rhi::BindingType::SampledTexture; e.visibility = F;
        globalEntries.push_back(e);                       // gi irradiance
        e = {}; e.binding = 5; e.type = rhi::BindingType::SampledTexture; e.visibility = F;
        globalEntries.push_back(e);                       // gi visibility
        e = {}; e.binding = 6; e.type = rhi::BindingType::SampledTexture; e.visibility = F;
        e.dim = Dim::Dim3D;
        globalEntries.push_back(e);                       // gi voxels
        e = {}; e.binding = 7; e.type = rhi::BindingType::SampledTexture; e.visibility = F;
        globalEntries.push_back(e);                       // environment
        e = {}; e.binding = 8; e.type = rhi::BindingType::Sampler; e.visibility = F;
        e.comparisonSampler = true;
        globalEntries.push_back(e);                       // shadow sampler
        for (uint32_t b : {9u, 10u, 11u, 12u}) {          // gi/env samplers
            e = {}; e.binding = b; e.type = rhi::BindingType::Sampler; e.visibility = F;
            globalEntries.push_back(e);
        }
    }
    gApp.globalLayout = std::make_unique<rhi::BindGroupLayout>(device, globalEntries);

    std::vector<WE> materialEntries;
    {
        WE e{};
        for (uint32_t b : {0u, 1u, 2u, 4u}) {             // albedo/normal/mr/emissive
            e = {}; e.binding = b; e.type = rhi::BindingType::SampledTexture; e.visibility = F;
            materialEntries.push_back(e);
        }
        e = {}; e.binding = 3; e.type = rhi::BindingType::UniformBuffer; e.visibility = F;
        materialEntries.push_back(e);                     // params
        for (uint32_t b : {5u, 6u, 7u, 8u}) {             // their samplers
            e = {}; e.binding = b; e.type = rhi::BindingType::Sampler; e.visibility = F;
            materialEntries.push_back(e);
        }
    }
    gApp.materialLayout = std::make_unique<rhi::BindGroupLayout>(device, materialEntries);

    std::vector<WE> tonemapEntries;
    {
        WE e{};
        e.binding = 0; e.type = rhi::BindingType::SampledTexture; e.visibility = F;
        tonemapEntries.push_back(e);                      // hdr
        e = {}; e.binding = 1; e.type = rhi::BindingType::SampledTexture; e.visibility = F;
        e.unfilterable = true;
        tonemapEntries.push_back(e);                      // depth (as float)
        e = {}; e.binding = 2; e.type = rhi::BindingType::SampledTexture; e.visibility = F;
        tonemapEntries.push_back(e);                      // bloom
        e = {}; e.binding = 3; e.type = rhi::BindingType::Sampler; e.visibility = F;
        tonemapEntries.push_back(e);
        e = {}; e.binding = 4; e.type = rhi::BindingType::Sampler; e.visibility = F;
        e.nonFilteringSampler = true;
        tonemapEntries.push_back(e);
        e = {}; e.binding = 5; e.type = rhi::BindingType::Sampler; e.visibility = F;
        tonemapEntries.push_back(e);
    }
    gApp.tonemapLayout = std::make_unique<rhi::BindGroupLayout>(device, tonemapEntries);

    // ---- Pipelines (the real transpiled shaders) ----
    rhi::Pipeline::Desc scene;
    scene.vertPath = "/shaders/shader.vert.wgsl";
    scene.fragPath = "/shaders/shader.frag.wgsl";
    scene.colorFormats = {rhi::Format::RGBA16Float};
    scene.depthFormat = rhi::Format::Depth32Float;
    scene.bindGroupLayouts = {gApp.globalLayout.get(), gApp.materialLayout.get()};
    scene.pushConstantSize = sizeof(PushConstants);
    gApp.scenePipeline = std::make_unique<rhi::Pipeline>(device, scene);

    rhi::Pipeline::Desc tonemap;
    tonemap.vertPath = "/shaders/tonemap.vert.wgsl";
    tonemap.fragPath = "/shaders/tonemap.frag.wgsl";
    tonemap.colorFormats = {rhi::Format::BGRA8Unorm};
    tonemap.vertexInput = false;
    tonemap.depthTest = false;
    tonemap.depthWrite = false;
    tonemap.cullMode = rhi::CullMode::None;
    tonemap.pushConstantSize = sizeof(TonemapPushConstants);
    gApp.tonemapPipeline = std::make_unique<rhi::Pipeline>(device, tonemap);

    // ---- Bind groups ----
    using BE = rhi::BindGroupEntry;
    std::vector<BE> globalBind;
    {
        BE e{};
        e.binding = 0; e.buffer = gApp.cameraUbo.get(); globalBind.push_back(e);
        e = {}; e.binding = 1; e.buffer = gApp.lightingUbo.get(); globalBind.push_back(e);
        e = {}; e.binding = 2; e.view = gApp.shadowArray->view(); globalBind.push_back(e);
        e = {}; e.binding = 3; e.buffer = gApp.bonesSsbo.get(); globalBind.push_back(e);
        e = {}; e.binding = 4; e.view = gApp.giIrradiance->view(); globalBind.push_back(e);
        e = {}; e.binding = 5; e.view = gApp.giVisibility->view(); globalBind.push_back(e);
        e = {}; e.binding = 6; e.view = gApp.giVoxels->view(); globalBind.push_back(e);
        e = {}; e.binding = 7; e.view = gApp.environment->imageView(); globalBind.push_back(e);
        e = {}; e.binding = 8; e.sampler = gApp.shadowSampler->handle(); globalBind.push_back(e);
        for (uint32_t b : {9u, 10u, 11u, 12u}) {
            e = {}; e.binding = b; e.sampler = gApp.linearSampler->handle();
            globalBind.push_back(e);
        }
    }
    gApp.globalGroup = std::make_unique<rhi::BindGroup>(*gApp.globalLayout, globalBind);

    std::vector<BE> materialBind;
    {
        BE e{};
        e.binding = 0; e.view = gApp.albedo->imageView(); materialBind.push_back(e);
        e = {}; e.binding = 1; e.view = gApp.normalMap->imageView(); materialBind.push_back(e);
        e = {}; e.binding = 2; e.view = gApp.white->imageView(); materialBind.push_back(e);
        e = {}; e.binding = 3; e.buffer = gApp.materialUbo.get(); materialBind.push_back(e);
        e = {}; e.binding = 4; e.view = gApp.black->imageView(); materialBind.push_back(e);
        for (uint32_t b : {5u, 6u, 7u, 8u}) {
            e = {}; e.binding = b; e.sampler = gApp.linearSampler->handle();
            materialBind.push_back(e);
        }
    }
    gApp.materialGroup = std::make_unique<rhi::BindGroup>(*gApp.materialLayout, materialBind);

    std::vector<BE> tonemapBind;
    {
        BE e{};
        e.binding = 0; e.view = gApp.hdr->view(); tonemapBind.push_back(e);
        e = {}; e.binding = 1; e.view = gApp.depth->view(); tonemapBind.push_back(e);
        e = {}; e.binding = 2; e.view = gApp.black->imageView(); tonemapBind.push_back(e);
        e = {}; e.binding = 3; e.sampler = gApp.linearSampler->handle(); tonemapBind.push_back(e);
        e = {}; e.binding = 4; e.sampler = gApp.nearestSampler->handle(); tonemapBind.push_back(e);
        e = {}; e.binding = 5; e.sampler = gApp.linearSampler->handle(); tonemapBind.push_back(e);
    }
    gApp.tonemapGroup = std::make_unique<rhi::BindGroup>(*gApp.tonemapLayout, tonemapBind);

    std::printf("saida-web: resources ready — Lit + tonemap on rhi::webgpu\n");
}

void frame() {
    gApp.time += 1.0 / 60.0;
    rhi::Device& device = *gApp.device;

    uint32_t imageIndex = 0;
    if (!gApp.surface->acquire(0, imageIndex)) return;

    // ---- Per-frame CPU data ----
    const float aspect = float(kWidth) / float(kHeight);
    const glm::vec3 eye(3.5f, 2.5f, 3.5f);
    UniformBufferObject cam{};
    cam.view[0] = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0, 1, 0));
    glm::mat4 proj = glm::perspective(glm::radians(55.0f), aspect, 0.1f, 100.0f);
    proj[1][1] *= -1.0f;  // the engine bakes the Vulkan Y flip into projection
    cam.proj[0] = proj;
    cam.view[1] = cam.view[0];
    cam.proj[1] = cam.proj[0];
    gApp.cameraUbo->write(&cam, sizeof(cam));

    LightingUBO lighting{};
    lighting.ambient = glm::vec4(0.16f, 0.17f, 0.2f, 1.0f);
    lighting.cameraPos = glm::vec4(eye, 1.0f);
    lighting.counts = glm::ivec4(1, 0, 0, 0);
    lighting.lights[0].dirType = glm::vec4(glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f)), 0.0f);
    lighting.lights[0].colorInt = glm::vec4(1.0f, 0.96f, 0.9f, 3.0f);
    gApp.lightingUbo->write(&lighting, sizeof(lighting));

    rhi::CommandEncoder encoder(device);

    // ---- Scene pass (HDR) ----
    {
        rhi::RenderPassDesc pass;
        pass.colorCount = 1;
        pass.colors[0].view = gApp.hdr->view();
        pass.colors[0].clearColor = {{0.05f, 0.07f, 0.12f, 1.0f}};
        pass.depth.view = gApp.depth->view();
        pass.width = kWidth;
        pass.height = kHeight;
        rhi::RenderPassEncoder rp = encoder.beginRenderPass(pass);
        rp.setPipeline(*gApp.scenePipeline);
        rp.setBindGroup(0, *gApp.globalGroup);
        rp.setBindGroup(1, *gApp.materialGroup);
        rp.setVertexBuffer(*gApp.vertexBuffer);
        rp.setIndexBuffer(*gApp.indexBuffer);

        // Spinning cube.
        PushConstants pc{};
        pc.model = glm::rotate(glm::mat4(1.0f), float(gApp.time) * 0.8f, glm::vec3(0, 1, 0));
        pc.params = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);  // unskinned
        rp.setPushConstants(&pc, sizeof(pc));
        rp.drawIndexed(gApp.indexCount);

        // Ground slab (same cube squashed).
        pc.model = glm::translate(glm::mat4(1.0f), glm::vec3(0, -1.6f, 0)) *
                   glm::scale(glm::mat4(1.0f), glm::vec3(6.0f, 0.1f, 6.0f));
        rp.setPushConstants(&pc, sizeof(pc));
        rp.drawIndexed(gApp.indexCount);
        rp.end();
    }

    // ---- Tonemap to canvas ----
    {
        rhi::RenderPassDesc pass;
        pass.colorCount = 1;
        pass.colors[0].view = gApp.surface->currentView();
        pass.width = kWidth;
        pass.height = kHeight;
        rhi::RenderPassEncoder rp = encoder.beginRenderPass(pass);
        rp.setPipeline(*gApp.tonemapPipeline);
        rp.setBindGroup(0, *gApp.tonemapGroup);
        TonemapPushConstants push{};
        push.fogParams.w = 1.0f;  // exposure
        rp.setPushConstants(&push, sizeof(push));
        rp.draw(3);
        rp.end();
    }

    gApp.surface->submitAndPresent(encoder.finish(), 0, imageIndex);
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
        initResources();
        emscripten_set_main_loop(frame, 0, false);
    });

    emscripten_exit_with_live_runtime();
    return 0;
}
