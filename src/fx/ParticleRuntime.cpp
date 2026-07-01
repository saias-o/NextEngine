#include "fx/ParticleRuntime.hpp"

#include "core/Paths.hpp"
#include "graphics/Buffer.hpp"
#include "graphics/ComputePipeline.hpp"
#include "graphics/VulkanDevice.hpp"

#include <array>
#include <algorithm>
#include <stdexcept>

namespace saida {

ParticleRuntime::ParticleRuntime(VulkanDevice& device, const Desc& desc)
    : device_(device), desc_(desc) {
    desc_.framesInFlight = std::max(1u, desc_.framesInFlight);
    desc_.maxParticles = std::max(1u, desc_.maxParticles);
    desc_.maxEmitters = std::max(1u, desc_.maxEmitters);
    createRenderResources();
    createComputeResources();
}

ParticleRuntime::~ParticleRuntime() = default;

uint32_t ParticleRuntime::frameIndex(uint32_t frame) const {
    return std::min(frame, desc_.framesInFlight - 1);
}

VkDescriptorSet ParticleRuntime::renderSet(uint32_t parity) const {
    return renderSets_[std::min<uint32_t>(parity, 1u)]->handle();
}

VkBuffer ParticleRuntime::indirectBuffer() const {
    return indirectBuffer_->handle();
}

void ParticleRuntime::reset() {
    initialized_ = false;
    aliveReadParity_ = 0;
}

ParticleRuntime::GpuEmitter* ParticleRuntime::mappedEmitters(uint32_t frame) const {
    return static_cast<GpuEmitter*>(emitterBuffers_[frameIndex(frame)]->mapped());
}

void ParticleRuntime::flushEmitters(uint32_t frame, uint32_t count) {
    if (count == 0) return;
    const uint32_t clamped = std::min(count, desc_.maxEmitters);
    emitterBuffers_[frameIndex(frame)]->flush(sizeof(GpuEmitter) * clamped);
}

uint32_t ParticleRuntime::recordCompute(VkCommandBuffer cmd, uint32_t frame,
                                        uint32_t emitterCount, uint32_t emitCount,
                                        float dt, float time) {
    if (!cmd) return aliveReadParity_;

    if (!initialized_) {
        recordInit(cmd);
        initialized_ = true;
    }

    const uint32_t readParity = aliveReadParity_;
    const uint32_t writeParity = 1u - readParity;
    const uint32_t clampedEmitters = std::min(emitterCount, desc_.maxEmitters);
    const uint32_t clampedEmitCount = std::min(emitCount, desc_.maxParticles);
    ComputePush push{desc_.maxParticles, clampedEmitters, clampedEmitCount, dt, time};
    VkDescriptorSet set = computeSet(frame, readParity);

    preparePipeline_->bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, preparePipeline_->layout(),
        0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd, preparePipeline_->layout(), VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(push), &push);
    vkCmdDispatch(cmd, 1, 1, 1);

    recordComputeBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);

    simPipeline_->bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, simPipeline_->layout(),
        0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd, simPipeline_->layout(), VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(push), &push);
    vkCmdDispatch(cmd, ComputePipeline::groupCount(desc_.maxParticles, 64), 1, 1);

    recordComputeBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);

    if (clampedEmitCount > 0 && clampedEmitters > 0) {
        emitPipeline_->bind(cmd);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, emitPipeline_->layout(),
            0, 1, &set, 0, nullptr);
        vkCmdPushConstants(cmd, emitPipeline_->layout(), VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(push), &push);
        vkCmdDispatch(cmd, ComputePipeline::groupCount(clampedEmitCount, 64), 1, 1);

        recordComputeBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
    }

    finalizePipeline_->bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, finalizePipeline_->layout(),
        0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd, finalizePipeline_->layout(), VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(push), &push);
    vkCmdDispatch(cmd, 1, 1, 1);

    recordComputeBarrier(cmd,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
            VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
            VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
        VK_ACCESS_2_SHADER_READ_BIT |
            VK_ACCESS_2_SHADER_WRITE_BIT |
            VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);

    aliveReadParity_ = writeParity;
    return writeParity;
}

void ParticleRuntime::createRenderResources() {
    renderSetLayout_ = std::make_unique<rhi::BindGroupLayout>(device_,
        std::vector<rhi::BindGroupLayoutEntry>{
            {0, rhi::BindingType::StorageBuffer, rhi::ShaderStages::Vertex},
            {1, rhi::BindingType::StorageBuffer, rhi::ShaderStages::Vertex},
        });
    renderSets_.resize(2);
}

