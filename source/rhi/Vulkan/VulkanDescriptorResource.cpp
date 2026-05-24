#include "VulkanDescriptorResource.h"

#include "VulkanContext.h"
#include "VulkanDevice.h"

bool VulkanDescriptorResource::createStorageBufferSet(const VulkanDevice& device,
                                                      VkDescriptorSetLayout layout,
                                                      VkBuffer buffer,
                                                      VkDeviceSize range,
                                                      QString& lastError)
{
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = range;
    return createStorageBufferSet(device, layout, std::vector<VkDescriptorBufferInfo>{bufferInfo}, lastError);
}

bool VulkanDescriptorResource::createStorageBufferSet(
    const VulkanDevice& device,
    VkDescriptorSetLayout layout,
    const std::vector<VkDescriptorBufferInfo>& buffers,
    QString& lastError)
{
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE || layout == VK_NULL_HANDLE || buffers.empty()) {
        lastError = QStringLiteral("Vulkan descriptor input is not initialized");
        return false;
    }
    for (const VkDescriptorBufferInfo& buffer : buffers) {
        if (buffer.buffer == VK_NULL_HANDLE || buffer.range == 0) {
            lastError = QStringLiteral("Vulkan descriptor buffer input is not initialized");
            return false;
        }
    }

    destroy(device);

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = static_cast<uint32_t>(buffers.size());

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    VkResult result = vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &descriptorPool_);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkCreateDescriptorPool(storage buffers) failed: ") +
            VulkanContext::formatResult(result);
        return false;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    result = vkAllocateDescriptorSets(vkDevice, &allocInfo, &descriptorSet_);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkAllocateDescriptorSets(storage buffers) failed: ") +
            VulkanContext::formatResult(result);
        return false;
    }

    std::vector<VkWriteDescriptorSet> descriptorWrites(buffers.size());
    for (size_t i = 0; i < descriptorWrites.size(); ++i) {
        descriptorWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[i].dstSet = descriptorSet_;
        descriptorWrites[i].dstBinding = static_cast<uint32_t>(i);
        descriptorWrites[i].dstArrayElement = 0;
        descriptorWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[i].descriptorCount = 1;
        descriptorWrites[i].pBufferInfo = &buffers[i];
    }
    vkUpdateDescriptorSets(vkDevice,
                           static_cast<uint32_t>(descriptorWrites.size()),
                           descriptorWrites.data(),
                           0,
                           nullptr);
    return true;
}

void VulkanDescriptorResource::destroy(const VulkanDevice& device)
{
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE) {
        descriptorPool_ = VK_NULL_HANDLE;
        descriptorSet_ = VK_NULL_HANDLE;
        return;
    }

    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vkDevice, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
        descriptorSet_ = VK_NULL_HANDLE;
    }
}
