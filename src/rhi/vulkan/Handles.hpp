#pragma once

#include <vulkan/vulkan.h>

// Vulkan backend for the neutral handle/value aliases (Étape 16.5.a). These are
// the types that legitimately cross render/ interfaces: non-owning views and
// samplers handed to bind groups, and the small extent/rect value types. Code in
// render/ and fx/ writes rhi::TextureView & co and compiles against either
// backend; only the backend .cpps name Vk*/WGPU* types.

namespace saida::rhi::vulkan {

using TextureHandle = VkImage;        // non-owning texture (transitions, copies)
using TextureView = VkImageView;      // non-owning view (attachments, bind groups)
using SamplerHandle = VkSampler;      // non-owning sampler (bind groups)
using Extent2D = VkExtent2D;
using Rect2D = VkRect2D;
// VkSampleCountFlagBits values equal the literal sample counts (1/2/4/8…), so
// the web backend's plain uint32_t is drop-in compatible at every use site.
using SampleCount = VkSampleCountFlagBits;

} // namespace saida::rhi::vulkan
