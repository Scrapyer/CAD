#include "VulkanBufferResource.h"

#include "VulkanContext.h"
#include "VulkanDevice.h"

#include <cstdint>
#include <cstring>

bool VulkanBufferResource::uploadHostVisible(const VulkanDevice& device,
                                             const void* data,
                                             VkDeviceSize size,
                                             VkBufferUsageFlags usage,
                                             const char* debugName,
                                             QString& lastError)
{
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE) {
        lastError = QStringLiteral("Vulkan device is not initialized");
        return false;
    }

    destroy(device);
    if (size == 0) {
        return true;
    }
    if (data == nullptr) {
        lastError = QStringLiteral("Vulkan buffer upload data is null");
        return false;
    }

    if (!create(device,
                size,
                usage,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                lastError)) {
        return false;
    }

    void* mapped = nullptr;
    VkResult result = vkMapMemory(vkDevice, memory_, 0, size, 0, &mapped);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkMapMemory(%1) failed: %2")
            .arg(QString::fromUtf8(debugName))
            .arg(VulkanContext::formatResult(result));
        destroy(device);
        return false;
    }

    std::memcpy(mapped, data, static_cast<size_t>(size));
    vkUnmapMemory(vkDevice, memory_);
    return true;
}

bool VulkanBufferResource::uploadDeviceLocal(const VulkanDevice& device,
                                             const void* data,
                                             VkDeviceSize size,
                                             VkBufferUsageFlags usage,
                                             VkCommandPool commandPool,
                                             VkQueue queue,
                                             const char* debugName,
                                             QString& lastError)
{
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE) {
        lastError = QStringLiteral("Vulkan device is not initialized");
        return false;
    }
    if (commandPool == VK_NULL_HANDLE || queue == VK_NULL_HANDLE) {
        lastError = QStringLiteral("Vulkan transfer queue is not initialized");
        return false;
    }

    destroy(device);
    if (size == 0) {
        return true;
    }
    if (data == nullptr) {
        lastError = QStringLiteral("Vulkan buffer upload data is null");
        return false;
    }

    VulkanBufferResource staging;
    if (!staging.uploadHostVisible(device,
                                   data,
                                   size,
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   debugName,
                                   lastError)) {
        return false;
    }

    if (!create(device,
                size,
                usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                lastError)) {
        staging.destroy(device);
        return false;
    }

    if (!copyBuffer(device,
                    commandPool,
                    queue,
                    staging.buffer(),
                    buffer_,
                    size,
                    debugName,
                    lastError)) {
        staging.destroy(device);
        destroy(device);
        return false;
    }

    staging.destroy(device);
    return true;
}

bool VulkanBufferResource::updateHostVisible(const VulkanDevice& device,
                                             const void* data,
                                             VkDeviceSize size,
                                             const char* debugName,
                                             QString& lastError)
{
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE || buffer_ == VK_NULL_HANDLE || memory_ == VK_NULL_HANDLE) {
        lastError = QStringLiteral("Vulkan buffer is not initialized");
        return false;
    }
    if (size > size_) {
        lastError = QStringLiteral("Vulkan buffer update exceeds allocated size");
        return false;
    }
    if (size == 0) {
        return true;
    }
    if (data == nullptr) {
        lastError = QStringLiteral("Vulkan buffer update data is null");
        return false;
    }

    void* mapped = nullptr;
    VkResult result = vkMapMemory(vkDevice, memory_, 0, size, 0, &mapped);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkMapMemory(%1 update) failed: %2")
            .arg(QString::fromUtf8(debugName))
            .arg(VulkanContext::formatResult(result));
        return false;
    }

    std::memcpy(mapped, data, static_cast<size_t>(size));
    vkUnmapMemory(vkDevice, memory_);
    return true;
}

bool VulkanBufferResource::readHostVisible(const VulkanDevice& device,
                                           void* data,
                                           VkDeviceSize size,
                                           const char* debugName,
                                           QString& lastError) const
{
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE || buffer_ == VK_NULL_HANDLE || memory_ == VK_NULL_HANDLE) {
        lastError = QStringLiteral("Vulkan buffer is not initialized");
        return false;
    }
    if (size > size_) {
        lastError = QStringLiteral("Vulkan buffer read exceeds allocated size");
        return false;
    }
    if (size == 0) {
        return true;
    }
    if (data == nullptr) {
        lastError = QStringLiteral("Vulkan buffer read destination is null");
        return false;
    }

    void* mapped = nullptr;
    VkResult result = vkMapMemory(vkDevice, memory_, 0, size, 0, &mapped);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkMapMemory(%1 read) failed: %2")
            .arg(QString::fromUtf8(debugName))
            .arg(VulkanContext::formatResult(result));
        return false;
    }

    std::memcpy(data, mapped, static_cast<size_t>(size));
    vkUnmapMemory(vkDevice, memory_);
    return true;
}

