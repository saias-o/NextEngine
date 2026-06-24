#include "render/features/WaterFeature.hpp"

#include "core/Paths.hpp"
#include "scene/Scene.hpp"
#include "scene/Node.hpp"
#include "scene/WaterNode.hpp"

#include <vector>

namespace ne {

void WaterFeature::createPipelines(const RenderContext& ctx) {
    // Reuses set 0 (camera + lighting + environment); look/feel via push constants.
    // Procedural grid (no vertex input), depth-tested + depth-writing (opaque), and
    // two-sided so the surface shows from above and below.
    std::vector<VkDescriptorSetLayout> setLayouts = {ctx.globalSetLayout};
    std::vector<VkFormat> colorFormats = {ctx.colorFormat};
    const char* vert = ctx.stereo() ? "multiview.water.vert.spv" : "water.vert.spv";
    pipeline_ = std::make_unique<Pipeline>(ctx.device,
        shaderPath(vert), shaderPath("water.frag.spv"),
        colorFormats, ctx.depthFormat, setLayouts, ctx.samples,
        false, true, sizeof(Push), true, VK_COMPARE_OP_LESS, VK_CULL_MODE_NONE,
        false, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, ctx.viewMask);
}

void WaterFeature::record(const FrameContext& fc) {
    std::vector<WaterNode*> waters;
    fc.scene.traverse([&](Node& n, const glm::mat4&) {
        if (auto* w = dynamic_cast<WaterNode*>(&n))
            if (n.isActiveInHierarchy()) waters.push_back(w);
    });
    if (waters.empty()) return;

    pipeline_->bind(fc.cmd);
    vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->layout(),
        0, 1, &fc.globalSet, 0, nullptr);

    for (WaterNode* w : waters) {
        glm::vec3 c = glm::vec3(w->worldTransform()[3]);
        Push pc{};
        pc.area = glm::vec4(c.x, c.y, c.z, w->size);
        pc.deep = glm::vec4(w->deepColor, w->roughness);
        pc.shallow = glm::vec4(w->foamColor, w->reflectivity);
        pc.waveA = glm::vec4(w->amplitude, w->wavelength, w->waveSpeed, w->choppiness);
        pc.detail1 = glm::vec4(w->detailScale, w->detailSpeed, w->detailStrength, w->detailAngle);
        pc.detail2 = glm::vec4(w->detail2Scale, w->detail2Speed, w->detail2Strength, w->detail2Angle);
        pc.look = glm::vec4(w->fresnelPower, w->specularPower, w->specularIntensity, w->foamThreshold);
        pc.misc = glm::vec4(fc.time, w->warpAmount, w->detailFadeDistance, w->foamIntensity);
        vkCmdPushConstants(fc.cmd, pipeline_->layout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(Push), &pc);
        vkCmdDraw(fc.cmd, kGridRes * kGridRes * 6, 1, 0, 0);
    }
}

} // namespace ne
