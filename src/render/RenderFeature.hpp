#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace saida {

class VulkanDevice;
class ResourceManager;
class Scene;
class Camera;
class Mesh;
class MeshNode;
class Material;
enum class MaterialType : uint32_t;

// CPU-prepared mesh draw shared by the main renderer and additive scene features
// (outline, decals, etc.). It mirrors the actual visible mesh/LOD/skinning choice
// for this frame, so features do not re-run visibility or animation selection.
struct SceneDraw {
    SceneDraw() = default;
    SceneDraw(Mesh* mesh, Material* material, MeshNode* node, const glm::mat4& world,
              bool castShadows, int32_t boneOffset, MaterialType materialType)
        : mesh(mesh), material(material), node(node), world(world),
          castShadows(castShadows), boneOffset(boneOffset),
          materialType(materialType) {}

    Mesh* mesh = nullptr;
    Material* material = nullptr;
    MeshNode* node = nullptr;
    glm::mat4 world{1.0f};
    bool castShadows = false;
    int32_t boneOffset = -1;
    MaterialType materialType;
};

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
    VkExtent2D extent{};                               // current HDR scene target
    const SceneDraw* draws = nullptr;                  // visible opaque mesh draws
    uint32_t drawCount = 0;
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

} // namespace saida
