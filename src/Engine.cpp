#include "Engine.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_transform.hpp>

#include "core/Window.hpp"
#include "graphics/Buffer.hpp"
#include "graphics/Mesh.hpp"
#include "graphics/Pipeline.hpp"
#include "graphics/Swapchain.hpp"
#include "graphics/Texture.hpp"
#include "graphics/VulkanDevice.hpp"

#include <array>
#include <chrono>
#include <stdexcept>
#include <string>

namespace ne {

namespace {

constexpr uint32_t kWidth = 800;
constexpr uint32_t kHeight = 600;
constexpr int kMaxFramesInFlight = 2;

const std::string kModelPath = std::string(NE_PROJECT_ROOT) + "/models/bugatti/bugatti.obj";
const std::string kTexturePath = std::string(NE_PROJECT_ROOT) + "/assets/textures/checker.png";

// A unit cube with per-face UVs, used to demonstrate texturing. Color is white
// so the fragment shader shows the texture unmodified.
const std::vector<Vertex> kCubeVertices = {
    // +Z
    {{-0.5f, -0.5f,  0.5f}, {1, 1, 1}, {0, 1}}, {{ 0.5f, -0.5f,  0.5f}, {1, 1, 1}, {1, 1}},
    {{ 0.5f,  0.5f,  0.5f}, {1, 1, 1}, {1, 0}}, {{-0.5f,  0.5f,  0.5f}, {1, 1, 1}, {0, 0}},
    // -Z
    {{ 0.5f, -0.5f, -0.5f}, {1, 1, 1}, {0, 1}}, {{-0.5f, -0.5f, -0.5f}, {1, 1, 1}, {1, 1}},
    {{-0.5f,  0.5f, -0.5f}, {1, 1, 1}, {1, 0}}, {{ 0.5f,  0.5f, -0.5f}, {1, 1, 1}, {0, 0}},
    // +Y
    {{-0.5f,  0.5f,  0.5f}, {1, 1, 1}, {0, 1}}, {{ 0.5f,  0.5f,  0.5f}, {1, 1, 1}, {1, 1}},
    {{ 0.5f,  0.5f, -0.5f}, {1, 1, 1}, {1, 0}}, {{-0.5f,  0.5f, -0.5f}, {1, 1, 1}, {0, 0}},
    // -Y
    {{-0.5f, -0.5f, -0.5f}, {1, 1, 1}, {0, 1}}, {{ 0.5f, -0.5f, -0.5f}, {1, 1, 1}, {1, 1}},
    {{ 0.5f, -0.5f,  0.5f}, {1, 1, 1}, {1, 0}}, {{-0.5f, -0.5f,  0.5f}, {1, 1, 1}, {0, 0}},
    // +X
    {{ 0.5f, -0.5f,  0.5f}, {1, 1, 1}, {0, 1}}, {{ 0.5f, -0.5f, -0.5f}, {1, 1, 1}, {1, 1}},
    {{ 0.5f,  0.5f, -0.5f}, {1, 1, 1}, {1, 0}}, {{ 0.5f,  0.5f,  0.5f}, {1, 1, 1}, {0, 0}},
    // -X
    {{-0.5f, -0.5f, -0.5f}, {1, 1, 1}, {0, 1}}, {{-0.5f, -0.5f,  0.5f}, {1, 1, 1}, {1, 1}},
    {{-0.5f,  0.5f,  0.5f}, {1, 1, 1}, {1, 0}}, {{-0.5f,  0.5f, -0.5f}, {1, 1, 1}, {0, 0}},
};

const std::vector<uint32_t> kCubeIndices = {
     0,  1,  2,  2,  3,  0,   4,  5,  6,  6,  7,  4,
     8,  9, 10, 10, 11,  8,  12, 13, 14, 14, 15, 12,
    16, 17, 18, 18, 19, 16,  20, 21, 22, 22, 23, 20,
};

} // namespace

Engine::Engine() {
    window_ = std::make_unique<Window>(kWidth, kHeight, "NextEngine - Vulkan Cube");
    device_ = std::make_unique<VulkanDevice>(*window_);
    swapchain_ = std::make_unique<Swapchain>(*device_, *window_);

    createDescriptorSetLayout();
    pipeline_ = std::make_unique<Pipeline>(*device_, "shaders/shader.vert.spv",
        "shaders/shader.frag.spv", swapchain_->renderPass(), descriptorSetLayout_);
    // Textured cube demo. To render the bugatti instead (untextured — it has no
    // UVs), swap this for: mesh_ = Mesh::fromObjFile(*device_, kModelPath);
    mesh_ = std::make_unique<Mesh>(*device_, kCubeVertices, kCubeIndices);
    texture_ = std::make_unique<Texture>(*device_, kTexturePath);

    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createCommandBuffers();
    createSyncObjects();
}

Engine::~Engine() {
    vkDeviceWaitIdle(device_->device());

    for (int i = 0; i < kMaxFramesInFlight; i++) {
        vkDestroySemaphore(device_->device(), renderFinishedSemaphores_[i], nullptr);
        vkDestroySemaphore(device_->device(), imageAvailableSemaphores_[i], nullptr);
        vkDestroyFence(device_->device(), inFlightFences_[i], nullptr);
    }
    vkDestroyDescriptorPool(device_->device(), descriptorPool_, nullptr);
    vkDestroyDescriptorSetLayout(device_->device(), descriptorSetLayout_, nullptr);
    // Remaining subsystems are torn down by their unique_ptr destructors, in
    // reverse declaration order, while device_ is still alive.
}

void Engine::run() {
    while (!window_->shouldClose()) {
        window_->pollEvents();
        drawFrame();
    }
    vkDeviceWaitIdle(device_->device());
}

void Engine::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 1;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uboBinding, samplerBinding};

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = static_cast<uint32_t>(bindings.size());
    ci.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device_->device(), &ci, nullptr, &descriptorSetLayout_) != VK_SUCCESS)
        throw std::runtime_error("failed to create descriptor set layout");
}

