#pragma once

// Root of the Render Hardware Interface (Étape 16.3). See PLAN_RHI.md.
//
// One backend per build, chosen at compile time — no runtime vtable in the frame
// path. As Vulkan resources/commands are extracted, their `rhi::` aliases are
// declared here from the active backend namespace (Vulkan on desktop/XR, WebGPU
// under Emscripten in Étape 16.4). The RHI abstracts resource creation and command
// recording, not the render logic.

#include "rhi/Capabilities.hpp"
#include "rhi/BufferUsage.hpp"
#include "rhi/Format.hpp"
#include "graphics/Buffer.hpp"   // Vulkan backend's Buffer (aliased below)
#include "graphics/Texture.hpp"  // Vulkan backend's Texture (aliased below)

namespace saida::rhi {

// Backend selection (compile-time). Types are aliased from the active backend as
// the extraction proceeds; `Capabilities` / `BufferUsage` are already neutral.
//
// 16.3.b — Buffer: the Vulkan wrapper (graphics/Buffer.hpp) with a neutral
// construction API. The WebGPU backend (16.4) will provide its own Buffer with
// the same surface, selected here under Emscripten.
using Buffer = saida::Buffer;

// 16.3.c — Texture: the Vulkan wrapper (image + view + sampler). Construction API
// is neutral (rhi::Format); handle accessors stay backend-internal for now.
using Texture = saida::Texture;

} // namespace saida::rhi
