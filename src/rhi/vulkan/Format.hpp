#pragma once

#include "rhi/Format.hpp"

#include <vulkan/vulkan.h>

// Vulkan backend: rhi::Format <-> VkFormat. Kept in the rhi/vulkan/ backend layer
// so the neutral rhi/Format.hpp stays free of Vulkan types (Étape 16.3).

namespace saida::rhi::vulkan {

inline VkFormat toVk(Format f) {
    switch (f) {
        case Format::Undefined:            return VK_FORMAT_UNDEFINED;
        case Format::RGBA8Unorm:           return VK_FORMAT_R8G8B8A8_UNORM;
        case Format::RGBA8Srgb:            return VK_FORMAT_R8G8B8A8_SRGB;
        case Format::BGRA8Unorm:           return VK_FORMAT_B8G8R8A8_UNORM;
        case Format::BGRA8Srgb:            return VK_FORMAT_B8G8R8A8_SRGB;
        case Format::RG16Float:            return VK_FORMAT_R16G16_SFLOAT;
        case Format::RGBA16Float:          return VK_FORMAT_R16G16B16A16_SFLOAT;
        case Format::RG32Float:            return VK_FORMAT_R32G32_SFLOAT;
        case Format::RGB32Float:           return VK_FORMAT_R32G32B32_SFLOAT;
        case Format::RGBA32Float:          return VK_FORMAT_R32G32B32A32_SFLOAT;
        case Format::RGBA32Sint:           return VK_FORMAT_R32G32B32A32_SINT;
        case Format::Depth16:              return VK_FORMAT_D16_UNORM;
        case Format::Depth32Float:         return VK_FORMAT_D32_SFLOAT;
        case Format::Depth24Stencil8:      return VK_FORMAT_D24_UNORM_S8_UINT;
        case Format::Depth32FloatStencil8: return VK_FORMAT_D32_SFLOAT_S8_UINT;
    }
    return VK_FORMAT_UNDEFINED;
}

} // namespace saida::rhi::vulkan