void ParticleRuntime::createComputeResources() {
    std::vector<rhi::BindGroupLayoutEntry> computeEntries;
    for (uint32_t i = 0; i < 7; ++i)
        computeEntries.push_back({i, rhi::BindingType::StorageBuffer, rhi::ShaderStages::Compute});
    computeSetLayout_ = std::make_unique<rhi::BindGroupLayout>(device_, computeEntries);

    emitterBuffers_.resize(desc_.framesInFlight);
    computeSets_.resize(desc_.framesInFlight * 2);

    const VkDeviceSize particleSize = sizeof(SimParticle) * desc_.maxParticles;
    const VkDeviceSize indexSize = sizeof(uint32_t) * desc_.maxParticles;
    const VkDeviceSize counterSize = sizeof(GpuCounters);
    const VkDeviceSize emitterSize = sizeof(GpuEmitter) * desc_.maxEmitters;
    simParticleBuffer_ = std::make_unique<Buffer>(device_, particleSize,
        rhi::BufferUsage::Storage, MemoryUsage::GpuOnly);
    for (auto& alive : aliveIndexBuffers_) {
        alive = std::make_unique<Buffer>(device_, indexSize,
            rhi::BufferUsage::Storage, MemoryUsage::GpuOnly);
    }
    deadIndexBuffer_ = std::make_unique<Buffer>(device_, indexSize,
        rhi::BufferUsage::Storage, MemoryUsage::GpuOnly);
    counterBuffer_ = std::make_unique<Buffer>(device_, counterSize,
        rhi::BufferUsage::Storage, MemoryUsage::GpuOnly);
    indirectBuffer_ = std::make_unique<Buffer>(device_, sizeof(VkDrawIndirectCommand),
        rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect,
        MemoryUsage::GpuOnly);

    for (uint32_t i = 0; i < desc_.framesInFlight; ++i) {
        emitterBuffers_[i] = std::make_unique<Buffer>(device_, emitterSize,
            rhi::BufferUsage::Storage, MemoryUsage::HostVisible);

        for (uint32_t parity = 0; parity < 2; ++parity) {
            const uint32_t setIndex = i * 2 + parity;
            const uint32_t writeParity = 1u - parity;

            std::vector<rhi::BindGroupEntry> entries(7);
            entries[0] = {0, simParticleBuffer_.get(), 0, particleSize};
            entries[1] = {1, aliveIndexBuffers_[parity].get(), 0, indexSize};
            entries[2] = {2, aliveIndexBuffers_[writeParity].get(), 0, indexSize};
            entries[3] = {3, deadIndexBuffer_.get(), 0, indexSize};
            entries[4] = {4, counterBuffer_.get(), 0, counterSize};
            entries[5] = {5, emitterBuffers_[i].get(), 0, emitterSize};
            entries[6] = {6, indirectBuffer_.get(), 0, sizeof(VkDrawIndirectCommand)};
            computeSets_[setIndex] = std::make_unique<rhi::BindGroup>(*computeSetLayout_, entries);
        }
    }

    const VkDeviceSize particleSizeRender = sizeof(SimParticle) * desc_.maxParticles;
    const VkDeviceSize indexSizeRender = sizeof(uint32_t) * desc_.maxParticles;
    for (uint32_t parity = 0; parity < 2; ++parity) {
        std::vector<rhi::BindGroupEntry> entries(2);
        entries[0] = {0, simParticleBuffer_.get(), 0, particleSizeRender};
        entries[1] = {1, aliveIndexBuffers_[parity].get(), 0, indexSizeRender};
        renderSets_[parity] = std::make_unique<rhi::BindGroup>(*renderSetLayout_, entries);
    }

    std::vector<VkDescriptorSetLayout> setLayouts = {computeSetLayout_->handle()};
    initPipeline_ = std::make_unique<ComputePipeline>(device_, shaderPath("particle_init.comp.spv"),
        setLayouts, sizeof(ComputePush));
    preparePipeline_ = std::make_unique<ComputePipeline>(device_, shaderPath("particle_prepare.comp.spv"),
        setLayouts, sizeof(ComputePush));
    emitPipeline_ = std::make_unique<ComputePipeline>(device_, shaderPath("particle_emit.comp.spv"),
        setLayouts, sizeof(ComputePush));
    simPipeline_ = std::make_unique<ComputePipeline>(device_, shaderPath("particle_sim.comp.spv"),
        setLayouts, sizeof(ComputePush));
    finalizePipeline_ = std::make_unique<ComputePipeline>(device_, shaderPath("particle_finalize.comp.spv"),
        setLayouts, sizeof(ComputePush));
}

VkDescriptorSet ParticleRuntime::computeSet(uint32_t frame, uint32_t readParity) const {
    const uint32_t f = frameIndex(frame);
    return computeSets_[f * 2 + std::min<uint32_t>(readParity, 1u)]->handle();
}

void ParticleRuntime::recordInit(VkCommandBuffer cmd) const {
    VkDescriptorSet set = computeSet(0, aliveReadParity_);
    ComputePush push{desc_.maxParticles, 0, 0, 0.0f, 0.0f};
    initPipeline_->bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, initPipeline_->layout(),
        0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd, initPipeline_->layout(), VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(push), &push);
    vkCmdDispatch(cmd, ComputePipeline::groupCount(desc_.maxParticles, 64), 1, 1);

    recordComputeBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
}

void ParticleRuntime::recordComputeBarrier(VkCommandBuffer cmd,
                                           VkPipelineStageFlags2 dstStage,
                                           VkAccessFlags2 dstAccess) const {
    VkMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
}

} // namespace saida
