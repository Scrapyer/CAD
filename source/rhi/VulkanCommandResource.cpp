#include "VulkanCommandResource.h"

#include "VulkanContext.h"
#include "VulkanDevice.h"

bool VulkanCommandResource::create(const VulkanDevice& device, QString& lastError)
{
    destroy(device);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = device.queueFamilies().graphics;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkResult result = vkCreateCommandPool(device.device(), &poolInfo, nullptr, &commandPool_);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkCreateCommandPool failed: ") +
            VulkanContext::formatResult(result);
        commandPool_ = VK_NULL_HANDLE;
        return false;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    result = vkAllocateCommandBuffers(device.device(), &allocInfo, &commandBuffer_);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkAllocateCommandBuffers failed: ") +
            VulkanContext::formatResult(result);
        destroy(device);
        return false;
    }

    return true;
}

void VulkanCommandResource::destroy(const VulkanDevice& device)
{
    VkDevice vkDevice = device.device();
    if (vkDevice != VK_NULL_HANDLE && commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(vkDevice, commandPool_, nullptr);
    }
    commandPool_ = VK_NULL_HANDLE;
    commandBuffer_ = VK_NULL_HANDLE;
}

bool VulkanCommandResource::resetCommandBuffer(const VulkanDevice& device, QString& lastError) const
{
    if (commandBuffer_ == VK_NULL_HANDLE) {
        lastError = QStringLiteral("Vulkan command buffer is not initialized");
        return false;
    }

    VkResult result = vkResetCommandBuffer(commandBuffer_, 0);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkResetCommandBuffer failed: ") +
            VulkanContext::formatResult(result);
        return false;
    }
    Q_UNUSED(device);
    return true;
}
