#pragma once

#include <vulkan/vulkan.h>

namespace ne {

class VulkanDevice;
class Window;

// Wraps Dear ImGui + its GLFW/Vulkan backends. Owns the ImGui context and a
// descriptor pool. Per frame: beginFrame() -> (build UI) -> endFrame() ->
// renderDrawData(cmd) inside the render pass.
class ImGuiLayer {
public:
    ImGuiLayer(VulkanDevice& device, Window& window, VkRenderPass renderPass,
               uint32_t imageCount, VkSampleCountFlagBits samples);
    ~ImGuiLayer();
    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void beginFrame();                       // start a new ImGui frame
    void endFrame();                         // finalize draw data (ImGui::Render)
    void renderDrawData(VkCommandBuffer cmd); // record into an active render pass

private:
    VulkanDevice& device_;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
};

} // namespace ne
