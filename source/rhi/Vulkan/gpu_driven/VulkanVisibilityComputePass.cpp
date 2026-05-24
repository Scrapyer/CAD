#include "VulkanVisibilityComputePass.h"

#include "VulkanBufferResource.h"
#include "VulkanContext.h"
#include "VulkanDevice.h"
#include "VulkanGpuDrivenMeshResources.h"

#include <QFile>

#include <algorithm>
#include <array>

namespace {
QString visibilityShaderPath()
{
#if defined(FERENDER_VULKAN_SHADER_DIR)
    return QStringLiteral(FERENDER_VULKAN_SHADER_DIR) +
        QStringLiteral("/vulkan_visibility.comp.spv");
#else
    return QString();
#endif
}

QString visibilityV2ShaderPath()
{
#if defined(FERENDER_VULKAN_SHADER_DIR)
    return QStringLiteral(FERENDER_VULKAN_SHADER_DIR) +
        QStringLiteral("/vulkan_visibility_v2.comp.spv");
#else
    return QString();
#endif
}

VkDescriptorSetLayoutBinding storageBinding(uint32_t binding)
{
    VkDescriptorSetLayoutBinding layoutBinding{};
    layoutBinding.binding = binding;
    layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    layoutBinding.descriptorCount = 1;
    layoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    return layoutBinding;
}
} // namespace

bool VulkanVisibilityComputePass::ensureInitialized(const VulkanDevice& device, QString& lastError)
{
    if (isInitialized()) {
        return true;
    }
    if (descriptorSetLayout_ == VK_NULL_HANDLE &&
        !createDescriptorSetLayout(device, lastError)) {
        return false;
    }
    if (pipeline_ == VK_NULL_HANDLE &&
        !createPipeline(device, visibilityShaderPath(), pipeline_, "gpu visibility", lastError)) {
        return false;
    }
    if (pipelineV2_ == VK_NULL_HANDLE) {
        QString v2Error;
        if (!createPipeline(device, visibilityV2ShaderPath(), pipelineV2_, "gpu visibility v2", v2Error)) {
            pipelineV2_ = VK_NULL_HANDLE;
        }
    }
    return true;
}

