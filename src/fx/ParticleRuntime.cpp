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

ParticleRuntime::~ParticleRuntime() {
    finalizePipeline_.reset();
    simPipeline_.reset();
    emitPipeline_.reset();
    preparePipeline_.reset();
    initPipeline_.reset();
    if (computePool_) vkDestroyDescriptorPool(device_.device(), computePool_, nullptr);
    if (computeSetLayout_) vkDestroyDescriptorSetLayout(device_.device(), computeSetLayout_, nullptr);
    if (renderPool_) vkDestroyDescriptorPool(device_.device(), renderPool_, nullptr);
    if (renderSetLayout_) vkDestroyDescriptorSetLayout(device_.device(), renderSetLayout_, nullptr);
}

uint32_t ParticleRuntime::frameIndex(uint32_t frame) const {
    return std::min(frame, desc_.framesInFlight - 1);
}

VkDescriptorSet ParticleRuntime::renderSet(uint32_t parity) const {
    return renderSets_[std::min<uint32_t>(parity, 1u)];
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
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    VkDescriptorSetLayoutBinding& particlesBinding = bindings[0];
    particlesBinding.binding = 0;
    particlesBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    particlesBinding.descriptorCount = 1;
    particlesBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding& aliveBinding = bindings[1];
    aliveBinding.binding = 1;
    aliveBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    aliveBinding.descriptorCount = 1;
    aliveBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device_.device(), &layoutInfo, nullptr, &renderSetLayout_) != VK_SUCCESS)
        throw std::runtime_error("failed to create particle render set layout");

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 2;
    if (vkCreateDescriptorPool(device_.device(), &poolInfo, nullptr, &renderPool_) != VK_SUCCESS)
        throw std::runtime_error("failed to create particle render descriptor pool");

    renderSets_.resize(2);
    for (uint32_t i = 0; i < 2; ++i) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = renderPool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &renderSetLayout_;
        if (vkAllocateDescriptorSets(device_.device(), &allocInfo, &renderSets_[i]) != VK_SUCCESS)
            throw std::runtime_error("failed to allocate particle render descriptor set");
    }
}

void ParticleRuntime::createComputeResources() {
    std::array<VkDescriptorSetLayoutBinding, 7> bindings{};
    for (uint32_t i = 0; i < static_cast<uint32_t>(bindings.size()); ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device_.device(), &layoutInfo, nullptr, &computeSetLayout_) != VK_SUCCESS)
        throw std::runtime_error("failed to create particle compute set layout");

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, desc_.framesInFlight * 14};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = desc_.framesInFlight * 2;
    if (vkCreateDescriptorPool(device_.device(), &poolInfo, nullptr, &computePool_) != VK_SUCCESS)
        throw std::runtime_error("failed to create particle compute descriptor pool");

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
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = computePool_;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &computeSetLayout_;
            if (vkAllocateDescriptorSets(device_.device(), &allocInfo, &computeSets_[setIndex]) != VK_SUCCESS)
                throw std::runtime_error("failed to allocate particle compute descriptor set");

            const uint32_t writeParity = 1u - parity;
            std::array<VkDescriptorBufferInfo, 7> infos{};
            infos[0] = {simParticleBuffer_->handle(), 0, particleSize};
            infos[1] = {aliveIndexBuffers_[parity]->handle(), 0, indexSize};
            infos[2] = {aliveIndexBuffers_[writeParity]->handle(), 0, indexSize};
            infos[3] = {deadIndexBuffer_->handle(), 0, indexSize};
            infos[4] = {counterBuffer_->handle(), 0, counterSize};
            infos[5] = {emitterBuffers_[i]->handle(), 0, emitterSize};
            infos[6] = {indirectBuffer_->handle(), 0, sizeof(VkDrawIndirectCommand)};

            std::array<VkWriteDescriptorSet, 7> writes{};
            for (uint32_t b = 0; b < static_cast<uint32_t>(writes.size()); ++b) {
                writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[b].dstSet = computeSets_[setIndex];
                writes[b].dstBinding = b;
                writes[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[b].descriptorCount = 1;
                writes[b].pBufferInfo = &infos[b];
            }
            vkUpdateDescriptorSets(device_.device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    const VkDeviceSize particleSizeRender = sizeof(SimParticle) * desc_.maxParticles;
    const VkDeviceSize indexSizeRender = sizeof(uint32_t) * desc_.maxParticles;
    for (uint32_t parity = 0; parity < 2; ++parity) {
        std::array<VkDescriptorBufferInfo, 2> infos{};
        infos[0] = {simParticleBuffer_->handle(), 0, particleSizeRender};
        infos[1] = {aliveIndexBuffers_[parity]->handle(), 0, indexSizeRender};
        std::array<VkWriteDescriptorSet, 2> writes{};
        for (uint32_t b = 0; b < static_cast<uint32_t>(writes.size()); ++b) {
            writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstSet = renderSets_[parity];
            writes[b].dstBinding = b;
            writes[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[b].descriptorCount = 1;
            writes[b].pBufferInfo = &infos[b];
        }
        vkUpdateDescriptorSets(device_.device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    std::vector<VkDescriptorSetLayout> setLayouts = {computeSetLayout_};
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
    return computeSets_[f * 2 + std::min<uint32_t>(readParity, 1u)];
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
