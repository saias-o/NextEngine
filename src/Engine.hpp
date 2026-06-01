#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include "core/Camera.hpp"

#include <memory>
#include <vector>

namespace ne {

class Window;
class VulkanDevice;
class Swapchain;
class Pipeline;
class Mesh;
class Buffer;
class Texture;
class Scene;
class Node;

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

// Top-level application object. Owns every subsystem and drives the render
// loop. Declaration order of the members matters: device_ outlives the
// resources that reference it during destruction.
class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void run();

private:
    void createDescriptorSetLayout();
    void createUniformBuffers();
    void createDescriptorPool();
    void createDescriptorSets();
    void createCommandBuffers();
    void createSyncObjects();

    void buildScene();
    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);
    void processInput(float dt);
    void gatherLights(LightingUBO& ubo);
    void updateUniformBuffer(uint32_t frame);
    void drawFrame();

    std::unique_ptr<Window> window_;
    std::unique_ptr<VulkanDevice> device_;
    std::unique_ptr<Swapchain> swapchain_;
    std::unique_ptr<Pipeline> pipeline_;
    std::unique_ptr<Mesh> mesh_;
    std::unique_ptr<Texture> texture_;
    std::unique_ptr<Scene> scene_;

    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets_;
    std::vector<std::unique_ptr<Buffer>> uniformBuffers_;
    std::vector<std::unique_ptr<Buffer>> lightingBuffers_;

    std::vector<VkCommandBuffer> commandBuffers_;
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkFence> inFlightFences_;
    uint32_t currentFrame_ = 0;

    Camera camera_;
};

} // namespace ne
