#pragma once

#include "graphics/VmaFwd.hpp"
#include "rhi/Format.hpp"
#include "rhi/TextureUsage.hpp"

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

// Vulkan backend for rhi::RenderTexture (Étape 16.3.f): neutral render-target
// creation. Owns the image, its whole-resource view (2D / 2D_ARRAY / 3D picked
// from the desc) and per-layer attachment views when layers > 1 (shadow array,
// XR stereo). Distinct from saida::Texture (sampled asset with mips + sampler).

namespace saida {
class VulkanDevice;
}

namespace saida::rhi::vulkan {

struct RenderTextureDesc {
    rhi::Format format = rhi::Format::Undefined;
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;       // > 1 → 3D texture (GI voxel grid)
    uint32_t layers = 1;      // > 1 → 2D array (shadow layers, XR eyes)
    uint32_t samples = 1;     // MSAA scene target
    rhi::TextureUsage usage = rhi::TextureUsage::None;
    // MemoryProfiler category; empty = untracked (matches pre-RHI behaviour).
    std::string memoryCategory;
};

class RenderTexture {
public:
    RenderTexture(VulkanDevice& device, const RenderTextureDesc& desc);
    ~RenderTexture();
    RenderTexture(const RenderTexture&) = delete;
    RenderTexture& operator=(const RenderTexture&) = delete;

    VkImage image() const { return image_; }
    VkImageView view() const { return view_; }              // whole resource
    VkImageView layerView(uint32_t layer) const { return layerViews_[layer]; }

    rhi::Format format() const { return desc_.format; }
    VkExtent2D extent() const { return {desc_.width, desc_.height}; }
    uint32_t layers() const { return desc_.layers; }

private:
    VulkanDevice& device_;
    RenderTextureDesc desc_;
    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    std::vector<VkImageView> layerViews_;  // filled when desc.layers > 1
    uint64_t trackedBytes_ = 0;
};

} // namespace saida::rhi::vulkan
