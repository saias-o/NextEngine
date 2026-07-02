#pragma once

#include "rhi/CommandTypes.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

// Vulkan backend for rhi::CommandEncoder (Étape 16.3.e). See PLAN_RHI.md §7.
//
// The encoder is a NON-OWNING view over a VkCommandBuffer — trivially copyable,
// no state beyond the handle. This is the interop rule that makes the migration
// divisible: converted subsystems take an encoder, unconverted ones keep the raw
// VkCommandBuffer, both record into the same frame. handle() is the migration
// escape hatch; its usage shrinks to zero inside render/fx as 16.3.e proceeds.
// The WebGPU backend (16.4) wraps WGPUCommandEncoder with the same surface
// (minus the Vulkan escape hatch, unused by then).

namespace saida {
class Buffer;
class ComputePipeline;
class Pipeline;
}

namespace saida::rhi::vulkan {

class BindGroup;

struct ColorAttachment {
    VkImageView view = VK_NULL_HANDLE;
    rhi::LoadOp loadOp = rhi::LoadOp::Clear;
    std::array<float, 4> clearColor{{0.0f, 0.0f, 0.0f, 1.0f}};
    bool store = true;                        // false = DONT_CARE (MSAA source)
    VkImageView resolveView = VK_NULL_HANDLE; // MSAA average resolve target
};

struct DepthAttachment {
    VkImageView view = VK_NULL_HANDLE;
    rhi::LoadOp loadOp = rhi::LoadOp::Clear;
    float clearDepth = 1.0f;
    bool store = true;
    VkImageView resolveView = VK_NULL_HANDLE; // SAMPLE_ZERO resolve (AO depth)
};

struct RenderPassDesc {
    std::array<ColorAttachment, 4> colors{};
    uint32_t colorCount = 0;
    DepthAttachment depth{};        // view == null → depth-less pass
    int32_t x = 0, y = 0;           // render area offset
    uint32_t width = 0, height = 0; // render area extent
    uint32_t layerCount = 1;
    uint32_t viewMask = 0;          // multiview (XR stereo); 0 = single view
    // Sets a full-render-area viewport + scissor at begin (the common case);
    // passes with a custom rect call setViewport/setScissor after.
    bool defaultViewportScissor = true;
};

// Records draws inside one dynamic-rendering pass. setPipeline() remembers the
// pipeline layout and push constant stages, so setBindGroup/setPushConstants
// need no layout parameter (same shape as WebGPU's render pass encoder).
class RenderPassEncoder {
public:
    void setPipeline(const saida::Pipeline& pipeline);
    void setBindGroup(uint32_t index, const BindGroup& group);
    void setBindGroup(uint32_t index, VkDescriptorSet set);  // migration interop
    void setPushConstants(const void* data, uint32_t size, uint32_t offset = 0);
    void setViewport(float x, float y, float width, float height,
                     float minDepth = 0.0f, float maxDepth = 1.0f);
    void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height);
    void setVertexBuffer(const saida::Buffer& buffer, uint64_t offset = 0);
    void setIndexBuffer(const saida::Buffer& buffer,
                        rhi::IndexType type = rhi::IndexType::Uint32, uint64_t offset = 0);
    void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
              uint32_t firstVertex = 0, uint32_t firstInstance = 0);
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0,
                     int32_t vertexOffset = 0, uint32_t firstInstance = 0);
    void drawIndirect(const saida::Buffer& buffer, uint64_t offset,
                      uint32_t drawCount, uint32_t stride);
    void end();

    VkCommandBuffer handle() const { return cmd_; }  // migration escape hatch

    // Migration escape hatch: view over a pass begun with raw vkCmdBeginRendering,
    // so converted subsystems can record inside unconverted passes. Usage shrinks
    // to zero as 16.3.e.g converts the remaining pass-begin sites.
    static RenderPassEncoder fromHandle(VkCommandBuffer cmd) { return RenderPassEncoder(cmd); }

private:
    friend class CommandEncoder;
    explicit RenderPassEncoder(VkCommandBuffer cmd) : cmd_(cmd) {}

    VkCommandBuffer cmd_;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkShaderStageFlags pushStages_ = 0;
};

// Compute dispatches. Vulkan has no begin/end for compute; the pass object
// exists so the WebGPU backend (which requires one) shares the same call shape.
class ComputePassEncoder {
public:
    void setPipeline(const saida::ComputePipeline& pipeline);
    void setBindGroup(uint32_t index, const BindGroup& group);
    void setBindGroup(uint32_t index, VkDescriptorSet set);  // migration interop
    void setPushConstants(const void* data, uint32_t size, uint32_t offset = 0);
    void dispatch(uint32_t x, uint32_t y = 1, uint32_t z = 1);
    void end() {}

    VkCommandBuffer handle() const { return cmd_; }  // migration escape hatch

private:
    friend class CommandEncoder;
    explicit ComputePassEncoder(VkCommandBuffer cmd) : cmd_(cmd) {}

    VkCommandBuffer cmd_;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
};

class CommandEncoder {
public:
    explicit CommandEncoder(VkCommandBuffer cmd) : cmd_(cmd) {}

    VkCommandBuffer handle() const { return cmd_; }  // migration escape hatch

    // Explicit neutral-state barrier (PLAN_RHI §7.3): no automatic tracking, the
    // caller states where the image was and where it goes. `discardContents`
    // keeps the source sync scope but drops the contents (oldLayout UNDEFINED),
    // for targets fully overwritten by the pass (shadow layers, HDR targets).
    void transition(VkImage image, rhi::ResourceState from, rhi::ResourceState to,
                    uint32_t baseLayer = 0, uint32_t layerCount = VK_REMAINING_ARRAY_LAYERS,
                    bool discardContents = false,
                    rhi::TextureAspect aspect = rhi::TextureAspect::Auto);

    // Compute→compute barrier for storage read/write chains (GI cascades).
    void storageBarrier();

    // Compute writes → graphics shader reads (storage buffers/images sampled or
    // fetched in vertex/fragment). WebGPU: no-op (driver-tracked), like all
    // barriers here (PLAN_RHI §7.3).
    void computeToGraphicsBarrier();

    // Compute writes → indirect draw args + vertex-stage storage reads (+ more
    // compute). The GPU-particle handoff: sim writes the indirect command and
    // the render buffers the draw consumes.
    void computeToIndirectBarrier();

    RenderPassEncoder beginRenderPass(const RenderPassDesc& desc);
    ComputePassEncoder beginComputePass() { return ComputePassEncoder(cmd_); }

    void copyBufferToBuffer(const saida::Buffer& src, const saida::Buffer& dst, uint64_t size,
                            uint64_t srcOffset = 0, uint64_t dstOffset = 0);

    // Tightly-packed buffer → texture upload (staging). The image must be in
    // CopyDst; the caller transitions around the copy (explicit sync, §7.3).
    void copyBufferToTexture(const saida::Buffer& src, VkImage dst,
                             uint32_t width, uint32_t height,
                             uint32_t mipLevel = 0, uint32_t baseLayer = 0,
                             uint32_t layerCount = 1, uint64_t srcOffset = 0);

private:
    VkCommandBuffer cmd_;
};

} // namespace saida::rhi::vulkan
