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
#include "scene/LightNode.hpp"
#include "scene/Node.hpp"
#include "scene/Scene.hpp"
#include "core/Log.hpp"

#include "scene/LightNode.hpp"
#include "scene/MeshNode.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
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
    : device_(device), swapchain_(swapchain), window_(window), resources_(resources), imgui_(imgui) {
    createGlobalSetLayout();
    lightBaker_ = std::make_unique<LightBaker>(device_, globalSetLayout_);
    createHdrResources();
    createPipeline(resources_.materialSetLayout());
    createTonemapPipeline();
    createUniformBuffers();
    shadowMap_ = std::make_unique<ShadowMap>(device_);
    createGlobalDescriptorPool();
    createGlobalDescriptorSets();
    createCommandBuffers();
    createSyncObjects();
    
    if (device_.capabilities().descriptorIndexing && device_.capabilities().multiDrawIndirect) {
        createGpuDrivenBuffers();
        createCullingPipeline();
    }
}

Renderer::~Renderer() {
    vkDeviceWaitIdle(device_.device());
    for (int i = 0; i < kMaxFramesInFlight; i++) {
        vkDestroySemaphore(device_.device(), imageAvailableSemaphores_[i], nullptr);
        vkDestroyFence(device_.device(), inFlightFences_[i], nullptr);
    }
    vkDestroyDescriptorPool(device_.device(), globalPool_, nullptr);
    vkDestroyDescriptorSetLayout(device_.device(), globalSetLayout_, nullptr);
    cleanupHdrResources();
    if (tonemapSampler_) vkDestroySampler(device_.device(), tonemapSampler_, nullptr);
    if (tonemapPool_) vkDestroyDescriptorPool(device_.device(), tonemapPool_, nullptr);
    if (tonemapSetLayout_) vkDestroyDescriptorSetLayout(device_.device(), tonemapSetLayout_, nullptr);
    
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
    lightingBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 2: shadow map array (sampled in the fragment shader).
    VkDescriptorSetLayoutBinding shadowBinding{};
    shadowBinding.binding = 2;
    shadowBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowBinding.descriptorCount = 1;
    shadowBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 3> bindings = {cameraBinding, lightingBinding, shadowBinding};

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
        shaderPath(fragShader), colorFormats, swapchain_.depthFormat(), setLayouts,
        swapchain_.samples());
}