void Engine::createUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    uniformBuffers_.reserve(kMaxFramesInFlight);
    for (int i = 0; i < kMaxFramesInFlight; i++) {
        uniformBuffers_.push_back(std::make_unique<Buffer>(*device_, bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, MemoryUsage::HostVisible));
    }
}

void Engine::createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(kMaxFramesInFlight);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(kMaxFramesInFlight);

    VkDescriptorPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    ci.pPoolSizes = poolSizes.data();
    ci.maxSets = static_cast<uint32_t>(kMaxFramesInFlight);

    if (vkCreateDescriptorPool(device_->device(), &ci, nullptr, &descriptorPool_) != VK_SUCCESS)
        throw std::runtime_error("failed to create descriptor pool");
}

void Engine::createDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(kMaxFramesInFlight, descriptorSetLayout_);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(kMaxFramesInFlight);
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets_.resize(kMaxFramesInFlight);
    if (vkAllocateDescriptorSets(device_->device(), &allocInfo, descriptorSets_.data()) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate descriptor sets");

    for (int i = 0; i < kMaxFramesInFlight; i++) {
        VkDescriptorBufferInfo bufInfo{};
        bufInfo.buffer = uniformBuffers_[i]->handle();
        bufInfo.offset = 0;
        bufInfo.range = sizeof(UniformBufferObject);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = texture_->imageView();
        imageInfo.sampler = texture_->sampler();

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSets_[i];
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &bufInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptorSets_[i];
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device_->device(),
            static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void Engine::createCommandBuffers() {
    commandBuffers_.resize(kMaxFramesInFlight);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = device_->commandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
    if (vkAllocateCommandBuffers(device_->device(), &allocInfo, commandBuffers_.data()) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate command buffers");
}

void Engine::createSyncObjects() {
    imageAvailableSemaphores_.resize(kMaxFramesInFlight);
    renderFinishedSemaphores_.resize(kMaxFramesInFlight);
    inFlightFences_.resize(kMaxFramesInFlight);

    VkSemaphoreCreateInfo semCI{};
    semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceCI{};
    fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < kMaxFramesInFlight; i++) {
        if (vkCreateSemaphore(device_->device(), &semCI, nullptr, &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device_->device(), &semCI, nullptr, &renderFinishedSemaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(device_->device(), &fenceCI, nullptr, &inFlightFences_[i]) != VK_SUCCESS)
            throw std::runtime_error("failed to create sync objects");
    }
}

void Engine::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("failed to begin recording command buffer");

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.1f, 0.1f, 0.1f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkExtent2D extent = swapchain_->extent();

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = swapchain_->renderPass();
    rpBegin.framebuffer = swapchain_->framebuffer(imageIndex);
    rpBegin.renderArea.offset = {0, 0};
    rpBegin.renderArea.extent = extent;
    rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
    rpBegin.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    pipeline_->bind(cmd);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    mesh_->bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->layout(),
        0, 1, &descriptorSets_[currentFrame_], 0, nullptr);
    mesh_->draw(cmd);

    vkCmdEndRenderPass(cmd);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        throw std::runtime_error("failed to record command buffer");
}

void Engine::updateUniformBuffer(uint32_t frame) {
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float>(currentTime - startTime).count();

    UniformBufferObject ubo{};
    // Turntable rotation around the vertical axis.
    ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(45.0f),
                            glm::vec3(0.0f, 1.0f, 0.0f));
    ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f),
                           glm::vec3(0.0f, 0.0f, 0.0f),
                           glm::vec3(0.0f, 1.0f, 0.0f));
    ubo.proj = glm::perspective(glm::radians(45.0f), swapchain_->aspectRatio(), 0.1f, 10.0f);
    ubo.proj[1][1] *= -1;  // Flip Y for Vulkan

    uniformBuffers_[frame]->write(&ubo, sizeof(ubo));
}

void Engine::drawFrame() {
    vkWaitForFences(device_->device(), 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device_->device(), swapchain_->handle(), UINT64_MAX,
        imageAvailableSemaphores_[currentFrame_], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain_->recreate();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image");
    }

    vkResetFences(device_->device(), 1, &inFlightFences_[currentFrame_]);

    updateUniformBuffer(currentFrame_);

    vkResetCommandBuffer(commandBuffers_[currentFrame_], 0);
    recordCommandBuffer(commandBuffers_[currentFrame_], imageIndex);

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores_[currentFrame_]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores_[currentFrame_]};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers_[currentFrame_];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(device_->graphicsQueue(), 1, &submitInfo, inFlightFences_[currentFrame_]) != VK_SUCCESS)
        throw std::runtime_error("failed to submit draw command buffer");

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapChains[] = {swapchain_->handle()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(device_->presentQueue(), &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || window_->wasResized()) {
        window_->resetResizedFlag();
        swapchain_->recreate();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image");
    }

    currentFrame_ = (currentFrame_ + 1) % kMaxFramesInFlight;
}

} // namespace ne
