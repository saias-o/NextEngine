#include "graphics/UIRenderer.hpp"
#include "core/Profiler.hpp"
#include "rhi/vulkan/CommandEncoder.hpp"
#include "rhi/vulkan/Format.hpp"
#include "graphics/VulkanDevice.hpp"
#include "graphics/ResourceManager.hpp"
#include "graphics/Pipeline.hpp"
#include "scene/Scene.hpp"
#include "scene/UICanvasNode.hpp"
#include "scene/UIColorNode.hpp"
#include "scene/UIImageNode.hpp"
#include "scene/UIButtonNode.hpp"
#include "scene/UIToggleNode.hpp"
#include "scene/WebCanvasNode.hpp"
#include "core/Log.hpp"
#include "core/Paths.hpp"

#include <algorithm>
#include <cmath>

namespace saida {

struct UIPushConstants {
    glm::vec2 position;
    glm::vec2 size;
    glm::vec2 screenSize;
    uint32_t textureId;
    uint32_t hasTexture;
    glm::vec4 color;
    glm::vec2 corners[4];
    uint32_t useCorners;
    uint32_t _pad;
};

UIRenderer::UIRenderer(VulkanDevice& device, ResourceManager& resources, rhi::Format colorFormat)
    : device_(device), resources_(resources) {
    Pipeline::Desc desc;
    desc.vertPath = shaderPath("ui.vert.spv");
    desc.fragPath = shaderPath("ui.frag.spv");
    desc.colorFormats = {colorFormat};
    desc.bindGroupLayouts = {resources_.globalMaterialSetLayout()};  // raw bindless set
    desc.vertexInput = false;
    desc.depthTest = false;
    desc.depthWrite = false;
    desc.cullMode = rhi::CullMode::None;
    desc.blendMode = rhi::BlendMode::Alpha;
    desc.topology = rhi::Topology::TriangleStrip;
    desc.pushConstantSize = sizeof(UIPushConstants);
    pipeline_ = std::make_unique<Pipeline>(device_, desc);

    Log::info("UIRenderer: pipeline created");
}

UIRenderer::~UIRenderer() = default;

void UIRenderer::gatherUI(Scene& scene, glm::vec2 viewportSize) {
    SAIDA_PROFILE_FUNCTION();
    drawCmds_.clear();
    webNodesToUpdate_.clear();
    
    UICanvasNode* canvas = scene.uiCanvas();
    for (WebCanvasNode* wcn : scene.webCanvases()) {
        if (!wcn || !wcn->isActiveInHierarchy()) continue;
        if (wcn->mode() == WebCanvasNode::Mode::ScreenSpace) {
            glm::vec2 pos = wcn->screenPosition();
            glm::vec2 size = wcn->screenSize();
            bool fillsViewport = viewportSize.x > 0.0f && viewportSize.y > 0.0f &&
                std::abs(pos.x) < 0.5f && std::abs(pos.y) < 0.5f &&
                std::abs(size.x - static_cast<float>(wcn->width())) < 0.5f &&
                std::abs(size.y - static_cast<float>(wcn->height())) < 0.5f;
            if (fillsViewport) {
                uint32_t targetWidth = std::max(1u, static_cast<uint32_t>(std::round(viewportSize.x)));
                uint32_t targetHeight = std::max(1u, static_cast<uint32_t>(std::round(viewportSize.y)));
                if (wcn->width() != targetWidth || wcn->height() != targetHeight) {
                    wcn->resize(targetWidth, targetHeight);
                }
            }
        }
        webNodesToUpdate_.push_back(wcn);
        if (wcn->mode() == WebCanvasNode::Mode::ScreenSpace && wcn->texture()) {
            UIDrawCmd cmd{};
            cmd.position = wcn->screenPosition();
            cmd.size = wcn->screenSize();
            cmd.color = glm::vec4(1.0f);
            cmd.textureId = resources_.ensureBindlessTextureIndex(wcn->texture());
            cmd.hasTexture = 1;
            cmd.sortOrder = wcn->renderOrder();
            drawCmds_.push_back(cmd);
        }
    }

    std::stable_sort(drawCmds_.begin(), drawCmds_.end(), [](const UIDrawCmd& a, const UIDrawCmd& b) {
        return a.sortOrder < b.sortOrder;
    });

    if (!loggedWebCanvasGather_ && !webNodesToUpdate_.empty()) {
        Log::info("UIRenderer: gathered ", webNodesToUpdate_.size(),
                  " WebCanvas node(s), ", drawCmds_.size(), " screen draw command(s)");
        loggedWebCanvasGather_ = true;
    }

    if (canvas && canvas->isActiveInHierarchy()) {
        for (auto& child : canvas->children()) {
            if (UINode* uiChild = dynamic_cast<UINode*>(child.get())) {
                traverseUI(uiChild);
            }
        }
    }

    SAIDA_PROFILE_COUNTER("UI/WebCanvases", webNodesToUpdate_.size());
    SAIDA_PROFILE_COUNTER("UI/DrawCommands", drawCmds_.size());
}

void UIRenderer::traverseUI(UINode* node) {
    if (!node->isActiveInHierarchy()) return;

    float globalX = node->globalX();
    float globalY = node->globalY();

    float drawX = globalX - (node->width() * node->pivotX());
    float drawY = globalY - (node->height() * node->pivotY());

    UIDrawCmd cmd{};
    cmd.position = {drawX, drawY};
    cmd.size = {node->width(), node->height()};
    cmd.color = glm::vec4(1.0f);
    cmd.textureId = 0;
    cmd.hasTexture = 0;

    bool draw = false;

    if (auto colorNode = dynamic_cast<UIColorNode*>(node)) {
        cmd.color = colorNode->color();
        draw = true;
    } else if (auto imageNode = dynamic_cast<UIImageNode*>(node)) {
        Texture* tex = resources_.getTexture(imageNode->texture());
        if (tex) {
            cmd.textureId = resources_.ensureBindlessTextureIndex(tex);
            cmd.hasTexture = 1;
            draw = true;
        }
    } else if (auto btnNode = dynamic_cast<UIButtonNode*>(node)) {
        cmd.color = btnNode->currentColor();
        draw = true;
    } else if (auto toggleNode = dynamic_cast<UIToggleNode*>(node)) {
        cmd.color = toggleNode->currentColor();
        draw = true;
    }

    if (draw) {
        drawCmds_.push_back(cmd);
    }

    for (auto& child : node->children()) {
        if (UINode* uiChild = dynamic_cast<UINode*>(child.get())) {
            traverseUI(uiChild);
        }
    }
}

void UIRenderer::updateAsyncTextures(rhi::CommandEncoder& encoder) {
    SAIDA_PROFILE_FUNCTION();
    for (auto* wcn : webNodesToUpdate_) {
        wcn->updateTextureIfNeededAsync(encoder);
    }
}

void UIRenderer::recordCommands(rhi::RenderPassEncoder& rp, uint32_t width, uint32_t height,
                                glm::vec2 viewportOffset, glm::vec2 viewportSize) {
    SAIDA_PROFILE_FUNCTION();
    if (drawCmds_.empty()) return;
    if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) {
        viewportSize = {static_cast<float>(width), static_cast<float>(height)};
    }

