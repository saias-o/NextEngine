#include "Engine.hpp"

#include <glm/gtc/matrix_transform.hpp>  // GLM_FORCE_* set globally by CMake

#include "core/Window.hpp"
#include "graphics/Buffer.hpp"
#include "graphics/Mesh.hpp"
#include "core/Paths.hpp"
#include "graphics/ImGuiLayer.hpp"
#include "graphics/Material.hpp"
#include "graphics/Pipeline.hpp"
#include "graphics/ResourceManager.hpp"
#include "graphics/Swapchain.hpp"
#include "graphics/Texture.hpp"
#include "graphics/VulkanDevice.hpp"
#include "scene/Behaviour.hpp"

#include "imgui.h"
#include "scene/LightNode.hpp"
#include "scene/MeshNode.hpp"
#include "scene/Scene.hpp"

#include <glm/gtc/quaternion.hpp>

#include <array>
#include <stdexcept>
#include <string>

namespace ne {

namespace {

constexpr uint32_t kWidth = 800;
constexpr uint32_t kHeight = 600;
constexpr int kMaxFramesInFlight = 2;

const std::string kTexturePath = assetPath("assets/textures/checker.png");

// A unit cube with per-face normals and UVs. Color is white so the fragment
// shader shows texture * lighting unmodified.
const std::vector<Vertex> kCubeVertices = {
    // +Z
    {{-0.5f, -0.5f,  0.5f}, {0, 0, 1}, {1, 1, 1}, {0, 1}}, {{ 0.5f, -0.5f,  0.5f}, {0, 0, 1}, {1, 1, 1}, {1, 1}},
    {{ 0.5f,  0.5f,  0.5f}, {0, 0, 1}, {1, 1, 1}, {1, 0}}, {{-0.5f,  0.5f,  0.5f}, {0, 0, 1}, {1, 1, 1}, {0, 0}},
    // -Z
    {{ 0.5f, -0.5f, -0.5f}, {0, 0, -1}, {1, 1, 1}, {0, 1}}, {{-0.5f, -0.5f, -0.5f}, {0, 0, -1}, {1, 1, 1}, {1, 1}},
    {{-0.5f,  0.5f, -0.5f}, {0, 0, -1}, {1, 1, 1}, {1, 0}}, {{ 0.5f,  0.5f, -0.5f}, {0, 0, -1}, {1, 1, 1}, {0, 0}},
    // +Y
    {{-0.5f,  0.5f,  0.5f}, {0, 1, 0}, {1, 1, 1}, {0, 1}}, {{ 0.5f,  0.5f,  0.5f}, {0, 1, 0}, {1, 1, 1}, {1, 1}},
    {{ 0.5f,  0.5f, -0.5f}, {0, 1, 0}, {1, 1, 1}, {1, 0}}, {{-0.5f,  0.5f, -0.5f}, {0, 1, 0}, {1, 1, 1}, {0, 0}},
    // -Y
    {{-0.5f, -0.5f, -0.5f}, {0, -1, 0}, {1, 1, 1}, {0, 1}}, {{ 0.5f, -0.5f, -0.5f}, {0, -1, 0}, {1, 1, 1}, {1, 1}},
    {{ 0.5f, -0.5f,  0.5f}, {0, -1, 0}, {1, 1, 1}, {1, 0}}, {{-0.5f, -0.5f,  0.5f}, {0, -1, 0}, {1, 1, 1}, {0, 0}},
    // +X
    {{ 0.5f, -0.5f,  0.5f}, {1, 0, 0}, {1, 1, 1}, {0, 1}}, {{ 0.5f, -0.5f, -0.5f}, {1, 0, 0}, {1, 1, 1}, {1, 1}},
    {{ 0.5f,  0.5f, -0.5f}, {1, 0, 0}, {1, 1, 1}, {1, 0}}, {{ 0.5f,  0.5f,  0.5f}, {1, 0, 0}, {1, 1, 1}, {0, 0}},
    // -X
    {{-0.5f, -0.5f, -0.5f}, {-1, 0, 0}, {1, 1, 1}, {0, 1}}, {{-0.5f, -0.5f,  0.5f}, {-1, 0, 0}, {1, 1, 1}, {1, 1}},
    {{-0.5f,  0.5f,  0.5f}, {-1, 0, 0}, {1, 1, 1}, {1, 0}}, {{-0.5f,  0.5f, -0.5f}, {-1, 0, 0}, {1, 1, 1}, {0, 0}},
};

const std::vector<uint32_t> kCubeIndices = {
     0,  1,  2,  2,  3,  0,   4,  5,  6,  6,  7,  4,
     8,  9, 10, 10, 11,  8,  12, 13, 14, 14, 15, 12,
    16, 17, 18, 18, 19, 16,  20, 21, 22, 22, 23, 20,
};

// Sample behaviour: continuously spins its node around an axis. Children inherit
// the rotation through the transform hierarchy.
class RotatorBehaviour : public Behaviour {
public:
    RotatorBehaviour(glm::vec3 axis, float degreesPerSecond)
        : axis_(glm::normalize(axis)), speed_(glm::radians(degreesPerSecond)) {}

