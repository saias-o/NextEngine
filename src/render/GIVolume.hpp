#pragma once

#include "graphics/StorageImage.hpp"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <array>
#include <memory>
#include <random>

namespace ne {

class VulkanDevice;
class Buffer;
class Scene;
class ComputePipeline;

// Layout of the DDGI irradiance probe volume — the SINGLE global-illumination
// primitive of the engine. A regular 3D grid of probes; each probe stores an
// octahedral irradiance tile and an octahedral visibility tile (mean distance,
// mean distance^2) for the Chebyshev occlusion test.
struct GIVolumeDesc {
    glm::vec3 origin{-12.0f, -4.0f, -12.0f};  // world-space min corner
    glm::vec3 spacing{3.0f, 3.0f, 3.0f};      // probe spacing (world units)
    glm::ivec3 counts{8, 4, 8};               // probes per axis
    int irradianceTexels = 8;                 // octahedral resolution / probe
    int visibilityTexels = 16;
    int voxelResolution = 64;                 // cubic voxel grid resolution
    int raysPerProbe = 64;                    // DDGI rays cast per probe per update
    float hysteresis = 0.97f;                 // temporal blend (Majercik 2019)
    float distExponent = 50.0f;               // visibility distance weight sharpness

    int probeCount() const { return counts.x * counts.y * counts.z; }
};

// Owns the GPU resources of the irradiance probe volume: two ping-pong atlases
// for irradiance (rgba16f) and visibility (rg16f), plus the sampler the lighting
// pass uses. The update compute pass (P2) writes the "write" atlas and reads the
// "read" atlas (previous frame) for the temporal blend; the lighting pass always
// samples the "read" atlas. In baked mode the atlases are simply frozen.
//
// P0: no compute yet — the atlases are filled once with a constant (ambient-like
// irradiance + infinite visibility) so the whole sampling path is exercised and
// the scene renders through the volume. P1/P2 replace the fill with voxelization
// + DDGI ray-marching.
class GIVolume {
public:
    GIVolume(VulkanDevice& device, const GIVolumeDesc& desc,
             VkDescriptorSetLayout materialSetLayout, VkDescriptorSetLayout globalSetLayout);
    ~GIVolume();
    GIVolume(const GIVolume&) = delete;
    GIVolume& operator=(const GIVolume&) = delete;

    // Re-voxelize the scene's albedo into the 3D grid (call before the main pass).
    // Clears, runs the 3 dominant-axis passes, and leaves the grid sampled-ready.
    void voxelize(VkCommandBuffer cmd, Scene& scene);

    // Swap the ping-pong atlases (call host-side at frame start). After this, the
    // "current" atlas is the one update() writes and the lighting pass samples;
    // the other holds the previous frame's result (read for temporal blending).
    void beginFrame() { curr_ ^= 1; }

    // Run the DDGI update: trace rays, blend into the current atlases, copy borders.
    // globalSet = set 0 for this frame (lights, shadow maps, voxel grid).
    void update(VkCommandBuffer cmd, VkDescriptorSet globalSet);

    // Views/sampler the renderer binds into set 0 (bindings 4/5/6) for shading.
    VkImageView irradianceView() const { return irradiance_[curr_]->view(); }
    VkImageView visibilityView() const { return visibility_[curr_]->view(); }
    VkImageView voxelView() const { return voxelView_; }
    VkSampler sampler() const { return sampler_; }

    const GIVolumeDesc& desc() const { return desc_; }
    int probesPerRow() const { return probesPerRow_; }
    // World-space size of the volume box (probe lattice extent).
    glm::vec3 worldExtent() const { return glm::vec3(desc_.counts - 1) * desc_.spacing; }

    // Atlas pixel dimensions (probesPerRow tiles wide, with a 1px border/probe).
    glm::ivec2 irradianceAtlasSize() const { return atlasSize(desc_.irradianceTexels); }
    glm::ivec2 visibilityAtlasSize() const { return atlasSize(desc_.visibilityTexels); }

private:
    void createSampler();
    void fillConstant();  // P0: clear atlases to a constant via a one-time command
    void createVoxelResources(VkDescriptorSetLayout materialSetLayout);
    void createComputeResources(VkDescriptorSetLayout globalSetLayout);

    glm::ivec2 atlasSize(int texels) const {
        int rows = (desc_.probeCount() + probesPerRow_ - 1) / probesPerRow_;
        return {probesPerRow_ * (texels + 2), rows * (texels + 2)};
    }

    VulkanDevice& device_;
    GIVolumeDesc desc_;
    int probesPerRow_ = 1;
    int curr_ = 0;  // current ping-pong atlas (written this frame, sampled by lighting)

    std::array<std::unique_ptr<StorageImage>, 2> irradiance_;  // rgba16f
    std::array<std::unique_ptr<StorageImage>, 2> visibility_;  // rg16f
    VkSampler sampler_ = VK_NULL_HANDLE;

    // --- Voxelization (P1) ---
    VkImage voxelImage_ = VK_NULL_HANDLE;       // 3D rgba16f albedo grid
    VmaAllocation voxelAllocation_ = VK_NULL_HANDLE;
    VkImageView voxelView_ = VK_NULL_HANDLE;
    std::unique_ptr<Buffer> voxelUbo_;          // origin/extent/res + 3 axis VPs
    VkDescriptorSetLayout voxelSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool voxelPool_ = VK_NULL_HANDLE;
    VkDescriptorSet voxelSet_ = VK_NULL_HANDLE;
    VkPipeline voxelPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout voxelPipelineLayout_ = VK_NULL_HANDLE;

    // --- DDGI update (P2): trace -> blend -> borders ---
    std::unique_ptr<Buffer> raysBuffer_;        // numProbes * raysPerProbe * 2 vec4
    VkDescriptorSetLayout giComputeSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool giComputePool_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 2> giComputeSets_{};  // one per ping-pong parity
    std::unique_ptr<ComputePipeline> tracePipeline_;
    std::unique_ptr<ComputePipeline> blendPipeline_;
    std::unique_ptr<ComputePipeline> borderPipeline_;
    std::mt19937 rng_{1234};
};

} // namespace ne
