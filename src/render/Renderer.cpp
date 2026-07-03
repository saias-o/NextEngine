#include "render/Renderer.hpp"

#include "core/Camera.hpp"
#include "core/Paths.hpp"
#include "project/Project.hpp"
#include "graphics/Buffer.hpp"
#include "core/Profiler.hpp"
#include "graphics/GpuProfiler.hpp"
#include "graphics/ImGuiLayer.hpp"
#include "graphics/Material.hpp"
#include "graphics/Mesh.hpp"
#include "graphics/Pipeline.hpp"
#include "graphics/ShadowMap.hpp"
#include "graphics/ResourceManager.hpp"
#include "render/GIVolume.hpp"
#include "render/PostProcessor.hpp"
#ifndef SAIDA_RHI_WEBGPU
#include "graphics/UIRenderer.hpp"
#endif
#include "scene/LightNode.hpp"
#include "scene/Node.hpp"
#include "scene/Scene.hpp"
#include "core/Time.hpp"
#include "render/RenderFeatureRegistry.hpp"
#include "core/Log.hpp"

#ifndef SAIDA_RHI_WEBGPU
#include "graphics/MemoryProfiler.hpp"
#include "graphics/Swapchain.hpp"
#include "graphics/VulkanDevice.hpp"
#include "rhi/vulkan/Format.hpp"
#endif

#include "scene/LightNode.hpp"
#include "scene/MeshNode.hpp"
#include "scene/MeshLod.hpp"
#ifndef SAIDA_RHI_WEBGPU
#include "scene/WebCanvasNode.hpp"
#endif
#include "scene/animation/Animator.hpp"
#ifdef SAIDA_ENABLE_XR
#include "xr/XrSession.hpp"   // xr::EyeView
#include "xr/toolkit/XRPassthrough.hpp"
#endif

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include "graphics/ComputePipeline.hpp"
#include "graphics/Texture.hpp"
#include <stdexcept>

