#include "graphics/UIRenderer.hpp"
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

namespace ne {

struct UIPushConstants {
    glm::vec2 position;
    glm::vec2 size;
    glm::vec2 screenSize;
    uint32_t textureId;
    uint32_t hasTexture;
    glm::vec4 color;
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

void UIRenderer::gatherUI(Scene& scene) {
    drawCmds_.clear();
    webNodesToUpdate_.clear();
    
    // Cherche un UICanvasNode et les WebCanvasNode
    UICanvasNode* canvas = nullptr;
    scene.traverse([&](Node& n, const glm::mat4&) {
        if (!canvas && dynamic_cast<UICanvasNode*>(&n)) {
            canvas = static_cast<UICanvasNode*>(&n);
        }
        
        // Render ScreenSpace WebCanvasNodes
        if (auto* wcn = dynamic_cast<WebCanvasNode*>(&n)) {
            if (wcn->isActiveInHierarchy()) {
                webNodesToUpdate_.push_back(wcn);
                if (wcn->mode() == WebCanvasNode::Mode::ScreenSpace && wcn->texture()) {
                    UIDrawCmd cmd{};
                    cmd.position = {0.0f, 0.0f}; // Drawn at top-left
                    cmd.size = {static_cast<float>(wcn->width()), static_cast<float>(wcn->height())};
                    cmd.color = glm::vec4(1.0f);
                    if (wcn->texture()->bindlessIndex() == ~0u) {
                        wcn->texture()->setBindlessIndex(resources_.getBindlessTextureIndex(wcn->texture()));
                    }
                    cmd.textureId = wcn->texture()->bindlessIndex();
                    cmd.hasTexture = 1;
                    drawCmds_.push_back(cmd);
                }
            }
        }
    });

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

void UIRenderer::recordCommands(VkCommandBuffer cmd, uint32_t width, uint32_t height) {
    if (drawCmds_.empty()) return;

    pipeline_->bind(cmd);

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
        push.position = drawCmd.position;
        push.size = drawCmd.size;
        push.color = drawCmd.color;
        push.textureId = drawCmd.textureId;
        push.hasTexture = drawCmd.hasTexture;

        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(UIPushConstants), &push);

        vkCmdDraw(cmd, 4, 1, 0, 0);
    }
}

} // namespace ne
