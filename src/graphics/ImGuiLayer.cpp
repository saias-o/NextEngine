#include "graphics/ImGuiLayer.hpp"

#include "core/Window.hpp"
#include "graphics/VulkanDevice.hpp"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include <stdexcept>

namespace ne {

ImGuiLayer::ImGuiLayer(VulkanDevice& device, Window& window, VkRenderPass renderPass,
                       uint32_t imageCount, VkSampleCountFlagBits samples)
    : device_(device) {
    // ImGui's backend allocates one combined-image-sampler descriptor per texture
    // (the font atlas, plus any user textures). A small free-able pool is plenty.
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 16;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 16;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device_.device(), &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS)
        throw std::runtime_error("failed to create ImGui descriptor pool");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;  // don't litter the cwd with imgui.ini
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(window.handle(), /*install_callbacks=*/true);

    ImGui_ImplVulkan_InitInfo info{};
    info.Instance = device_.instance();
    info.PhysicalDevice = device_.physicalDevice();
    info.Device = device_.device();
    info.QueueFamily = device_.findQueueFamilies().graphicsFamily.value();
    info.Queue = device_.graphicsQueue();
    info.DescriptorPool = descriptorPool_;
    info.RenderPass = renderPass;
    info.MinImageCount = 2;
    info.ImageCount = imageCount;
    info.MSAASamples = samples;
    info.PipelineCache = device_.pipelineCache();
    info.Subpass = 0;
    ImGui_ImplVulkan_Init(&info);
    // Font atlas is uploaded lazily on the first frame; no manual step needed.
}

ImGuiLayer::~ImGuiLayer() {
    // Caller guarantees the device is idle before destruction.
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(device_.device(), descriptorPool_, nullptr);
}

void ImGuiLayer::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::endFrame() {
    ImGui::Render();
}

void ImGuiLayer::renderDrawData(VkCommandBuffer cmd) {
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

} // namespace ne
