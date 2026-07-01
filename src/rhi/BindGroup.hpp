#pragma once

#include "rhi/ShaderStages.hpp"

#include <cstdint>

// Backend-neutral bind group layout description (Étape 16.3.d). A bind group is
// the RHI name for a Vulkan descriptor set / WebGPU bind group; set 0/1/2 map
// 1:1. The backend classes live in rhi/<backend>/BindGroup.hpp.

namespace saida::rhi {

enum class BindingType : uint32_t {
    UniformBuffer,
    StorageBuffer,
    CombinedImageSampler,  // desktop GLSL; web shaders split texture/sampler (16.2)
    SampledTexture,
    Sampler,
    StorageImage,
};

struct BindGroupLayoutEntry {
    uint32_t binding = 0;
    BindingType type = BindingType::UniformBuffer;
    ShaderStages visibility = ShaderStages::VertexFragment;
    uint32_t count = 1;  // array size (e.g. bindless texture arrays)
};

} // namespace saida::rhi
