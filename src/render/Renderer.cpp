#include "render/Renderer.hpp"

#include "core/Camera.hpp"
#include "core/Paths.hpp"
#include "core/Window.hpp"
#include "graphics/Buffer.hpp"
#include "graphics/ImGuiLayer.hpp"
#include "graphics/Material.hpp"
#include "graphics/Mesh.hpp"
#include "graphics/Pipeline.hpp"
#include "graphics/Swapchain.hpp"
#include "graphics/VulkanDevice.hpp"
#include "scene/LightNode.hpp"
#include "scene/Node.hpp"
#include "scene/Scene.hpp"

#include <array>
#include <stdexcept>

namespace ne {

namespace {
constexpr int kMaxFramesInFlight = 2;
}

Renderer::Renderer(VulkanDevice& device, Swapchain& swapchain, Window& window,
                   VkDescriptorSetLayout materialSetLayout, ImGuiLayer& imgui)
    : device_(device), swapchain_(swapchain), window_(window), imgui_(imgui) {
    createGlobalSetLayout();
    createPipeline(materialSetLayout);
    createUniformBuffers();
    createGlobalDescriptorPool();
    createGlobalDescriptorSets();
    createCommandBuffers();
    createSyncObjects();
}

Renderer::~Renderer() {
    vkDeviceWaitIdle(device_.device());
    for (int i = 0; i < kMaxFramesInFlight; i++) {
        vkDestroySemaphore(device_.device(), imageAvailableSemaphores_[i], nullptr);
        vkDestroyFence(device_.device(), inFlightFences_[i], nullptr);
    }
    vkDestroyDescriptorPool(device_.device(), globalPool_, nullptr);
    vkDestroyDescriptorSetLayout(device_.device(), globalSetLayout_, nullptr);
    // pipeline_ and the buffers are torn down by their destructors.
}

void Renderer::createGlobalSetLayout() {
    // Set 0: camera UBO (vertex) + lighting UBO (fragment), shared by all draws.
    VkDescriptorSetLayoutBinding cameraBinding{};
    cameraBinding.binding = 0;
    cameraBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    cameraBinding.descriptorCount = 1;
    cameraBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding lightingBinding{};
    lightingBinding.binding = 1;
    lightingBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    lightingBinding.descriptorCount = 1;
    lightingBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {cameraBinding, lightingBinding};

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = static_cast<uint32_t>(bindings.size());
    ci.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device_.device(), &ci, nullptr, &globalSetLayout_) != VK_SUCCESS)
        throw std::runtime_error("failed to create global descriptor set layout");
}

void Renderer::createPipeline(VkDescriptorSetLayout materialSetLayout) {
    std::vector<VkDescriptorSetLayout> setLayouts = {globalSetLayout_, materialSetLayout};
    pipeline_ = std::make_unique<Pipeline>(device_, shaderPath("shader.vert.spv"),
        shaderPath("shader.frag.spv"), swapchain_.renderPass(), setLayouts,
        swapchain_.samples());
}

void Renderer::createUniformBuffers() {
    uniformBuffers_.reserve(kMaxFramesInFlight);
    lightingBuffers_.reserve(kMaxFramesInFlight);
    for (int i = 0; i < kMaxFramesInFlight; i++) {
        uniformBuffers_.push_back(std::make_unique<Buffer>(device_, sizeof(UniformBufferObject),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, MemoryUsage::HostVisible));
        lightingBuffers_.push_back(std::make_unique<Buffer>(device_, sizeof(LightingUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, MemoryUsage::HostVisible));
    }
}

void Renderer::createGlobalDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = static_cast<uint32_t>(kMaxFramesInFlight) * 2;  // camera + lighting

    VkDescriptorPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.poolSizeCount = 1;
    ci.pPoolSizes = &poolSize;
    ci.maxSets = static_cast<uint32_t>(kMaxFramesInFlight);
    if (vkCreateDescriptorPool(device_.device(), &ci, nullptr, &globalPool_) != VK_SUCCESS)
        throw std::runtime_error("failed to create global descriptor pool");
}