    void onUpdate(float dt) override {
        angle_ += dt * speed_;
        node()->transform().rotation = glm::angleAxis(angle_, axis_);
    }

private:
    glm::vec3 axis_;
    float speed_;
    float angle_ = 0.0f;
};

} // namespace

Engine::Engine() {
    window_ = std::make_unique<Window>(kWidth, kHeight, "NextEngine");
    device_ = std::make_unique<VulkanDevice>(*window_);
    swapchain_ = std::make_unique<Swapchain>(*device_, *window_);
    resources_ = std::make_unique<ResourceManager>(*device_);

    createGlobalSetLayout();
    std::vector<VkDescriptorSetLayout> setLayouts = {
        globalSetLayout_, resources_->materialSetLayout()};
    pipeline_ = std::make_unique<Pipeline>(*device_, shaderPath("shader.vert.spv"),
        shaderPath("shader.frag.spv"), swapchain_->renderPass(), setLayouts,
        swapchain_->samples());

    buildScene();  // creates meshes/textures/materials via resources_

    createUniformBuffers();
    createGlobalDescriptorPool();
    createGlobalDescriptorSets();
    createCommandBuffers();
    createSyncObjects();

    imgui_ = std::make_unique<ImGuiLayer>(*device_, *window_, swapchain_->renderPass(),
        swapchain_->imageCount(), swapchain_->samples());

    // Pull back and look slightly down to see the whole orbiting hierarchy.
    camera_.position = {0.0f, 2.5f, 8.0f};
    camera_.yaw = -90.0f;
    camera_.pitch = -15.0f;
}

Engine::~Engine() {
    vkDeviceWaitIdle(device_->device());

    // renderFinished semaphores live in the Swapchain (one per image).
    for (int i = 0; i < kMaxFramesInFlight; i++) {
        vkDestroySemaphore(device_->device(), imageAvailableSemaphores_[i], nullptr);
        vkDestroyFence(device_->device(), inFlightFences_[i], nullptr);
    }
    vkDestroyDescriptorPool(device_->device(), globalPool_, nullptr);
    vkDestroyDescriptorSetLayout(device_->device(), globalSetLayout_, nullptr);
    // Remaining subsystems are torn down by their unique_ptr destructors, in
    // reverse declaration order, while device_ is still alive.
}

void Engine::run() {
    double last = glfwGetTime();
    while (!window_->shouldClose()) {
        window_->pollEvents();

        double now = glfwGetTime();
        float dt = static_cast<float>(now - last);
        last = now;

        processInput(dt);
        scene_->updateTree(dt);  // runs node behaviours

        imgui_->beginFrame();
        drawUI();
        imgui_->endFrame();  // finalize draw data even if drawFrame skips (resize)

        drawFrame();
    }
    vkDeviceWaitIdle(device_->device());
}

void Engine::buildScene() {
    scene_ = std::make_unique<Scene>();

    // Shared resources, loaded once and cached by the ResourceManager.
    // (To load the bugatti instead — untextured, no UVs — use:
    //  resources_->loadMesh(assetPath("models/bugatti/bugatti.obj")).)
    Mesh* cube = resources_->createMesh("cube", kCubeVertices, kCubeIndices);
    Texture* checker = resources_->loadTexture(kTexturePath);
    // Three materials sharing the texture but tinted differently (per-material data).
    Material* matWhite = resources_->createMaterial("white", checker, glm::vec4(1.0f));
    Material* matWarm = resources_->createMaterial("warm", checker, glm::vec4(1.0f, 0.55f, 0.4f, 1.0f));
    Material* matCool = resources_->createMaterial("cool", checker, glm::vec4(0.45f, 0.7f, 1.0f, 1.0f));

    // A small hierarchy to show transform inheritance: a central "planet" with
    // an orbiting "moon", which itself has an orbiting "sub-moon". They share the
    // cube mesh but each has its own material. The orbiting motion comes entirely
    // from RotatorBehaviours on the parents — children inherit it.
    Node* planet = scene_->createChild<MeshNode>("planet", cube, matWhite);
    planet->addBehaviour<RotatorBehaviour>(glm::vec3(0, 1, 0), 40.0f);

    Node* moon = planet->createChild<MeshNode>("moon", cube, matWarm);
    moon->transform().position = {2.0f, 0.0f, 0.0f};
    moon->transform().scale = glm::vec3(0.45f);
    moon->addBehaviour<RotatorBehaviour>(glm::vec3(0, 1, 0), 90.0f);

    Node* subMoon = moon->createChild<MeshNode>("sub-moon", cube, matCool);
    subMoon->transform().position = {2.0f, 0.0f, 0.0f};
    subMoon->transform().scale = glm::vec3(0.5f);

    // A directional "sun" (warm, slightly overhead).
    sun_ = scene_->createChild<LightNode>("sun", LightType::Directional);
    sun_->direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));
    sun_->color = {1.0f, 0.95f, 0.85f};
    sun_->intensity = 1.0f;

    // A point light orbiting the scene: a rotating pivot with the light offset
    // from it, so the RotatorBehaviour makes it circle the objects.
    Node* lightPivot = scene_->createChild<Node>("light-pivot");
    lightPivot->addBehaviour<RotatorBehaviour>(glm::vec3(0, 1, 0), 70.0f);
    lamp_ = lightPivot->createChild<LightNode>("lamp", LightType::Point);
    lamp_->transform().position = {3.5f, 1.5f, 0.0f};
    lamp_->color = {0.3f, 0.5f, 1.0f};
    lamp_->intensity = 4.0f;
    lamp_->range = 8.0f;
}