void Renderer::createUniformBuffers() {
    uniformBuffers_.reserve(kMaxFramesInFlight);
    lightingBuffers_.reserve(kMaxFramesInFlight);
    for (int i = 0; i < kMaxFramesInFlight; i++) {
        uniformBuffers_.push_back(std::make_unique<Buffer>(device_, sizeof(UniformBufferObject),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, MemoryUsage::HostVisible));
        lightingBuffers_.push_back(std::make_unique<Buffer>(device_, sizeof(LightingUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, MemoryUsage::HostVisible));
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
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(kMaxFramesInFlight) * 2;  // camera + lighting
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(kMaxFramesInFlight);  // shadow map

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

        std::array<VkWriteDescriptorSet, 3> writes{};
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

void Renderer::gatherScene(LightingUBO& ubo, Scene& scene, const Camera& camera, Project* project) {
    auto& settings = scene.settings();
    ubo.ambient = settings.ambientLight;
    ubo.cameraPos = glm::vec4(camera.position, 1.0f);
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
        
        InstanceData& inst = instanceData[instanceCount];
        inst.model = world;
        inst.boundingSphere = glm::vec4(0.0f, 0.0f, 0.0f, radius);
                    inst.materialIndex = mat->bindlessIndex();
                    
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
                                  ", matIdx=", inst.materialIndex);
                    }
                    if (instanceCount == 0) { frameCounter++; }
                    
                    instanceCount++;
                }
            }
        }
        
        currentInstanceCount_ = instanceCount;
        
        instanceBuffers_[currentFrame_]->flush(instanceCount * sizeof(InstanceData));
        originalDrawCommandBuffers_[currentFrame_]->flush(instanceCount * sizeof(VkDrawIndexedIndirectCommand));
        
        // Clear countBuffer to 0 using CPU map (since it's HostVisible? Wait, countBuffers_ is MemoryUsage::GpuOnly!  
        // We must use vkCmdFillBuffer or similar in the command buffer!)
    } else {
        currentDraws_.clear();
        Frustum frustum = camera.getFrustum();

        for (MeshNode* node : scene.meshes()) {
            if (Mesh* m = node->mesh()) {
                if (Material* mat = node->material()) {
                    const glm::mat4& world = node->worldTransform();
                    // Frustum Culling
                    float maxScale = std::max({
                        glm::length(glm::vec3(world[0])),
                        glm::length(glm::vec3(world[1])),
                        glm::length(glm::vec3(world[2]))
                    });
                    float radius = 0.866f * maxScale;
                    glm::vec3 center = glm::vec3(world[3]);

                    bool inside = true;
                    for (int i = 0; i < 6; ++i) {
                        if (glm::dot(glm::vec3(frustum.planes[i]), center) + frustum.planes[i].w < -radius) {
                            inside = false;
                            break;
                        }
                    }
                    
                    if (inside) {
                        bool baked = settings.lightingMode == LightingMode::Baked
                                     && lightBaker_->has(node);
                        currentDraws_.push_back(DrawCmd{m, mat, world, node->castShadows(),
                                                        baked, lightBaker_->lightmapSet(node)});
                    }
                }
            }
        }

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
    VkExtent2D extent = swapchain_.extent();
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

    const bool msaa = swapchain_.samples() != VK_SAMPLE_COUNT_1_BIT;
    if (msaa) {
        imageInfo.samples = swapchain_.samples();
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
    std::vector<VkFormat> colorFormats = {swapchain_.colorFormat()};
    
    tonemapPipeline_ = std::make_unique<Pipeline>(device_, shaderPath("tonemap.vert.spv"),
        shaderPath("tonemap.frag.spv"), colorFormats, VK_FORMAT_UNDEFINED, setLayouts,
        VK_SAMPLE_COUNT_1_BIT, false, false);
}

void Renderer::recordTonemapPass(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkImageMemoryBarrier2 toShaderRead = imageBarrier2(hdrImage_,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    
    VkImage swapImage = swapchain_.image(imageIndex);
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
    colorAttach.imageView = swapchain_.imageView(imageIndex);
    colorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = swapchain_.extent();
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttach;

    vkCmdBeginRendering(cmd, &renderingInfo);
    tonemapPipeline_->bind(cmd);

    VkViewport viewport{};
    viewport.width = static_cast<float>(swapchain_.extent().width);
    viewport.height = static_cast<float>(swapchain_.extent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = swapchain_.extent();
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, tonemapPipeline_->layout(),
        0, 1, &tonemapSet_, 0, nullptr);
        
    vkCmdPushConstants(cmd, tonemapPipeline_->layout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float), &exposure_);

    vkCmdDraw(cmd, 3, 1, 0, 0);
    imgui_.renderDrawData(cmd);  // UI on top, in the LDR swapchain pass
    vkCmdEndRendering(cmd);
}

void Renderer::updateUniformBuffer(uint32_t frame, Scene& scene, Camera& camera, Project* project) {
    camera.setPerspective(glm::radians(45.0f), swapchain_.aspectRatio(), 0.1f, 100.0f);
    
    // Store camera frustum for culling compute shader
    cameraFrustum_ = camera.getFrustum();

    UniformBufferObject ubo{};
    ubo.view = camera.view();
    ubo.proj = camera.projection();
    uniformBuffers_[frame]->write(&ubo, sizeof(ubo));

    LightingUBO lighting{};
    gatherScene(lighting, scene, camera, project);
    lightingBuffers_[frame]->write(&lighting, sizeof(lighting));
}

void Renderer::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex, Scene& scene) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("failed to begin recording command buffer");

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
    VkExtent2D extent = swapchain_.extent();

    const bool msaa = swapchain_.samples() != VK_SAMPLE_COUNT_1_BIT;
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
    preBarriers[preCount++] = imageBarrier2(swapchain_.depthImage(),
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
    depthAttach.imageView = swapchain_.depthView();
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

            // Set 2: Lightmap. Use node's baked lightmap if valid, otherwise fallback.
            VkDescriptorSet lmSet = lightBaker_->lightmapSet(nullptr);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->layout(),
                                    2, 1, &lmSet, 0, nullptr);

            PushConstants pc{};
            pc.model = cmdDraw.world;
            // param.x > 0.5 tells shader to use baked lightmap. Since it's a stub, use 0.0.
            pc.params = glm::vec4(scene.settings().lightingMode == LightingMode::Baked ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
            vkCmdPushConstants(cmd, pipeline_->layout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(PushConstants), &pc);

            cmdDraw.mesh->bind(cmd);
            cmdDraw.mesh->draw(cmd);
        }
        if (cpuDrawLog < 5) cpuDrawLog++;
    }

    vkCmdEndRendering(cmd);

    recordTonemapPass(cmd, imageIndex);

    VkImage swapImage = swapchain_.image(imageIndex);
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
    VkResult result = vkAcquireNextImageKHR(device_.device(), swapchain_.handle(), UINT64_MAX,
        imageAvailableSemaphores_[currentFrame_], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain_.recreate();
        cleanupHdrResources();
        createHdrResources();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image");
    }

    vkResetFences(device_.device(), 1, &inFlightFences_[currentFrame_]);

    // Honour a one-shot bake request: allocate lightmaps before gathering the
    // scene so this frame's draws already sample the (about-to-be-baked) maps.
    auto& settings = scene.settings();
    if (settings.bakeRequested) {
        lightBaker_->prepare(scene);
        doBake_ = true;
    }

    updateUniformBuffer(currentFrame_, scene, camera, project);

    vkResetCommandBuffer(commandBuffers_[currentFrame_], 0);
    recordCommandBuffer(commandBuffers_[currentFrame_], imageIndex, scene);

    if (doBake_) {
        settings.bakeRequested = false;
        settings.baked = true;
        doBake_ = false;
    }

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores_[currentFrame_]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSemaphores[] = {swapchain_.renderFinishedSemaphore(imageIndex)};

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
    VkSwapchainKHR swapChains[] = {swapchain_.handle()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(device_.presentQueue(), &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || window_.wasResized()) {
        window_.resetResizedFlag();
        swapchain_.recreate();
        cleanupHdrResources();
        createHdrResources();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image");
    }

    currentFrame_ = (currentFrame_ + 1) % kMaxFramesInFlight;
}

} // namespace ne
