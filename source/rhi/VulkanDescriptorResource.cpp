#include "VulkanDescriptorResource.h"

#include "VulkanContext.h"
#include "VulkanDevice.h"

bool VulkanDescriptorResource::createStorageBufferSet(const VulkanDevice& device,
                                                      VkDescriptorSetLayout layout,
                                                      VkBuffer buffer,
                                                      VkDeviceSize range,
                                                      QString& lastError)
{
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE || layout == VK_NULL_HANDLE || buffer == VK_NULL_HANDLE) {
        lastError = QStringLiteral("Vulkan descriptor input is not initialized");
        return false;
    }

    destroy(device);

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    VkResult result = vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &descriptorPool_);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkCreateDescriptorPool(storage buffer) failed: ") +
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
        lastError = QStringLiteral("vkAllocateDescriptorSets(storage buffer) failed: ") +
            VulkanContext::formatResult(result);
        return false;
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = range;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet_;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(vkDevice, 1, &descriptorWrite, 0, nullptr);
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