bool VulkanVisibilityComputePass::createDescriptorSetLayout(
    const VulkanDevice& device,
    QString& lastError)
{
    destroy(device);

    std::array<VkDescriptorSetLayoutBinding, 12> bindings = {{
        storageBinding(0),
        storageBinding(1),
        storageBinding(2),
        storageBinding(3),
        storageBinding(4),
        storageBinding(5),
        storageBinding(6),
        storageBinding(7),
        storageBinding(8),
        storageBinding(9),
        storageBinding(10),
        storageBinding(11)
    }};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkResult result = vkCreateDescriptorSetLayout(
        device.device(), &layoutInfo, nullptr, &descriptorSetLayout_);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkCreateDescriptorSetLayout(gpu visibility) failed: ") +
            VulkanContext::formatResult(result);
        descriptorSetLayout_ = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool VulkanVisibilityComputePass::createPipeline(const VulkanDevice& device,
                                                 const QString& shaderPath,
                                                 VkPipeline& pipeline,
                                                 const char* debugName,
                                                 QString& lastError)
{
    if (descriptorSetLayout_ == VK_NULL_HANDLE) {
        lastError = QStringLiteral("Vulkan visibility descriptor set layout is not initialized");
        return false;
    }

    if (pipelineLayout_ == VK_NULL_HANDLE) {
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout_;

        VkResult result = vkCreatePipelineLayout(
            device.device(), &layoutInfo, nullptr, &pipelineLayout_);
        if (result != VK_SUCCESS) {
            lastError = QStringLiteral("vkCreatePipelineLayout(%1) failed: %2")
                .arg(QString::fromUtf8(debugName))
                .arg(VulkanContext::formatResult(result));
            pipelineLayout_ = VK_NULL_HANDLE;
            return false;
        }
    }

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (!createShaderModule(device, shaderPath, shaderModule, lastError)) {
        return false;
    }

    VkPipelineShaderStageCreateInfo shaderStage{};
    shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStage.module = shaderModule;
    shaderStage.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStage;
    pipelineInfo.layout = pipelineLayout_;

    VkResult result = vkCreateComputePipelines(
        device.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    vkDestroyShaderModule(device.device(), shaderModule, nullptr);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkCreateComputePipelines(%1) failed: %2")
            .arg(QString::fromUtf8(debugName))
            .arg(VulkanContext::formatResult(result));
        pipeline = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

bool VulkanVisibilityComputePass::createShaderModule(
    const VulkanDevice& device,
    const QString& shaderPath,
    VkShaderModule& shaderModule,
    QString& lastError) const
{
    if (shaderPath.isEmpty()) {
        lastError = QStringLiteral("Vulkan visibility shader path is not configured");
        return false;
    }

    QFile file(shaderPath);
    if (!file.open(QIODevice::ReadOnly)) {
        lastError = QStringLiteral("Failed to open Vulkan visibility shader: ") + shaderPath;
        return false;
    }

    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty() || bytes.size() % 4 != 0) {
        lastError = QStringLiteral("Invalid Vulkan visibility shader bytecode: ") + shaderPath;
        return false;
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = static_cast<size_t>(bytes.size());
    createInfo.pCode = reinterpret_cast<const uint32_t*>(bytes.constData());

    VkResult result = vkCreateShaderModule(device.device(), &createInfo, nullptr, &shaderModule);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkCreateShaderModule(gpu visibility) failed: ") +
            VulkanContext::formatResult(result);
        shaderModule = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool VulkanVisibilityComputePass::updateDescriptorSet(
    const VulkanDevice& device,
    const VulkanGpuDrivenMeshResources& resources,
    bool useSurfaceV2,
    QString& lastError)
{
    if (!isInitialized() || !resources.isReady()) {
        lastError = QStringLiteral("Vulkan visibility compute inputs are not initialized");
        return false;
    }
    if (useSurfaceV2 && !resources.hasV2Sidecar()) {
        lastError = QStringLiteral("Vulkan visibility compute V2 inputs are not initialized");
        return false;
    }
    if (useSurfaceV2 && !resources.hasUniquePointIndices()) {
        lastError = QStringLiteral("Vulkan visibility compute V2 point inputs are not initialized");
        return false;
    }
    if (useSurfaceV2 && pipelineV2_ == VK_NULL_HANDLE) {
        lastError = QStringLiteral("Vulkan visibility compute V2 pipeline is not initialized");
        return false;
    }
    const VulkanBufferResource& fallbackMetaResource = useSurfaceV2
        ? resources.triangleMetaV2Resource
        : resources.triangleMetaResource;

    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device.device(), descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
        descriptorSet_ = VK_NULL_HANDLE;
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 12;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    VkResult result = vkCreateDescriptorPool(device.device(), &poolInfo, nullptr, &descriptorPool_);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkCreateDescriptorPool(gpu visibility) failed: ") +
            VulkanContext::formatResult(result);
        return false;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout_;
    result = vkAllocateDescriptorSets(device.device(), &allocInfo, &descriptorSet_);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkAllocateDescriptorSets(gpu visibility) failed: ") +
            VulkanContext::formatResult(result);
        return false;
    }

    VkDescriptorBufferInfo triangleInfo{};
    triangleInfo.buffer = useSurfaceV2
        ? resources.triangleMetaV2Resource.buffer()
        : resources.triangleMetaResource.buffer();
    triangleInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo partInfo{};
    partInfo.buffer = resources.partStateResource.isValid()
        ? resources.partStateResource.buffer()
        : fallbackMetaResource.buffer();
    partInfo.range = resources.partStateResource.isValid() ? VK_WHOLE_SIZE : sizeof(uint32_t);

    VkDescriptorBufferInfo hiddenInfo{};
    hiddenInfo.buffer = resources.hiddenElementResource.isValid()
        ? resources.hiddenElementResource.buffer()
        : fallbackMetaResource.buffer();
    hiddenInfo.range = resources.hiddenElementResource.isValid() ? VK_WHOLE_SIZE : sizeof(uint32_t);

    VkDescriptorBufferInfo visibleInfo{};
    visibleInfo.buffer = resources.visibleIndexResource.buffer();
    visibleInfo.range = resources.visibleIndexRangeBytes();

    VkDescriptorBufferInfo indirectInfo{};
    indirectInfo.buffer = resources.indirectCommandResource.buffer();
    indirectInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo uniformInfo{};
    uniformInfo.buffer = resources.frameUniformResource.buffer();
    uniformInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo edgeMetaInfo{};
    edgeMetaInfo.buffer = resources.edgeMetaResource.isValid()
        ? resources.edgeMetaResource.buffer()
        : fallbackMetaResource.buffer();
    edgeMetaInfo.range = resources.edgeMetaResource.isValid() ? VK_WHOLE_SIZE : sizeof(uint32_t);

    VkDescriptorBufferInfo visibleEdgeInfo{};
    visibleEdgeInfo.buffer = resources.visibleEdgeIndexResource.isValid()
        ? resources.visibleEdgeIndexResource.buffer()
        : resources.visibleIndexResource.buffer();
    visibleEdgeInfo.range = resources.visibleEdgeIndexResource.isValid()
        ? resources.visibleEdgeIndexRangeBytes()
        : sizeof(uint32_t);

    VkDescriptorBufferInfo edgeIndirectInfo{};
    edgeIndirectInfo.buffer = resources.edgeIndirectCommandResource.isValid()
        ? resources.edgeIndirectCommandResource.buffer()
        : resources.indirectCommandResource.buffer();
    edgeIndirectInfo.range = resources.edgeIndirectCommandResource.isValid()
        ? VK_WHOLE_SIZE
        : sizeof(uint32_t);

    VkDescriptorBufferInfo visiblePointInfo{};
    visiblePointInfo.buffer = resources.visiblePointIndexResource.isValid()
        ? resources.visiblePointIndexResource.buffer()
        : resources.visibleIndexResource.buffer();
    visiblePointInfo.range = resources.visiblePointIndexResource.isValid()
        ? resources.visiblePointIndexRangeBytes()
        : sizeof(uint32_t);

    VkDescriptorBufferInfo pointIndirectInfo{};
    pointIndirectInfo.buffer = resources.pointIndirectCommandResource.isValid()
        ? resources.pointIndirectCommandResource.buffer()
        : resources.indirectCommandResource.buffer();
    pointIndirectInfo.range = resources.pointIndirectCommandResource.isValid()
        ? VK_WHOLE_SIZE
        : sizeof(uint32_t);

    VkDescriptorBufferInfo pointFlagInfo{};
    pointFlagInfo.buffer = resources.visiblePointFlagResource.isValid()
        ? resources.visiblePointFlagResource.buffer()
        : fallbackMetaResource.buffer();
    pointFlagInfo.range = resources.visiblePointFlagResource.isValid()
        ? resources.visiblePointFlagRangeBytes()
        : sizeof(uint32_t);

    std::array<VkDescriptorBufferInfo, 12> bufferInfos = {{
        triangleInfo,
        partInfo,
        hiddenInfo,
        visibleInfo,
        indirectInfo,
        uniformInfo,
        edgeMetaInfo,
        visibleEdgeInfo,
        edgeIndirectInfo,
        visiblePointInfo,
        pointIndirectInfo,
        pointFlagInfo
    }};
    std::array<VkWriteDescriptorSet, 12> writes{};
    for (uint32_t i = 0; i < static_cast<uint32_t>(writes.size()); ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descriptorSet_;
        writes[i].dstBinding = i;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].descriptorCount = 1;
        writes[i].pBufferInfo = &bufferInfos[i];
    }
    vkUpdateDescriptorSets(device.device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    return true;
}

void VulkanVisibilityComputePass::record(
    VkCommandBuffer commandBuffer,
    const VulkanGpuDrivenMeshResources& resources,
    bool useSurfaceV2) const
{
    const VkPipeline activePipeline = useSurfaceV2 ? pipelineV2_ : pipeline_;
    if (commandBuffer == VK_NULL_HANDLE ||
        activePipeline == VK_NULL_HANDLE ||
        pipelineLayout_ == VK_NULL_HANDLE ||
        descriptorSet_ == VK_NULL_HANDLE ||
        !resources.isReady()) {
        return;
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, activePipeline);
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipelineLayout_,
                            0,
                            1,
                            &descriptorSet_,
                            0,
                            nullptr);
    const uint32_t itemCount = std::max(resources.triangleCount, resources.edgeCount);
    const uint32_t groupCount = (itemCount + 127u) / 128u;
    if (groupCount == 0) {
        return;
    }
    vkCmdDispatch(commandBuffer, groupCount, 1, 1);
}

void VulkanVisibilityComputePass::destroy(const VulkanDevice& device)
{
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE) {
        descriptorSetLayout_ = VK_NULL_HANDLE;
        descriptorPool_ = VK_NULL_HANDLE;
        descriptorSet_ = VK_NULL_HANDLE;
        pipelineLayout_ = VK_NULL_HANDLE;
        pipeline_ = VK_NULL_HANDLE;
        pipelineV2_ = VK_NULL_HANDLE;
        return;
    }

    if (pipelineV2_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(vkDevice, pipelineV2_, nullptr);
        pipelineV2_ = VK_NULL_HANDLE;
    }
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(vkDevice, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vkDevice, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vkDevice, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
        descriptorSet_ = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vkDevice, descriptorSetLayout_, nullptr);
        descriptorSetLayout_ = VK_NULL_HANDLE;
    }
}