namespace saida {

namespace {
constexpr int kMaxFramesInFlight = 2;

// Directional shadows cover a fixed-size world box centered on the origin; large
// enough for the demo scene, easy to grow later (or fit to the camera/scene AABB).
constexpr float kShadowOrthoHalfSize = 25.0f;
constexpr float kShadowOrthoDepth = 100.0f;
constexpr uint32_t kAllTextureLayers = ~0u;

// Picks an up vector not colinear with the light direction.
glm::vec3 safeUp(const glm::vec3& dir) {
    return std::abs(dir.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
}

// Light-space view-proj for a directional light (orthographic), flipped for Vulkan.
glm::mat4 directionalMatrix(const glm::vec3& dir, float distance) {
    float halfSize = distance;
    float depth = distance * 4.0f;
    glm::vec3 eye = -dir * (depth * 0.5f);
    glm::mat4 view = glm::lookAt(eye, glm::vec3(0.0f), safeUp(dir));
    glm::mat4 proj = glm::ortho(-halfSize, halfSize,
                                -halfSize, halfSize,
                                0.1f, depth);
    proj[1][1] *= -1.0f;
    return proj * view;
}

#ifndef SAIDA_RHI_WEBGPU
struct WebCanvasWorldDraw {
    WebCanvasNode* node = nullptr;
    float distance = 0.0f;
};

struct DrawIndexedIndirectCommand {
    uint32_t indexCount = 0;
    uint32_t instanceCount = 0;
    uint32_t firstIndex = 0;
    int32_t vertexOffset = 0;
    uint32_t firstInstance = 0;
};
static_assert(sizeof(DrawIndexedIndirectCommand) == 20,
              "DrawIndexedIndirectCommand must match Vulkan/WebGPU indirect args");

std::vector<WebCanvasWorldDraw> collectWorldWebCanvasDraws(Scene& scene, glm::vec3 viewpoint) {
    std::vector<WebCanvasWorldDraw> draws;
    for (WebCanvasNode* node : scene.webCanvases()) {
        if (!node) continue;
        if (node->mode() != WebCanvasNode::Mode::WorldSpace || !node->texture()) continue;
        glm::vec3 center = glm::vec3(node->worldTransform() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        draws.push_back({node, glm::length(center - viewpoint)});
    }
    std::stable_sort(draws.begin(), draws.end(), [](const WebCanvasWorldDraw& a, const WebCanvasWorldDraw& b) {
        if (a.node->renderOrder() != b.node->renderOrder()) {
            return a.node->renderOrder() < b.node->renderOrder();
        }
        return a.distance > b.distance;
    });
    return draws;
}
#endif

#ifdef SAIDA_RHI_WEBGPU
struct DrawIndexedIndirectCommand {
    uint32_t indexCount = 0;
    uint32_t instanceCount = 0;
    uint32_t firstIndex = 0;
    int32_t vertexOffset = 0;
    uint32_t firstInstance = 0;
};
static_assert(sizeof(DrawIndexedIndirectCommand) == 20,
              "DrawIndexedIndirectCommand must match Vulkan/WebGPU indirect args");
#endif

// DDGI volume configuration scaled by the GPU's quality tier. Denser probes +
// more rays = finer, cleaner GI; lower tiers (mobile/Quest) stay cheap. Same
// world coverage, centered on the origin.
GIVolumeDesc giDescForTier(QualityTier tier) {
    GIVolumeDesc d;
    switch (tier) {
        case QualityTier::Ultra:
        case QualityTier::High:
            d.counts = {20, 10, 20}; d.spacing = glm::vec3(1.2f);
            d.raysPerProbe = 96; d.voxelResolution = 96;
            break;
        case QualityTier::Medium:
            d.counts = {16, 8, 16}; d.spacing = glm::vec3(1.5f);
            d.raysPerProbe = 64; d.voxelResolution = 80;
            break;
        case QualityTier::Low:
        default:
            d.counts = {10, 5, 10}; d.spacing = glm::vec3(2.4f);
            d.raysPerProbe = 48; d.voxelResolution = 64;
            break;
    }
    // Center the lattice on the world origin.
    d.origin = -0.5f * glm::vec3(d.counts - 1) * d.spacing;
    return d;
}

#ifndef SAIDA_RHI_WEBGPU
uint32_t sampleCountValue(VkSampleCountFlagBits samples) {
    switch (samples) {
        case VK_SAMPLE_COUNT_2_BIT: return 2;
        case VK_SAMPLE_COUNT_4_BIT: return 4;
        case VK_SAMPLE_COUNT_8_BIT: return 8;
        case VK_SAMPLE_COUNT_16_BIT: return 16;
        case VK_SAMPLE_COUNT_32_BIT: return 32;
        case VK_SAMPLE_COUNT_64_BIT: return 64;
        case VK_SAMPLE_COUNT_1_BIT:
        default: return 1;
    }
}

uint64_t imageBytes(VkExtent2D extent, uint32_t bytesPerPixel,
                    uint32_t layers = 1, uint32_t samples = 1) {
    return static_cast<uint64_t>(extent.width) * extent.height *
           layers * samples * bytesPerPixel;
}
#endif

void hashCombine(uint64_t& h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
}

void hashFloat(uint64_t& h, float v) {
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(v), "float hash expects 32-bit floats");
    std::memcpy(&bits, &v, sizeof(bits));
    hashCombine(h, bits);
}

void hashVec4(uint64_t& h, const glm::vec4& v) {
    hashFloat(h, v.x);
    hashFloat(h, v.y);
    hashFloat(h, v.z);
    hashFloat(h, v.w);
}

void hashMat4(uint64_t& h, const glm::mat4& m) {
    for (int c = 0; c < 4; ++c) hashVec4(h, m[c]);
}

// Light-space view-proj for a spot light (perspective from its cone), Vulkan-flipped.
glm::mat4 spotMatrix(const glm::vec3& pos, const glm::vec3& dir, float outerAngleDeg, float range) {
    float fovy = glm::clamp(glm::radians(outerAngleDeg) * 2.0f, glm::radians(10.0f), glm::radians(170.0f));
    glm::mat4 view = glm::lookAt(pos, pos + dir, safeUp(dir));
    glm::mat4 proj = glm::perspective(fovy, 1.0f, 0.1f, std::max(range, 1.0f));
    proj[1][1] *= -1.0f;
    return proj * view;
}
}

Renderer::Renderer(rhi::Device& device, rhi::Surface& swapchain, Window& window,
                   ResourceManager& resources, ImGuiLayer& imgui)
    : device_(device), swapchain_(&swapchain), window_(&window), resources_(resources), imgui_(&imgui) {
    createGlobalSetLayout();
#ifdef SAIDA_RHI_WEBGPU
    (void)imgui;
#else
    uiRenderer_ = std::make_unique<UIRenderer>(device_, resources_, rhi::vulkan::fromVk(swapchain_->colorFormat()));
#endif
    createHdrResources();
    createPipeline(resources_.materialSetLayout());
    createWebCanvasWorldPipeline();
    createTonemapPipeline();
#ifdef SAIDA_RHI_WEBGPU
    buildFeatures(0, swapchain_->depthFormat(), swapchain_->samples());
#else
    buildFeatures(0, rhi::vulkan::fromVk(swapchain_->depthFormat()), swapchain_->samples());
#endif
    createUniformBuffers();
    shadowMap_ = std::make_unique<ShadowMap>(device_);
    gi_ = std::make_unique<GIVolume>(device_, giDescForTier(device_.capabilities().tier),
                                     resources_.materialSetLayout(), *globalSetLayout_);
    createGlobalDescriptorSets();
    gpuProfiler_ = std::make_unique<GpuProfiler>(device_, kMaxFramesInFlight);

    if (device_.capabilities().descriptorIndexing && device_.capabilities().multiDrawIndirect) {
#ifndef SAIDA_RHI_WEBGPU
        createGpuDrivenBuffers();
        createCullingPipeline();
#endif
    }
}

#ifdef SAIDA_RHI_WEBGPU
Renderer::Renderer(rhi::Device& device, rhi::Surface& swapchain, ResourceManager& resources)
    : device_(device), swapchain_(&swapchain), resources_(resources) {
    createGlobalSetLayout();
    createHdrResources();
    createPipeline(resources_.materialSetLayout());
    createWebCanvasWorldPipeline();
    createTonemapPipeline();
    buildFeatures(0, swapchain_->depthFormat(), swapchain_->samples());
    createUniformBuffers();
    shadowMap_ = std::make_unique<ShadowMap>(device_);
    gi_ = std::make_unique<GIVolume>(device_, giDescForTier(device_.capabilities().tier),
                                     resources_.materialSetLayout(), *globalSetLayout_);
    createGlobalDescriptorSets();
    gpuProfiler_ = std::make_unique<GpuProfiler>(device_, kMaxFramesInFlight);
}
#endif

#ifdef SAIDA_ENABLE_XR
Renderer::Renderer(VulkanDevice& device, Window& window, ResourceManager& resources,
                   VkExtent2D xrEyeExtent, VkFormat xrColorFormat, uint32_t xrViewCount)
    : device_(device), window_(&window), resources_(resources),
      xrMode_(true), xrExtent_(xrEyeExtent), xrColorFormat_(xrColorFormat),
      xrViewCount_(xrViewCount) {
    // Shared rendering machinery (identical to desktop) ...
    createGlobalSetLayout();
    createUniformBuffers();
    shadowMap_ = std::make_unique<ShadowMap>(device_);
    gi_ = std::make_unique<GIVolume>(device_, giDescForTier(device_.capabilities().tier),
                                     resources_.materialSetLayout(), *globalSetLayout_);
    createGlobalDescriptorSets();
    // ... plus the XR-specific stereo targets + multiview pipelines.
    createXrTargets();
    createXrPipelines();
    gpuProfiler_ = std::make_unique<GpuProfiler>(device_, kMaxFramesInFlight);
}
#endif

Renderer::~Renderer() {
    device_.waitIdle();
    features_.clear();  // destroy feature pipelines/descriptors while the device is valid
#ifdef SAIDA_ENABLE_XR
    if (xrMode_) {
        for (auto& post : xrPostProcessors_) post.reset();
        cleanupXrTargets();
    }
#endif
    cleanupHdrResources();

    // pipeline_, culling groups and the buffers are torn down by their destructors.
}

void Renderer::setViewportRect(glm::vec2 position, glm::vec2 size) {
    viewportPos_ = position;
    viewportSize_ = size;
    viewportOverride_ = size.x > 1.0f && size.y > 1.0f;
}

void Renderer::clearViewportRect() {
    viewportOverride_ = false;
    viewportPos_ = glm::vec2(0.0f);
    viewportSize_ = glm::vec2(0.0f);
}

rhi::Rect2D Renderer::activeRenderRect() const {
    rhi::Extent2D full = swapchain_ ? swapchain_->extent() : rhi::Extent2D{1, 1};
    if (!viewportOverride_) return {{0, 0}, full};

    int32_t x = static_cast<int32_t>(std::max(0.0f, std::floor(viewportPos_.x)));
    int32_t y = static_cast<int32_t>(std::max(0.0f, std::floor(viewportPos_.y)));
    uint32_t w = static_cast<uint32_t>(std::max(1.0f, std::round(viewportSize_.x)));
    uint32_t h = static_cast<uint32_t>(std::max(1.0f, std::round(viewportSize_.y)));

    if (static_cast<uint32_t>(x) >= full.width || static_cast<uint32_t>(y) >= full.height) {
        return {{0, 0}, full};
    }
    w = std::min(w, full.width - static_cast<uint32_t>(x));
    h = std::min(h, full.height - static_cast<uint32_t>(y));
    return {{x, y}, {w, h}};
}

void Renderer::createGlobalSetLayout() {
    // Set 0: camera + lighting + shadow array + bone SSBO + DDGI atlases + env map.
#ifdef SAIDA_RHI_WEBGPU
    using WE = rhi::webgpu::BindGroupLayoutEntry;
    using Dim = rhi::webgpu::TextureDim;
    // Fragment|Compute mirrors the desktop layout: the DDGI trace compute pass
    // reads the voxel grid, shadows and environment through this same set 0.
    const auto FC = rhi::ShaderStages::Fragment | rhi::ShaderStages::Compute;
    const auto V = rhi::ShaderStages::Vertex;
    std::vector<WE> entries;
    WE e{};
    e.binding = 0; e.type = rhi::BindingType::UniformBuffer; e.visibility = V;
    entries.push_back(e);
    e = {}; e.binding = 1; e.type = rhi::BindingType::UniformBuffer; e.visibility = FC;
    entries.push_back(e);
    e = {}; e.binding = 2; e.type = rhi::BindingType::SampledTexture; e.visibility = FC;
    e.dim = Dim::Dim2DArray; e.depthTexture = true;
    entries.push_back(e);
    e = {}; e.binding = 3; e.type = rhi::BindingType::StorageBuffer; e.visibility = V;
    e.readOnlyStorage = true;
    entries.push_back(e);
    e = {}; e.binding = 4; e.type = rhi::BindingType::SampledTexture; e.visibility = FC;
    entries.push_back(e);
    e = {}; e.binding = 5; e.type = rhi::BindingType::SampledTexture; e.visibility = FC;
    entries.push_back(e);
    e = {}; e.binding = 6; e.type = rhi::BindingType::SampledTexture; e.visibility = FC;
    e.dim = Dim::Dim3D;
    entries.push_back(e);
    e = {}; e.binding = 7; e.type = rhi::BindingType::SampledTexture; e.visibility = FC;
    entries.push_back(e);
    e = {}; e.binding = 8; e.type = rhi::BindingType::Sampler; e.visibility = FC;
    e.comparisonSampler = true;
    entries.push_back(e);
    for (uint32_t b : {9u, 10u, 11u, 12u}) {
        e = {}; e.binding = b; e.type = rhi::BindingType::Sampler; e.visibility = FC;
        entries.push_back(e);
    }
    globalSetLayout_ = std::make_unique<rhi::BindGroupLayout>(device_, entries);
#else
    globalSetLayout_ = std::make_unique<rhi::BindGroupLayout>(device_,
        std::vector<rhi::BindGroupLayoutEntry>{
            {0, rhi::BindingType::UniformBuffer, rhi::ShaderStages::Vertex},                       // camera
            {1, rhi::BindingType::UniformBuffer, rhi::ShaderStages::Fragment | rhi::ShaderStages::Compute},  // lighting
            {2, rhi::BindingType::CombinedImageSampler, rhi::ShaderStages::Fragment | rhi::ShaderStages::Compute},  // shadow array
            {3, rhi::BindingType::StorageBuffer, rhi::ShaderStages::Vertex},                        // bone matrices
            {4, rhi::BindingType::CombinedImageSampler, rhi::ShaderStages::Fragment | rhi::ShaderStages::Compute},  // GI irradiance
            {5, rhi::BindingType::CombinedImageSampler, rhi::ShaderStages::Fragment | rhi::ShaderStages::Compute},  // GI visibility
            {6, rhi::BindingType::CombinedImageSampler, rhi::ShaderStages::Fragment | rhi::ShaderStages::Compute},  // GI voxel
            {7, rhi::BindingType::CombinedImageSampler, rhi::ShaderStages::Fragment | rhi::ShaderStages::Compute},  // environment
        });
#endif
}

void Renderer::createPipeline(rhi::BindGroupLayout& materialSetLayout) {
    bool useGpuDriven = false; // TEMPORARILY DISABLED FOR DEBUGGING

    std::string vertShader = useGpuDriven ? "bindless.shader.vert.spv" : "shader.vert.spv";
    std::string fragShader = useGpuDriven ? "bindless.shader.frag.spv" : "shader.frag.spv";

    // Classic path: set 0 = global, set 1 = material.
    // Dormant GPU-driven path: set 0 = global, set 1 = bindless material data
    // (raw layout, out of RHI scope until 16.4's caps.bindless fallback), set 2
    // = culling/instance data.
    rhi::Pipeline::Desc desc;
    desc.vertPath = shaderPath(vertShader);
    desc.fragPath = shaderPath(fragShader);
    desc.colorFormats = {rhi::Format::RGBA16Float};
    // Same value as the Vulkan backend's legacy default range; the web backend
    // has no default (0 = no push block) and needs it to emit @group(3).
    desc.pushConstantSize = sizeof(PushConstants);
#ifdef SAIDA_RHI_WEBGPU
    desc.depthFormat = swapchain_->depthFormat();
    desc.samples = swapchain_->samples();
#else
    desc.depthFormat = rhi::vulkan::fromVk(swapchain_->depthFormat());
    desc.samples = sampleCountValue(swapchain_->samples());
#endif
#ifndef SAIDA_RHI_WEBGPU
    if (useGpuDriven) {
        desc.bindGroupLayouts = {globalSetLayout_.get(), resources_.globalMaterialSetLayout(),
                                 cullingSetLayout_.get()};
    } else {
#endif
        desc.bindGroupLayouts = {globalSetLayout_.get(), &materialSetLayout};
#ifndef SAIDA_RHI_WEBGPU
    }
#endif
    pipeline_ = std::make_unique<rhi::Pipeline>(device_, desc);

    // Unlit variant: same vertex shader + identical set layout and push-constant
    // range as Lit, only the fragment differs, so the draw loop swaps pipelines
    // per material with no other change. Always built against the classic
    // (non-bindless) 2-set layout used by the per-object draw path.
    desc.vertPath = shaderPath("shader.vert.spv");
    desc.fragPath = shaderPath("unlit.frag.spv");
    desc.bindGroupLayouts = {globalSetLayout_.get(), &materialSetLayout};
    unlitPipeline_ = std::make_unique<rhi::Pipeline>(device_, desc);
}

void Renderer::createWebCanvasWorldPipeline() {
#ifdef SAIDA_RHI_WEBGPU
    return;
#else
    if (!resources_.globalMaterialSetLayout()) {
        Log::warn("WebCanvas world-space rendering disabled: descriptor indexing is unavailable.");
        return;
    }

    rhi::Pipeline::Desc desc;
    desc.vertPath = shaderPath("web_canvas_world.vert.spv");
    desc.fragPath = shaderPath("web_canvas_world.frag.spv");
    desc.colorFormats = {rhi::Format::RGBA16Float};
    desc.depthFormat = rhi::vulkan::fromVk(swapchain_->depthFormat());
    desc.bindGroupLayouts = {resources_.globalMaterialSetLayout()};  // raw bindless set
    desc.samples = sampleCountValue(swapchain_->samples());
    desc.vertexInput = false;
    desc.depthWrite = false;
    desc.depthCompare = rhi::CompareOp::LessOrEqual;
    desc.cullMode = rhi::CullMode::None;
    desc.blendMode = rhi::BlendMode::Alpha;
    desc.topology = rhi::Topology::TriangleStrip;
    desc.pushConstantSize = sizeof(WebCanvasWorldPushConstants);
    webCanvasWorldPipeline_ = std::make_unique<rhi::Pipeline>(device_, desc);
#endif
}

void Renderer::createUniformBuffers() {
    uniformBuffers_.reserve(kMaxFramesInFlight);
    lightingBuffers_.reserve(kMaxFramesInFlight);
    boneMatricesBuffers_.reserve(kMaxFramesInFlight);
    // Allocate a 4MB buffer for global bone matrices (enough for 65536 bones)
    const uint64_t kBoneBufferSize = 4 * 1024 * 1024;
    for (int i = 0; i < kMaxFramesInFlight; i++) {
        uniformBuffers_.push_back(std::make_unique<rhi::Buffer>(device_, sizeof(UniformBufferObject),
            rhi::BufferUsage::Uniform, MemoryUsage::HostVisible));
        lightingBuffers_.push_back(std::make_unique<rhi::Buffer>(device_, sizeof(LightingUBO),
            rhi::BufferUsage::Uniform, MemoryUsage::HostVisible));
        boneMatricesBuffers_.push_back(std::make_unique<rhi::Buffer>(device_, kBoneBufferSize,
            rhi::BufferUsage::Storage, MemoryUsage::HostVisible));
    }
}

void Renderer::createGpuDrivenBuffers() {
    instanceBuffers_.reserve(kMaxFramesInFlight);
    originalDrawCommandBuffers_.reserve(kMaxFramesInFlight);
    drawCommandBuffers_.reserve(kMaxFramesInFlight);
    countBuffers_.reserve(kMaxFramesInFlight);

    uint64_t instanceBufferSize = kMaxInstances * sizeof(InstanceData);
    uint64_t drawCommandBufferSize = kMaxInstances * sizeof(DrawIndexedIndirectCommand);

    for (int i = 0; i < kMaxFramesInFlight; i++) {
        instanceBuffers_.push_back(std::make_unique<rhi::Buffer>(device_, instanceBufferSize,
            rhi::BufferUsage::Storage, MemoryUsage::HostVisible));
            
        originalDrawCommandBuffers_.push_back(std::make_unique<rhi::Buffer>(device_, drawCommandBufferSize,
            rhi::BufferUsage::Storage, MemoryUsage::HostVisible));
        
        drawCommandBuffers_.push_back(std::make_unique<rhi::Buffer>(device_, drawCommandBufferSize,
            rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect,
            MemoryUsage::GpuOnly));
            
        countBuffers_.push_back(std::make_unique<rhi::Buffer>(device_, sizeof(uint32_t),
            rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect | rhi::BufferUsage::TransferDst,
            MemoryUsage::GpuOnly));
    }
}

void Renderer::createCullingPipeline() {
#ifdef SAIDA_RHI_WEBGPU
    return;
#else
    // Set 0 for the cull dispatch: 0=instances (read, also vertex-fetched by the
    // bindless draw), 1=original draw commands (read), 2=count (write),
    // 3=culled draw commands (write).
    cullingSetLayout_ = std::make_unique<rhi::BindGroupLayout>(device_,
        std::vector<rhi::BindGroupLayoutEntry>{
            {0, rhi::BindingType::StorageBuffer, rhi::ShaderStages::Compute | rhi::ShaderStages::Vertex},
            {1, rhi::BindingType::StorageBuffer, rhi::ShaderStages::Compute},
            {2, rhi::BindingType::StorageBuffer, rhi::ShaderStages::Compute},
            {3, rhi::BindingType::StorageBuffer, rhi::ShaderStages::Compute},
        });

    cullingGroups_.resize(kMaxFramesInFlight);
    for (int i = 0; i < kMaxFramesInFlight; i++) {
        rhi::BindGroupEntry instances{0, instanceBuffers_[i].get()};
        rhi::BindGroupEntry originalDraws{1, originalDrawCommandBuffers_[i].get()};
        rhi::BindGroupEntry count{2, countBuffers_[i].get()};
        rhi::BindGroupEntry culledDraws{3, drawCommandBuffers_[i].get()};
        cullingGroups_[i] = std::make_unique<rhi::BindGroup>(*cullingSetLayout_,
            std::vector<rhi::BindGroupEntry>{instances, originalDraws, count, culledDraws});
    }

    struct CullingPushConstants {
        glm::vec4 frustumPlanes[6];
        uint32_t instanceCount;
    };

    std::vector<rhi::vulkan::BindGroupLayoutRef> plLayouts = {*cullingSetLayout_};
    cullingPipeline_ = std::make_unique<ComputePipeline>(device_, shaderPath("culling.comp.spv"),
        plLayouts, sizeof(CullingPushConstants));
#endif
}

void Renderer::rebuildGlobalSet(int frame) {
    rhi::BindGroupEntry cameraEntry;
    cameraEntry.binding = 0;
    cameraEntry.buffer = uniformBuffers_[frame].get();
    cameraEntry.range = sizeof(UniformBufferObject);

    rhi::BindGroupEntry lightEntry;
    lightEntry.binding = 1;
    lightEntry.buffer = lightingBuffers_[frame].get();
    lightEntry.range = sizeof(LightingUBO);

    rhi::BindGroupEntry shadowEntry;
    shadowEntry.binding = 2;
    shadowEntry.view = shadowMap_->arrayView();
#ifndef SAIDA_RHI_WEBGPU
    shadowEntry.sampler = shadowMap_->sampler();
    shadowEntry.textureState = rhi::ResourceState::DepthRead;
#endif

    rhi::BindGroupEntry boneEntry;
    boneEntry.binding = 3;
    boneEntry.buffer = boneMatricesBuffers_[frame].get();
    boneEntry.range = 4 * 1024 * 1024; // 4MB

    // Atlases live in GENERAL (compute-written, fragment-sampled).
    rhi::BindGroupEntry giIrradianceEntry;
    giIrradianceEntry.binding = 4;
    giIrradianceEntry.view = gi_->irradianceView();
#ifndef SAIDA_RHI_WEBGPU
    giIrradianceEntry.sampler = gi_->sampler();
    giIrradianceEntry.textureState = rhi::ResourceState::StorageReadWrite;
#endif

    rhi::BindGroupEntry giVisibilityEntry;
    giVisibilityEntry.binding = 5;
    giVisibilityEntry.view = gi_->visibilityView();
#ifndef SAIDA_RHI_WEBGPU
    giVisibilityEntry.sampler = gi_->sampler();
    giVisibilityEntry.textureState = rhi::ResourceState::StorageReadWrite;
#endif

    rhi::BindGroupEntry giVoxelEntry;
    giVoxelEntry.binding = 6;
    giVoxelEntry.view = gi_->voxelView();
#ifndef SAIDA_RHI_WEBGPU
    giVoxelEntry.sampler = gi_->sampler();
#endif

    Texture* environment = resources_.defaultWhiteTexture();
    rhi::BindGroupEntry environmentEntry;
    environmentEntry.binding = 7;
    environmentEntry.view = environment->imageView();
#ifndef SAIDA_RHI_WEBGPU
    environmentEntry.sampler = environment->sampler();
#endif

#ifdef SAIDA_RHI_WEBGPU
    rhi::BindGroupEntry shadowSamplerEntry;
    shadowSamplerEntry.binding = 8;
    shadowSamplerEntry.sampler = shadowMap_->sampler();

    rhi::BindGroupEntry giIrradianceSamplerEntry;
    giIrradianceSamplerEntry.binding = 9;
    giIrradianceSamplerEntry.sampler = gi_->sampler();

    rhi::BindGroupEntry giVisibilitySamplerEntry;
    giVisibilitySamplerEntry.binding = 10;
    giVisibilitySamplerEntry.sampler = gi_->sampler();

    rhi::BindGroupEntry giVoxelSamplerEntry;
    giVoxelSamplerEntry.binding = 11;
    giVoxelSamplerEntry.sampler = gi_->sampler();

    rhi::BindGroupEntry environmentSamplerEntry;
    environmentSamplerEntry.binding = 12;
    environmentSamplerEntry.sampler = environment->sampler();

    globalGroups_[frame] = std::make_unique<rhi::BindGroup>(*globalSetLayout_,
        std::vector<rhi::BindGroupEntry>{cameraEntry, lightEntry, shadowEntry, boneEntry,
            giIrradianceEntry, giVisibilityEntry, giVoxelEntry, environmentEntry,
            shadowSamplerEntry, giIrradianceSamplerEntry, giVisibilitySamplerEntry,
            giVoxelSamplerEntry, environmentSamplerEntry});

    // Variant for the DDGI compute pass. WebGPU merges every bound group's
    // resources into the dispatch usage scope, so having the current atlases
    // sampled here (bindings 4/5) while the GI set storage-writes them fails
    // validation. The compute shaders never sample them — bind dummies.
    rhi::BindGroupEntry giIrradianceDummy = giIrradianceEntry;
    giIrradianceDummy.view = environment->imageView();
    rhi::BindGroupEntry giVisibilityDummy = giVisibilityEntry;
    giVisibilityDummy.view = environment->imageView();
    giComputeGlobalGroups_[frame] = std::make_unique<rhi::BindGroup>(*globalSetLayout_,
        std::vector<rhi::BindGroupEntry>{cameraEntry, lightEntry, shadowEntry, boneEntry,
            giIrradianceDummy, giVisibilityDummy, giVoxelEntry, environmentEntry,
            shadowSamplerEntry, giIrradianceSamplerEntry, giVisibilitySamplerEntry,
            giVoxelSamplerEntry, environmentSamplerEntry});
#else
    globalGroups_[frame] = std::make_unique<rhi::BindGroup>(*globalSetLayout_,
        std::vector<rhi::BindGroupEntry>{cameraEntry, lightEntry, shadowEntry, boneEntry,
            giIrradianceEntry, giVisibilityEntry, giVoxelEntry, environmentEntry});
#endif

    cachedGiIrradianceView_[frame] = gi_->irradianceView();
    cachedGiVisibilityView_[frame] = gi_->visibilityView();
    cachedGiSampler_[frame] = gi_->sampler();
    cachedEnvironmentView_[frame] = environment->imageView();
    cachedEnvironmentSampler_[frame] = environment->sampler();
}

void Renderer::createGlobalDescriptorSets() {
    globalGroups_.resize(kMaxFramesInFlight);
#ifdef SAIDA_RHI_WEBGPU
    giComputeGlobalGroups_.resize(kMaxFramesInFlight);
#endif
    for (int i = 0; i < kMaxFramesInFlight; i++) rebuildGlobalSet(i);
}

void Renderer::updateGlobalShadowDescriptor() {
    // The shadow map array view changes on resize; every frame slot samples the
    // same array, so every slot's bind group is stale and must be rebuilt.
    for (int i = 0; i < kMaxFramesInFlight; i++) rebuildGlobalSet(i);
}

void Renderer::updateGIDescriptors() {
    // After GIVolume::beginFrame() the "current" atlas (sampled by the lighting
    // pass) can change; rebuild this frame slot's bind group only when stale.
    // Safe: drawFrame already waited on this frame's fence.
    if (cachedGiIrradianceView_[currentFrame_] == gi_->irradianceView() &&
        cachedGiVisibilityView_[currentFrame_] == gi_->visibilityView() &&
        cachedGiSampler_[currentFrame_] == gi_->sampler()) {
        return;
    }
    rebuildGlobalSet(currentFrame_);
}

void Renderer::updateEnvironmentDescriptor(Scene& scene) {
    Texture* environment = resources_.defaultWhiteTexture();
    if (scene.settings().skyboxTexture != kAssetInvalid) {
        if (Texture* skybox = resources_.getTexture(scene.settings().skyboxTexture)) {
            environment = skybox;
        }
    }

    if (cachedEnvironmentView_[currentFrame_] == environment->imageView() &&
        cachedEnvironmentSampler_[currentFrame_] == environment->sampler()) {
        return;
    }
    rebuildGlobalSet(currentFrame_);
}

bool Renderer::shouldUpdateRealtimeGI(bool dirty) const {
    int cadence = 16;
    switch (device_.capabilities().tier) {
        case QualityTier::Ultra:
        case QualityTier::High:
            cadence = 8;
            break;
        case QualityTier::Medium:
            cadence = 12;
            break;
        case QualityTier::Low:
        default:
            cadence = 16;
            break;
    }

    if (giRealtimeWarmupRemaining_ > 0) return true;
    if (!dirty) return false;
    if (giLastMode_ == static_cast<int>(GIMode::FullRealtime)) return true;
    return (giFrameCounter_ % static_cast<uint64_t>(cadence)) == 0;
}

uint64_t Renderer::giDirtySignature(const Scene& scene) const {
    const SceneSettings& settings = scene.settings();
    uint64_t h = 1469598103934665603ull;
    hashCombine(h, static_cast<uint64_t>(settings.lightingMode));
    hashCombine(h, settings.giEnabled ? 1ull : 0ull);
    hashCombine(h, static_cast<uint64_t>(settings.giMode));
    hashFloat(h, settings.giIntensity);
    hashCombine(h, static_cast<uint64_t>(settings.skyboxTexture));
    hashFloat(h, settings.skyboxExposure);
    hashFloat(h, settings.skyboxRotation);
    hashCombine(h, settings.iblEnabled ? 1ull : 0ull);
    hashFloat(h, settings.iblDiffuseIntensity);
    hashFloat(h, settings.iblSpecularIntensity);

    hashCombine(h, static_cast<uint64_t>(scene.meshes().size()));
    hashCombine(h, static_cast<uint64_t>(scene.lights().size()));
    for (const LightNode* light : scene.lights()) {
        if (!light) continue;
        hashCombine(h, static_cast<uint64_t>(light->type));
        hashVec4(h, glm::vec4(light->color, light->intensity));
        hashFloat(h, light->range);
        hashFloat(h, light->spotInnerAngle);
        hashFloat(h, light->spotOuterAngle);
        hashCombine(h, light->castShadows ? 1ull : 0ull);
        hashMat4(h, light->worldTransform());
    }
    return h;
}

Renderer::TonemapPushConstants Renderer::tonemapPushConstants(
    const SceneSettings& settings, const glm::mat4& projection) const {
    TonemapPushConstants push{};
    push.invProjection = glm::inverse(projection);
    push.aoParams = glm::vec4(settings.aoEnabled ? 1.0f : 0.0f,
                              std::max(settings.aoRadius, 0.0f),
                              std::max(settings.aoIntensity, 0.0f),
                              std::max(settings.aoPower, 0.001f));
    push.fogColor = settings.fogColor;
    push.fogParams = glm::vec4(settings.fogEnabled ? 1.0f : 0.0f,
                               std::max(settings.fogStart, 0.0f),
                               std::max(settings.fogDensity, 0.0f), exposure_);
    push.bloomParams = glm::vec4(settings.bloomEnabled ? 1.0f : 0.0f,
                                 std::max(settings.bloomThreshold, 0.0f),
                                 std::max(settings.bloomIntensity, 0.0f),
                                 std::max(settings.bloomRadius, 0.0f));
    push.sourceRect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    push.projectionParams = glm::vec4(push.invProjection[0][0],
                                      push.invProjection[1][1],
                                      push.invProjection[2][3],
                                      push.invProjection[3][3]);
    push.projectionParams2 = glm::vec4(push.invProjection[2][2],
                                       push.invProjection[3][2],
                                       0.0f, 0.0f);
    return push;
}

void Renderer::gatherScene(LightingUBO& ubo, Scene& scene, const glm::vec3& cameraPos,
                           const Frustum* cullFrustum, Project* project,
                           const glm::mat4* view, const glm::mat4* proj) {
    SAIDA_PROFILE_FUNCTION();
    lodMatricesValid_ = view && proj;
    if (lodMatricesValid_) {
        lodView_ = *view;
        lodProj_ = *proj;
    }
    auto& settings = scene.settings();
    ubo.ambient = settings.ambientLight;
    ubo.cameraPos = glm::vec4(cameraPos, 1.0f);
    ubo.shadowParams = glm::vec4(project ? project->shadowSoftness() : 1.0f, 0.0f, 0.0f, 0.0f);

    int lightCount = 0;
    shadowCount_ = 0;

    for (LightNode* light : scene.lights()) {
        if (lightCount >= kMaxLights) break;
        const glm::mat4& world = light->worldTransform();
        glm::vec3 worldPos = glm::vec3(world[3]);
        glm::vec3 worldDir = glm::normalize(glm::mat3(world) * light->direction);

        GpuLight& gl = ubo.lights[lightCount];
        gl.posRange = glm::vec4(worldPos, light->range);
        gl.colorInt = glm::vec4(light->color, light->intensity);
        float type = light->type == LightType::Directional ? 0.0f
                   : light->type == LightType::Point       ? 1.0f
                                                           : 2.0f;  // Spot
        gl.dirType = glm::vec4(worldDir, type);
        gl.spotShadow = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);

        if (light->type == LightType::Spot) {
            gl.spotShadow.x = std::cos(glm::radians(light->spotInnerAngle));
            gl.spotShadow.y = std::cos(glm::radians(light->spotOuterAngle));
        }

        // Directional and spot lights cast 2D shadow maps (point would need a
        // cube map). Shadows stay live in both realtime and baked GI modes.
        bool wantsShadow = light->castShadows &&
            (light->type == LightType::Directional || light->type == LightType::Spot);
        if (wantsShadow && shadowCount_ < kMaxShadowCasters) {
            glm::mat4 lightVP = light->type == LightType::Directional
                ? directionalMatrix(worldDir, project ? project->shadowDistance() : 25.0f)
                : spotMatrix(worldPos, worldDir, light->spotOuterAngle, light->range);
            shadowMatrices_[shadowCount_] = lightVP;
            ubo.shadowMatrices[shadowCount_] = lightVP;
            gl.spotShadow.z = static_cast<float>(shadowCount_);
            ++shadowCount_;
        }

        ++lightCount;
    }

