#include "VulkanStagingUploadContext.h"

#include "VulkanContext.h"
#include "VulkanDevice.h"

bool VulkanStagingUploadContext::uploadBuffer(const VulkanDevice& device,
                                              VulkanBufferResource& destination,
                                              const void* data,
                                              VkDeviceSize size,
                                              VkBufferUsageFlags usage,
                                              const char* debugName,
                                              QString& lastError)
{
    if (size == 0) {
        destination.destroy(device);
        return true;
    }
    if (data == nullptr) {
        lastError = QStringLiteral("Vulkan staging upload data is null");
        return false;
    }

    auto staging = std::make_unique<VulkanBufferResource>();
    if (!staging->uploadHostVisible(device,
                                    data,
                                    size,
                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    debugName,
                                    lastError)) {
        return false;
    }

    if (!destination.createDeviceLocal(device, size, usage, debugName, lastError)) {
        staging->destroy(device);
        return false;
    }

    CopyRequest request;
    request.source = staging->buffer();
    request.destination = destination.buffer();
    request.size = size;
    request.debugName = debugName;
    stagingResources_.push_back(std::move(staging));
    copies_.push_back(request);
    return true;
}

bool VulkanStagingUploadContext::submit(const VulkanDevice& device,
                                        VkCommandPool commandPool,
                                        VkQueue queue,
                                        QString& lastError)
{
    if (copies_.empty()) {
        return true;
    }
    if (commandPool == VK_NULL_HANDLE || queue == VK_NULL_HANDLE) {
        lastError = QStringLiteral("Vulkan transfer queue is not initialized");
        discard(device);
        return false;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkResult result = vkAllocateCommandBuffers(device.device(), &allocInfo, &commandBuffer);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkAllocateCommandBuffers(staging batch copy) failed: ") +
            VulkanContext::formatResult(result);
        discard(device);
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkBeginCommandBuffer(staging batch copy) failed: ") +
            VulkanContext::formatResult(result);
        vkFreeCommandBuffers(device.device(), commandPool, 1, &commandBuffer);
        discard(device);
        return false;
    }

    for (const CopyRequest& request : copies_) {
        VkBufferCopy copyRegion{};
        copyRegion.size = request.size;
        vkCmdCopyBuffer(commandBuffer, request.source, request.destination, 1, &copyRegion);
    }

    result = vkEndCommandBuffer(commandBuffer);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkEndCommandBuffer(staging batch copy) failed: ") +
            VulkanContext::formatResult(result);
        vkFreeCommandBuffers(device.device(), commandPool, 1, &commandBuffer);
        discard(device);
        return false;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence uploadFence = VK_NULL_HANDLE;
    result = vkCreateFence(device.device(), &fenceInfo, nullptr, &uploadFence);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkCreateFence(staging batch copy) failed: ") +
            VulkanContext::formatResult(result);
        vkFreeCommandBuffers(device.device(), commandPool, 1, &commandBuffer);
        discard(device);
        return false;
    }

    result = vkQueueSubmit(queue, 1, &submitInfo, uploadFence);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkQueueSubmit(staging batch copy) failed: ") +
            VulkanContext::formatResult(result);
        vkDestroyFence(device.device(), uploadFence, nullptr);
        vkFreeCommandBuffers(device.device(), commandPool, 1, &commandBuffer);
        discard(device);
        return false;
    }

    result = vkWaitForFences(device.device(), 1, &uploadFence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkWaitForFences(staging batch copy) failed: ") +
            VulkanContext::formatResult(result);
        vkDestroyFence(device.device(), uploadFence, nullptr);
        vkFreeCommandBuffers(device.device(), commandPool, 1, &commandBuffer);
        discard(device);
        return false;
    }

    vkDestroyFence(device.device(), uploadFence, nullptr);
    vkFreeCommandBuffers(device.device(), commandPool, 1, &commandBuffer);
    discard(device);
    return true;
}

void VulkanStagingUploadContext::discard(const VulkanDevice& device)
{
    for (const std::unique_ptr<VulkanBufferResource>& staging : stagingResources_) {
        if (staging) {
            staging->destroy(device);
        }
    }
    stagingResources_.clear();
    copies_.clear();
}
