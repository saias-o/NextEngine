#include "render/Renderer.hpp"

#include "core/Camera.hpp"
#include "core/Paths.hpp"
#include "core/Window.hpp"
#include "project/Project.hpp"
#include "graphics/Buffer.hpp"
#include "graphics/ImGuiLayer.hpp"
#include "graphics/Material.hpp"
#include "graphics/Mesh.hpp"
#include "graphics/GpuSync.hpp"
#include "graphics/Pipeline.hpp"
#include "graphics/ShadowMap.hpp"
#include "graphics/Swapchain.hpp"
#include "graphics/VulkanDevice.hpp"
#include "graphics/ResourceManager.hpp"
#include "render/LightBaker.hpp"
#include "render/GIVolume.hpp"
#include "graphics/UIRenderer.hpp"
#include "scene/LightNode.hpp"
#include "scene/Node.hpp"
#include "scene/Scene.hpp"
#include "core/Log.hpp"

#include "scene/LightNode.hpp"
#include "scene/MeshNode.hpp"
#include "scene/animation/Animator.hpp"
#include "xr/XrSession.hpp"   // xr::EyeView
#include "xr/toolkit/XRPassthrough.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include "graphics/ComputePipeline.hpp"
#include "graphics/Texture.hpp"
#include <stdexcept>

namespace ne {

namespace {
constexpr int kMaxFramesInFlight = 2;

// Directional shadows cover a fixed-size world box centered on the origin; large
// enough for the demo scene, easy to grow later (or fit to the camera/scene AABB).
constexpr float kShadowOrthoHalfSize = 25.0f;
constexpr float kShadowOrthoDepth = 100.0f;

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

// Light-space view-proj for a spot light (perspective from its cone), Vulkan-flipped.
glm::mat4 spotMatrix(const glm::vec3& pos, const glm::vec3& dir, float outerAngleDeg, float range) {
    float fovy = glm::clamp(glm::radians(outerAngleDeg) * 2.0f, glm::radians(10.0f), glm::radians(170.0f));
    glm::mat4 view = glm::lookAt(pos, pos + dir, safeUp(dir));
    glm::mat4 proj = glm::perspective(fovy, 1.0f, 0.1f, std::max(range, 1.0f));
    proj[1][1] *= -1.0f;
    return proj * view;
}
}

Renderer::Renderer(VulkanDevice& device, Swapchain& swapchain, Window& window,
                   ResourceManager& resources, ImGuiLayer& imgui)
    : device_(device), swapchain_(&swapchain), window_(window), resources_(resources), imgui_(&imgui) {
    createGlobalSetLayout();
    lightBaker_ = std::make_unique<LightBaker>(device_, globalSetLayout_);
    uiRenderer_ = std::make_unique<UIRenderer>(device_, resources_, swapchain_->colorFormat());
    createHdrResources();
    createPipeline(resources_.materialSetLayout());
    createTonemapPipeline();
    createSkyboxPipeline();
    createUniformBuffers();
    shadowMap_ = std::make_unique<ShadowMap>(device_);
    gi_ = std::make_unique<GIVolume>(device_, giDescForTier(device_.capabilities().tier),
                                     resources_.materialSetLayout(), globalSetLayout_);
    createGlobalDescriptorPool();
    createGlobalDescriptorSets();
    createCommandBuffers();
    createSyncObjects();

    if (device_.capabilities().descriptorIndexing && device_.capabilities().multiDrawIndirect) {
        createGpuDrivenBuffers();
        createCullingPipeline();
    }
}

Renderer::Renderer(VulkanDevice& device, Window& window, ResourceManager& resources,
                   VkExtent2D xrEyeExtent, VkFormat xrColorFormat, uint32_t xrViewCount)
    : device_(device), window_(window), resources_(resources),
      xrMode_(true), xrExtent_(xrEyeExtent), xrColorFormat_(xrColorFormat),
      xrViewCount_(xrViewCount) {
    // Shared rendering machinery (identical to desktop) ...
    createGlobalSetLayout();
    lightBaker_ = std::make_unique<LightBaker>(device_, globalSetLayout_);
    createUniformBuffers();
    shadowMap_ = std::make_unique<ShadowMap>(device_);
    gi_ = std::make_unique<GIVolume>(device_, giDescForTier(device_.capabilities().tier),
                                     resources_.materialSetLayout(), globalSetLayout_);
    createGlobalDescriptorPool();
    createGlobalDescriptorSets();
    // ... plus the XR-specific stereo targets + multiview pipelines.
    createXrTargets();
    createXrPipelines();
}

Renderer::~Renderer() {
    vkDeviceWaitIdle(device_.device());
    // Desktop-only present sync (not created in XR mode).
    for (size_t i = 0; i < inFlightFences_.size(); i++) {
        vkDestroySemaphore(device_.device(), imageAvailableSemaphores_[i], nullptr);
        vkDestroyFence(device_.device(), inFlightFences_[i], nullptr);
    }
    if (xrMode_) {
        cleanupXrTargets();
        if (xrTonemapPool_) vkDestroyDescriptorPool(device_.device(), xrTonemapPool_, nullptr);
    }
    vkDestroyDescriptorPool(device_.device(), globalPool_, nullptr);
    vkDestroyDescriptorSetLayout(device_.device(), globalSetLayout_, nullptr);
    cleanupHdrResources();
    if (tonemapSampler_) vkDestroySampler(device_.device(), tonemapSampler_, nullptr);
    if (tonemapPool_) vkDestroyDescriptorPool(device_.device(), tonemapPool_, nullptr);
    if (tonemapSetLayout_) vkDestroyDescriptorSetLayout(device_.device(), tonemapSetLayout_, nullptr);
    
    if (skyboxPool_) vkDestroyDescriptorPool(device_.device(), skyboxPool_, nullptr);
    if (skyboxSetLayout_) vkDestroyDescriptorSetLayout(device_.device(), skyboxSetLayout_, nullptr);

    if (cullingSetLayout_) vkDestroyDescriptorSetLayout(device_.device(), cullingSetLayout_, nullptr);
    if (cullingPool_) vkDestroyDescriptorPool(device_.device(), cullingPool_, nullptr);
    // pipeline_ and the buffers are torn down by their destructors.
}

void Renderer::createGlobalSetLayout() {
    // Set 0: camera UBO (vertex) + lighting UBO (fragment), shared by all draws.
    VkDescriptorSetLayoutBinding cameraBinding{};
    cameraBinding.binding = 0;
    cameraBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    cameraBinding.descriptorCount = 1;
    cameraBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding lightingBinding{};
    lightingBinding.binding = 1;
    lightingBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    lightingBinding.descriptorCount = 1;
    lightingBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 2: shadow map array (sampled in the fragment shader and DDGI trace).
    VkDescriptorSetLayoutBinding shadowBinding{};
    shadowBinding.binding = 2;
    shadowBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowBinding.descriptorCount = 1;
    shadowBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 3: Global SSBO for bone matrices
    VkDescriptorSetLayoutBinding boneBinding{};
    boneBinding.binding = 3;
    boneBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boneBinding.descriptorCount = 1;
    boneBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // Bindings 4/5: DDGI irradiance + visibility atlases (sampled in the fragment
    // shader for indirect diffuse — the single GI primitive).
    VkDescriptorSetLayoutBinding giIrradianceBinding{};
    giIrradianceBinding.binding = 4;
    giIrradianceBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    giIrradianceBinding.descriptorCount = 1;
    giIrradianceBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding giVisibilityBinding{};
    giVisibilityBinding.binding = 5;
    giVisibilityBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    giVisibilityBinding.descriptorCount = 1;
    giVisibilityBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 6: voxelized scene albedo (debug view + DDGI trace).
    VkDescriptorSetLayoutBinding giVoxelBinding{};
    giVoxelBinding.binding = 6;
    giVoxelBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    giVoxelBinding.descriptorCount = 1;
    giVoxelBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

    std::array<VkDescriptorSetLayoutBinding, 7> bindings = {
        cameraBinding, lightingBinding, shadowBinding, boneBinding,
        giIrradianceBinding, giVisibilityBinding, giVoxelBinding};

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = static_cast<uint32_t>(bindings.size());
    ci.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device_.device(), &ci, nullptr, &globalSetLayout_) != VK_SUCCESS)
        throw std::runtime_error("failed to create global descriptor set layout");
}

