#pragma once

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <unordered_map>
#include "graphics/Texture.hpp"

namespace ne {

class ImGuiTextureCache {
public:
    ImTextureID get(Texture* tex) {
        if (!tex) return 0;
        auto it = cache_.find(tex);
        if (it != cache_.end()) return it->second;

        // Lazy register via ImGui_ImplVulkan_AddTexture
        ImTextureID id = (ImTextureID)ImGui_ImplVulkan_AddTexture(
            tex->sampler(), tex->imageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        cache_[tex] = id;
        return id;
    }

    ~ImGuiTextureCache() {
        clear();
    }

    void clear() {
        for (auto& pair : cache_) {
            if (pair.second) {
                ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)pair.second);
            }
        }
        cache_.clear();
    }

private:
    std::unordered_map<Texture*, ImTextureID> cache_;
};

} // namespace ne