    int mode = settings.lightingMode == LightingMode::Baked ? 1 : 0;
    ubo.counts = glm::ivec4(lightCount, mode, 0, 0);

    // DDGI volume params — sampled identically in realtime and baked modes.
    const GIVolumeDesc& gd = gi_->desc();
    ubo.giOrigin  = glm::vec4(gd.origin, settings.giEnabled ? 1.0f : 0.0f);
    ubo.giSpacing = glm::vec4(gd.spacing, settings.giIntensity);  // w = indirect multiplier
    ubo.giCounts  = glm::ivec4(gd.counts, gi_->probesPerRow());
    ubo.giAtlas   = glm::ivec4(gd.irradianceTexels, gd.visibilityTexels,
                               settings.giDebugVoxels ? 1 : 0, gd.voxelResolution);
    const bool iblActive = settings.iblEnabled && settings.skyboxTexture != kAssetInvalid;
    const float environmentExposure = std::max(settings.skyboxExposure, 0.0f);
    ubo.environmentParams = glm::vec4(
        iblActive ? 1.0f : 0.0f,
        std::max(settings.iblDiffuseIntensity, 0.0f) * environmentExposure,
        std::max(settings.iblSpecularIntensity, 0.0f) * environmentExposure,
        settings.skyboxRotation);

