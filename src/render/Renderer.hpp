#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

#include <array>
#include <memory>
#include <vector>

#include "core/Camera.hpp"

namespace ne {

class VulkanDevice;
class Swapchain;
class Window;
class Pipeline;
class ComputePipeline;
class Buffer;
class ImGuiLayer;
class Scene;
class Camera;
class Mesh;
class Project;
class Swapchain;
class Material;
class ResourceManager;
class ShadowMap;
class LightBaker;

struct UniformBufferObject {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

// Per-object scene push constant (vertex + fragment). Mirrors PushConstants in
// shader.vert / shader.frag.
struct PushConstants {
    glm::mat4 model;
    glm::vec4 params;  // x = useLightmap (sample the baked lightmap)
};

// Limits kept simple and easy to raise; mirrored by the shader constants.
constexpr int kMaxLights = 16;
constexpr int kMaxShadowCasters = 4;

// std140-compatible lighting block (mirrors the LightingUBO in shader.frag).
// One unified light type covers directional / point / spot. Everything is
// vec4/ivec4 to avoid std140 vec3 padding surprises.
struct GpuLight {
    glm::vec4 posRange{0.0f};    // xyz world pos (point/spot), w = range
    glm::vec4 colorInt{0.0f};    // rgb color, w = intensity
    glm::vec4 dirType{0.0f};     // xyz direction (dir/spot), w = type (0 dir,1 point,2 spot)
    glm::vec4 spotShadow{0.0f, 0.0f, -1.0f, 0.0f};  // x cosInner, y cosOuter, z shadowIndex, w unused
};

struct LightingUBO {
    glm::vec4 ambient{0.0f};
    glm::vec4 cameraPos{0.0f};
    glm::vec4 shadowParams; // x = softness, yzw = unused
    glm::ivec4 counts{0};  // x = light count, y = mode (0 realtime, 1 baked)
    GpuLight lights[kMaxLights]{};
    glm::mat4 shadowMatrices[kMaxShadowCasters]{};  // light-space view-proj per shadow caster
};

struct DrawCmd {
    Mesh* mesh;
    Material* material;
    glm::mat4 world;
    bool castShadows;
    bool useLightmap;            // sample the baked lightmap instead of live lighting
    VkDescriptorSet lightmapSet; // set 2 (baked lightmap, or default white)

    bool operator<(const DrawCmd& other) const {
        return material < other.material;
    }
};

struct InstanceData {
    glm::mat4 model;
    glm::vec4 boundingSphere; // xyz = center, w = radius
    uint32_t materialIndex;
    uint32_t pad[3];
};

// Owns the GPU frame machinery: the scene pipeline, set-0 (global) descriptors,
// per-frame uniform buffers, command buffers and sync objects. Each frame it
// updates the camera/lighting UBOs from the scene, records the scene draws plus
// the ImGui overlay, then submits and presents (handling resize). The Engine
// keeps orchestration (window, scene, loop); the Renderer is the only thing that
// touches the swap chain present path — the seam a future XR path slots into.
class Renderer {
public:
    Renderer(VulkanDevice& device, Swapchain& swapchain, Window& window,
             ResourceManager& resources, ImGuiLayer& imgui);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void drawFrame(Scene& scene, Camera& camera, Project* project);

private:
    void updateGlobalShadowDescriptor();
    void createGlobalSetLayout();
    void createPipeline(VkDescriptorSetLayout materialSetLayout);
    void createHdrResources();
    void cleanupHdrResources();
    void createTonemapPipeline();
    void recordTonemapPass(VkCommandBuffer cmd, uint32_t imageIndex);
    void createUniformBuffers();
    void createGlobalDescriptorPool();
    void createGlobalDescriptorSets();
    void createCommandBuffers();
    void createSyncObjects();
    void createGpuDrivenBuffers();
    void createCullingPipeline();

    void updateUniformBuffer(uint32_t frame, Scene& scene, Camera& camera, Project* project);
    void gatherScene(LightingUBO& ubo, Scene& scene, const Camera& camera, Project* project);
    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex, Scene& scene);

    VulkanDevice& device_;
    Swapchain& swapchain_;
    Window& window_;
    ResourceManager& resources_;
    ImGuiLayer& imgui_;

    std::unique_ptr<Pipeline> pipeline_;
    std::unique_ptr<ShadowMap> shadowMap_;
    std::unique_ptr<LightBaker> lightBaker_;

    // HDR offscreen target — scene renders here, then tonemapped to swapchain.
    VkImage hdrImage_ = VK_NULL_HANDLE;
    VmaAllocation hdrAllocation_ = VK_NULL_HANDLE;
    VkImageView hdrView_ = VK_NULL_HANDLE;
    VkImage hdrMsaaImage_ = VK_NULL_HANDLE;  // MSAA resolve target (when MSAA on)
    VmaAllocation hdrMsaaAllocation_ = VK_NULL_HANDLE;
    VkImageView hdrMsaaView_ = VK_NULL_HANDLE;

    // Tonemap pipeline
    std::unique_ptr<Pipeline> tonemapPipeline_;
    VkDescriptorSetLayout tonemapSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool tonemapPool_ = VK_NULL_HANDLE;
    VkDescriptorSet tonemapSet_ = VK_NULL_HANDLE;
    VkSampler tonemapSampler_ = VK_NULL_HANDLE;
    float exposure_ = 1.0f;

    VkDescriptorSetLayout globalSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool globalPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> globalSets_;
    std::vector<std::unique_ptr<Buffer>> uniformBuffers_;
    std::vector<std::unique_ptr<Buffer>> lightingBuffers_;

    // GPU-driven rendering resources
    std::vector<std::unique_ptr<Buffer>> instanceBuffers_;
    std::vector<std::unique_ptr<Buffer>> originalDrawCommandBuffers_;
    std::vector<std::unique_ptr<Buffer>> drawCommandBuffers_;
    std::vector<std::unique_ptr<Buffer>> countBuffers_;
    static constexpr uint32_t kMaxInstances = 100000;
    
    std::unique_ptr<ComputePipeline> cullingPipeline_;
    VkDescriptorSetLayout cullingSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool cullingPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> cullingSets_;
    uint32_t currentInstanceCount_ = 0;
    Frustum cameraFrustum_;

    std::vector<VkCommandBuffer> commandBuffers_;
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkFence> inFlightFences_;
    uint32_t currentFrame_ = 0;
    
    std::vector<DrawCmd> currentDraws_;

    // Shadow casters collected this frame: light-space view-proj matrices and
    // their count, consumed by the shadow passes recorded before the main pass.
    std::array<glm::mat4, kMaxShadowCasters> shadowMatrices_{};
    int shadowCount_ = 0;

    bool doBake_ = false;  // run the bake passes this frame (set from bakeRequested)
};

} // namespace ne
