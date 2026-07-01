#pragma once

#include <cstdint>

// Backend-neutral command recording types (Étape 16.3.e). See PLAN_RHI.md §7.3:
// barriers stay explicit (no automatic tracking) but are expressed as neutral
// resource states; the Vulkan backend maps a state to layout+stage+access, the
// WebGPU backend no-ops transitions entirely (driver-tracked).

namespace saida::rhi {

enum class ResourceState : uint32_t {
    Undefined,         // contents discarded (valid as source state only)
    ShaderRead,        // sampled/read in any shader stage
    ColorAttachment,   // render target color write
    DepthWrite,        // depth attachment write
    DepthRead,         // depth sampled in shaders (shadow maps, AO depth)
    StorageReadWrite,  // storage image/buffer access (compute or fragment)
    CopySrc,
    CopyDst,
    Present,           // ready for presentation
};

enum class LoadOp : uint32_t {
    Load,
    Clear,
    DontCare,
};

enum class IndexType : uint32_t {
    Uint16,
    Uint32,
};

// Aspect selection for transitions. Auto derives depth from the states involved
// (DepthWrite/DepthRead); override for e.g. a depth image moving to ShaderRead
// from Undefined, where neither state names depth.
enum class TextureAspect : uint32_t {
    Auto,
    Color,
    Depth,
};

} // namespace saida::rhi
