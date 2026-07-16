#pragma once

#include "rhi/Rhi.hpp"

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Rml {
class Context;
class ElementDocument;
}

namespace saida {

class ResourceManager;
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
#ifdef SAIDA_RHI_WEBGPU
    rhi::BindGroup* textureGroup = nullptr;
#endif
};

class UIRenderer {
public:
    UIRenderer(rhi::Device& device, ResourceManager& resources, rhi::Format colorFormat);
    ~UIRenderer();
    UIRenderer(const UIRenderer&) = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;

    void gatherUI(Scene& scene, glm::vec2 viewportSize = glm::vec2(0.0f));
    void updateAsyncTextures(rhi::CommandEncoder& encoder);
    void recordCommands(rhi::RenderPassEncoder& rp, uint32_t width, uint32_t height,
                        glm::vec2 viewportOffset = glm::vec2(0.0f),
                        glm::vec2 viewportSize = glm::vec2(0.0f));

private:
    void traverseUI(UINode* node);
#ifdef SAIDA_RHI_WEBGPU
    void gatherLegacyWebUI(UICanvasNode& canvas, glm::vec2 viewportSize);
#endif

    rhi::Device& device_;
    ResourceManager& resources_;
    
    std::unique_ptr<rhi::Pipeline> pipeline_;

    std::vector<UIDrawCmd> drawCmds_;
    std::vector<WebCanvasNode*> webNodesToUpdate_;
    bool loggedWebCanvasGather_ = false;

#ifdef SAIDA_RHI_WEBGPU
    std::unique_ptr<rhi::BindGroupLayout> textureLayout_;
    std::unique_ptr<rhi::Texture> legacyUiTexture_;
    std::unique_ptr<rhi::BindGroup> legacyUiTextureGroup_;
    std::string legacyContextName_;
    Rml::Context* legacyContext_ = nullptr;
    Rml::ElementDocument* legacyDocument_ = nullptr;
    uint64_t legacyUiHash_ = 0;
    uint32_t legacyUiWidth_ = 0;
    uint32_t legacyUiHeight_ = 0;
    bool loggedLegacyUiRaster_ = false;
#endif
};

} // namespace saida