void Renderer::createGlobalDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(kMaxFramesInFlight, globalSetLayout_);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = globalPool_;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(kMaxFramesInFlight);
    allocInfo.pSetLayouts = layouts.data();

    globalSets_.resize(kMaxFramesInFlight);
    if (vkAllocateDescriptorSets(device_.device(), &allocInfo, globalSets_.data()) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate global descriptor sets");

    for (int i = 0; i < kMaxFramesInFlight; i++) {
        VkDescriptorBufferInfo cameraInfo{};
        cameraInfo.buffer = uniformBuffers_[i]->handle();
        cameraInfo.offset = 0;
        cameraInfo.range = sizeof(UniformBufferObject);

        VkDescriptorBufferInfo lightInfo{};
        lightInfo.buffer = lightingBuffers_[i]->handle();
        lightInfo.offset = 0;
        lightInfo.range = sizeof(LightingUBO);

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = globalSets_[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &cameraInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = globalSets_[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &lightInfo;

        vkUpdateDescriptorSets(device_.device(),
            static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void Renderer::createCommandBuffers() {
    commandBuffers_.resize(kMaxFramesInFlight);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = device_.commandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
    if (vkAllocateCommandBuffers(device_.device(), &allocInfo, commandBuffers_.data()) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate command buffers");
}

void Renderer::createSyncObjects() {
    // imageAvailable semaphores and fences are per frame-in-flight; the
    // renderFinished semaphores are per swap-chain image and owned by Swapchain.
    imageAvailableSemaphores_.resize(kMaxFramesInFlight);
    inFlightFences_.resize(kMaxFramesInFlight);

    VkSemaphoreCreateInfo semCI{};
    semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceCI{};
    fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < kMaxFramesInFlight; i++) {
        if (vkCreateSemaphore(device_.device(), &semCI, nullptr, &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(device_.device(), &fenceCI, nullptr, &inFlightFences_[i]) != VK_SUCCESS)
            throw std::runtime_error("failed to create sync objects");
    }
}

void Renderer::gatherLights(LightingUBO& ubo, Scene& scene, const Camera& camera) {
    ubo.ambient = glm::vec4(0.04f, 0.04f, 0.05f, 0.0f);
    ubo.cameraPos = glm::vec4(camera.position, 1.0f);
    ubo.dirDirection = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);  // intensity 0 = no dir light yet
    ubo.dirColor = glm::vec4(0.0f);
    int pointCount = 0;

    scene.traverse([&](Node& node, const glm::mat4& world) {
        LightNode* light = node.asLight();
        if (!light) return;

        if (light->type == LightType::Directional) {
            glm::vec3 dir = glm::normalize(glm::mat3(world) * light->direction);
            ubo.dirDirection = glm::vec4(dir, light->intensity);
            ubo.dirColor = glm::vec4(light->color, 1.0f);
        } else if (pointCount < 4) {
            glm::vec3 pos = glm::vec3(world[3]);
            ubo.pointLights[pointCount].position = glm::vec4(pos, light->range);
            ubo.pointLights[pointCount].color = glm::vec4(light->color, light->intensity);
            ++pointCount;
        }
    });

    ubo.counts = glm::ivec4(pointCount, /*mode=realtime*/ 0, 0, 0);
}

void Renderer::updateUniformBuffer(uint32_t frame, Scene& scene, Camera& camera) {
    camera.setPerspective(glm::radians(45.0f), swapchain_.aspectRatio(), 0.1f, 100.0f);

    UniformBufferObject ubo{};
    ubo.view = camera.view();
    ubo.proj = camera.projection();
    uniformBuffers_[frame]->write(&ubo, sizeof(ubo));

    LightingUBO lighting{};
    gatherLights(lighting, scene, camera);
    lightingBuffers_[frame]->write(&lighting, sizeof(lighting));
}

void Renderer::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex, Scene& scene) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("failed to begin recording command buffer");

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.1f, 0.1f, 0.1f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkExtent2D extent = swapchain_.extent();

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = swapchain_.renderPass();
    rpBegin.framebuffer = swapchain_.framebuffer(imageIndex);
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

    VkPipelineLayout layout = pipeline_->layout();

    // Set 0: per-frame global data (camera + lighting), shared by every object.
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
        0, 1, &globalSets_[currentFrame_], 0, nullptr);

    // Walk the scene: per mesh node, bind its material (set 1) and push its
    // world matrix, then draw.
    scene.traverse([&](Node& node, const glm::mat4& world) {
        Mesh* m = node.mesh();
        Material* mat = node.material();
        if (!m || !mat) return;
        VkDescriptorSet materialSet = mat->descriptorSet();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
            1, 1, &materialSet, 0, nullptr);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(glm::mat4), &world);
        m->bind(cmd);
        m->draw(cmd);
    });

    imgui_.renderDrawData(cmd);  // UI on top, same render pass

    vkCmdEndRenderPass(cmd);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        throw std::runtime_error("failed to record command buffer");
}

void Renderer::drawFrame(Scene& scene, Camera& camera) {
    vkWaitForFences(device_.device(), 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device_.device(), swapchain_.handle(), UINT64_MAX,
        imageAvailableSemaphores_[currentFrame_], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain_.recreate();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image");
    }

    vkResetFences(device_.device(), 1, &inFlightFences_[currentFrame_]);

    updateUniformBuffer(currentFrame_, scene, camera);

    vkResetCommandBuffer(commandBuffers_[currentFrame_], 0);
    recordCommandBuffer(commandBuffers_[currentFrame_], imageIndex, scene);

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores_[currentFrame_]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signalSemaphores[] = {swapchain_.renderFinishedSemaphore(imageIndex)};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers_[currentFrame_];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(device_.graphicsQueue(), 1, &submitInfo, inFlightFences_[currentFrame_]) != VK_SUCCESS)
        throw std::runtime_error("failed to submit draw command buffer");

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapChains[] = {swapchain_.handle()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(device_.presentQueue(), &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || window_.wasResized()) {
        window_.resetResizedFlag();
        swapchain_.recreate();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image");
    }

    currentFrame_ = (currentFrame_ + 1) % kMaxFramesInFlight;
}

} // namespace ne
