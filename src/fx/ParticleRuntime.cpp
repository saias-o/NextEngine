#include "fx/ParticleRuntime.hpp"

#include "core/Paths.hpp"
#include "graphics/Buffer.hpp"
#include "graphics/ComputePipeline.hpp"
#include "graphics/VulkanDevice.hpp"

#include <array>
#include <algorithm>
#include <stdexcept>

namespace ne {

ParticleRuntime::ParticleRuntime(VulkanDevice& device, const Desc& desc)
    : device_(device), desc_(desc) {
    desc_.framesInFlight = std::max(1u, desc_.framesInFlight);
    desc_.maxParticles = std::max(1u, desc_.maxParticles);
    desc_.maxEmitters = std::max(1u, desc_.maxEmitters);
    createRenderResources();
    createComputeResources();
}

ParticleRuntime::~ParticleRuntime() {
    simPipeline_.reset();
    emitPipeline_.reset();
    if (computePool_) vkDestroyDescriptorPool(device_.device(), computePool_, nullptr);
    if (computeSetLayout_) vkDestroyDescriptorSetLayout(device_.device(), computeSetLayout_, nullptr);
    if (renderPool_) vkDestroyDescriptorPool(device_.device(), renderPool_, nullptr);
    if (renderSetLayout_) vkDestroyDescriptorSetLayout(device_.device(), renderSetLayout_, nullptr);
}

uint32_t ParticleRuntime::frameIndex(uint32_t frame) const {
    return std::min(frame, desc_.framesInFlight - 1);
}

VkDescriptorSet ParticleRuntime::renderSet(uint32_t frame) const {
    return renderSets_[frameIndex(frame)];
}

ParticleRuntime::RenderParticle* ParticleRuntime::mappedRenderParticles(uint32_t frame) const {
    return static_cast<RenderParticle*>(renderParticleBuffers_[frameIndex(frame)]->mapped());
}

void ParticleRuntime::flushRenderParticles(uint32_t frame, uint32_t count) {
    if (count == 0) return;
    const uint32_t clamped = std::min(count, desc_.maxParticles);
    renderParticleBuffers_[frameIndex(frame)]->flush(sizeof(RenderParticle) * clamped);
}

VkDescriptorSet ParticleRuntime::computeSet(uint32_t frame) const {
    return computeSets_[frameIndex(frame)];
}

ParticleRuntime::GpuEmitter* ParticleRuntime::mappedEmitters(uint32_t frame) const {
    return static_cast<GpuEmitter*>(emitterBuffers_[frameIndex(frame)]->mapped());
}

void ParticleRuntime::flushEmitters(uint32_t frame, uint32_t count) {
    if (count == 0) return;
    const uint32_t clamped = std::min(count, desc_.maxEmitters);
    emitterBuffers_[frameIndex(frame)]->flush(sizeof(GpuEmitter) * clamped);
}

void ParticleRuntime::recordCompute(VkCommandBuffer cmd, uint32_t frame,
                                    uint32_t emitterCount, uint32_t emitCount,
                                    float dt, float time) {
    if (!cmd || emitterCount == 0 || emitCount == 0) return;
    const uint32_t f = frameIndex(frame);
    const uint32_t clampedEmitters = std::min(emitterCount, desc_.maxEmitters);
    const uint32_t clampedEmitCount = std::min(emitCount, desc_.maxParticles);
    ComputePush push{desc_.maxParticles, clampedEmitters, dt, time};
    VkDescriptorSet set = computeSets_[f];

    recordResetGpuFrame(cmd, f, clampedEmitCount);

    emitPipeline_->bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, emitPipeline_->layout(),
        0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd, emitPipeline_->layout(), VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(push), &push);
    vkCmdDispatch(cmd, ComputePipeline::groupCount(clampedEmitCount, 64), 1, 1);

    recordComputeBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);

    simPipeline_->bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, simPipeline_->layout(),
        0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd, simPipeline_->layout(), VK_SHADER_STAGE_COMPUTE_BIT,
        0, sizeof(push), &push);
    vkCmdDispatch(cmd, ComputePipeline::groupCount(desc_.maxParticles, 64), 1, 1);

    recordComputeBarrier(cmd, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
        VK_ACCESS_2_SHADER_READ_BIT);
}

void ParticleRuntime::createRenderResources() {
    VkDescriptorSetLayoutBinding particlesBinding{};
    particlesBinding.binding = 0;
    particlesBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    particlesBinding.descriptorCount = 1;
    particlesBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &particlesBinding;
    if (vkCreateDescriptorSetLayout(device_.device(), &layoutInfo, nullptr, &renderSetLayout_) != VK_SUCCESS)
        throw std::runtime_error("failed to create particle render set layout");

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, desc_.framesInFlight};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = desc_.framesInFlight;
    if (vkCreateDescriptorPool(device_.device(), &poolInfo, nullptr, &renderPool_) != VK_SUCCESS)
        throw std::runtime_error("failed to create particle render descriptor pool");

    renderParticleBuffers_.resize(desc_.framesInFlight);
    renderSets_.resize(desc_.framesInFlight);
    const VkDeviceSize bufferSize = sizeof(RenderParticle) * desc_.maxParticles;
    for (uint32_t i = 0; i < desc_.framesInFlight; ++i) {
        renderParticleBuffers_[i] = std::make_unique<Buffer>(device_, bufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, MemoryUsage::HostVisible);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = renderPool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &renderSetLayout_;
        if (vkAllocateDescriptorSets(device_.device(), &allocInfo, &renderSets_[i]) != VK_SUCCESS)
            throw std::runtime_error("failed to allocate particle render descriptor set");

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = renderParticleBuffers_[i]->handle();
        bufferInfo.offset = 0;
        bufferInfo.range = bufferSize;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = renderSets_[i];
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(device_.device(), 1, &write, 0, nullptr);
    }
}

