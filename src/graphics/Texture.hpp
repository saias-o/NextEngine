#pragma once

#include "graphics/VmaFwd.hpp"

#include <cstdint>
#include <string>

namespace saida {

class VulkanDevice;
class Buffer;

// Loads an image file (via stb_image) into a sampled, device-local texture:
// owns the VkImage, its view and a sampler. RAII.
class Texture {
public:
    Texture(VulkanDevice& device, const std::string& path, bool srgb = true);
    Texture(VulkanDevice& device, const uint8_t* pixels, uint32_t width, uint32_t height, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB, bool generateMipmaps = true);
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    VkImageView imageView() const { return imageView_; }
    VkSampler sampler() const { return sampler_; }
    
    // Updates the texture content dynamically (e.g. for UI)
    void updatePixels(const uint8_t* pixels, size_t size);
    
    // Updates the texture asynchronously during a command buffer recording
    void updatePixelsAsync(VkCommandBuffer cmd, Buffer& stagingBuffer, uint32_t width, uint32_t height);
    
    void setBindlessIndex(uint32_t idx) { bindlessIndex_ = idx; }
    uint32_t bindlessIndex() const { return bindlessIndex_; }

private:
    void createSampler();
    void generateMipmaps();
    VkImageView createImageView(VkFormat format, VkImageAspectFlags aspect);

    VulkanDevice& device_;
    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkImageView imageView_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t mipLevels_ = 1;
    uint64_t trackedBytes_ = 0;
    std::string trackedCategory_;
    
    uint32_t bindlessIndex_ = ~0u;
};

} // namespace saida
