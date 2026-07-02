#pragma once

#include "rhi/PipelineState.hpp"  // CompareOp

#include <cstdint>

// Backend-neutral sampler description (Étape 16.3.f). Backends map it to their
// own sampler object (Vulkan: rhi/vulkan/Sampler.hpp). Asset textures keep the
// sampler embedded in Texture (16.3.c); this covers the render-target samplers
// owned by the render logic (tonemap, bloom, GI atlases, shadow PCF).

namespace saida::rhi {

enum class FilterMode : uint32_t { Nearest, Linear };

enum class AddressMode : uint32_t { Repeat, ClampToEdge, ClampToBorder };

struct SamplerDesc {
    FilterMode magFilter = FilterMode::Linear;
    FilterMode minFilter = FilterMode::Linear;
    FilterMode mipFilter = FilterMode::Nearest;
    AddressMode addressMode = AddressMode::ClampToEdge;  // all three axes
    bool compareEnabled = false;         // hardware PCF (shadow maps)
    CompareOp compare = CompareOp::Less;
    bool whiteBorder = false;            // ClampToBorder: white = "outside frustum is lit"
};

} // namespace saida::rhi
