#include "render/features/DebugLinesFeature.hpp"

#include "core/Paths.hpp"
#include "graphics/Mesh.hpp"
#include "scene/Scene.hpp"
#include "scene/Node.hpp"
#include "scene/animation/Animator.hpp"

#include <algorithm>

namespace saida {

void DebugLinesFeature::createPipelines(const RenderContext& ctx) {
    if (ctx.stereo()) return;  // editor-only aid; not drawn in XR

    std::vector<VkDescriptorSetLayout> setLayouts = {ctx.globalSetLayout};
    std::vector<VkFormat> colorFormats = {ctx.colorFormat};
    // Lines into the HDR target; depth test off so the skeleton shows through the
    // mesh. Reuses set 0 (camera) + the standard Vertex layout.
    pipeline_ = std::make_unique<Pipeline>(ctx.device,
        shaderPath("debug_line.vert.spv"), shaderPath("debug_line.frag.spv"),
        colorFormats, ctx.depthFormat, setLayouts, ctx.samples,
        true, false, 0, false, VK_COMPARE_OP_LESS, VK_CULL_MODE_NONE, false,
        VK_PRIMITIVE_TOPOLOGY_LINE_LIST);

    buffers_.reserve(ctx.framesInFlight);
    for (uint32_t i = 0; i < ctx.framesInFlight; ++i)
        buffers_.push_back(std::make_unique<Buffer>(ctx.device,
            kMaxVerts * sizeof(Vertex),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, MemoryUsage::HostVisible));
}

void DebugLinesFeature::record(const FrameContext& fc) {
    if (fc.stereo || !pipeline_) return;
    if (!fc.scene.settings().showSkeletons) return;

    // Build bone segments (parent→child) in world space from every Animator.
    std::vector<Vertex> verts;
    const glm::vec3 boneColor(0.1f, 1.0f, 0.2f);
    fc.scene.traverse([&](Node& node, const glm::mat4& world) {
        Animator* anim = node.getBehaviour<Animator>();
        if (!anim || !anim->rig()) return;
        const GlobalPose& gp = anim->globalPose();
        const auto& bones = anim->rig()->bones();
        const size_t n = std::min(bones.size(), gp.globalMatrices.size());
        for (size_t i = 0; i < n && verts.size() < kMaxVerts; ++i) {
            int parent = bones[i].parentIndex;
            if (parent < 0 || static_cast<size_t>(parent) >= n) continue;
            Vertex a{}; a.pos = glm::vec3(world * gp.globalMatrices[parent][3]); a.color = boneColor;
            Vertex b{}; b.pos = glm::vec3(world * gp.globalMatrices[i][3]);      b.color = boneColor;
            verts.push_back(a);
            verts.push_back(b);
        }
    });
    if (verts.empty()) return;

    const uint32_t count = std::min<uint32_t>(static_cast<uint32_t>(verts.size()), kMaxVerts);
    buffers_[fc.frameIndex]->write(verts.data(), count * sizeof(Vertex));

    pipeline_->bind(fc.cmd);
    vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->layout(),
        0, 1, &fc.globalSet, 0, nullptr);
    VkBuffer vb = buffers_[fc.frameIndex]->handle();
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(fc.cmd, 0, 1, &vb, &offset);
    vkCmdDraw(fc.cmd, count, 1, 0, 0);
}

} // namespace saida
