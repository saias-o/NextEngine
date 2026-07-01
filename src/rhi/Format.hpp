#pragma once

#include <cstdint>

// Backend-neutral texture / attachment formats (Étape 16.3, RHI). Backends map
// these to their own enum (Vulkan: rhi/vulkan/Format.hpp; WebGPU in 16.4). The
// set grows as the extraction reaches render targets and pipelines.

namespace saida::rhi {

enum class Format : uint32_t {
    Undefined = 0,

    // 8-bit unsigned normalized / sRGB
    RGBA8Unorm,
    RGBA8Srgb,
    BGRA8Unorm,
    BGRA8Srgb,

    // floating-point colour
    RG16Float,
    RGBA16Float,
    RG32Float,
    RGB32Float,
    RGBA32Float,
    RGBA32Sint,

    // depth / stencil
    Depth16,
    Depth32Float,
    Depth24Stencil8,
    Depth32FloatStencil8,
};

} // namespace saida::rhi