    auto getAnimatorInParent = [](Node* n) -> Animator* {
#ifdef SAIDA_RHI_WEBGPU
        (void)n;
        return nullptr;
#else
        while (n) {
            if (auto* a = n->getBehaviour<Animator>()) return a;
            n = n->parent();
        }
        return nullptr;
#endif
    };

    uint32_t currentBoneCount = 0;
    glm::mat4* boneData = static_cast<glm::mat4*>(boneMatricesBuffers_[currentFrame_]->mapped());

    shadowDraws_.clear();
    if (shadowCount_ > 0) {
        for (MeshNode* node : scene.meshes()) {
            if (!node->castShadows()) continue;
            Mesh* mesh = node->mesh();
            if (!mesh) continue;
            const glm::mat4& world = node->worldTransform();
            if (node->hasLods() && lodMatricesValid_) {
                const float coverage = computeScreenCoverage(world, mesh->bounds(), lodView_, lodProj_);
                mesh = node->meshForLod(node->selectLodIndex(coverage));
            }
            if (mesh) shadowDraws_.push_back(ShadowDraw{mesh, world});
        }
    }

    bool useGpuDriven = false; // TEMPORARILY DISABLED FOR DEBUGGING
    if (useGpuDriven) {
        InstanceData* instanceData = static_cast<InstanceData*>(instanceBuffers_[currentFrame_]->mapped());
        auto* drawData = static_cast<DrawIndexedIndirectCommand*>(originalDrawCommandBuffers_[currentFrame_]->mapped());
        
        uint32_t instanceCount = 0;
        for (MeshNode* node : scene.meshes()) {
            if (Mesh* m = node->mesh()) {
                if (Material* mat = node->material()) {
                    if (instanceCount >= kMaxInstances) break;
                    
                    const glm::mat4& world = node->worldTransform();
                    float maxScale = std::max({
                        glm::length(glm::vec3(world[0])),
                        glm::length(glm::vec3(world[1])),
                        glm::length(glm::vec3(world[2]))
                    });
                    const float localRadius = glm::length(m->bounds().extent()) * 0.5f;
                    float radius = (localRadius > 0.001f ? localRadius : 0.866f) * maxScale;
                    
                    int32_t boneOffset = -1;
                    if (Animator* anim = getAnimatorInParent(node)) {
                        if (!anim->globalPose().skinningMatrices.empty()) {
                            const auto& mats = anim->globalPose().skinningMatrices;
                            boneOffset = currentBoneCount;
                            std::memcpy(&boneData[boneOffset], mats.data(), mats.size() * sizeof(glm::mat4));
                            currentBoneCount += static_cast<uint32_t>(mats.size());
                        }
                    }

                    InstanceData& inst = instanceData[instanceCount];
                    inst.model = world;
                    inst.boundingSphere = glm::vec4(0.0f, 0.0f, 0.0f, radius);
                    inst.materialIndex = mat->bindlessIndex();
                    inst.boneOffset = boneOffset;
                    
                    DrawIndexedIndirectCommand& draw = drawData[instanceCount];
                    auto alloc = m->geometryAllocation();
                    draw.indexCount = alloc.indexCount;
                    draw.instanceCount = 1;
                    draw.firstIndex = alloc.firstIndex;
                    draw.vertexOffset = alloc.vertexOffset;
                    draw.firstInstance = instanceCount;
                    
                    static int frameCounter = 0;
                    if (frameCounter % 600 == 0) {
                        Log::info("GPU Draw: instance=", instanceCount, ", indices=", draw.indexCount, 
                                  ", vOffset=", draw.vertexOffset, ", fIndex=", draw.firstIndex, 
                                  ", matIdx=", inst.materialIndex, ", boneOffset=", boneOffset);
                    }
                    if (instanceCount == 0) { frameCounter++; }
                    
                    instanceCount++;
                }
            }
        }
        
        currentInstanceCount_ = instanceCount;
        
        if (instanceCount > 0) {
            instanceBuffers_[currentFrame_]->flush(instanceCount * sizeof(InstanceData));
            originalDrawCommandBuffers_[currentFrame_]->flush(instanceCount * sizeof(DrawIndexedIndirectCommand));
        }
        if (currentBoneCount > 0) {
            boneMatricesBuffers_[currentFrame_]->flush(currentBoneCount * sizeof(glm::mat4));
        }
        
        // Clear countBuffer to 0 using CPU map (since it's HostVisible? Wait, countBuffers_ is MemoryUsage::GpuOnly!  
        // We must use vkCmdFillBuffer or similar in the command buffer!)
    } else {
        currentDraws_.clear();

        for (MeshNode* node : scene.meshes()) {
            if (Mesh* m = node->mesh()) {
                if (Material* mat = node->material()) {
                    const glm::mat4& world = node->worldTransform();
                    Mesh* drawMesh = m;
                    Material* drawMat = mat;
                    if (node->hasLods() && lodMatricesValid_) {
                        const float coverage = computeScreenCoverage(world, m->bounds(), lodView_, lodProj_);
                        const int lod = node->selectLodIndex(coverage);
                        node->setActiveLodIndex(lod);
                        drawMesh = node->meshForLod(lod);
                        drawMat = node->materialForLod(lod);
                    }
                    if (!drawMesh || !drawMat) continue;

                    // Frustum culling against the camera (desktop). XR passes a
                    // null frustum (stereo) → render everything, no per-eye cull.
                    bool inside = true;
                    if (cullFrustum) {
                        float maxScale = std::max({
                            glm::length(glm::vec3(world[0])),
                            glm::length(glm::vec3(world[1])),
                            glm::length(glm::vec3(world[2]))
                        });
                        const float localRadius = glm::length(m->bounds().extent()) * 0.5f;
                        float radius = (localRadius > 0.001f ? localRadius : 0.866f) * maxScale;
                        glm::vec3 center = glm::vec3(world * glm::vec4(m->bounds().center(), 1.0f));
                        for (int i = 0; i < 6; ++i) {
                            if (glm::dot(glm::vec3(cullFrustum->planes[i]), center) +
                                    cullFrustum->planes[i].w < -radius) {
                                inside = false;
                                break;
                            }
                        }
                    }

                    if (inside) {
                        int32_t boneOffset = -1;
                        if (Animator* anim = getAnimatorInParent(node)) {
                            if (!anim->globalPose().skinningMatrices.empty()) {
                                const auto& mats = anim->globalPose().skinningMatrices;
                                boneOffset = currentBoneCount;
                                std::memcpy(&boneData[boneOffset], mats.data(), mats.size() * sizeof(glm::mat4));
                                currentBoneCount += static_cast<uint32_t>(mats.size());
                            }
                        }

                        currentDraws_.push_back(SceneDraw{drawMesh, drawMat, node, world, node->castShadows(),
                                                          boneOffset, drawMat->desc().type});
                    }
                }
            }
        }

        if (currentBoneCount > 0) {
            boneMatricesBuffers_[currentFrame_]->flush(currentBoneCount * sizeof(glm::mat4));
        }

        // Sort draws by material to minimize Vulkan pipeline descriptor set binds
        std::sort(currentDraws_.begin(), currentDraws_.end(),
                  [](const SceneDraw& a, const SceneDraw& b) {
                      if (a.materialType != b.materialType) return a.materialType < b.materialType;
                      return a.material < b.material;
                  });
    }

