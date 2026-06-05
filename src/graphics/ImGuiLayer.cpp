#include "graphics/ImGuiLayer.hpp"

#include "core/Window.hpp"
#include "graphics/VulkanDevice.hpp"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include <stdexcept>

namespace ne {

ImGuiLayer::ImGuiLayer(VulkanDevice& device, Window& window, VkFormat colorFormat,
                       uint32_t imageCount, VkSampleCountFlagBits samples)
    : device_(device), colorFormat_(colorFormat) {
    // ImGui's backend allocates one combined-image-sampler descriptor per texture
    // (the font atlas, plus any user textures). A small free-able pool is plenty.
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1000;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device_.device(), &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS)
        throw std::runtime_error("failed to create ImGui descriptor pool");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // don't litter the cwd with imgui.ini
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;  // enable docking
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(window.handle(), /*install_callbacks=*/true);

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = device_.instance();
    info.PhysicalDevice = device_.physicalDevice();
    info.Device = device_.device();
    info.QueueFamily = device_.findQueueFamilies().graphicsFamily.value();
    info.Queue = device_.graphicsQueue();
    info.DescriptorPool = descriptorPool_;
    info.MinImageCount = 2;
    info.ImageCount = imageCount;
    info.PipelineCache = device_.pipelineCache();
    info.PipelineInfoMain.MSAASamples = samples;

    // Dynamic rendering: ImGui builds its pipeline against the swap-chain color
    // format instead of a render pass. colorFormat_ outlives Init for the backend.
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat_;
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
