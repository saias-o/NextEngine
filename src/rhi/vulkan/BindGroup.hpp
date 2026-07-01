#pragma once

#include "rhi/BindGroup.hpp"
#include "rhi/CommandTypes.hpp"

#include <vulkan/vulkan.h>

#include <vector>

// Vulkan backend for rhi::BindGroup(Layout) (Étape 16.3.d). A BindGroupLayout
// wraps a VkDescriptorSetLayout and owns a growable pool hidden from callers
// (WebGPU has no descriptor pools). A BindGroup is an immutable descriptor set:
// to change a binding, recreate the group — re-pointing is rare (GI atlas,
// tonemap targets) and recreation keeps both backends on one model.

namespace saida {
class Buffer;
class VulkanDevice;
}

namespace saida::rhi::vulkan {

class BindGroup;

class BindGroupLayout {
public:
    BindGroupLayout(VulkanDevice& device, std::vector<rhi::BindGroupLayoutEntry> entries);
    ~BindGroupLayout();
    BindGroupLayout(const BindGroupLayout&) = delete;
    BindGroupLayout& operator=(const BindGroupLayout&) = delete;

    // Migration escape hatch: legacy Pipeline constructors and raw
    // vkCmdBindDescriptorSets calls; usage shrinks to zero as 16.3.d/e proceed.
    VkDescriptorSetLayout handle() const { return layout_; }

    const std::vector<rhi::BindGroupLayoutEntry>& entries() const { return entries_; }

private:
    friend class BindGroup;
    VkDescriptorSet allocate(VkDescriptorPool& outPool);
    void free(VkDescriptorPool pool, VkDescriptorSet set);
    void addPool();

    VulkanDevice& device_;
    std::vector<rhi::BindGroupLayoutEntry> entries_;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    struct Pool {
        VkDescriptorPool handle = VK_NULL_HANDLE;
        uint32_t remaining = 0;
    };
    std::vector<Pool> pools_;
};

// One resource bound at `binding`. Exactly one of buffer / view(+sampler) is
// set, matching the layout entry's BindingType.
struct BindGroupEntry {
    uint32_t binding = 0;

    const saida::Buffer* buffer = nullptr;  // UniformBuffer / StorageBuffer
    uint64_t offset = 0;
    uint64_t range = 0;                     // 0 = whole buffer

    VkImageView view = VK_NULL_HANDLE;      // textures / storage images
    VkSampler sampler = VK_NULL_HANDLE;     // Sampler / CombinedImageSampler
    // Layout the texture is in when sampled (ShaderRead for color textures,
    // DepthRead for sampled depth like shadow maps, StorageReadWrite for images).
    rhi::ResourceState textureState = rhi::ResourceState::ShaderRead;
};

class BindGroup {
public:
    BindGroup(BindGroupLayout& layout, const std::vector<BindGroupEntry>& entries);
    ~BindGroup();
    BindGroup(const BindGroup&) = delete;
    BindGroup& operator=(const BindGroup&) = delete;

    // Migration escape hatch (raw vkCmdBindDescriptorSets); RenderPassEncoder /
    // ComputePassEncoder::setBindGroup is the target API.
    VkDescriptorSet handle() const { return set_; }

private:
    BindGroupLayout& layout_;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;  // owning pool (needed to free)
    VkDescriptorSet set_ = VK_NULL_HANDLE;
};

} // namespace saida::rhi::vulkan
