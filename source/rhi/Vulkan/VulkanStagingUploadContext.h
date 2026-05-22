#pragma once

#include "VulkanBufferResource.h"

#include <QString>

#include <vulkan/vulkan.h>

#include <memory>
#include <vector>

class VulkanDevice;

/**
 * @brief 批量 staging buffer 上传上下文。
 *
 * 一次收集多个 host-visible staging -> device-local buffer copy，最后用单个
 * command buffer 和 fence 提交，减少连续上传模型时的同步对象创建次数。
 */
class VulkanStagingUploadContext {
public:
    VulkanStagingUploadContext() = default;
    ~VulkanStagingUploadContext() = default;

    VulkanStagingUploadContext(const VulkanStagingUploadContext&) = delete;
    VulkanStagingUploadContext& operator=(const VulkanStagingUploadContext&) = delete;

    bool uploadBuffer(const VulkanDevice& device,
                      VulkanBufferResource& destination,
                      const void* data,
                      VkDeviceSize size,
                      VkBufferUsageFlags usage,
                      const char* debugName,
                      QString& lastError);
    bool submit(const VulkanDevice& device,
                VkCommandPool commandPool,
                VkQueue queue,
                QString& lastError);
    void discard(const VulkanDevice& device);

    bool empty() const { return copies_.empty(); }

private:
    struct CopyRequest {
        VkBuffer source = VK_NULL_HANDLE;
        VkBuffer destination = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        const char* debugName = nullptr;
    };

    std::vector<std::unique_ptr<VulkanBufferResource>> stagingResources_;
    std::vector<CopyRequest> copies_;
};