    rp.setPipeline(*pipeline_);
    rp.setViewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));

    const int32_t scissorX = static_cast<int32_t>(std::max(0.0f, std::floor(viewportOffset.x)));
    const int32_t scissorY = static_cast<int32_t>(std::max(0.0f, std::floor(viewportOffset.y)));
    rp.setScissor(scissorX, scissorY,
        std::min(width - static_cast<uint32_t>(scissorX),
                 static_cast<uint32_t>(std::max(1.0f, std::round(viewportSize.x)))),
        std::min(height - static_cast<uint32_t>(scissorY),
                 static_cast<uint32_t>(std::max(1.0f, std::round(viewportSize.y)))));

    // Set 0 matches ui.frag and contains the bindless texture table.
    rp.setBindGroup(0, resources_.globalMaterialSet());

    UIPushConstants push{};
    push.screenSize = {static_cast<float>(width), static_cast<float>(height)};

    for (const auto& drawCmd : drawCmds_) {
        push.position = viewportOffset + drawCmd.position;
        push.size = drawCmd.size;
        push.color = drawCmd.color;
        push.textureId = drawCmd.textureId;
        push.hasTexture = drawCmd.hasTexture;
        push.useCorners = drawCmd.useCorners;
        for (int i = 0; i < 4; ++i) push.corners[i] = drawCmd.corners[i];

        rp.setPushConstants(&push, sizeof(UIPushConstants));
        rp.draw(4);
    }
}

} // namespace saida
