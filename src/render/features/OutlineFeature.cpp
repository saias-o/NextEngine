#include "render/features/OutlineFeature.hpp"

#include "core/Paths.hpp"
#include "graphics/Mesh.hpp"
#include "scene/MeshNode.hpp"

#include <algorithm>
#include <vector>

namespace saida {

void OutlineFeature::createPipelines(const RenderContext& ctx) {
    std::vector<VkDescriptorSetLayout> setLayouts = {ctx.globalSetLayout};
    std::vector<VkFormat> colorFormats = {ctx.colorFormat};
    const char* vert = ctx.stereo() ? "multiview.outline.vert.spv" : "outline.vert.spv";

    pipeline_ = std::make_unique<Pipeline>(ctx.device,
        shaderPath(vert), shaderPath("outline.frag.spv"),
        colorFormats, ctx.depthFormat, setLayouts, ctx.samples,
        true, true, sizeof(Push), false, VK_COMPARE_OP_LESS_OR_EQUAL,
        VK_CULL_MODE_FRONT_BIT, true, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        ctx.viewMask);
}

void OutlineFeature::record(const FrameContext& fc) {
    if (!pipeline_ || !fc.draws || fc.drawCount == 0) return;

    const float viewportW = static_cast<float>(std::max(1u, fc.extent.width));
    const float viewportH = static_cast<float>(std::max(1u, fc.extent.height));
    bool bound = false;

    for (uint32_t i = 0; i < fc.drawCount; ++i) {
        const SceneDraw& draw = fc.draws[i];
        MeshNode* node = draw.node;
        if (!node || !draw.mesh || !node->outlineEnabled()) continue;

        const float width = node->outlineWidth();
        const glm::vec4 color = node->outlineColor();
        if (width <= 0.0f || color.a <= 0.0f) continue;

        if (!bound) {
            pipeline_->bind(fc.cmd);
            vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipeline_->layout(), 0, 1, &fc.globalSet, 0, nullptr);
            bound = true;
        }

        Push pc{};
        pc.model = draw.world;
        pc.color = color;
        pc.params = glm::vec4(width, static_cast<float>(draw.boneOffset),
                              viewportW, viewportH);
        const Aabb& bounds = draw.mesh->bounds();
        pc.localCenter = glm::vec4(bounds.center(), 0.0f);
        pc.localHalfExtent = glm::vec4(glm::max(bounds.extent() * 0.5f, glm::vec3(1e-4f)), 0.0f);
        vkCmdPushConstants(fc.cmd, pipeline_->layout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(Push), &pc);

        draw.mesh->bind(fc.cmd);
        draw.mesh->draw(fc.cmd);
    }
}

} // namespace saida