void Engine::drawUI() {
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("NextEngine");

    ImGui::Text("%.1f FPS (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
    ImGui::Text("Camera  %.1f  %.1f  %.1f", camera_.position.x, camera_.position.y, camera_.position.z);
    ImGui::TextDisabled("TAB: free/lock cursor | WASD+mouse: fly");

    if (ImGui::CollapsingHeader("Sun (directional)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("sun");
        ImGui::SliderFloat("intensity", &sun_->intensity, 0.0f, 4.0f);
        ImGui::ColorEdit3("color", &sun_->color.x);
        ImGui::SliderFloat3("direction", &sun_->direction.x, -1.0f, 1.0f);
        ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Lamp (point)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("lamp");
        ImGui::SliderFloat("intensity", &lamp_->intensity, 0.0f, 12.0f);
        ImGui::SliderFloat("range", &lamp_->range, 0.5f, 20.0f);
        ImGui::ColorEdit3("color", &lamp_->color.x);
        ImGui::PopID();
    }

    ImGui::End();
}

void Engine::processInput(float dt) {
    if (window_->keyDown(GLFW_KEY_ESCAPE)) {
        window_->close();
        return;
    }

    // TAB toggles between fly-camera (cursor captured) and UI (cursor free).
    bool tabDown = window_->keyDown(GLFW_KEY_TAB);
    if (tabDown && !tabWasDown_)
        window_->setCursorCaptured(!window_->cursorCaptured());
    tabWasDown_ = tabDown;

    // Always drain the accumulated mouse delta so it can't pile up while in UI.
    double dx, dy;
    window_->consumeMouseDelta(dx, dy);

    // When the cursor is free (UI mode), ImGui owns the mouse; don't fly.
    if (!window_->cursorCaptured())
        return;

    // Mouse look.
    constexpr float sensitivity = 0.1f;
    camera_.rotate(static_cast<float>(dx) * sensitivity,
                   -static_cast<float>(dy) * sensitivity);  // screen Y is down

    // Keyboard movement (WASD horizontal, Space/Ctrl up/down). Shift to sprint.
    float speed = (window_->keyDown(GLFW_KEY_LEFT_SHIFT) ? 6.0f : 2.5f) * dt;
    glm::vec3 front = camera_.front();
    glm::vec3 right = camera_.right();
    if (window_->keyDown(GLFW_KEY_W)) camera_.position += front * speed;
    if (window_->keyDown(GLFW_KEY_S)) camera_.position -= front * speed;
    if (window_->keyDown(GLFW_KEY_D)) camera_.position += right * speed;
    if (window_->keyDown(GLFW_KEY_A)) camera_.position -= right * speed;
    if (window_->keyDown(GLFW_KEY_SPACE)) camera_.position += glm::vec3(0, 1, 0) * speed;
    if (window_->keyDown(GLFW_KEY_LEFT_CONTROL)) camera_.position -= glm::vec3(0, 1, 0) * speed;
}

void Engine::createGlobalSetLayout() {
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

    if (vkCreateDescriptorSetLayout(device_->device(), &ci, nullptr, &globalSetLayout_) != VK_SUCCESS)
        throw std::runtime_error("failed to create global descriptor set layout");
}

void Engine::createUniformBuffers() {
    uniformBuffers_.reserve(kMaxFramesInFlight);
    lightingBuffers_.reserve(kMaxFramesInFlight);
    for (int i = 0; i < kMaxFramesInFlight; i++) {
        uniformBuffers_.push_back(std::make_unique<Buffer>(*device_, sizeof(UniformBufferObject),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, MemoryUsage::HostVisible));
        lightingBuffers_.push_back(std::make_unique<Buffer>(*device_, sizeof(LightingUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, MemoryUsage::HostVisible));
    }
}

void Engine::createGlobalDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = static_cast<uint32_t>(kMaxFramesInFlight) * 2;  // camera + lighting

    VkDescriptorPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.poolSizeCount = 1;
    ci.pPoolSizes = &poolSize;
    ci.maxSets = static_cast<uint32_t>(kMaxFramesInFlight);

    if (vkCreateDescriptorPool(device_->device(), &ci, nullptr, &globalPool_) != VK_SUCCESS)
        throw std::runtime_error("failed to create global descriptor pool");
}

void Engine::createGlobalDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(kMaxFramesInFlight, globalSetLayout_);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = globalPool_;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(kMaxFramesInFlight);
    allocInfo.pSetLayouts = layouts.data();

    globalSets_.resize(kMaxFramesInFlight);
    if (vkAllocateDescriptorSets(device_->device(), &allocInfo, globalSets_.data()) != VK_SUCCESS)
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
        if (vkCreateSemaphore(device_->device(), &semCI, nullptr, &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
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

    VkPipelineLayout layout = pipeline_->layout();

    // Set 0: per-frame global data (camera + lighting), shared by every object.
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
        0, 1, &globalSets_[currentFrame_], 0, nullptr);

    // Walk the scene: per mesh node, bind its material (set 1) and push its
    // world matrix, then draw.
    scene_->traverse([&](Node& node, const glm::mat4& world) {
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

    imgui_->renderDrawData(cmd);  // UI on top, same render pass

    vkCmdEndRenderPass(cmd);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        throw std::runtime_error("failed to record command buffer");
}

void Engine::gatherLights(LightingUBO& ubo) {
    ubo.ambient = glm::vec4(0.04f, 0.04f, 0.05f, 0.0f);
    ubo.cameraPos = glm::vec4(camera_.position, 1.0f);
    ubo.dirDirection = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);  // intensity 0 = no dir light yet
    ubo.dirColor = glm::vec4(0.0f);
    ubo.counts = glm::ivec4(0);
    int pointCount = 0;

    scene_->traverse([&](Node& node, const glm::mat4& world) {
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

void Engine::updateUniformBuffer(uint32_t frame) {
    camera_.setPerspective(glm::radians(45.0f), swapchain_->aspectRatio(), 0.1f, 100.0f);

    UniformBufferObject ubo{};
    ubo.view = camera_.view();
    ubo.proj = camera_.projection();
    uniformBuffers_[frame]->write(&ubo, sizeof(ubo));

    LightingUBO lighting{};
    gatherLights(lighting);
    lightingBuffers_[frame]->write(&lighting, sizeof(lighting));
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
    VkSemaphore signalSemaphores[] = {swapchain_->renderFinishedSemaphore(imageIndex)};

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
