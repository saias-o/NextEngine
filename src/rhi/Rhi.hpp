#pragma once

// Root of the Render Hardware Interface (Étape 16.3). See PLAN_RHI.md.
//
// One backend per build, chosen at compile time — no runtime vtable in the frame
// path. As Vulkan resources/commands are extracted, their `rhi::` aliases are
// declared here from the active backend namespace (Vulkan on desktop/XR, WebGPU
// under Emscripten in Étape 16.4). The RHI abstracts resource creation and command
// recording, not the render logic.

#include "rhi/Capabilities.hpp"

namespace saida::rhi {

// Backend selection (compile-time). Type aliases such as
//   using Buffer  = vulkan::Buffer;
//   using Texture = vulkan::Texture;
// land here as the extraction proceeds (16.3.b …). `Capabilities` is already
// backend-neutral and lives in rhi/Capabilities.hpp.

} // namespace saida::rhi
