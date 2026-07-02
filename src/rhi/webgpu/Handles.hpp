#pragma once

#include "rhi/webgpu/WebGpu.hpp"

#include <cstdint>

// WebGPU backend for the neutral handle/value aliases (Étape 16.5.a) — mirror
// of rhi/vulkan/Handles.hpp.

namespace saida::rhi::webgpu {

using TextureHandle = WGPUTexture;
using TextureView = WGPUTextureView;
using SamplerHandle = WGPUSampler;

struct Extent2D {
    uint32_t width = 0;
    uint32_t height = 0;
};

struct Offset2D {
    int32_t x = 0;
    int32_t y = 0;
};

struct Rect2D {
    Offset2D offset{};
    Extent2D extent{};
};

// Plain sample count (1/2/4…): numerically identical to the Vulkan backend's
// VkSampleCountFlagBits values.
using SampleCount = uint32_t;

} // namespace saida::rhi::webgpu
