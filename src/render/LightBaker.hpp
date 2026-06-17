#pragma once

#include "graphics/VmaFwd.hpp"

#include <vulkan/vulkan.h>

#include <unordered_map>

namespace ne {

class VulkanDevice;
class Scene;
class MeshNode;

// GPU lightmap baker (direct light + shadows, no GI).
//
// On request it renders every "Include in light baking" mesh into its own
// lightmap texture, evaluating the *same* lighting + shadow maps as the realtime
// pass (via the shared shaders/lighting.glsl and bake.frag), but storing only the
// view-independent diffuse irradiance. At runtime the scene shader samples that
// lightmap for baked meshes and adds realtime specular, so a baked surface
// matches the realtime result exactly.
//
// Owns the bake render pass / pipeline, a per-instance lightmap (image + view +
// framebuffer + descriptor) keyed by MeshNode, and a default 1x1 white lightmap
// bound for non-baked draws. The scene pipeline binds the lightmap as set 2.
class LightBaker {
public:
    static constexpr uint32_t kLightmapSize = 512;

    // globalSetLayout = set 0 (camera + lighting + shadow), reused by the bake
    // pipeline so the bake shades with the live lights and shadow maps.
    LightBaker(VulkanDevice& device, VkDescriptorSetLayout globalSetLayout);
    ~LightBaker();
    LightBaker(const LightBaker&) = delete;
    LightBaker& operator=(const LightBaker&) = delete;

    // Set 2 layout (combined image sampler) for building the scene pipeline.
    VkDescriptorSetLayout setLayout() const { return setLayout_; }

    // Allocate/keep a lightmap per included mesh. Call outside command recording.
    void prepare(Scene& scene);

    // Record the bake passes (one per included mesh) into cmd, after the shadow
    // passes and before the main pass. globalSet = set 0 for the current frame.
    void record(VkCommandBuffer cmd, VkDescriptorSet globalSet, Scene& scene);

    bool has(MeshNode* node) const { return lightmaps_.count(node) != 0; }
    // The baked set for this node, or the default white set when not baked.
    VkDescriptorSet lightmapSet(MeshNode* node) const;

private:
    struct Lightmap {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };

    void createSetLayoutAndPool();
    void createPipeline(VkDescriptorSetLayout globalSetLayout);
    void createSampler();
    void createDefaultLightmap();
    void createDilation();   // seam-dilation pass (fills the UV gutter)
    void recordDilate(VkCommandBuffer cmd, const Lightmap& lm);
    Lightmap createLightmap();
    VkDescriptorSet allocateSet(VkImageView view);
    void destroyLightmap(Lightmap& lm);

    VulkanDevice& device_;
    VkFormat format_{};

    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    // Default white lightmap, bound for non-baked draws.
    VkImage defaultImage_ = VK_NULL_HANDLE;
    VmaAllocation defaultAllocation_ = VK_NULL_HANDLE;
    VkImageView defaultView_ = VK_NULL_HANDLE;
    VkDescriptorSet defaultSet_ = VK_NULL_HANDLE;

    // Seam dilation: a fullscreen pass samples the baked lightmap (coverage in
    // alpha) and grows covered texels into the UV gutter, then copies back.
    VkImage dilateTemp_ = VK_NULL_HANDLE;
    VmaAllocation dilateTempAlloc_ = VK_NULL_HANDLE;
    VkImageView dilateTempView_ = VK_NULL_HANDLE;
    VkPipelineLayout dilateLayout_ = VK_NULL_HANDLE;
    VkPipeline dilatePipeline_ = VK_NULL_HANDLE;

    std::unordered_map<MeshNode*, Lightmap> lightmaps_;
};

} // namespace ne
