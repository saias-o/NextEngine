#pragma once

#include "rhi/Format.hpp"
#include "rhi/webgpu/WebGpu.hpp"

#include <cstdint>
#include <string>

// WebGPU backend for rhi::Texture (asset texture, Étape 16.4). Same surface as
// the Vulkan Texture: RGBA8 pixels in, sampled texture + embedded sampler out.
// Mip chain is generated on the CPU (box filter) — the web has no vkCmdBlitImage
// and a compute mip pass is not worth its weight for asset uploads.

namespace saida::rhi::webgpu {

class Device;

class Texture {
public:
    Texture(Device& device, const uint8_t* pixels, uint32_t width, uint32_t height,
            rhi::Format format = rhi::Format::RGBA8Srgb, bool generateMipmaps = true);
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    WGPUTextureView imageView() const { return view_; }
    WGPUSampler sampler() const { return sampler_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }

    void updatePixels(const uint8_t* pixels, size_t size);

private:
    void upload(const uint8_t* pixels);

    Device& device_;
    WGPUTexture texture_ = nullptr;
    WGPUTextureView view_ = nullptr;
    WGPUSampler sampler_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t mipLevels_ = 1;
};

} // namespace saida::rhi::webgpu