    SAIDA_PROFILE_COUNTER("Renderer/VisibleDraws", currentDraws_.size());
    SAIDA_PROFILE_COUNTER("Renderer/ShadowCasters", shadowDraws_.size());
    SAIDA_PROFILE_COUNTER("Renderer/ShadowLights", shadowCount_);
    SAIDA_PROFILE_COUNTER("Animation/BoneMatrices", currentBoneCount);
}

void Renderer::createHdrResources() {
#ifdef SAIDA_RHI_WEBGPU
    rhi::Extent2D extent = swapchain_->extent();
    const bool msaa = false;
#else
    rhi::Extent2D extent = swapchain_->extent();
    const bool msaa = swapchain_->samples() != VK_SAMPLE_COUNT_1_BIT;
#endif

    rhi::RenderTextureDesc hdrDesc;
    hdrDesc.format = rhi::Format::RGBA16Float;
    hdrDesc.width = extent.width;
    hdrDesc.height = extent.height;
    hdrDesc.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled;
    hdrDesc.memoryCategory = "RenderTarget/DesktopHDR";
    hdrTexture_ = std::make_unique<rhi::RenderTexture>(device_, hdrDesc);

    if (msaa) {
        rhi::RenderTextureDesc msaaDesc = hdrDesc;
#ifdef SAIDA_RHI_WEBGPU
        msaaDesc.samples = swapchain_->samples();
#else
        msaaDesc.samples = sampleCountValue(swapchain_->samples());
#endif
        msaaDesc.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Transient;
        hdrMsaaTexture_ = std::make_unique<rhi::RenderTexture>(device_, msaaDesc);

        rhi::RenderTextureDesc depthDesc;
#ifdef SAIDA_RHI_WEBGPU
        depthDesc.format = swapchain_->depthFormat();
#else
        depthDesc.format = rhi::vulkan::fromVk(swapchain_->depthFormat());
#endif
        depthDesc.width = extent.width;
        depthDesc.height = extent.height;
        depthDesc.usage = rhi::TextureUsage::DepthAttachment | rhi::TextureUsage::Sampled;
        depthDesc.memoryCategory = "RenderTarget/DesktopHDR";
        depthResolveTexture_ = std::make_unique<rhi::RenderTexture>(device_, depthDesc);
    }

    postProcessor_ = std::make_unique<PostProcessor>(device_, extent,
        rhi::Format::RGBA16Float, hdrTexture_->view());
    updateTonemapDescriptorSet();
}

void Renderer::cleanupHdrResources() {
    postProcessor_.reset();
    depthResolveTexture_.reset();
    hdrMsaaTexture_.reset();
    hdrTexture_.reset();
}

void Renderer::createTonemapPipeline() {
#ifdef SAIDA_RHI_WEBGPU
    using WE = rhi::webgpu::BindGroupLayoutEntry;
    const auto F = rhi::ShaderStages::Fragment;
    std::vector<WE> tonemapEntries;
    WE e{};
    e.binding = 0; e.type = rhi::BindingType::SampledTexture; e.visibility = F;
    tonemapEntries.push_back(e);
    e = {}; e.binding = 1; e.type = rhi::BindingType::SampledTexture; e.visibility = F;
    e.unfilterable = true;
    tonemapEntries.push_back(e);
    e = {}; e.binding = 2; e.type = rhi::BindingType::SampledTexture; e.visibility = F;
    tonemapEntries.push_back(e);
    e = {}; e.binding = 3; e.type = rhi::BindingType::Sampler; e.visibility = F;
    tonemapEntries.push_back(e);
    e = {}; e.binding = 4; e.type = rhi::BindingType::Sampler; e.visibility = F;
    e.nonFilteringSampler = true;
    tonemapEntries.push_back(e);
    e = {}; e.binding = 5; e.type = rhi::BindingType::Sampler; e.visibility = F;
    tonemapEntries.push_back(e);
    tonemapSetLayout_ = std::make_unique<rhi::BindGroupLayout>(device_, tonemapEntries);
#else
    tonemapSetLayout_ = std::make_unique<rhi::BindGroupLayout>(device_,
        std::vector<rhi::BindGroupLayoutEntry>{
            {0, rhi::BindingType::CombinedImageSampler, rhi::ShaderStages::Fragment},  // HDR
            {1, rhi::BindingType::CombinedImageSampler, rhi::ShaderStages::Fragment},  // depth (AO)
            {2, rhi::BindingType::CombinedImageSampler, rhi::ShaderStages::Fragment},  // bloom
        });
#endif

    rhi::SamplerDesc linearDesc;
    linearDesc.mipFilter = rhi::FilterMode::Linear;
    tonemapSampler_ = std::make_unique<rhi::Sampler>(device_, linearDesc);

    rhi::SamplerDesc nearestDesc;
    nearestDesc.magFilter = rhi::FilterMode::Nearest;
    nearestDesc.minFilter = rhi::FilterMode::Nearest;
    tonemapDepthSampler_ = std::make_unique<rhi::Sampler>(device_, nearestDesc);

    rhi::Pipeline::Desc tonemapDesc;
    tonemapDesc.vertPath = shaderPath("tonemap.vert.spv");
    tonemapDesc.fragPath = shaderPath("tonemap.frag.spv");
#ifdef SAIDA_RHI_WEBGPU
    tonemapDesc.colorFormats = {swapchain_->colorFormat()};
#else
    tonemapDesc.colorFormats = {rhi::vulkan::fromVk(swapchain_->colorFormat())};
#endif
    tonemapDesc.bindGroupLayouts = {tonemapSetLayout_.get()};
    tonemapDesc.vertexInput = false;
    tonemapDesc.depthTest = false;
    // Fullscreen triangle: never cull. On web the naga SPIR-V→WGSL pass flips
    // clip-space Y, which inverts the winding of raw-clip-coord triangles (the
    // scene is unaffected: its projection flip cancels naga's) — with the
    // default Back cull the tonemap triangle disappears entirely.
    tonemapDesc.cullMode = rhi::CullMode::None;
    tonemapDesc.pushConstantSize = sizeof(TonemapPushConstants);
    tonemapPipeline_ = std::make_unique<rhi::Pipeline>(device_, tonemapDesc);

    updateTonemapDescriptorSet();
}

void Renderer::updateTonemapDescriptorSet() {
    if (!tonemapSetLayout_ || !hdrTexture_ || !swapchain_ || !tonemapSampler_ || !tonemapDepthSampler_ || !postProcessor_) {
        return;
    }

    rhi::BindGroupEntry hdrEntry;
    hdrEntry.binding = 0;
    hdrEntry.view = hdrTexture_->view();
#ifndef SAIDA_RHI_WEBGPU
    hdrEntry.sampler = tonemapSampler_->handle();
#endif

    rhi::BindGroupEntry depthEntry;
    depthEntry.binding = 1;
    depthEntry.view = depthResolveTexture_ ? depthResolveTexture_->view() : swapchain_->depthView();
#ifndef SAIDA_RHI_WEBGPU
    depthEntry.sampler = tonemapDepthSampler_->handle();
#endif

    rhi::BindGroupEntry bloomEntry;
    bloomEntry.binding = 2;
    bloomEntry.view = postProcessor_->bloomView();
#ifndef SAIDA_RHI_WEBGPU
    bloomEntry.sampler = postProcessor_->bloomSampler();
#endif

#ifdef SAIDA_RHI_WEBGPU
    rhi::BindGroupEntry hdrSamplerEntry;
    hdrSamplerEntry.binding = 3;
    hdrSamplerEntry.sampler = tonemapSampler_->handle();

    rhi::BindGroupEntry depthSamplerEntry;
    depthSamplerEntry.binding = 4;
    depthSamplerEntry.sampler = tonemapDepthSampler_->handle();

    rhi::BindGroupEntry bloomSamplerEntry;
    bloomSamplerEntry.binding = 5;
    bloomSamplerEntry.sampler = postProcessor_->bloomSampler();

    tonemapSet_ = std::make_unique<rhi::BindGroup>(*tonemapSetLayout_,
        std::vector<rhi::BindGroupEntry>{hdrEntry, depthEntry, bloomEntry,
            hdrSamplerEntry, depthSamplerEntry, bloomSamplerEntry});
#else
    tonemapSet_ = std::make_unique<rhi::BindGroup>(*tonemapSetLayout_,
        std::vector<rhi::BindGroupEntry>{hdrEntry, depthEntry, bloomEntry});
#endif
}

void Renderer::recordTonemapPass(rhi::CommandEncoder& encoder, uint32_t imageIndex,
                                 Scene& scene, const Camera& camera) {
    SAIDA_PROFILE_FUNCTION();
#ifdef SAIDA_RHI_WEBGPU
    auto cmd = encoder.handle();
#else
    VkCommandBuffer cmd = encoder.handle();  // GPU profiler zones (desktop tooling)
#endif
    GpuProfiler* gpuProfiler = Profiler::instance().enabled() ? gpuProfiler_.get() : nullptr;
    SAIDA_GPU_PROFILE_SCOPE(gpuProfiler, cmd, "GPU/Tonemap+EditorUI");

    // Scene results -> sampled inputs; the swapchain image comes back from
    // presentation with undefined contents (fully overwritten by this pass).
    encoder.transition(hdrTexture_->image(), rhi::ResourceState::ColorAttachment,
                       rhi::ResourceState::ShaderRead);
    auto depthImage = depthResolveTexture_ ? depthResolveTexture_->image()
                                           : swapchain_->depthImage();
    encoder.transition(depthImage, rhi::ResourceState::DepthWrite,
                       rhi::ResourceState::ShaderRead);
    encoder.transition(swapchain_->image(imageIndex), rhi::ResourceState::ColorAttachment,
                       rhi::ResourceState::ColorAttachment, 0, kAllTextureLayers,
                       /*discardContents=*/true);

    rhi::Rect2D renderRect = activeRenderRect();
    rhi::Extent2D fullExtent = swapchain_->extent();
    glm::vec4 sourceRect(
        static_cast<float>(renderRect.offset.x) / static_cast<float>(std::max(1u, fullExtent.width)),
        static_cast<float>(renderRect.offset.y) / static_cast<float>(std::max(1u, fullExtent.height)),
        static_cast<float>(renderRect.extent.width) / static_cast<float>(std::max(1u, fullExtent.width)),
        static_cast<float>(renderRect.extent.height) / static_cast<float>(std::max(1u, fullExtent.height)));

    if (postProcessor_) {
        postProcessor_->recordBloom(encoder, scene.settings(), sourceRect, gpuProfiler);
    }

    rhi::RenderPassDesc pass;
    pass.colorCount = 1;
    pass.colors[0].view = swapchain_->imageView(imageIndex);
    pass.colors[0].loadOp = rhi::LoadOp::Clear;
    pass.width = fullExtent.width;
    pass.height = fullExtent.height;
    pass.defaultViewportScissor = false;  // tonemap draws into the viewport rect only

    rhi::RenderPassEncoder rp = encoder.beginRenderPass(pass);
    rp.setPipeline(*tonemapPipeline_);
    rp.setViewport(static_cast<float>(renderRect.offset.x), static_cast<float>(renderRect.offset.y),
                   static_cast<float>(renderRect.extent.width), static_cast<float>(renderRect.extent.height));
    rp.setScissor(renderRect.offset.x, renderRect.offset.y,
                  renderRect.extent.width, renderRect.extent.height);
    rp.setBindGroup(0, *tonemapSet_);

    TonemapPushConstants push = tonemapPushConstants(scene.settings(), camera.projection());
    push.sourceRect = sourceRect;
    {
        SAIDA_GPU_PROFILE_SCOPE(gpuProfiler, cmd, "Post/Tonemap");
        rp.setPushConstants(&push, sizeof(TonemapPushConstants));
        rp.draw(3);
    }
    {
        SAIDA_GPU_PROFILE_SCOPE(gpuProfiler, cmd, "Post/UI");
#ifndef SAIDA_RHI_WEBGPU
        {
            SAIDA_PROFILE_SCOPE("UI/RecordCommands");
            uiRenderer_->recordCommands(rp, fullExtent.width, fullExtent.height,
                                        {static_cast<float>(renderRect.offset.x), static_cast<float>(renderRect.offset.y)},
                                        {static_cast<float>(renderRect.extent.width), static_cast<float>(renderRect.extent.height)});
        }
        {
            SAIDA_PROFILE_SCOPE("ImGui/RenderDrawData");
            imgui_->renderDrawData(rp.handle());  // editor-only overlay, permanent escape hatch
        }
#endif
    }
    rp.end();
}

void Renderer::buildFeatures(uint32_t viewMask, rhi::Format depthFormat,
                            rhi::SampleCount samples) {
    // The Renderer knows ONLY the ScenePassFeature interface. Concrete effects
    // (water, skybox, particles, …) plug in through the registry, so this never
    // names or includes a concrete effect — adding one touches no Renderer code.
    // On web the registry only carries the ported subset (skybox + GPU
    // particles); WaterFeature/Outline/DebugLines stay desktop-only.
    features_ = RenderFeatureRegistry::instance().build();
    RenderContext ctx{device_, resources_, *globalSetLayout_,
                      rhi::Format::RGBA16Float, depthFormat, samples,
                      viewMask, static_cast<uint32_t>(kMaxFramesInFlight)};
    for (auto& f : features_) f->createPipelines(ctx);
}

void Renderer::recordFeatures(FrameContext& fc) {
    for (auto& f : features_) f->record(fc);
}

void Renderer::recordMeshDraws(rhi::RenderPassEncoder& rp, rhi::Pipeline* firstPipeline, bool xrMultiview) {
    SAIDA_PROFILE_FUNCTION();
    if (!firstPipeline) return;

    Material* lastMaterial = nullptr;
    rhi::Pipeline* activePipeline = firstPipeline;
    uint64_t drawCalls = 0;
    uint64_t triangles = 0;
    const rhi::BindGroup& globalSet = *globalGroups_[currentFrame_];
    rp.setPipeline(*activePipeline);
    rp.setBindGroup(0, globalSet);

    for (const auto& draw : currentDraws_) {
        rhi::Pipeline* want = scenePipelineFor(draw.materialType);
#ifdef SAIDA_ENABLE_XR
        if (xrMultiview)
            want = xrScenePipelineFor(draw.materialType);
#else
        (void)xrMultiview;
#endif

        if (want != activePipeline) {
            rp.setPipeline(*want);
            rp.setBindGroup(0, globalSet);
            activePipeline = want;
            lastMaterial = nullptr;
        }

        if (draw.material != lastMaterial) {
            rp.setBindGroup(1, draw.material->descriptorSet());
            lastMaterial = draw.material;
        }

        PushConstants pc{};
        pc.model = draw.world;
        // params.y: offset into the global bone matrix buffer, or -1.
        pc.params = glm::vec4(0.0f, static_cast<float>(draw.boneOffset), 0.0f, 0.0f);
        rp.setPushConstants(&pc, sizeof(PushConstants));

        draw.mesh->bind(rp);
        draw.mesh->draw(rp);
        ++drawCalls;
        triangles += draw.mesh->allocation().indexCount / 3;
    }
    SAIDA_PROFILE_COUNTER("Renderer/DrawCalls", drawCalls);
    SAIDA_PROFILE_COUNTER("Renderer/Triangles", triangles);
}

void Renderer::recordWorldWebCanvases(rhi::RenderPassEncoder& rp, Scene& scene, const Camera& camera) {
#ifdef SAIDA_RHI_WEBGPU
    (void)rp;
    (void)scene;
    (void)camera;
#else
    if (!webCanvasWorldPipeline_ || !resources_.globalMaterialSet()) return;

    std::vector<WebCanvasWorldDraw> draws = collectWorldWebCanvasDraws(scene, camera.position);
    if (draws.empty()) return;

    rp.setPipeline(*webCanvasWorldPipeline_);
    rp.setBindGroup(0, resources_.globalMaterialSet());

    for (const WebCanvasWorldDraw& draw : draws) {
        WebCanvasNode* node = draw.node;
        Texture* texture = node->texture();
        if (!texture) continue;

        WebCanvasWorldPushConstants pc{};
        pc.model = node->worldTransform();
        pc.params = glm::vec4(node->worldWidth(), node->worldHeight(),
                              static_cast<float>(resources_.ensureBindlessTextureIndex(texture)), 1.0f);
        rp.setPushConstants(&pc, sizeof(pc));
        rp.draw(4);
    }
#endif
}

void Renderer::recordShadowPasses(rhi::CommandEncoder& encoder) {
    shadowMap_->record(encoder, shadowCount_,
        [this](rhi::RenderPassEncoder& rp, int layer) {
            for (const ShadowDraw& draw : shadowDraws_) {
                glm::mat4 mvp = shadowMatrices_[layer] * draw.world;
                rp.setPushConstants(&mvp, sizeof(mvp));
                draw.mesh->bind(rp);
                draw.mesh->draw(rp);
            }
        });
}

void Renderer::updateUniformBuffer(uint32_t frame, Scene& scene, Camera& camera, Project* project) {
    rhi::Rect2D renderRect = activeRenderRect();
    float aspect = renderRect.extent.width / static_cast<float>(std::max(1u, renderRect.extent.height));
    camera.setPerspective(glm::radians(camera.fovDegrees), aspect,
                          camera.nearZ, camera.farZ);
    
    // Store camera frustum for culling compute shader
    cameraFrustum_ = camera.getFrustum();

    UniformBufferObject ubo{};
    // Mono: both eye slots hold the same matrix (the desktop shader uses index 0).
    ubo.view[0] = ubo.view[1] = camera.view();
    ubo.proj[0] = ubo.proj[1] = camera.projection();
    uniformBuffers_[frame]->write(&ubo, sizeof(ubo));

    LightingUBO lighting{};
    gatherScene(lighting, scene, camera.position, &cameraFrustum_, project,
                &ubo.view[0], &ubo.proj[0]);
    lightingBuffers_[frame]->write(&lighting, sizeof(lighting));
}

void Renderer::recordCommandBuffer(rhi::CommandEncoder& encoder, uint32_t imageIndex,
                                   Scene& scene, const Camera& camera) {
    // GPU profiling is a desktop-only escape hatch on the raw command buffer.
#ifdef SAIDA_RHI_WEBGPU
    auto cmd = encoder.handle();
#else
    VkCommandBuffer cmd = encoder.handle();
#endif
    GpuProfiler* gpuProfiler = Profiler::instance().enabled() ? gpuProfiler_.get() : nullptr;
    if (gpuProfiler) gpuProfiler->resetQueries(cmd);
    uint32_t gpuFrameZone = gpuProfiler ? gpuProfiler->beginZone(cmd, "GPU/Frame") : UINT32_MAX;

    {
        SAIDA_PROFILE_SCOPE("UI/UpdateAsyncTextures");
#ifndef SAIDA_RHI_WEBGPU
        uiRenderer_->updateAsyncTextures(encoder);
#endif
    }

    // GI update (skipped when the volume is frozen in baked mode): re-voxelize the
    // scene albedo, then trace/blend/border the DDGI probes. The lighting pass
    // samples the result. When frozen, the previously-baked atlas is kept.
    if (giUpdateThisFrame_) {
        SAIDA_GPU_PROFILE_SCOPE(gpuProfiler, cmd, "GPU/DDGI");
        gi_->voxelize(encoder, scene, gpuProfiler);
#ifdef SAIDA_RHI_WEBGPU
        gi_->update(encoder, *giComputeGlobalGroups_[currentFrame_], gpuProfiler);
#else
        gi_->update(encoder, *globalGroups_[currentFrame_], gpuProfiler);
#endif
    }

    bool useGpuDriven = false; // TEMPORARILY DISABLED FOR DEBUGGING

    if (useGpuDriven && currentInstanceCount_ > 0) {
        // 1. Clear the count buffer, visible to the cull dispatch.
        encoder.fillBuffer(*countBuffers_[currentFrame_], 0, sizeof(uint32_t), 0);
        encoder.transferToComputeBarrier();

        // 2. Cull dispatch: writes the culled draw commands + count.
        rhi::ComputePassEncoder cp = encoder.beginComputePass();
        cp.setPipeline(*cullingPipeline_);
        cp.setBindGroup(0, *cullingGroups_[currentFrame_]);

        struct CullingPushConstants {
            glm::vec4 frustumPlanes[6];
            uint32_t instanceCount;
        } pc;
        for (int i = 0; i < 6; ++i) pc.frustumPlanes[i] = cameraFrustum_.planes[i];
        pc.instanceCount = currentInstanceCount_;
        cp.setPushConstants(&pc, sizeof(pc));
        cp.dispatch((currentInstanceCount_ + 63) / 64);
        cp.end();

        // Culled commands/count -> indirect draw reads.
        encoder.computeToIndirectBarrier();
    }

    // Shadow passes: render scene depth from each caster's POV before the main
    // pass. The caster list is not camera-culled, so off-camera casters still
    // cast into view; each shadow layer just reuses it.
    {
        SAIDA_GPU_PROFILE_SCOPE(gpuProfiler, cmd, "GPU/Shadows");
        recordShadowPasses(encoder);
    }

    auto& settings = scene.settings();
    glm::vec4 clearColor = settings.clearColor;
    rhi::Rect2D renderRect = activeRenderRect();
    rhi::Extent2D extent = renderRect.extent;

    // Feature pre-pass: compute that must run OUTSIDE the scene render pass (GPU
    // particle sim). Recorded here between the shadow passes and the HDR pass;
    // the resulting particles are drawn inside the pass by the same feature.
    {
        SAIDA_GPU_PROFILE_SCOPE(gpuProfiler, cmd, "Scene/FeaturesPrePass");
        PrePassContext ppc{encoder, currentFrame_, scene, Time::elapsed(), false,
                           &camera, nullptr, extent};
        for (auto& f : features_) f->recordPrePass(ppc);
    }

#ifdef SAIDA_RHI_WEBGPU
    const bool msaa = false;
#else
    const bool msaa = swapchain_->samples() != VK_SAMPLE_COUNT_1_BIT;
#endif
    // Color is rendered into the MSAA target and resolved to the HDR image; with
    // no MSAA we render straight to the HDR image.
    auto colorImage = msaa ? hdrMsaaTexture_->image() : hdrTexture_->image();
    auto colorView = msaa ? hdrMsaaTexture_->view() : hdrTexture_->view();

    // Dynamic rendering does no implicit layout transitions: do them by hand.
    // Same-state transitions with discardContents keep the previous frame's sync
    // scope but drop the stale contents (the pass clears everything).
    encoder.transition(colorImage, rhi::ResourceState::ColorAttachment,
                       rhi::ResourceState::ColorAttachment, 0, kAllTextureLayers,
                       /*discardContents=*/true);
    if (msaa) {
        encoder.transition(hdrTexture_->image(), rhi::ResourceState::ColorAttachment,
                           rhi::ResourceState::ColorAttachment, 0, kAllTextureLayers,
                           /*discardContents=*/true);
        encoder.transition(depthResolveTexture_->image(), rhi::ResourceState::DepthWrite,
                           rhi::ResourceState::DepthWrite, 0, kAllTextureLayers,
                           /*discardContents=*/true);
    }
    encoder.transition(swapchain_->depthImage(), rhi::ResourceState::DepthWrite,
                       rhi::ResourceState::DepthWrite, 0, kAllTextureLayers,
                       /*discardContents=*/true);

    rhi::RenderPassDesc scenePass;
    scenePass.colorCount = 1;
    scenePass.colors[0].view = colorView;
    scenePass.colors[0].loadOp = rhi::LoadOp::Clear;
    scenePass.colors[0].clearColor = {{clearColor.r, clearColor.g, clearColor.b, clearColor.a}};
    scenePass.colors[0].store = !msaa;                          // MSAA source is resolved, not kept
    scenePass.colors[0].resolveView = msaa ? hdrTexture_->view() : rhi::TextureView{};
    scenePass.depth.view = swapchain_->depthView();
    scenePass.depth.loadOp = rhi::LoadOp::Clear;
    scenePass.depth.store = !msaa;
    scenePass.depth.resolveView = msaa ? depthResolveTexture_->view() : rhi::TextureView{};
    scenePass.x = renderRect.offset.x;
    scenePass.y = renderRect.offset.y;
    scenePass.width = extent.width;
    scenePass.height = extent.height;

    {
    SAIDA_GPU_PROFILE_SCOPE(gpuProfiler, cmd, "GPU/SceneHDR");
    rhi::RenderPassEncoder rp = encoder.beginRenderPass(scenePass);

#ifndef SAIDA_RHI_WEBGPU
    if (useGpuDriven) {
        rp.setPipeline(*pipeline_);
        // Set 0: per-frame global data (camera + lighting), shared by every object.
        rp.setBindGroup(0, *globalGroups_[currentFrame_]);
        // Set 1: bindless textures + MaterialData SSBO (raw, out of RHI scope until 16.4).
        rp.setBindGroup(1, resources_.globalMaterialSet());
        // Set 2: instance buffer (shared with the cull dispatch).
        rp.setBindGroup(2, *cullingGroups_[currentFrame_]);

        PushConstants gfxPc{};
        gfxPc.model = glm::mat4(1.0f); // Unused in bindless
        gfxPc.params = glm::vec4(0.0f);
        rp.setPushConstants(&gfxPc, sizeof(PushConstants));

        auto& geometry = resources_.geometry();
        rp.setVertexBuffer(*geometry.vertexBuffer());
        rp.setIndexBuffer(*geometry.indexBuffer());

        if (currentInstanceCount_ > 0) {
            if (device_.capabilities().drawIndirectCount) {
                rp.drawIndexedIndirectCount(*drawCommandBuffers_[currentFrame_], 0,
                                            *countBuffers_[currentFrame_], 0,
                                            currentInstanceCount_,
                                            sizeof(DrawIndexedIndirectCommand));
            } else {
                // 16.4 fallback: the cull shader writes instanceCount=0 for culled
                // draws, so a plain indirect loop stays correct without the count.
                rp.drawIndexedIndirect(*drawCommandBuffers_[currentFrame_], 0,
                                       currentInstanceCount_,
                                       sizeof(DrawIndexedIndirectCommand));
            }
        }
    } else
#endif
    {
        SAIDA_GPU_PROFILE_SCOPE(gpuProfiler, cmd, "Scene/Opaque");
        recordMeshDraws(rp, pipeline_.get(), false);
    }

    // Scene-pass features are recorded after opaque meshes.
    FrameContext fc{encoder, rp,
                    currentFrame_, globalGroups_[currentFrame_].get(), scene,
                    Time::elapsed(), false, &camera, nullptr, false, extent,
                    currentDraws_.data(), static_cast<uint32_t>(currentDraws_.size())};
    {
        SAIDA_GPU_PROFILE_SCOPE(gpuProfiler, cmd, "Scene/Features");
        recordFeatures(fc);
    }
    recordWorldWebCanvases(rp, scene, camera);

    rp.end();
    }

    recordTonemapPass(encoder, imageIndex, scene, camera);

    // Transition the swap-chain image from color attachment to present.
    encoder.transition(swapchain_->image(imageIndex), rhi::ResourceState::ColorAttachment,
                       rhi::ResourceState::Present);

    if (gpuProfiler) gpuProfiler->endZone(cmd, gpuFrameZone);
}

void Renderer::drawFrame(Scene& scene, Camera& camera, Project* project) {
    swapchain_->waitFrame(currentFrame_);
    if (Profiler::instance().enabled() && gpuProfiler_) {
        gpuProfiler_->beginFrame(currentFrame_);
        gpuProfiler_->publishLatest();
    }

    if (project) {
        SAIDA_PROFILE_SCOPE("Renderer/ProjectSettings");
#ifndef SAIDA_RHI_WEBGPU
        if (swapchain_->setVSync(project->vSync())) {
            cleanupHdrResources();
            createHdrResources();
            return;
        }
#endif

        if (shadowMap_->resize(project->shadowResolution())) {
            updateGlobalShadowDescriptor();
        }
    }

    uint32_t imageIndex;
    if (!swapchain_->acquire(currentFrame_, imageIndex)) {
        // Out of date: the surface recreated itself; rebuild the HDR chain and
        // skip this frame.
        cleanupHdrResources();
        createHdrResources();
        return;
    }

    auto& settings = scene.settings();

    const int lightingMode = static_cast<int>(settings.lightingMode);
    const int giMode = static_cast<int>(settings.giMode);
    const bool giModeChanged = giMode != giLastMode_;
    const bool lightingModeChanged = lightingMode != giLastLightingMode_;
    const bool giEnabledChanged = settings.giEnabled != giWasEnabled_;
    const bool giHierarchyChanged = Node::g_hierarchyVersion != giLastHierarchyVersion_;
    const bool giTransformChanged = Node::g_transformVersion != giLastTransformVersion_;
    const uint64_t giSignature = giDirtySignature(scene);
    const bool giContentChanged = giSignature != giLastDirtySignature_;
    const bool giDirty = giHierarchyChanged || giTransformChanged || giContentChanged;
    if (giModeChanged || lightingModeChanged || giEnabledChanged || giHierarchyChanged) {
        giRealtimeWarmupRemaining_ = kGIRealtimeWarmupFrames;
        giLastMode_ = giMode;
        giLastLightingMode_ = lightingMode;
        giWasEnabled_ = settings.giEnabled;
    }

    // Decide whether the DDGI volume updates this frame. Full realtime updates
    // every frame; amortized realtime warms up then updates on a cadence.
    // Baked: a bake request runs the update for kGIBakeFrames frames to converge
    // the volume, then freezes it (direct lighting + shadows stay live in both
    // modes; only the indirect volume is frozen).
    if (settings.lightingMode == LightingMode::Realtime) {
        settings.bakeRequested = false;  // bake only applies in baked mode
        giUpdateThisFrame_ = settings.giEnabled && shouldUpdateRealtimeGI(giDirty);
        if (giUpdateThisFrame_ && giRealtimeWarmupRemaining_ > 0) {
            --giRealtimeWarmupRemaining_;
        }
    } else {  // Baked
        if (settings.bakeRequested) {
            settings.bakeRequested = false;
            giBakeFramesRemaining_ = kGIBakeFrames;
            settings.baked = false;
        }
        giUpdateThisFrame_ = giBakeFramesRemaining_ > 0;
        if (giUpdateThisFrame_ && --giBakeFramesRemaining_ == 0) {
            // DDGI volume converged → freeze it. All surfaces sample the frozen
            // volume for indirect; direct lighting + shadows stay live.
            settings.baked = true;
        }
    }
    if (giUpdateThisFrame_ || !settings.giEnabled) {
        giLastHierarchyVersion_ = Node::g_hierarchyVersion;
        giLastTransformVersion_ = Node::g_transformVersion;
        giLastDirtySignature_ = giSignature;
    }
    ++giFrameCounter_;

    // Swap the DDGI ping-pong (only when updating, so the frozen atlas is kept)
    // and re-point this frame's set 0 at the current atlas (safe: fence waited).
    if (giUpdateThisFrame_) gi_->beginFrame();
    {
        SAIDA_PROFILE_SCOPE("Renderer/UpdateDescriptors");
        updateGIDescriptors();
        updateEnvironmentDescriptor(scene);
    }

    rhi::Rect2D renderRect = activeRenderRect();
    {
        SAIDA_PROFILE_SCOPE("Renderer/UpdateUniforms");
        updateUniformBuffer(currentFrame_, scene, camera, project);
    }
    {
        SAIDA_PROFILE_SCOPE("UI/Gather");
#ifndef SAIDA_RHI_WEBGPU
        uiRenderer_->gatherUI(scene,
            {static_cast<float>(renderRect.extent.width), static_cast<float>(renderRect.extent.height)});
#endif
    }

    rhi::CommandEncoder encoder = swapchain_->beginFrameCommands(currentFrame_);
    {
        SAIDA_PROFILE_SCOPE("Renderer/RecordCommandBuffer");
        recordCommandBuffer(encoder, imageIndex, scene, camera);
    }
    if (swapchain_->submitAndPresent(encoder, currentFrame_, imageIndex)) {
        // Out of date / suboptimal / resized: the surface recreated itself;
        // rebuild the HDR chain sized to the new extent.
        cleanupHdrResources();
        createHdrResources();
    }

    currentFrame_ = (currentFrame_ + 1) % kMaxFramesInFlight;
}

} // namespace saida