void ParticleRuntime::createComputeResources() {
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
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

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, desc_.framesInFlight * 5};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = desc_.framesInFlight;
    if (vkCreateDescriptorPool(device_.device(), &poolInfo, nullptr, &computePool_) != VK_SUCCESS)
        throw std::runtime_error("failed to create particle compute descriptor pool");

    simParticleBuffers_.resize(desc_.framesInFlight);
    aliveIndexBuffers_.resize(desc_.framesInFlight);
    deadIndexBuffers_.resize(desc_.framesInFlight);
    counterBuffers_.resize(desc_.framesInFlight);
    emitterBuffers_.resize(desc_.framesInFlight);
    computeSets_.resize(desc_.framesInFlight);

    const VkDeviceSize particleSize = sizeof(SimParticle) * desc_.maxParticles;
    const VkDeviceSize indexSize = sizeof(uint32_t) * desc_.maxParticles;
    const VkDeviceSize counterSize = sizeof(GpuCounters);
    const VkDeviceSize emitterSize = sizeof(GpuEmitter) * desc_.maxEmitters;
    createDeadListTemplate();
    for (uint32_t i = 0; i < desc_.framesInFlight; ++i) {
        simParticleBuffers_[i] = std::make_unique<Buffer>(device_, particleSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, MemoryUsage::GpuOnly);
        aliveIndexBuffers_[i] = std::make_unique<Buffer>(device_, indexSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            MemoryUsage::GpuOnly);
        deadIndexBuffers_[i] = std::make_unique<Buffer>(device_, indexSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            MemoryUsage::GpuOnly);
        counterBuffers_[i] = std::make_unique<Buffer>(device_, counterSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, MemoryUsage::HostVisible);
        emitterBuffers_[i] = std::make_unique<Buffer>(device_, emitterSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, MemoryUsage::HostVisible);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = computePool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &computeSetLayout_;
        if (vkAllocateDescriptorSets(device_.device(), &allocInfo, &computeSets_[i]) != VK_SUCCESS)
            throw std::runtime_error("failed to allocate particle compute descriptor set");

        std::array<VkDescriptorBufferInfo, 5> infos{};
        infos[0] = {simParticleBuffers_[i]->handle(), 0, particleSize};
        infos[1] = {aliveIndexBuffers_[i]->handle(), 0, indexSize};
        infos[2] = {deadIndexBuffers_[i]->handle(), 0, indexSize};
        infos[3] = {counterBuffers_[i]->handle(), 0, counterSize};
        infos[4] = {emitterBuffers_[i]->handle(), 0, emitterSize};

        std::array<VkWriteDescriptorSet, 5> writes{};
        for (uint32_t b = 0; b < static_cast<uint32_t>(writes.size()); ++b) {
            writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstSet = computeSets_[i];
            writes[b].dstBinding = b;
            writes[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[b].descriptorCount = 1;
            writes[b].pBufferInfo = &infos[b];
        }
        vkUpdateDescriptorSets(device_.device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    std::vector<VkDescriptorSetLayout> setLayouts = {computeSetLayout_};
    emitPipeline_ = std::make_unique<ComputePipeline>(device_, shaderPath("particle_emit.comp.spv"),
        setLayouts, sizeof(ComputePush));
    simPipeline_ = std::make_unique<ComputePipeline>(device_, shaderPath("particle_sim.comp.spv"),
        setLayouts, sizeof(ComputePush));
}

void ParticleRuntime::createDeadListTemplate() {
    std::vector<uint32_t> indices(desc_.maxParticles);
    for (uint32_t i = 0; i < desc_.maxParticles; ++i) indices[i] = i;

    deadListTemplateBuffer_ = std::make_unique<Buffer>(device_, sizeof(uint32_t) * desc_.maxParticles,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUsage::HostVisible);
    deadListTemplateBuffer_->write(indices.data(), sizeof(uint32_t) * desc_.maxParticles);
}

void ParticleRuntime::recordResetGpuFrame(VkCommandBuffer cmd, uint32_t frame, uint32_t emitCount) {
    const uint32_t f = frameIndex(frame);
    GpuCounters counters{};
    counters.aliveCount = 0;
    counters.deadCount = desc_.maxParticles;
    counters.emitCount = std::min(emitCount, desc_.maxParticles);
    counterBuffers_[f]->write(&counters, sizeof(counters));

    VkBufferCopy copy{};
    copy.size = sizeof(uint32_t) * desc_.maxParticles;
    vkCmdCopyBuffer(cmd, deadListTemplateBuffer_->handle(), deadIndexBuffers_[f]->handle(), 1, &copy);

    VkMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
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

} // namespace ne
