#pragma once

#include <vulkan/vulkan.h>

namespace saida {

class VulkanDevice;
class Window;

// Wraps Dear ImGui + its GLFW/Vulkan backends. Owns the ImGui context and a
// descriptor pool. Per frame: beginFrame() -> (build UI) -> endFrame() ->
// renderDrawData(cmd) inside the render pass.
class ImGuiLayer {
public:
    ImGuiLayer(VulkanDevice& device, Window& window, VkFormat colorFormat,
               uint32_t imageCount, VkSampleCountFlagBits samples);
    ~ImGuiLayer();
    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void beginFrame();                       // start a new ImGui frame
    void endFrame();                         // finalize draw data (ImGui::Render)
    void renderDrawData(VkCommandBuffer cmd); // record inside an active dynamic-rendering pass

private:
    VulkanDevice& device_;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;  // kept alive for the backend's pipeline info
};

} // namespace saida