#ifdef SAIDA_ENABLE_XR
namespace saida {

namespace {
// All eyes render in one pass into a 2-layer image; the view mask has one bit
// per eye (0b11 for stereo). gl_ViewIndex selects the per-eye matrices.
uint32_t xrViewMask(uint32_t viewCount) { return (1u << viewCount) - 1u; }
}

void Renderer::createXrTargets() {
    // 2-layer HDR color: the array view is the multiview render target, the
    // per-layer views feed each eye's tonemap. Same layout for depth.
    rhi::RenderTextureDesc hdrDesc;
    hdrDesc.format = rhi::Format::RGBA16Float;
    hdrDesc.width = xrExtent_.width;
    hdrDesc.height = xrExtent_.height;
    hdrDesc.layers = xrViewCount_;            // one layer per eye
    hdrDesc.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled;
    hdrDesc.memoryCategory = "RenderTarget/XR";
    xrHdrTexture_ = std::make_unique<rhi::RenderTexture>(device_, hdrDesc);

    rhi::RenderTextureDesc depthDesc;
    depthDesc.format = rhi::vulkan::fromVk(device_.findDepthFormat());
    depthDesc.width = xrExtent_.width;
    depthDesc.height = xrExtent_.height;
    depthDesc.layers = xrViewCount_;
    depthDesc.usage = rhi::TextureUsage::DepthAttachment | rhi::TextureUsage::Sampled;
    depthDesc.memoryCategory = "RenderTarget/XR";
    xrDepthTexture_ = std::make_unique<rhi::RenderTexture>(device_, depthDesc);

    for (uint32_t i = 0; i < std::min<uint32_t>(xrViewCount_, 2); ++i) {
        xrPostProcessors_[i] = std::make_unique<PostProcessor>(device_, xrExtent_,
            rhi::Format::RGBA16Float, xrHdrTexture_->layerView(i));
    }
}

void Renderer::cleanupXrTargets() {
    for (auto& post : xrPostProcessors_) post.reset();
    xrDepthTexture_.reset();
    xrHdrTexture_.reset();
}

void Renderer::createXrPipelines() {
    const VkFormat hdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    const VkFormat depthFormat = device_.findDepthFormat();
    const uint32_t viewMask = xrViewMask(xrViewCount_);

    rhi::Pipeline::Desc sceneDesc;
    sceneDesc.colorFormats = {rhi::vulkan::fromVk(hdrFormat)};
    sceneDesc.depthFormat = rhi::vulkan::fromVk(depthFormat);
    sceneDesc.bindGroupLayouts = {globalSetLayout_.get(), &resources_.materialSetLayout()};
    sceneDesc.samples = 1;
    sceneDesc.viewMask = viewMask;

    sceneDesc.vertPath = shaderPath("multiview.shader.vert.spv");
    sceneDesc.fragPath = shaderPath("shader.frag.spv");
    xrScenePipeline_ = std::make_unique<rhi::Pipeline>(device_, sceneDesc);

    // Unlit multiview variant — same vertex shader + layout as the lit scene
    // pipeline, only the fragment differs (no per-eye data, so no MULTIVIEW frag).
    sceneDesc.fragPath = shaderPath("unlit.frag.spv");
    xrUnlitPipeline_ = std::make_unique<rhi::Pipeline>(device_, sceneDesc);

    if (resources_.globalMaterialSetLayout()) {
        rhi::Pipeline::Desc webDesc;
        webDesc.vertPath = shaderPath("multiview.web_canvas_world.vert.spv");
        webDesc.fragPath = shaderPath("web_canvas_world.frag.spv");
        webDesc.colorFormats = {rhi::vulkan::fromVk(hdrFormat)};
        webDesc.depthFormat = rhi::vulkan::fromVk(depthFormat);
        webDesc.bindGroupLayouts = {resources_.globalMaterialSetLayout()};  // raw bindless set
        webDesc.vertexInput = false;
        webDesc.depthWrite = false;
        webDesc.depthCompare = rhi::CompareOp::LessOrEqual;
        webDesc.cullMode = rhi::CullMode::None;
        webDesc.blendMode = rhi::BlendMode::Alpha;
        webDesc.topology = rhi::Topology::TriangleStrip;
        webDesc.pushConstantSize = sizeof(WebCanvasWorldPushConstants);
        webDesc.viewMask = viewMask;
        xrWebCanvasWorldPipeline_ = std::make_unique<rhi::Pipeline>(device_, webDesc);
    }

    // Same registry as desktop; each feature builds its stereo pipeline from the
    // viewMask. The Renderer stays ignorant of which effects exist.
    buildFeatures(viewMask, rhi::vulkan::fromVk(depthFormat), VK_SAMPLE_COUNT_1_BIT);

    {
        tonemapSetLayout_ = std::make_unique<rhi::BindGroupLayout>(device_,
            std::vector<rhi::BindGroupLayoutEntry>{
                {0, rhi::BindingType::CombinedImageSampler, rhi::ShaderStages::Fragment},  // HDR
                {1, rhi::BindingType::CombinedImageSampler, rhi::ShaderStages::Fragment},  // depth (AO)
                {2, rhi::BindingType::CombinedImageSampler, rhi::ShaderStages::Fragment},  // bloom
            });

        rhi::SamplerDesc linearDesc;
        linearDesc.mipFilter = rhi::FilterMode::Linear;
        tonemapSampler_ = std::make_unique<rhi::Sampler>(device_, linearDesc);

        rhi::SamplerDesc nearestDesc;
        nearestDesc.magFilter = rhi::FilterMode::Nearest;
        nearestDesc.minFilter = rhi::FilterMode::Nearest;
        tonemapDepthSampler_ = std::make_unique<rhi::Sampler>(device_, nearestDesc);

        updateXrTonemapDescriptorSets();

        rhi::Pipeline::Desc xrTonemapDesc;
        xrTonemapDesc.vertPath = shaderPath("tonemap.vert.spv");
        xrTonemapDesc.fragPath = shaderPath("tonemap.frag.spv");
        xrTonemapDesc.colorFormats = {rhi::vulkan::fromVk(xrColorFormat_)};
        xrTonemapDesc.bindGroupLayouts = {tonemapSetLayout_.get()};
        xrTonemapDesc.vertexInput = false;
        xrTonemapDesc.depthTest = false;
        xrTonemapDesc.pushConstantSize = sizeof(TonemapPushConstants);
        xrTonemapPipeline_ = std::make_unique<rhi::Pipeline>(device_, xrTonemapDesc);
    }
}

void Renderer::updateXrTonemapDescriptorSets() {
    if (!tonemapSetLayout_ || !tonemapSampler_ || !tonemapDepthSampler_) return;
    const uint32_t n = std::min<uint32_t>(xrViewCount_, 2);
    for (uint32_t i = 0; i < n; ++i) {
        if (!xrPostProcessors_[i]) continue;

        rhi::BindGroupEntry hdrEntry;
        hdrEntry.binding = 0;
        hdrEntry.view = xrHdrTexture_->layerView(i);
        hdrEntry.sampler = tonemapSampler_->handle();

        rhi::BindGroupEntry depthEntry;
        depthEntry.binding = 1;
        depthEntry.view = xrDepthTexture_->layerView(i);
        depthEntry.sampler = tonemapDepthSampler_->handle();

        rhi::BindGroupEntry bloomEntry;
        bloomEntry.binding = 2;
        bloomEntry.view = xrPostProcessors_[i]->bloomView();
        bloomEntry.sampler = xrPostProcessors_[i]->bloomSampler();

        xrTonemapSets_[i] = std::make_unique<rhi::BindGroup>(*tonemapSetLayout_,
            std::vector<rhi::BindGroupEntry>{hdrEntry, depthEntry, bloomEntry});
    }
}

void Renderer::updateUniformBufferXr(uint32_t frame, const std::vector<EyeRenderInfo>& eyes,
                                     Scene& scene, Project* project) {
    UniformBufferObject ubo{};
    const uint32_t n = std::min<uint32_t>(static_cast<uint32_t>(eyes.size()), 2);
    for (uint32_t i = 0; i < n; ++i) {
        ubo.view[i] = eyes[i].view;
        ubo.proj[i] = eyes[i].projection;
    }
    if (n == 1) { ubo.view[1] = ubo.view[0]; ubo.proj[1] = ubo.proj[0]; }
    uniformBuffers_[frame]->write(&ubo, sizeof(ubo));

    // Specular/view-dependent terms use the eye centroid (head). No frustum cull:
    // a single combined-stereo frustum isn't worth the complexity here.
    glm::vec3 head(0.0f);
    for (uint32_t i = 0; i < n; ++i) head += eyes[i].eyePosition;
    head /= static_cast<float>(std::max(1u, n));

    LightingUBO lighting{};
    const glm::mat4* view = n > 0 ? &eyes[0].view : nullptr;
    const glm::mat4* proj = n > 0 ? &eyes[0].projection : nullptr;
    gatherScene(lighting, scene, head, nullptr, project, view, proj);
    lightingBuffers_[frame]->write(&lighting, sizeof(lighting));
}

void Renderer::recordXrScenePass(rhi::CommandEncoder& encoder, Scene& scene,
                                 const std::vector<EyeRenderInfo>& eyes) {
    auto& settings = scene.settings();
    // Passthrough (AR): clear fully transparent so the compositor blends the real
    // world through the background; opaque geometry (alpha 1) stays visible.
    const bool passthrough = XRPassthrough::enabled();
    glm::vec4 clearColor = settings.clearColor;
    if (passthrough) clearColor.a = 0.0f;

    // Feature pre-pass compute (GPU particle sim) — outside any render pass.
    {
        PrePassContext ppc{encoder, currentFrame_, scene, Time::elapsed(), true,
                           nullptr, &eyes, xrExtent_};
        for (auto& f : features_) f->recordPrePass(ppc);
    }

    // Both eye layers come back from sampling with stale contents (cleared below).
    encoder.transition(xrHdrTexture_->image(), rhi::ResourceState::ColorAttachment,
                       rhi::ResourceState::ColorAttachment, 0, xrViewCount_,
                       /*discardContents=*/true);
    encoder.transition(xrDepthTexture_->image(), rhi::ResourceState::DepthWrite,
                       rhi::ResourceState::DepthWrite, 0, xrViewCount_,
                       /*discardContents=*/true);

    rhi::RenderPassDesc pass;
    pass.colorCount = 1;
    pass.colors[0].view = xrHdrTexture_->view();
    pass.colors[0].loadOp = rhi::LoadOp::Clear;
    pass.colors[0].clearColor = {{clearColor.r, clearColor.g, clearColor.b, clearColor.a}};
    pass.depth.view = xrDepthTexture_->view();
    pass.depth.loadOp = rhi::LoadOp::Clear;
    pass.width = xrExtent_.width;
    pass.height = xrExtent_.height;
    pass.layerCount = 1;                        // ignored when viewMask != 0
    pass.viewMask = xrViewMask(xrViewCount_);   // multiview: both eyes in one pass

    rhi::RenderPassEncoder rp = encoder.beginRenderPass(pass);

    recordMeshDraws(rp, xrScenePipeline_.get(), true);

    // passthrough is forwarded so the skybox skips itself.
    FrameContext fc{encoder, rp,
                    currentFrame_, globalGroups_[currentFrame_].get(), scene,
                    Time::elapsed(), true, nullptr, &eyes, passthrough, xrExtent_,
                    currentDraws_.data(), static_cast<uint32_t>(currentDraws_.size())};
    recordFeatures(fc);
    recordXrWorldWebCanvases(rp, scene, eyes);

    rp.end();
}

void Renderer::recordXrWorldWebCanvases(rhi::RenderPassEncoder& rp, Scene& scene,
                                        const std::vector<EyeRenderInfo>& eyes) {
    if (!xrWebCanvasWorldPipeline_ || !resources_.globalMaterialSet()) return;

    glm::vec3 head(0.0f);
    for (const EyeRenderInfo& eye : eyes) head += eye.eyePosition;
    head /= static_cast<float>(std::max<size_t>(1, eyes.size()));

    std::vector<WebCanvasWorldDraw> draws = collectWorldWebCanvasDraws(scene, head);
    if (draws.empty()) return;

    rp.setPipeline(*xrWebCanvasWorldPipeline_);
    rp.setBindGroup(0, resources_.globalMaterialSet());

    for (const WebCanvasWorldDraw& draw : draws) {
        WebCanvasNode* node = draw.node;
        Texture* texture = node->texture();
        if (!texture) continue;

        WebCanvasWorldPushConstants pc{};
        pc.model = node->worldTransform();
        pc.params = glm::vec4(node->worldWidth(), node->worldHeight(),
                              static_cast<float>(resources_.ensureBindlessTextureIndex(texture)), 1.0f);
        rp.setPushConstants(&pc, sizeof(pc));
        rp.draw(4);
    }
}

void Renderer::recordXrTonemap(rhi::CommandEncoder& encoder, Scene& scene,
                               const std::vector<EyeRenderInfo>& eyes) {
    VkCommandBuffer cmd = encoder.handle();  // GPU profiler zones (desktop tooling)
    GpuProfiler* gpuProfiler = Profiler::instance().enabled() ? gpuProfiler_.get() : nullptr;
    // HDR + depth (all eye layers) -> shader read for sampling.
    encoder.transition(xrHdrTexture_->image(), rhi::ResourceState::ColorAttachment,
                       rhi::ResourceState::ShaderRead, 0, xrViewCount_);
    encoder.transition(xrDepthTexture_->image(), rhi::ResourceState::DepthWrite,
                       rhi::ResourceState::ShaderRead, 0, xrViewCount_);

    const uint32_t n = std::min<uint32_t>(static_cast<uint32_t>(eyes.size()), xrViewCount_);
    {
    SAIDA_GPU_PROFILE_SCOPE(gpuProfiler, cmd, "GPU/Tonemap+EditorUI");
    for (uint32_t i = 0; i < n; ++i) {
        if (xrPostProcessors_[i]) {
            xrPostProcessors_[i]->recordBloom(encoder, scene.settings(),
                glm::vec4(0.0f, 0.0f, 1.0f, 1.0f), gpuProfiler);
        }
    }

    for (uint32_t i = 0; i < n; ++i) {
        const EyeRenderInfo& eye = eyes[i];
        // The XR image starts UNDEFINED; the compositor wants it left in
        // COLOR_ATTACHMENT_OPTIMAL, which is exactly where rendering leaves it.
        encoder.transition(eye.image, rhi::ResourceState::ColorAttachment,
                           rhi::ResourceState::ColorAttachment, 0, VK_REMAINING_ARRAY_LAYERS,
                           /*discardContents=*/true);

        rhi::RenderPassDesc pass;
        pass.colorCount = 1;
        pass.colors[0].view = eye.imageView;
        pass.colors[0].loadOp = rhi::LoadOp::DontCare;  // fully overwritten by the tonemap
        pass.width = eye.extent.width;
        pass.height = eye.extent.height;

        rhi::RenderPassEncoder rp = encoder.beginRenderPass(pass);
        rp.setPipeline(*xrTonemapPipeline_);
        rp.setBindGroup(0, *xrTonemapSets_[i]);
        TonemapPushConstants push = tonemapPushConstants(scene.settings(), eye.projection);
        {
            SAIDA_GPU_PROFILE_SCOPE(gpuProfiler, cmd, "Post/Tonemap");
            rp.setPushConstants(&push, sizeof(TonemapPushConstants));
            rp.draw(3);
        }
        rp.end();
    }
    }
}

void Renderer::drawXr(VkCommandBuffer cmd, const std::vector<EyeRenderInfo>& eyes,
                      Scene& scene, Project* project) {
    if (eyes.empty()) return;
    auto& settings = scene.settings();

    const int lightingMode = static_cast<int>(settings.lightingMode);
    const int giMode = static_cast<int>(settings.giMode);
    const bool giModeChanged = giMode != giLastMode_;
    const bool lightingModeChanged = lightingMode != giLastLightingMode_;
    const bool giEnabledChanged = settings.giEnabled != giWasEnabled_;
    const bool giHierarchyChanged = Node::g_hierarchyVersion != giLastHierarchyVersion_;
    const bool giTransformChanged = Node::g_transformVersion != giLastTransformVersion_;
    const uint64_t giSignature = giDirtySignature(scene);
    const bool giContentChanged = giSignature != giLastDirtySignature_;
    const bool giDirty = giHierarchyChanged || giTransformChanged || giContentChanged;
    if (giModeChanged || lightingModeChanged || giEnabledChanged || giHierarchyChanged) {
        giRealtimeWarmupRemaining_ = kGIRealtimeWarmupFrames;
        giLastMode_ = giMode;
        giLastLightingMode_ = lightingMode;
        giWasEnabled_ = settings.giEnabled;
    }

    // GI update cadence (mirrors drawFrame; no editor bake UI in XR yet).
    if (settings.lightingMode == LightingMode::Realtime) {
        giUpdateThisFrame_ = settings.giEnabled &&
            shouldUpdateRealtimeGI(giDirty);
        if (giUpdateThisFrame_ && giRealtimeWarmupRemaining_ > 0) {
            --giRealtimeWarmupRemaining_;
        }
    } else {
        if (settings.bakeRequested) {
            settings.bakeRequested = false;
            giBakeFramesRemaining_ = kGIBakeFrames;
            settings.baked = false;
        }
        giUpdateThisFrame_ = giBakeFramesRemaining_ > 0;
        if (giUpdateThisFrame_ && --giBakeFramesRemaining_ == 0)
            settings.baked = true;
    }
    if (giUpdateThisFrame_ || !settings.giEnabled) {
        giLastHierarchyVersion_ = Node::g_hierarchyVersion;
        giLastTransformVersion_ = Node::g_transformVersion;
        giLastDirtySignature_ = giSignature;
    }
    ++giFrameCounter_;

    if (giUpdateThisFrame_) gi_->beginFrame();
    updateGIDescriptors();
    updateEnvironmentDescriptor(scene);

    // CPU prep (per-eye matrices, lighting, draw list, bones).
    updateUniformBufferXr(currentFrame_, eyes, scene, project);

    // View-independent passes recorded once, then the stereo scene + tonemap.
    rhi::CommandEncoder encoder(cmd);
    if (giUpdateThisFrame_) {
        GpuProfiler* gpuProfiler = Profiler::instance().enabled() ? gpuProfiler_.get() : nullptr;
        SAIDA_GPU_PROFILE_SCOPE(gpuProfiler, cmd, "GPU/DDGI");
        gi_->voxelize(encoder, scene, gpuProfiler);
        gi_->update(encoder, *globalGroups_[currentFrame_], gpuProfiler);
    }
    recordShadowPasses(encoder);

    recordXrScenePass(encoder, scene, eyes);
    recordXrTonemap(encoder, scene, eyes);

    currentFrame_ = (currentFrame_ + 1) % kMaxFramesInFlight;
}

} // namespace saida
#endif // SAIDA_ENABLE_XR

