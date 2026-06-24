#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace ne {

class VulkanDevice;
class ResourceManager;
class Scene;
class Camera;

// Everything one eye/view needs for stereo (multiview) rendering. Lives here so
// both the Renderer and the render features can see it without a cycle.
struct EyeRenderInfo {
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkExtent2D extent{};
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec3 eyePosition{0.0f};
};

// Creation-time context handed to a feature once, so it can build its pipeline(s)
// and descriptors against the engine's HDR scene target. `viewMask == 0` means the
// mono (desktop) path; a non-zero mask means stereo multiview (XR).
struct RenderContext {
    VulkanDevice& device;
    ResourceManager& resources;
    VkDescriptorSetLayout globalSetLayout;  // set 0: camera + lighting + environment
    VkFormat colorFormat;                   // HDR color attachment
    VkFormat depthFormat;
    VkSampleCountFlagBits samples;
    uint32_t viewMask;
    uint32_t framesInFlight;

    bool stereo() const { return viewMask != 0; }
};

// Per-frame context for recording inside the HDR scene pass, AFTER the opaque
// draws. Mono fills `camera`; stereo fills `eyes` (and may set `passthrough`).
struct FrameContext {
    VkCommandBuffer cmd;
    uint32_t frameIndex;
    VkDescriptorSet globalSet;   // set 0 for this frame (camera + lighting + env)
    Scene& scene;
    float time;
    bool stereo;
    const Camera* camera = nullptr;                    // valid when !stereo
    const std::vector<EyeRenderInfo>* eyes = nullptr;  // valid when stereo
    bool passthrough = false;                          // XR see-through: skip opaque sky
};

// A self-contained extra draw in the HDR scene pass. Register one with the Renderer
// and it gets `createPipelines` once and `record` every frame — so adding an effect
// (water, skybox, decals, particles, …) is a new file + one registration line and
// NEVER edits the Renderer. Features own their pipelines/descriptors (RAII).
class ScenePassFeature {
public:
    virtual ~ScenePassFeature() = default;
    virtual void createPipelines(const RenderContext& ctx) = 0;
    virtual void record(const FrameContext& fc) = 0;
};

} // namespace ne
