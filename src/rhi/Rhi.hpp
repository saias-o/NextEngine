#pragma once

// Root of the Render Hardware Interface (Étape 16.3). See PLAN_RHI.md.
//
// One backend per build, chosen at compile time — no runtime vtable in the frame
// path. As Vulkan resources/commands are extracted, their `rhi::` aliases are
// declared here from the active backend namespace (Vulkan on desktop/XR, WebGPU
// under Emscripten in Étape 16.4). The RHI abstracts resource creation and command
// recording, not the render logic.

#include "rhi/Capabilities.hpp"
#include "rhi/BindGroup.hpp"
#include "rhi/BufferUsage.hpp"
#include "rhi/CommandTypes.hpp"
#include "rhi/Format.hpp"
#include "rhi/PipelineState.hpp"
#include "rhi/Sampler.hpp"
#include "rhi/ShaderStages.hpp"
#include "rhi/TextureUsage.hpp"
#include "rhi/vulkan/BindGroup.hpp"
#include "rhi/vulkan/CommandEncoder.hpp"
#include "rhi/vulkan/RenderTexture.hpp"
#include "rhi/vulkan/Sampler.hpp"
#include "graphics/Buffer.hpp"   // Vulkan backend's Buffer (aliased below)
#include "graphics/Pipeline.hpp" // Vulkan backend's Pipeline (aliased below)
#include "graphics/Texture.hpp"  // Vulkan backend's Texture (aliased below)

namespace saida {
class VulkanDevice;  // Vulkan backend's Device (aliased below, fwd-decl only)
class Swapchain;     // Vulkan backend's Surface (aliased below, fwd-decl only)
}

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

// 16.3.d — Pipeline (neutral Desc), bind groups (descriptor sets behind a
// pool-free API).
using Pipeline = saida::Pipeline;
using BindGroupLayout = vulkan::BindGroupLayout;
using BindGroup = vulkan::BindGroup;
using BindGroupEntry = vulkan::BindGroupEntry;

// 16.3.e — Command recording: encoder + pass encoders (sync expressed as
// neutral ResourceState transitions, hidden barriers in the backend).
using CommandEncoder = vulkan::CommandEncoder;
using RenderPassEncoder = vulkan::RenderPassEncoder;
using ComputePassEncoder = vulkan::ComputePassEncoder;
using RenderPassDesc = vulkan::RenderPassDesc;
using ColorAttachment = vulkan::ColorAttachment;
using DepthAttachment = vulkan::DepthAttachment;

// 16.3.f — Device / Surface / render targets. Device is the GPU handle
// (queues, allocator, caps, withSingleTimeEncoder); Surface owns presentation
// (acquire/submit/present with the frame sync hidden inside — the Desktop/XR/
// Web seam). RenderTexture is neutral render-target creation.
using Device = saida::VulkanDevice;
using Surface = saida::Swapchain;
using RenderTexture = vulkan::RenderTexture;
using RenderTextureDesc = vulkan::RenderTextureDesc;
using Sampler = vulkan::Sampler;

} // namespace saida::rhi
