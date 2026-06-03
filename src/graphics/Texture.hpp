#pragma once

#include "graphics/VmaFwd.hpp"
#include <imgui.h>

#include <string>

namespace ne {

class VulkanDevice;

// Loads an image file (via stb_image) into a sampled, device-local texture:
// owns the VkImage, its view and a sampler. RAII.
class Texture {
public:
    Texture(VulkanDevice& device, const std::string& path);
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    VkImageView imageView() const { return imageView_; }
    VkSampler sampler() const { return sampler_; }

    ImTextureID getImGuiTextureID();

private:
    void createSampler();

    VulkanDevice& device_;
    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkImageView imageView_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    ImTextureID imGuiTexId_ = 0;
};

} // namespace ne
