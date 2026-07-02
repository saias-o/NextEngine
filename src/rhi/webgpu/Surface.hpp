#pragma once

#include "rhi/Format.hpp"
#include "rhi/webgpu/CommandEncoder.hpp"
#include "rhi/webgpu/Handles.hpp"
#include "rhi/webgpu/RenderTexture.hpp"
#include "rhi/webgpu/WebGpu.hpp"

#include <memory>

// WebGPU backend for rhi::Surface (Étape 16.4). Presentation to the HTML
// canvas, mirroring the Swapchain API shape: waitFrame/acquire/submitAndPresent.
// On the web all frame sync is driver-managed and presentation is implicit
// (the browser composites after the RAF callback returns) — the calls keep the
// same signatures so the frame code reads identically on both backends.

namespace saida::rhi::webgpu {

class Device;

class Surface {
public:
    // `canvasSelector` is the CSS selector of the target canvas ("#canvas").
    Surface(Device& device, const char* canvasSelector, uint32_t width, uint32_t height);
    ~Surface();
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    Extent2D extent() const { return {width_, height_}; }
    rhi::Format colorFormat() const { return rhi::Format::BGRA8Unorm; }
    rhi::Format depthFormat() const { return rhi::Format::Depth32Float; }
    uint32_t samples() const { return 1; }

    // ---- presentation API (mirror of the Vulkan Swapchain) ----
    void waitFrame(uint32_t) const {}
    // Grabs the canvas texture for this frame. Returns false if the surface
    // has no texture (tab hidden etc.) — skip the frame.
    bool acquire(uint32_t frame, uint32_t& imageIndex);
    // Submits the finished command buffer; present is implicit.
    bool submitAndPresent(WGPUCommandBuffer cmd, uint32_t frame, uint32_t imageIndex);
    CommandEncoder beginFrameCommands(uint32_t frame);
    bool submitAndPresent(CommandEncoder& encoder, uint32_t frame, uint32_t imageIndex);

    // The current frame's canvas view (valid between acquire and submit).
    WGPUTextureView currentView() const { return currentView_; }
    WGPUTexture image(uint32_t) const { return currentTexture_; }
    WGPUTextureView imageView(uint32_t) const { return currentView_; }
    WGPUTexture depthImage() const { return depth_ ? depth_->image() : nullptr; }
    WGPUTextureView depthView() const { return depth_ ? depth_->view() : nullptr; }

private:
    Device& device_;
    WGPUSurface surface_ = nullptr;
    WGPUTexture currentTexture_ = nullptr;
    WGPUTextureView currentView_ = nullptr;
    std::unique_ptr<RenderTexture> depth_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};

} // namespace saida::rhi::webgpu