void VulkanBufferResource::destroy(const VulkanDevice& device)
{
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE) {
        buffer_ = VK_NULL_HANDLE;
        memory_ = VK_NULL_HANDLE;
        size_ = 0;
        return;
    }

    if (buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkDevice, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
    }
    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vkDevice, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
    size_ = 0;
}

bool VulkanBufferResource::create(const VulkanDevice& device,
                                  VkDeviceSize size,
                                  VkBufferUsageFlags usage,
                                  VkMemoryPropertyFlags properties,
                                  QString& lastError)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(device.device(), &bufferInfo, nullptr, &buffer_);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkCreateBuffer failed: ") + VulkanContext::formatResult(result);
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device.device(), buffer_, &requirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(device, requirements.memoryTypeBits, properties);
    if (allocInfo.memoryTypeIndex == UINT32_MAX) {
        lastError = QStringLiteral("No compatible Vulkan buffer memory type found");
        vkDestroyBuffer(device.device(), buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
        return false;
    }

    result = vkAllocateMemory(device.device(), &allocInfo, nullptr, &memory_);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkAllocateMemory failed: ") + VulkanContext::formatResult(result);
        vkDestroyBuffer(device.device(), buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
        return false;
    }

    result = vkBindBufferMemory(device.device(), buffer_, memory_, 0);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkBindBufferMemory failed: ") + VulkanContext::formatResult(result);
        destroy(device);
        return false;
    }

    size_ = size;
    return true;
}

bool VulkanBufferResource::copyBuffer(const VulkanDevice& device,
                                      VkCommandPool commandPool,
                                      VkQueue queue,
                                      VkBuffer source,
                                      VkBuffer destination,
                                      VkDeviceSize size,
                                      const char* debugName,
                                      QString& lastError) const
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkResult result = vkAllocateCommandBuffers(device.device(), &allocInfo, &commandBuffer);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkAllocateCommandBuffers(%1 copy) failed: %2")
            .arg(QString::fromUtf8(debugName))
            .arg(VulkanContext::formatResult(result));
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkBeginCommandBuffer(%1 copy) failed: %2")
            .arg(QString::fromUtf8(debugName))
            .arg(VulkanContext::formatResult(result));
        vkFreeCommandBuffers(device.device(), commandPool, 1, &commandBuffer);
        return false;
    }

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, source, destination, 1, &copyRegion);

    result = vkEndCommandBuffer(commandBuffer);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkEndCommandBuffer(%1 copy) failed: %2")
            .arg(QString::fromUtf8(debugName))
            .arg(VulkanContext::formatResult(result));
        vkFreeCommandBuffers(device.device(), commandPool, 1, &commandBuffer);
        return false;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence copyFence = VK_NULL_HANDLE;
    result = vkCreateFence(device.device(), &fenceInfo, nullptr, &copyFence);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkCreateFence(%1 copy) failed: %2")
            .arg(QString::fromUtf8(debugName))
            .arg(VulkanContext::formatResult(result));
        vkFreeCommandBuffers(device.device(), commandPool, 1, &commandBuffer);
        return false;
    }

    result = vkQueueSubmit(queue, 1, &submitInfo, copyFence);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkQueueSubmit(%1 copy) failed: %2")
            .arg(QString::fromUtf8(debugName))
            .arg(VulkanContext::formatResult(result));
        vkDestroyFence(device.device(), copyFence, nullptr);
        vkFreeCommandBuffers(device.device(), commandPool, 1, &commandBuffer);
        return false;
    }

    result = vkWaitForFences(device.device(), 1, &copyFence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkWaitForFences(%1 copy) failed: %2")
            .arg(QString::fromUtf8(debugName))
            .arg(VulkanContext::formatResult(result));
        vkDestroyFence(device.device(), copyFence, nullptr);
        vkFreeCommandBuffers(device.device(), commandPool, 1, &commandBuffer);
        return false;
    }

    vkDestroyFence(device.device(), copyFence, nullptr);
    vkFreeCommandBuffers(device.device(), commandPool, 1, &commandBuffer);
    return true;
}

uint32_t VulkanBufferResource::findMemoryType(const VulkanDevice& device,
                                              uint32_t typeFilter,
                                              VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(device.physicalDevice(), &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}
