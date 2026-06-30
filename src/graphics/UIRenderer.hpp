#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace saida {

class VulkanDevice;
class ResourceManager;
class Pipeline;
class Scene;
class UINode;
class WebCanvasNode;
class UICanvasNode;

struct UIDrawCmd {
    glm::vec2 position;
    glm::vec2 size;
    glm::vec2 corners[4];
    glm::vec4 color;
    uint32_t textureId;
    uint32_t hasTexture;
    uint32_t useCorners = 0;
    int sortOrder = 0;
};

class UIRenderer {
public:
    UIRenderer(VulkanDevice& device, ResourceManager& resources, VkFormat colorFormat);
    ~UIRenderer();
    UIRenderer(const UIRenderer&) = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;

    void gatherUI(Scene& scene, glm::vec2 viewportSize = glm::vec2(0.0f));
    void updateAsyncTextures(VkCommandBuffer cmd);
    void recordCommands(VkCommandBuffer cmd, uint32_t width, uint32_t height,
                        glm::vec2 viewportOffset = glm::vec2(0.0f),
                        glm::vec2 viewportSize = glm::vec2(0.0f));

private:
    void traverseUI(UINode* node);

    VulkanDevice& device_;
    ResourceManager& resources_;
    
    std::unique_ptr<Pipeline> pipeline_;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;

    std::vector<UIDrawCmd> drawCmds_;
    std::vector<WebCanvasNode*> webNodesToUpdate_;
    bool loggedWebCanvasGather_ = false;
};

} // namespace saida