void Renderer::createPipeline(VkDescriptorSetLayout materialSetLayout) {
    bool useGpuDriven = false; // TEMPORARILY DISABLED FOR DEBUGGING
    
    std::string vertShader = useGpuDriven ? "bindless.shader.vert.spv" : "shader.vert.spv";
    std::string fragShader = useGpuDriven ? "bindless.shader.frag.spv" : "shader.frag.spv";
    
    // set 0 = global, set 1 = material, set 2 = per-object baked lightmap, set 3 = culling/bindless (optional).
    std::vector<VkDescriptorSetLayout> setLayouts = {
        globalSetLayout_, materialSetLayout, lightBaker_->setLayout()};
        
    if (useGpuDriven) {
        setLayouts.push_back(cullingSetLayout_);
    }
        
    std::vector<VkFormat> colorFormats = {VK_FORMAT_R16G16B16A16_SFLOAT};
    pipeline_ = std::make_unique<Pipeline>(device_, shaderPath(vertShader),
        shaderPath(fragShader), colorFormats, swapchain_->depthFormat(), setLayouts,
        swapchain_->samples());
}

void Renderer::createUniformBuffers() {
    uniformBuffers_.reserve(kMaxFramesInFlight);
    lightingBuffers_.reserve(kMaxFramesInFlight);
    boneMatricesBuffers_.reserve(kMaxFramesInFlight);
    // Allocate a 4MB buffer for global bone matrices (enough for 65536 bones)
    const VkDeviceSize kBoneBufferSize = 4 * 1024 * 1024;
    for (int i = 0; i < kMaxFramesInFlight; i++) {
        uniformBuffers_.push_back(std::make_unique<Buffer>(device_, sizeof(UniformBufferObject),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, MemoryUsage::HostVisible));
        lightingBuffers_.push_back(std::make_unique<Buffer>(device_, sizeof(LightingUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, MemoryUsage::HostVisible));
        boneMatricesBuffers_.push_back(std::make_unique<Buffer>(device_, kBoneBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, MemoryUsage::HostVisible));
    }
}

void Renderer::createGpuDrivenBuffers() {
    instanceBuffers_.reserve(kMaxFramesInFlight);
    originalDrawCommandBuffers_.reserve(kMaxFramesInFlight);
    drawCommandBuffers_.reserve(kMaxFramesInFlight);
    countBuffers_.reserve(kMaxFramesInFlight);

    VkDeviceSize instanceBufferSize = kMaxInstances * sizeof(InstanceData);
    VkDeviceSize drawCommandBufferSize = kMaxInstances * sizeof(VkDrawIndexedIndirectCommand);

    for (int i = 0; i < kMaxFramesInFlight; i++) {
        instanceBuffers_.push_back(std::make_unique<Buffer>(device_, instanceBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, MemoryUsage::HostVisible));
            
        originalDrawCommandBuffers_.push_back(std::make_unique<Buffer>(device_, drawCommandBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, MemoryUsage::HostVisible));
        
        drawCommandBuffers_.push_back(std::make_unique<Buffer>(device_, drawCommandBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            MemoryUsage::GpuOnly));
            
        countBuffers_.push_back(std::make_unique<Buffer>(device_, sizeof(uint32_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            MemoryUsage::GpuOnly));
    }
}

void Renderer::createCullingPipeline() {
    // Descriptor Set 0 for Culling Pipeline:
    // Binding 0: InstanceBuffer (Storage, Read)
    // Binding 1: DrawCommandBuffer (Storage, Write)
    // Binding 2: CountBuffer (Storage, Write)
    // Binding 3: OriginalDrawCommandBuffer (Storage, Read)
    
    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = static_cast<uint32_t>(bindings.size());
    ci.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device_.device(), &ci, nullptr, &cullingSetLayout_) != VK_SUCCESS)
        throw std::runtime_error("failed to create culling set layout");

    std::array<VkDescriptorPoolSize, 1> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(kMaxFramesInFlight) * 4;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(kMaxFramesInFlight);
    if (vkCreateDescriptorPool(device_.device(), &poolInfo, nullptr, &cullingPool_) != VK_SUCCESS)
        throw std::runtime_error("failed to create culling descriptor pool");

    std::vector<VkDescriptorSetLayout> layouts(kMaxFramesInFlight, cullingSetLayout_);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = cullingPool_;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(kMaxFramesInFlight);
    allocInfo.pSetLayouts = layouts.data();

    cullingSets_.resize(kMaxFramesInFlight);
    if (vkAllocateDescriptorSets(device_.device(), &allocInfo, cullingSets_.data()) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate culling descriptor sets");

    for (int i = 0; i < kMaxFramesInFlight; i++) {
        VkDescriptorBufferInfo instanceInfo{};
        instanceInfo.buffer = instanceBuffers_[i]->handle();
        instanceInfo.offset = 0;
        instanceInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo drawCmdInfo{};
        drawCmdInfo.buffer = drawCommandBuffers_[i]->handle();
        drawCmdInfo.offset = 0;
        drawCmdInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo countInfo{};
        countInfo.buffer = countBuffers_[i]->handle();
        countInfo.offset = 0;
        countInfo.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo origDrawCmdInfo{};
        origDrawCmdInfo.buffer = originalDrawCommandBuffers_[i]->handle();
        origDrawCmdInfo.offset = 0;
        origDrawCmdInfo.range = VK_WHOLE_SIZE;

        std::array<VkWriteDescriptorSet, 4> writes{};
        
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = cullingSets_[i];
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &instanceInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = cullingSets_[i];
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &origDrawCmdInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = cullingSets_[i];
        writes[2].dstBinding = 2;
        writes[2].dstArrayElement = 0;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &countInfo;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = cullingSets_[i];
        writes[3].dstBinding = 3;
        writes[3].dstArrayElement = 0;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo = &drawCmdInfo;

        vkUpdateDescriptorSets(device_.device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
    
    // We also need to specify the push constants
    struct CullingPushConstants {
        glm::vec4 frustumPlanes[6];
        uint32_t instanceCount;
    };
    uint32_t pushConstantSize = sizeof(CullingPushConstants);

    std::vector<VkDescriptorSetLayout> plLayouts = {cullingSetLayout_};
    cullingPipeline_ = std::make_unique<ComputePipeline>(device_, shaderPath("culling.comp.spv"), plLayouts, pushConstantSize);
}

void Renderer::createGlobalDescriptorPool() {
    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(kMaxFramesInFlight) * 2;  // camera + lighting
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(kMaxFramesInFlight) * 4;  // shadow + GI irradiance + visibility + voxels
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = static_cast<uint32_t>(kMaxFramesInFlight);  // bone matrices SSBO

    VkDescriptorPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    ci.pPoolSizes = poolSizes.data();
    ci.maxSets = static_cast<uint32_t>(kMaxFramesInFlight);
    if (vkCreateDescriptorPool(device_.device(), &ci, nullptr, &globalPool_) != VK_SUCCESS)
        throw std::runtime_error("failed to create global descriptor pool");
}

void Renderer::createGlobalDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(kMaxFramesInFlight, globalSetLayout_);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = globalPool_;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(kMaxFramesInFlight);
    allocInfo.pSetLayouts = layouts.data();

    globalSets_.resize(kMaxFramesInFlight);
    if (vkAllocateDescriptorSets(device_.device(), &allocInfo, globalSets_.data()) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate global descriptor sets");

    for (int i = 0; i < kMaxFramesInFlight; i++) {
        VkDescriptorBufferInfo cameraInfo{};
        cameraInfo.buffer = uniformBuffers_[i]->handle();
        cameraInfo.offset = 0;
        cameraInfo.range = sizeof(UniformBufferObject);

        VkDescriptorBufferInfo lightInfo{};
        lightInfo.buffer = lightingBuffers_[i]->handle();
        lightInfo.offset = 0;
        lightInfo.range = sizeof(LightingUBO);

        VkDescriptorImageInfo shadowInfo{};
        shadowInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadowInfo.imageView = shadowMap_->arrayView();
        shadowInfo.sampler = shadowMap_->sampler();

        VkDescriptorBufferInfo boneInfo{};
        boneInfo.buffer = boneMatricesBuffers_[i]->handle();
        boneInfo.offset = 0;
        boneInfo.range = 4 * 1024 * 1024; // 4MB

        // Atlases live in GENERAL (compute-written, fragment-sampled).
        VkDescriptorImageInfo giIrradianceInfo{};
        giIrradianceInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        giIrradianceInfo.imageView = gi_->irradianceView();
        giIrradianceInfo.sampler = gi_->sampler();

        VkDescriptorImageInfo giVisibilityInfo{};
        giVisibilityInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        giVisibilityInfo.imageView = gi_->visibilityView();
        giVisibilityInfo.sampler = gi_->sampler();

        VkDescriptorImageInfo giVoxelInfo{};
        giVoxelInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        giVoxelInfo.imageView = gi_->voxelView();
        giVoxelInfo.sampler = gi_->sampler();

        std::array<VkWriteDescriptorSet, 7> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = globalSets_[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &cameraInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = globalSets_[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &lightInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = globalSets_[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo = &shadowInfo;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = globalSets_[i];
        writes[3].dstBinding = 3;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo = &boneInfo;

        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = globalSets_[i];
        writes[4].dstBinding = 4;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[4].descriptorCount = 1;
        writes[4].pImageInfo = &giIrradianceInfo;

        writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet = globalSets_[i];
        writes[5].dstBinding = 5;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[5].descriptorCount = 1;
        writes[5].pImageInfo = &giVisibilityInfo;

        writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet = globalSets_[i];
        writes[6].dstBinding = 6;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[6].descriptorCount = 1;
        writes[6].pImageInfo = &giVoxelInfo;

        vkUpdateDescriptorSets(device_.device(),
            static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void Renderer::updateGlobalShadowDescriptor() {
    for (int i = 0; i < kMaxFramesInFlight; i++) {
        VkDescriptorImageInfo shadowInfo{};
        shadowInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadowInfo.imageView = shadowMap_->arrayView();
        shadowInfo.sampler = shadowMap_->sampler();

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = globalSets_[i];
        write.dstBinding = 2;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &shadowInfo;

        vkUpdateDescriptorSets(device_.device(), 1, &write, 0, nullptr);
    }
}

void Renderer::updateGIDescriptors() {
    // After GIVolume::beginFrame() the "current" atlas (sampled by the lighting
    // pass) has changed; re-point set 0 bindings 4/5 for this frame's set. Safe:
    // drawFrame already waited on this frame's fence.
    VkDescriptorImageInfo irr{};
    irr.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    irr.imageView = gi_->irradianceView();
    irr.sampler = gi_->sampler();
    VkDescriptorImageInfo vis{};
    vis.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    vis.imageView = gi_->visibilityView();
    vis.sampler = gi_->sampler();

    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = globalSets_[currentFrame_];
    writes[0].dstBinding = 4;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &irr;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = globalSets_[currentFrame_];
    writes[1].dstBinding = 5;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &vis;
    vkUpdateDescriptorSets(device_.device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void Renderer::createCommandBuffers() {
    commandBuffers_.resize(kMaxFramesInFlight);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = device_.commandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
    if (vkAllocateCommandBuffers(device_.device(), &allocInfo, commandBuffers_.data()) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate command buffers");
}

void Renderer::createSyncObjects() {
    // imageAvailable semaphores and fences are per frame-in-flight; the
    // renderFinished semaphores are per swap-chain image and owned by Swapchain.
    imageAvailableSemaphores_.resize(kMaxFramesInFlight);
    inFlightFences_.resize(kMaxFramesInFlight);

    VkSemaphoreCreateInfo semCI{};
    semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceCI{};
    fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < kMaxFramesInFlight; i++) {
        if (vkCreateSemaphore(device_.device(), &semCI, nullptr, &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(device_.device(), &fenceCI, nullptr, &inFlightFences_[i]) != VK_SUCCESS)
            throw std::runtime_error("failed to create sync objects");
    }
}

void Renderer::gatherScene(LightingUBO& ubo, Scene& scene, const glm::vec3& cameraPos,
                           const Frustum* cullFrustum, Project* project) {
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
        // cube map). Realtime mode always re-renders them; the baked stub does
        // not yet freeze anything, so shadows stay live either way for now.
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

    auto getAnimatorInParent = [](Node* n) -> Animator* {
        while (n) {
            if (auto* a = n->getBehaviour<Animator>()) return a;
            n = n->parent();
        }
        return nullptr;
    };

    uint32_t currentBoneCount = 0;
    glm::mat4* boneData = static_cast<glm::mat4*>(boneMatricesBuffers_[currentFrame_]->mapped());

    bool useGpuDriven = false; // TEMPORARILY DISABLED FOR DEBUGGING
    if (useGpuDriven) {
        InstanceData* instanceData = static_cast<InstanceData*>(instanceBuffers_[currentFrame_]->mapped());
        VkDrawIndexedIndirectCommand* drawData = static_cast<VkDrawIndexedIndirectCommand*>(originalDrawCommandBuffers_[currentFrame_]->mapped());
        
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
                    float radius = 0.866f; // DamagedHelmet doesn't report bounds, so we use a standard local radius for the unit cube.
                    
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
                    
                    VkDrawIndexedIndirectCommand& draw = drawData[instanceCount];
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
        
        instanceBuffers_[currentFrame_]->flush(instanceCount * sizeof(InstanceData));
        originalDrawCommandBuffers_[currentFrame_]->flush(instanceCount * sizeof(VkDrawIndexedIndirectCommand));
        boneMatricesBuffers_[currentFrame_]->flush(currentBoneCount * sizeof(glm::mat4));
        
        // Clear countBuffer to 0 using CPU map (since it's HostVisible? Wait, countBuffers_ is MemoryUsage::GpuOnly!  
        // We must use vkCmdFillBuffer or similar in the command buffer!)
    } else {
        currentDraws_.clear();

        for (MeshNode* node : scene.meshes()) {
            if (Mesh* m = node->mesh()) {
                if (Material* mat = node->material()) {
                    const glm::mat4& world = node->worldTransform();
                    // Frustum culling against the camera (desktop). XR passes a
                    // null frustum (stereo) → render everything, no per-eye cull.
                    bool inside = true;
                    if (cullFrustum) {
                        float maxScale = std::max({
                            glm::length(glm::vec3(world[0])),
                            glm::length(glm::vec3(world[1])),
                            glm::length(glm::vec3(world[2]))
                        });
                        float radius = 0.866f * maxScale;
                        glm::vec3 center = glm::vec3(world[3]);
                        for (int i = 0; i < 6; ++i) {
                            if (glm::dot(glm::vec3(cullFrustum->planes[i]), center) +
                                    cullFrustum->planes[i].w < -radius) {
                                inside = false;
                                break;
                            }
                        }
                    }

                    if (inside) {
                        bool baked = settings.lightingMode == LightingMode::Baked
                                     && lightBaker_->has(node);
                        
                        int32_t boneOffset = -1;
                        if (Animator* anim = getAnimatorInParent(node)) {
                            if (!anim->globalPose().skinningMatrices.empty()) {
                                const auto& mats = anim->globalPose().skinningMatrices;
                                boneOffset = currentBoneCount;
                                std::memcpy(&boneData[boneOffset], mats.data(), mats.size() * sizeof(glm::mat4));
                                currentBoneCount += static_cast<uint32_t>(mats.size());
                            }
                        }

                        currentDraws_.push_back(DrawCmd{m, mat, world, node->castShadows(),
                                                        baked, lightBaker_->lightmapSet(node), boneOffset});
                    }
                }
            }
        }

        boneMatricesBuffers_[currentFrame_]->flush(currentBoneCount * sizeof(glm::mat4));

        // Sort draws by material to minimize Vulkan pipeline descriptor set binds
        std::sort(currentDraws_.begin(), currentDraws_.end());

        static int cpuLog = 0;
        if (cpuLog < 5) {
            Log::info("--- CPU Path Frame ", cpuLog, " ---");
            Log::info("Number of meshes to draw: ", currentDraws_.size());
            for(size_t i=0; i < currentDraws_.size(); ++i) {
                Log::info("  Draw ", i, " has mat baseColor=(", 
                          currentDraws_[i].material->desc().baseColor.r, ", ",
                          currentDraws_[i].material->desc().baseColor.g, ")");
            }
            cpuLog++;
        }
    }
}

void Renderer::createHdrResources() {
    VkExtent2D extent = swapchain_->extent();
    VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = extent.width;
    imageInfo.extent.height = extent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(device_.allocator(), &imageInfo, &allocInfo,
                       &hdrImage_, &hdrAllocation_, nullptr) != VK_SUCCESS)
        throw std::runtime_error("failed to create HDR image");

    hdrView_ = device_.createImageView(hdrImage_, format, VK_IMAGE_ASPECT_COLOR_BIT);

    const bool msaa = swapchain_->samples() != VK_SAMPLE_COUNT_1_BIT;
    if (msaa) {
        imageInfo.samples = swapchain_->samples();
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
        if (vmaCreateImage(device_.allocator(), &imageInfo, &allocInfo,
                           &hdrMsaaImage_, &hdrMsaaAllocation_, nullptr) != VK_SUCCESS)
            throw std::runtime_error("failed to create MSAA HDR image");
        hdrMsaaView_ = device_.createImageView(hdrMsaaImage_, format, VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

void Renderer::cleanupHdrResources() {
    if (hdrMsaaView_) {
        vkDestroyImageView(device_.device(), hdrMsaaView_, nullptr);
        vmaDestroyImage(device_.allocator(), hdrMsaaImage_, hdrMsaaAllocation_);
        hdrMsaaView_ = VK_NULL_HANDLE;
    }
    if (hdrView_) {
        vkDestroyImageView(device_.device(), hdrView_, nullptr);
        vmaDestroyImage(device_.allocator(), hdrImage_, hdrAllocation_);
        hdrView_ = VK_NULL_HANDLE;
    }
}

void Renderer::createTonemapPipeline() {
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;

    if (vkCreateDescriptorSetLayout(device_.device(), &layoutInfo, nullptr, &tonemapSetLayout_) != VK_SUCCESS)
        throw std::runtime_error("failed to create tonemap descriptor set layout");

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(device_.device(), &poolInfo, nullptr, &tonemapPool_) != VK_SUCCESS)
        throw std::runtime_error("failed to create tonemap descriptor pool");

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = tonemapPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &tonemapSetLayout_;

    if (vkAllocateDescriptorSets(device_.device(), &allocInfo, &tonemapSet_) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate tonemap descriptor set");

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(device_.device(), &samplerInfo, nullptr, &tonemapSampler_) != VK_SUCCESS)
        throw std::runtime_error("failed to create tonemap sampler");

    std::vector<VkDescriptorSetLayout> setLayouts = {tonemapSetLayout_};
    std::vector<VkFormat> colorFormats = {swapchain_->colorFormat()};
    
    tonemapPipeline_ = std::make_unique<Pipeline>(device_, shaderPath("tonemap.vert.spv"),
        shaderPath("tonemap.frag.spv"), colorFormats, VK_FORMAT_UNDEFINED, setLayouts,
        VK_SAMPLE_COUNT_1_BIT, false, false);
}

void Renderer::recordTonemapPass(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkImageMemoryBarrier2 toShaderRead = imageBarrier2(hdrImage_,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    
    VkImage swapImage = swapchain_->image(imageIndex);
    VkImageMemoryBarrier2 toColorAttach = imageBarrier2(swapImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    std::array<VkImageMemoryBarrier2, 2> barriers = {toShaderRead, toColorAttach};
    cmdImageBarriers(cmd, barriers.data(), barriers.size());

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = hdrView_;
    imageInfo.sampler = tonemapSampler_;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = tonemapSet_;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device_.device(), 1, &write, 0, nullptr);

    VkRenderingAttachmentInfo colorAttach{};
    colorAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttach.imageView = swapchain_->imageView(imageIndex);
    colorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = swapchain_->extent();
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttach;

    vkCmdBeginRendering(cmd, &renderingInfo);
    tonemapPipeline_->bind(cmd);

    VkViewport viewport{};
    viewport.width = static_cast<float>(swapchain_->extent().width);
    viewport.height = static_cast<float>(swapchain_->extent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = swapchain_->extent();
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, tonemapPipeline_->layout(),
        0, 1, &tonemapSet_, 0, nullptr);
        
    vkCmdPushConstants(cmd, tonemapPipeline_->layout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float), &exposure_);

    vkCmdDraw(cmd, 3, 1, 0, 0);
    uiRenderer_->recordCommands(cmd, swapchain_->extent().width, swapchain_->extent().height);
    imgui_->renderDrawData(cmd);  // UI on top, in the LDR swapchain pass
    vkCmdEndRendering(cmd);
}

void Renderer::createSkyboxPipeline() {
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;

    if (vkCreateDescriptorSetLayout(device_.device(), &layoutInfo, nullptr, &skyboxSetLayout_) != VK_SUCCESS)
        throw std::runtime_error("failed to create skybox descriptor set layout");

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(device_.device(), &poolInfo, nullptr, &skyboxPool_) != VK_SUCCESS)
        throw std::runtime_error("failed to create skybox descriptor pool");

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = skyboxPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &skyboxSetLayout_;

    if (vkAllocateDescriptorSets(device_.device(), &allocInfo, &skyboxSet_) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate skybox descriptor set");

    std::vector<VkDescriptorSetLayout> setLayouts = {skyboxSetLayout_};
    std::vector<VkFormat> colorFormats = {VK_FORMAT_R16G16B16A16_SFLOAT};
    
    // Disable depth write, but keep depth test (LESS_OR_EQUAL since Z=1.0)
    skyboxPipeline_ = std::make_unique<Pipeline>(device_, shaderPath("skybox.vert.spv"),
        shaderPath("skybox.frag.spv"), colorFormats, swapchain_->depthFormat(), setLayouts,
        swapchain_->samples(), false, true, sizeof(SkyboxPushConstants),
        false, VK_COMPARE_OP_LESS_OR_EQUAL, VK_CULL_MODE_NONE);
}

void Renderer::recordSkyboxPass(VkCommandBuffer cmd, Scene& scene, const Camera& camera) {
    if (scene.settings().skyboxTexture == kAssetInvalid) return;
    
    Texture* tex = resources_.getTexture(scene.settings().skyboxTexture);
    if (!tex) return;

    if (scene.settings().skyboxTexture != currentSkyboxTexture_) {
        // Update descriptor set with the texture
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = tex->imageView();
        imageInfo.sampler = tex->sampler();

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = skyboxSet_;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device_.device(), 1, &write, 0, nullptr);
        
        currentSkyboxTexture_ = scene.settings().skyboxTexture;
    }

    skyboxPipeline_->bind(cmd);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline_->layout(),
        0, 1, &skyboxSet_, 0, nullptr);

    SkyboxPushConstants pc{};
    glm::mat4 view = camera.view();
    view[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // remove translation
    pc.invViewProj = glm::inverse(camera.projection() * view);
    pc.exposure = scene.settings().skyboxExposure;
    pc.rotation = scene.settings().skyboxRotation;

    vkCmdPushConstants(cmd, skyboxPipeline_->layout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SkyboxPushConstants), &pc);

    // Draw full screen triangle
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void Renderer::updateUniformBuffer(uint32_t frame, Scene& scene, Camera& camera, Project* project) {
    camera.setPerspective(glm::radians(45.0f), swapchain_->aspectRatio(), 0.1f, 100.0f);
    
    // Store camera frustum for culling compute shader
    cameraFrustum_ = camera.getFrustum();

    UniformBufferObject ubo{};
    // Mono: both eye slots hold the same matrix (the desktop shader uses index 0).
    ubo.view[0] = ubo.view[1] = camera.view();
    ubo.proj[0] = ubo.proj[1] = camera.projection();
    uniformBuffers_[frame]->write(&ubo, sizeof(ubo));

    LightingUBO lighting{};
    gatherScene(lighting, scene, camera.position, &cameraFrustum_, project);
    lightingBuffers_[frame]->write(&lighting, sizeof(lighting));
}

void Renderer::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex, Scene& scene, const Camera& camera) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("failed to begin recording command buffer");

    uiRenderer_->updateAsyncTextures(cmd);

    // GI update (skipped when the volume is frozen in baked mode): re-voxelize the
    // scene albedo, then trace/blend/border the DDGI probes. The lighting pass
    // samples the result. When frozen, the previously-baked atlas is kept.
    if (giUpdateThisFrame_) {
        gi_->voxelize(cmd, scene);
        gi_->update(cmd, globalSets_[currentFrame_]);
    }

    bool useGpuDriven = false; // TEMPORARILY DISABLED FOR DEBUGGING

    if (useGpuDriven && currentInstanceCount_ > 0) {
        // 1. Clear CountBuffer
        vkCmdFillBuffer(cmd, countBuffers_[currentFrame_]->handle(), 0, sizeof(uint32_t), 0);

        // Barrier: Wait for FillBuffer to finish before compute shader writes
        VkBufferMemoryBarrier2 fillBarrier{};
        fillBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        fillBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        fillBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        fillBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        fillBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        fillBarrier.buffer = countBuffers_[currentFrame_]->handle();
        fillBarrier.offset = 0;
        fillBarrier.size = sizeof(uint32_t);

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.bufferMemoryBarrierCount = 1;
        depInfo.pBufferMemoryBarriers = &fillBarrier;
        vkCmdPipelineBarrier2(cmd, &depInfo);

        // 2. Dispatch Compute Shader
        cullingPipeline_->bind(cmd);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullingPipeline_->layout(),
                                0, 1, &cullingSets_[currentFrame_], 0, nullptr);

        // Push constants for compute shader
        struct CullingPushConstants {
            glm::vec4 frustumPlanes[6];
            uint32_t instanceCount;
        } pc;
        for (int i = 0; i < 6; ++i) pc.frustumPlanes[i] = cameraFrustum_.planes[i];
        pc.instanceCount = currentInstanceCount_;

        vkCmdPushConstants(cmd, cullingPipeline_->layout(), VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(pc), &pc);

        uint32_t groupCount = (currentInstanceCount_ + 63) / 64;
        vkCmdDispatch(cmd, groupCount, 1, 1);

            // Barrier: Wait for compute shader to finish before graphics reads
            std::array<VkBufferMemoryBarrier2, 2> computeBarriers{};
            computeBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            computeBarriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            computeBarriers[0].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            computeBarriers[0].dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
            computeBarriers[0].dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
            computeBarriers[0].buffer = drawCommandBuffers_[currentFrame_]->handle();
            computeBarriers[0].offset = 0;
            computeBarriers[0].size = VK_WHOLE_SIZE;

            computeBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            computeBarriers[1].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            computeBarriers[1].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            computeBarriers[1].dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
            computeBarriers[1].dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
            computeBarriers[1].buffer = countBuffers_[currentFrame_]->handle();
            computeBarriers[1].offset = 0;
            computeBarriers[1].size = sizeof(uint32_t);

            VkDependencyInfo computeDepInfo{};
        computeDepInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        computeDepInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(computeBarriers.size());
        computeDepInfo.pBufferMemoryBarriers = computeBarriers.data();
        vkCmdPipelineBarrier2(cmd, &computeDepInfo);
    }

    // Shadow passes: render scene depth from each caster's POV before the main
    // pass. Iterate the full mesh list (not the camera-culled draws) so casters
    // outside the camera frustum still cast into view.
    shadowMap_->record(cmd, shadowCount_,
        [this, &scene](VkCommandBuffer c, VkPipelineLayout layout, int layer) {
            for (MeshNode* node : scene.meshes()) {
                Mesh* mesh = node->mesh();
                if (!mesh || !node->castShadows()) continue;
                glm::mat4 mvp = shadowMatrices_[layer] * node->worldTransform();
                vkCmdPushConstants(c, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &mvp);
                mesh->bind(c);
                mesh->draw(c);
            }
        });

    // Lightmap bake (one-shot, requested via the editor): render each included
    // mesh into its lightmap using the freshly-rendered shadow maps + lights.
    if (doBake_)
        lightBaker_->record(cmd, globalSets_[currentFrame_], scene);

    auto& settings = scene.settings();
    glm::vec4 clearColor = settings.clearColor;
    VkExtent2D extent = swapchain_->extent();

    const bool msaa = swapchain_->samples() != VK_SAMPLE_COUNT_1_BIT;
    // Color is rendered into the MSAA target and resolved to the HDR image; with
    // no MSAA we render straight to the HDR image.
    VkImage colorImage = msaa ? hdrMsaaImage_ : hdrImage_;
    VkImageView colorView = msaa ? hdrMsaaView_ : hdrView_;

    // Dynamic rendering does no implicit layout transitions: do them by hand.
    std::array<VkImageMemoryBarrier2, 3> preBarriers{};
    uint32_t preCount = 0;
    preBarriers[preCount++] = imageBarrier2(colorImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    if (msaa)
        preBarriers[preCount++] = imageBarrier2(hdrImage_,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    preBarriers[preCount++] = imageBarrier2(swapchain_->depthImage(),
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, 0,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    cmdImageBarriers(cmd, preBarriers.data(), preCount);

    VkRenderingAttachmentInfo colorAttach{};
    colorAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttach.imageView = colorView;
    colorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttach.storeOp = msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    colorAttach.clearValue.color = {{clearColor.r, clearColor.g, clearColor.b, clearColor.a}};
    if (msaa) {
        colorAttach.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        colorAttach.resolveImageView = hdrView_;
        colorAttach.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkRenderingAttachmentInfo depthAttach{};
    depthAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttach.imageView = swapchain_->depthView();
    depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttach.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttach;
    renderingInfo.pDepthAttachment = &depthAttach;

    vkCmdBeginRendering(cmd, &renderingInfo);
    pipeline_->bind(cmd);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    VkPipelineLayout layout = pipeline_->layout();

    // Set 0: per-frame global data (camera + lighting), shared by every object.
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
        0, 1, &globalSets_[currentFrame_], 0, nullptr);

    if (useGpuDriven) {
        // Set 1: Bindless Textures + MaterialData SSBO
        VkDescriptorSet globalMaterialSet = resources_.globalMaterialSet();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
            1, 1, &globalMaterialSet, 0, nullptr);
            
        // Set 2: Lightmap (we just bind default for now since bindless lightmaps aren't implemented yet)
        VkDescriptorSet defaultLightmapSet = lightBaker_->lightmapSet(nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
            2, 1, &defaultLightmapSet, 0, nullptr);
            
        // Push constants for the graphics pipeline (overwrites the compute ones in the push constant memory)
        PushConstants gfxPc{};
        gfxPc.model = glm::mat4(1.0f); // Unused in bindless
        gfxPc.params = glm::vec4(scene.settings().lightingMode == LightingMode::Baked ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
        vkCmdPushConstants(cmd, pipeline_->layout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(PushConstants), &gfxPc);

        // Set 3: InstanceBuffer (from cullingSets_)
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
            3, 1, &cullingSets_[currentFrame_], 0, nullptr);
            
        // Bind the global vertex and index buffers
        auto& geometry = resources_.geometry();
        VkBuffer vertexBuffers[] = {geometry.vertexBuffer()->handle()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, geometry.indexBuffer()->handle(), 0, VK_INDEX_TYPE_UINT32);
        
        // Execute draw indirect count
        if (currentInstanceCount_ > 0) {
            if (device_.capabilities().drawIndirectCount) {
                // vkCmdDrawIndexedIndirectCount requires Vulkan 1.2 core or the extension
                auto func = (PFN_vkCmdDrawIndexedIndirectCount)vkGetDeviceProcAddr(device_.device(), "vkCmdDrawIndexedIndirectCount");
                if (!func) func = (PFN_vkCmdDrawIndexedIndirectCount)vkGetDeviceProcAddr(device_.device(), "vkCmdDrawIndexedIndirectCountKHR");
                
                if (func) {
                    func(cmd,
                         drawCommandBuffers_[currentFrame_]->handle(), 0,
                         countBuffers_[currentFrame_]->handle(), 0,
                         currentInstanceCount_, sizeof(VkDrawIndexedIndirectCommand));
                } else {
                    vkCmdDrawIndexedIndirect(cmd, drawCommandBuffers_[currentFrame_]->handle(), 0, currentInstanceCount_, sizeof(VkDrawIndexedIndirectCommand));
                }
            } else {
                vkCmdDrawIndexedIndirect(cmd, drawCommandBuffers_[currentFrame_]->handle(), 0, currentInstanceCount_, sizeof(VkDrawIndexedIndirectCommand));
            }
        }
    } else {
        // Render all visible meshes from the collected RenderView
        Material* lastMaterial = nullptr;
        
        static int cpuDrawLog = 0;
        if (cpuDrawLog < 5) {
            Log::info("Executing ", currentDraws_.size(), " draw commands.");
        }
        
        for (const auto& cmdDraw : currentDraws_) {
            if (cmdDraw.material != lastMaterial) {
                VkDescriptorSet matSet = cmdDraw.material->descriptorSet();
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->layout(),
                                        1, 1, &matSet, 0, nullptr);
                lastMaterial = cmdDraw.material;
            }

            // Set 2: this node's baked lightmap (or the default when not baked).
            VkDescriptorSet lmSet = cmdDraw.lightmapSet;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->layout(),
                                    2, 1, &lmSet, 0, nullptr);

            PushConstants pc{};
            pc.model = cmdDraw.world;
            // params.x > 0.5: this is a baked static mesh → use its frozen lightmap
            // diffuse + live specular. Otherwise: full live lighting + DDGI indirect.
            // params.y = boneOffset.
            pc.params = glm::vec4(cmdDraw.useLightmap ? 1.0f : 0.0f,
                                  static_cast<float>(cmdDraw.boneOffset), 0.0f, 0.0f);
            vkCmdPushConstants(cmd, pipeline_->layout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(PushConstants), &pc);

            cmdDraw.mesh->bind(cmd);
            cmdDraw.mesh->draw(cmd);
        }
        if (cpuDrawLog < 5) cpuDrawLog++;
    }

    recordSkyboxPass(cmd, scene, camera);

    vkCmdEndRendering(cmd);

    recordTonemapPass(cmd, imageIndex);

    VkImage swapImage = swapchain_->image(imageIndex);
    // Transition the swap-chain image from color attachment to present.
    VkImageMemoryBarrier2 toPresent = imageBarrier2(swapImage,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
    cmdImageBarrier(cmd, toPresent);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        throw std::runtime_error("failed to record command buffer");
}

void Renderer::drawFrame(Scene& scene, Camera& camera, Project* project) {
    vkWaitForFences(device_.device(), 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);

    if (project) {
        if (shadowMap_->resize(project->shadowResolution())) {
            updateGlobalShadowDescriptor();
        }
    }

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device_.device(), swapchain_->handle(), UINT64_MAX,
        imageAvailableSemaphores_[currentFrame_], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain_->recreate();
        cleanupHdrResources();
        createHdrResources();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image");
    }

    vkResetFences(device_.device(), 1, &inFlightFences_[currentFrame_]);

    auto& settings = scene.settings();

    // Decide whether the DDGI volume updates this frame. Realtime: every frame.
    // Baked: a bake request runs the update for kGIBakeFrames frames to converge
    // the volume, then freezes it (direct lighting + shadows stay live in both
    // modes; only the indirect volume is frozen).
    if (settings.lightingMode == LightingMode::Realtime) {
        settings.bakeRequested = false;  // bake only applies in baked mode
        giUpdateThisFrame_ = settings.giEnabled;
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

    // Swap the DDGI ping-pong (only when updating, so the frozen atlas is kept)
    // and re-point this frame's set 0 at the current atlas (safe: fence waited).
    if (giUpdateThisFrame_) gi_->beginFrame();
    updateGIDescriptors();

    updateUniformBuffer(currentFrame_, scene, camera, project);
    uiRenderer_->gatherUI(scene);

    vkResetCommandBuffer(commandBuffers_[currentFrame_], 0);
    recordCommandBuffer(commandBuffers_[currentFrame_], imageIndex, scene, camera);
    doBake_ = false;  // one-shot lightmap bake consumed this frame

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores_[currentFrame_]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSemaphores[] = {swapchain_->renderFinishedSemaphore(imageIndex)};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers_[currentFrame_];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(device_.graphicsQueue(), 1, &submitInfo, inFlightFences_[currentFrame_]) != VK_SUCCESS)
        throw std::runtime_error("failed to submit draw command buffer");

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapChains[] = {swapchain_->handle()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(device_.presentQueue(), &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || window_.wasResized()) {
        window_.resetResizedFlag();
        swapchain_->recreate();
        cleanupHdrResources();
        createHdrResources();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image");
    }

    currentFrame_ = (currentFrame_ + 1) % kMaxFramesInFlight;
}

// ────────────────────────────────────────────────────────────── XR (multiview)

namespace {
// All eyes render in one pass into a 2-layer image; the view mask has one bit
// per eye (0b11 for stereo). gl_ViewIndex selects the per-eye matrices.
uint32_t xrViewMask(uint32_t viewCount) { return (1u << viewCount) - 1u; }
}

void Renderer::createXrTargets() {
    const VkFormat hdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    auto makeView = [&](VkImage img, VkFormat fmt, VkImageAspectFlags aspect,
                        VkImageViewType type, uint32_t baseLayer, uint32_t layers) {
        VkImageViewCreateInfo v{};
        v.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        v.image = img;
        v.viewType = type;
        v.format = fmt;
        v.subresourceRange = {aspect, 0, 1, baseLayer, layers};
        VkImageView view;
        if (vkCreateImageView(device_.device(), &v, nullptr, &view) != VK_SUCCESS)
            throw std::runtime_error("XR: failed to create image view");
        return view;
    };

    VkImageCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.extent = {xrExtent_.width, xrExtent_.height, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = xrViewCount_;            // one layer per eye
    ci.format = hdrFormat;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;       // MSAA is a follow-up for XR
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;

    // 2-layer HDR color: one array view to render into (multiview), plus a
    // single-layer view per eye that the tonemap pass samples.
    if (vmaCreateImage(device_.allocator(), &ci, &ai, &xrHdrImage_, &xrHdrAllocation_, nullptr) != VK_SUCCESS)
        throw std::runtime_error("XR: failed to create HDR image");
    xrHdrArrayView_ = makeView(xrHdrImage_, hdrFormat, VK_IMAGE_ASPECT_COLOR_BIT,
                               VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, xrViewCount_);
    for (uint32_t i = 0; i < xrViewCount_; ++i)
        xrHdrLayerViews_[i] = makeView(xrHdrImage_, hdrFormat, VK_IMAGE_ASPECT_COLOR_BIT,
                                       VK_IMAGE_VIEW_TYPE_2D, i, 1);

    // 2-layer depth.
    const VkFormat depthFormat = device_.findDepthFormat();
    ci.format = depthFormat;
    ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (vmaCreateImage(device_.allocator(), &ci, &ai, &xrDepthImage_, &xrDepthAllocation_, nullptr) != VK_SUCCESS)
        throw std::runtime_error("XR: failed to create depth image");
    xrDepthArrayView_ = makeView(xrDepthImage_, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT,
                                 VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, xrViewCount_);
}

void Renderer::cleanupXrTargets() {
    VkDevice d = device_.device();
    if (xrDepthArrayView_) vkDestroyImageView(d, xrDepthArrayView_, nullptr);
    if (xrDepthImage_) vmaDestroyImage(device_.allocator(), xrDepthImage_, xrDepthAllocation_);
    for (auto& v : xrHdrLayerViews_) { if (v) vkDestroyImageView(d, v, nullptr); v = VK_NULL_HANDLE; }
    if (xrHdrArrayView_) vkDestroyImageView(d, xrHdrArrayView_, nullptr);
    if (xrHdrImage_) vmaDestroyImage(device_.allocator(), xrHdrImage_, xrHdrAllocation_);
    xrDepthArrayView_ = VK_NULL_HANDLE; xrDepthImage_ = VK_NULL_HANDLE;
    xrHdrArrayView_ = VK_NULL_HANDLE; xrHdrImage_ = VK_NULL_HANDLE;
}

void Renderer::createXrPipelines() {
    const VkFormat hdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    const VkFormat depthFormat = device_.findDepthFormat();
    const uint32_t viewMask = xrViewMask(xrViewCount_);
    const std::vector<VkFormat> hdrColor = {hdrFormat};

    // ── Scene (multiview): per-eye camera matrices via gl_ViewIndex ──
    std::vector<VkDescriptorSetLayout> sceneLayouts = {
        globalSetLayout_, resources_.materialSetLayout(), lightBaker_->setLayout()};
    xrScenePipeline_ = std::make_unique<Pipeline>(device_,
        shaderPath("multiview.shader.vert.spv"), shaderPath("shader.frag.spv"),
        hdrColor, depthFormat, sceneLayouts, VK_SAMPLE_COUNT_1_BIT,
        true, true, 0, true, VK_COMPARE_OP_LESS, VK_CULL_MODE_BACK_BIT, false,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, viewMask);

    // ── Skybox descriptor set (texture) + multiview pipeline ──
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding = 0;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo lci{};
        lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        lci.bindingCount = 1;
        lci.pBindings = &b;
        if (vkCreateDescriptorSetLayout(device_.device(), &lci, nullptr, &skyboxSetLayout_) != VK_SUCCESS)
            throw std::runtime_error("XR: failed to create skybox set layout");

        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        VkDescriptorPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pci.poolSizeCount = 1;
        pci.pPoolSizes = &ps;
        pci.maxSets = 1;
        if (vkCreateDescriptorPool(device_.device(), &pci, nullptr, &skyboxPool_) != VK_SUCCESS)
            throw std::runtime_error("XR: failed to create skybox pool");

        VkDescriptorSetAllocateInfo asi{};
        asi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        asi.descriptorPool = skyboxPool_;
        asi.descriptorSetCount = 1;
        asi.pSetLayouts = &skyboxSetLayout_;
        if (vkAllocateDescriptorSets(device_.device(), &asi, &skyboxSet_) != VK_SUCCESS)
            throw std::runtime_error("XR: failed to allocate skybox set");

        std::vector<VkDescriptorSetLayout> skyLayouts = {skyboxSetLayout_};
        xrSkyboxPipeline_ = std::make_unique<Pipeline>(device_,
            shaderPath("skybox.vert.spv"), shaderPath("multiview.skybox.frag.spv"),
            hdrColor, depthFormat, skyLayouts, VK_SAMPLE_COUNT_1_BIT,
            false, true, sizeof(XrSkyboxPush), false, VK_COMPARE_OP_LESS_OR_EQUAL,
            VK_CULL_MODE_NONE, false, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, viewMask);
    }

    // ── Tonemap set layout + sampler + per-eye sets + (mono) pipeline ──
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding = 0;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo lci{};
        lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        lci.bindingCount = 1;
        lci.pBindings = &b;
        if (vkCreateDescriptorSetLayout(device_.device(), &lci, nullptr, &tonemapSetLayout_) != VK_SUCCESS)
            throw std::runtime_error("XR: failed to create tonemap set layout");

        VkSamplerCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sci.magFilter = VK_FILTER_LINEAR;
        sci.minFilter = VK_FILTER_LINEAR;
        sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        if (vkCreateSampler(device_.device(), &sci, nullptr, &tonemapSampler_) != VK_SUCCESS)
            throw std::runtime_error("XR: failed to create tonemap sampler");

        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, xrViewCount_};
        VkDescriptorPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pci.poolSizeCount = 1;
        pci.pPoolSizes = &ps;
        pci.maxSets = xrViewCount_;
        if (vkCreateDescriptorPool(device_.device(), &pci, nullptr, &xrTonemapPool_) != VK_SUCCESS)
            throw std::runtime_error("XR: failed to create tonemap pool");

        // One descriptor set per eye, each pinned to that eye's HDR layer view.
        for (uint32_t i = 0; i < xrViewCount_; ++i) {
            VkDescriptorSetAllocateInfo asi{};
            asi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            asi.descriptorPool = xrTonemapPool_;
            asi.descriptorSetCount = 1;
            asi.pSetLayouts = &tonemapSetLayout_;
            if (vkAllocateDescriptorSets(device_.device(), &asi, &xrTonemapSets_[i]) != VK_SUCCESS)
                throw std::runtime_error("XR: failed to allocate tonemap set");

            VkDescriptorImageInfo ii{};
            ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            ii.imageView = xrHdrLayerViews_[i];
            ii.sampler = tonemapSampler_;
            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = xrTonemapSets_[i];
            w.dstBinding = 0;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.descriptorCount = 1;
            w.pImageInfo = &ii;
            vkUpdateDescriptorSets(device_.device(), 1, &w, 0, nullptr);
        }

        std::vector<VkDescriptorSetLayout> setLayouts = {tonemapSetLayout_};
        std::vector<VkFormat> colorFormats = {xrColorFormat_};
        xrTonemapPipeline_ = std::make_unique<Pipeline>(device_,
            shaderPath("tonemap.vert.spv"), shaderPath("tonemap.frag.spv"),
            colorFormats, VK_FORMAT_UNDEFINED, setLayouts, VK_SAMPLE_COUNT_1_BIT,
            false, false);
    }
}

void Renderer::updateUniformBufferXr(uint32_t frame, const std::vector<xr::EyeView>& eyes,
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
    gatherScene(lighting, scene, head, nullptr, project);
    lightingBuffers_[frame]->write(&lighting, sizeof(lighting));
}

void Renderer::recordXrScenePass(VkCommandBuffer cmd, Scene& scene,
                                 const std::vector<xr::EyeView>& eyes) {
    auto& settings = scene.settings();
    // Passthrough (AR): clear fully transparent so the compositor blends the real
    // world through the background; opaque geometry (alpha 1) stays visible.
    const bool passthrough = XRPassthrough::enabled();
    glm::vec4 clearColor = settings.clearColor;
    if (passthrough) clearColor.a = 0.0f;

    std::array<VkImageMemoryBarrier2, 2> pre{};
    pre[0] = imageBarrier2(xrHdrImage_,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, xrViewCount_);
    pre[1] = imageBarrier2(xrDepthImage_,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, 0,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, 0, xrViewCount_);
    cmdImageBarriers(cmd, pre.data(), pre.size());

    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = xrHdrArrayView_;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{clearColor.r, clearColor.g, clearColor.b, clearColor.a}};

    VkRenderingAttachmentInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView = xrDepthArrayView_;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo ri{};
    ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea.offset = {0, 0};
    ri.renderArea.extent = xrExtent_;
    ri.layerCount = 1;                       // ignored when viewMask != 0
    ri.viewMask = xrViewMask(xrViewCount_);  // multiview: both eyes in one pass
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color;
    ri.pDepthAttachment = &depth;

    vkCmdBeginRendering(cmd, &ri);

    VkViewport vp{};
    vp.width = static_cast<float>(xrExtent_.width);
    vp.height = static_cast<float>(xrExtent_.height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{};
    sc.extent = xrExtent_;
    vkCmdSetScissor(cmd, 0, 1, &sc);

    // Opaque scene (same draw list / material binding model as the desktop path).
    xrScenePipeline_->bind(cmd);
    VkPipelineLayout layout = xrScenePipeline_->layout();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
        0, 1, &globalSets_[currentFrame_], 0, nullptr);

    Material* lastMaterial = nullptr;
    for (const auto& d : currentDraws_) {
        if (d.material != lastMaterial) {
            VkDescriptorSet matSet = d.material->descriptorSet();
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1, &matSet, 0, nullptr);
            lastMaterial = d.material;
        }
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 2, 1, &d.lightmapSet, 0, nullptr);
        PushConstants pc{};
        pc.model = d.world;
        pc.params = glm::vec4(d.useLightmap ? 1.0f : 0.0f, static_cast<float>(d.boneOffset), 0.0f, 0.0f);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(PushConstants), &pc);
        d.mesh->bind(cmd);
        d.mesh->draw(cmd);
    }

    // Skybox (multiview): per-eye inverse view-proj picked by gl_ViewIndex.
    // Skipped in passthrough — an opaque sky would hide the real world.
    if (!passthrough && settings.skyboxTexture != kAssetInvalid) {
        if (Texture* tex = resources_.getTexture(settings.skyboxTexture)) {
            if (settings.skyboxTexture != currentSkyboxTexture_) {
                VkDescriptorImageInfo ii{};
                ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                ii.imageView = tex->imageView();
                ii.sampler = tex->sampler();
                VkWriteDescriptorSet w{};
                w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w.dstSet = skyboxSet_;
                w.dstBinding = 0;
                w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w.descriptorCount = 1;
                w.pImageInfo = &ii;
                vkUpdateDescriptorSets(device_.device(), 1, &w, 0, nullptr);
                currentSkyboxTexture_ = settings.skyboxTexture;
            }
            xrSkyboxPipeline_->bind(cmd);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                xrSkyboxPipeline_->layout(), 0, 1, &skyboxSet_, 0, nullptr);
            XrSkyboxPush push{};
            const uint32_t n = std::min<uint32_t>(static_cast<uint32_t>(eyes.size()), 2);
            for (uint32_t i = 0; i < n; ++i) {
                glm::mat4 view = eyes[i].view;
                view[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);  // strip translation
                push.invViewProj[i] = glm::inverse(eyes[i].projection * view);
            }
            if (n == 1) push.invViewProj[1] = push.invViewProj[0];
            push.exposure = settings.skyboxExposure;
            push.rotation = settings.skyboxRotation;
            vkCmdPushConstants(cmd, xrSkyboxPipeline_->layout(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(XrSkyboxPush), &push);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        }
    }

    vkCmdEndRendering(cmd);
}

void Renderer::recordXrTonemap(VkCommandBuffer cmd, const std::vector<xr::EyeView>& eyes) {
    // HDR (all layers) → shader read for sampling.
    VkImageMemoryBarrier2 hdrToRead = imageBarrier2(xrHdrImage_,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, xrViewCount_);
    cmdImageBarrier(cmd, hdrToRead);

    const uint32_t n = std::min<uint32_t>(static_cast<uint32_t>(eyes.size()), xrViewCount_);
    for (uint32_t i = 0; i < n; ++i) {
        const xr::EyeView& eye = eyes[i];
        // The XR image starts UNDEFINED; the compositor wants it left in
        // COLOR_ATTACHMENT_OPTIMAL, which is exactly where rendering leaves it.
        VkImageMemoryBarrier2 toColor = imageBarrier2(eye.image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        cmdImageBarrier(cmd, toColor);

        VkRenderingAttachmentInfo color{};
        color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color.imageView = eye.imageView;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea.offset = {0, 0};
        ri.renderArea.extent = eye.extent;
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &color;

        vkCmdBeginRendering(cmd, &ri);
        xrTonemapPipeline_->bind(cmd);
        VkViewport vp{};
        vp.width = static_cast<float>(eye.extent.width);
        vp.height = static_cast<float>(eye.extent.height);
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{};
        sc.extent = eye.extent;
        vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            xrTonemapPipeline_->layout(), 0, 1, &xrTonemapSets_[i], 0, nullptr);
        vkCmdPushConstants(cmd, xrTonemapPipeline_->layout(), VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(float), &exposure_);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRendering(cmd);
    }
}

void Renderer::drawXr(VkCommandBuffer cmd, const std::vector<xr::EyeView>& eyes,
                      Scene& scene, Project* project) {
    if (eyes.empty()) return;
    auto& settings = scene.settings();

    // GI update cadence (mirrors drawFrame; no editor bake UI in XR yet).
    if (settings.lightingMode == LightingMode::Realtime) {
        giUpdateThisFrame_ = settings.giEnabled;
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

    if (giUpdateThisFrame_) gi_->beginFrame();
    updateGIDescriptors();

    // CPU prep (per-eye matrices, lighting, draw list, bones).
    updateUniformBufferXr(currentFrame_, eyes, scene, project);

    // View-independent passes recorded once, then the stereo scene + tonemap.
    if (giUpdateThisFrame_) {
        gi_->voxelize(cmd, scene);
        gi_->update(cmd, globalSets_[currentFrame_]);
    }
    shadowMap_->record(cmd, shadowCount_,
        [this, &scene](VkCommandBuffer c, VkPipelineLayout layout, int layer) {
            for (MeshNode* node : scene.meshes()) {
                Mesh* mesh = node->mesh();
                if (!mesh || !node->castShadows()) continue;
                glm::mat4 mvp = shadowMatrices_[layer] * node->worldTransform();
                vkCmdPushConstants(c, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &mvp);
                mesh->bind(c);
                mesh->draw(c);
            }
        });

    recordXrScenePass(cmd, scene, eyes);
    recordXrTonemap(cmd, eyes);

    currentFrame_ = (currentFrame_ + 1) % kMaxFramesInFlight;
}

} // namespace ne
