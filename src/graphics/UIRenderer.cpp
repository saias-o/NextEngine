#include "graphics/UIRenderer.hpp"
#include "graphics/VulkanDevice.hpp"
#include "graphics/ResourceManager.hpp"
#include "graphics/Pipeline.hpp"
#include "core/Camera.hpp"
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

namespace ne {

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

UIRenderer::UIRenderer(VulkanDevice& device, ResourceManager& resources, VkFormat colorFormat)
    : device_(device), resources_(resources) {
    
    std::vector<VkDescriptorSetLayout> setLayouts = {
        resources_.globalMaterialSetLayout()
    };

    std::vector<VkFormat> colorFormats = {colorFormat};
    
    pipeline_ = std::make_unique<Pipeline>(
        device_, 
        shaderPath("ui.vert.spv"), 
        shaderPath("ui.frag.spv"), 
        colorFormats, 
        VK_FORMAT_UNDEFINED, 
        setLayouts,
        VK_SAMPLE_COUNT_1_BIT, 
        false, // useVertexInput
        false, // useDepth
        sizeof(UIPushConstants), // pushConstantSize
        false, // depthWrite
        VK_COMPARE_OP_ALWAYS, // depthCompare
        VK_CULL_MODE_NONE, // cullMode
        true, // useBlending
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP // topology
    );

    // Get the generated pipeline layout directly from Pipeline
    pipelineLayout_ = pipeline_->layout();

    Log::info("UIRenderer: Pipeline cree");
}

UIRenderer::~UIRenderer() {
    // pipelineLayout_ is owned by Pipeline, so we don't destroy it here.
}

void UIRenderer::gatherUI(Scene& scene, const Camera* camera, glm::vec2 viewportSize) {
    (void)camera;
    drawCmds_.clear();
    webNodesToUpdate_.clear();
    
    UICanvasNode* canvas = scene.uiCanvas();
    scene.traverse([&](Node& node, const glm::mat4&) {
        auto* wcn = dynamic_cast<WebCanvasNode*>(&node);
        if (!wcn) return;
        if (!wcn->isActiveInHierarchy()) return;
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
            if (wcn->texture()->bindlessIndex() == ~0u) {
                wcn->texture()->setBindlessIndex(resources_.getBindlessTextureIndex(wcn->texture()));
            }
            cmd.textureId = wcn->texture()->bindlessIndex();
            cmd.hasTexture = 1;
            cmd.sortOrder = wcn->renderOrder();
            drawCmds_.push_back(cmd);
        }
    });

    std::stable_sort(drawCmds_.begin(), drawCmds_.end(), [](const UIDrawCmd& a, const UIDrawCmd& b) {
        return a.sortOrder < b.sortOrder;
    });

    if (!loggedWebCanvasGather_ && !webNodesToUpdate_.empty()) {
        Log::info("UIRenderer: gathered ", webNodesToUpdate_.size(),
                  " WebCanvas node(s), ", drawCmds_.size(), " screen draw command(s)");
        loggedWebCanvasGather_ = true;
    }

    if (canvas && canvas->isActiveInHierarchy()) {
        // Traverse recursivement les UINodes
        for (auto& child : canvas->children()) {
            if (UINode* uiChild = dynamic_cast<UINode*>(child.get())) {
                traverseUI(uiChild);
            }
        }
    }
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
            cmd.textureId = tex->bindlessIndex();
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

void UIRenderer::updateAsyncTextures(VkCommandBuffer cmd) {
    for (auto* wcn : webNodesToUpdate_) {
        wcn->updateTextureIfNeededAsync(cmd);
    }
}

void UIRenderer::recordCommands(VkCommandBuffer cmd, uint32_t width, uint32_t height,
                                glm::vec2 viewportOffset, glm::vec2 viewportSize) {
    if (drawCmds_.empty()) return;
    if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) {
        viewportSize = {static_cast<float>(width), static_cast<float>(height)};
    }

    pipeline_->bind(cmd);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {
        static_cast<int32_t>(std::max(0.0f, std::floor(viewportOffset.x))),
        static_cast<int32_t>(std::max(0.0f, std::floor(viewportOffset.y)))
    };
    scissor.extent = {
        std::min(width - static_cast<uint32_t>(scissor.offset.x),
                 static_cast<uint32_t>(std::max(1.0f, std::round(viewportSize.x)))),
        std::min(height - static_cast<uint32_t>(scissor.offset.y),
                 static_cast<uint32_t>(std::max(1.0f, std::round(viewportSize.y))))
    };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind le Global Set pour les textures bindless (au set 1, comme défini dans le shader ui.frag layout(set=1))
    // Attention : on a créé notre pipeline avec 1 seul layout (le GlobalSetLayout).
    // Donc il s'attend à le trouver au set 0 dans le pipelineLayout, MAIS dans ui.frag on a dit `layout(set = 1)`.
    // Il faut que ça matche. Le plus simple est de le binder au set 1, et donc de fournir un set 0 vide dans le layout.
    // Ou bien changer le shader pour set=0.
    // On va binder au même index que dans le layout.
    // Wait, on a mis layoutCount=1 dans pipelineLayout, donc c'est le set 0 !
    // Je corrigerai le shader via replace si besoin.
    VkDescriptorSet globalSet = resources_.globalMaterialSet();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &globalSet, 0, nullptr);

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

        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(UIPushConstants), &push);

        vkCmdDraw(cmd, 4, 1, 0, 0);
    }
}

} // namespace ne
