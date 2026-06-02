#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace ne {

class VulkanDevice;
class Swapchain;
class Window;
class Pipeline;
class Buffer;
class ImGuiLayer;
class Scene;
class Camera;

struct UniformBufferObject {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

// std140-compatible lighting block (mirrors the LightingUBO in shader.frag).
// Everything is vec4/ivec4 to avoid std140 vec3 padding surprises.
struct GpuPointLight {
    glm::vec4 position;  // xyz = world pos, w = range
    glm::vec4 color;     // rgb = color, w = intensity
};

struct LightingUBO {
    glm::vec4 ambient{0.0f};
    glm::vec4 cameraPos{0.0f};
    glm::vec4 dirDirection{0.0f, -1.0f, 0.0f, 0.0f};  // w = intensity
    glm::vec4 dirColor{0.0f};
    GpuPointLight pointLights[4]{};
    glm::ivec4 counts{0};  // x = active point lights, y = mode
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
             VkDescriptorSetLayout materialSetLayout, ImGuiLayer& imgui);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void drawFrame(Scene& scene, Camera& camera);

private:
    void createGlobalSetLayout();
    void createPipeline(VkDescriptorSetLayout materialSetLayout);
    void createUniformBuffers();
    void createGlobalDescriptorPool();
    void createGlobalDescriptorSets();
    void createCommandBuffers();
    void createSyncObjects();

    void updateUniformBuffer(uint32_t frame, Scene& scene, Camera& camera);
    void gatherLights(LightingUBO& ubo, Scene& scene, const Camera& camera);
    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex, Scene& scene);

    VulkanDevice& device_;
    Swapchain& swapchain_;
    Window& window_;
    ImGuiLayer& imgui_;

    std::unique_ptr<Pipeline> pipeline_;
    VkDescriptorSetLayout globalSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool globalPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> globalSets_;
    std::vector<std::unique_ptr<Buffer>> uniformBuffers_;
    std::vector<std::unique_ptr<Buffer>> lightingBuffers_;

    std::vector<VkCommandBuffer> commandBuffers_;
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkFence> inFlightFences_;
    uint32_t currentFrame_ = 0;
};

} // namespace ne
