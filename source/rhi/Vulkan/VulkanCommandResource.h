#pragma once

#include <QString>

#include <vulkan/vulkan.h>

class VulkanDevice;

/**
 * @brief Vulkan command pool 与主 command buffer 生命周期封装。
 *
 * 当前 renderer 使用一条 graphics command buffer 录制主视口、拾取和 readback 命令；
 * 静态 buffer staging 上传也复用同一个可 reset command pool 分配一次性 copy buffer。
 */
class VulkanCommandResource {
public:
    VulkanCommandResource() = default;
    ~VulkanCommandResource() = default;

    VulkanCommandResource(const VulkanCommandResource&) = delete;
    VulkanCommandResource& operator=(const VulkanCommandResource&) = delete;

    bool create(const VulkanDevice& device, QString& lastError);
    void destroy(const VulkanDevice& device);

    bool resetCommandBuffer(const VulkanDevice& device, QString& lastError) const;

    bool isValid() const { return commandPool_ != VK_NULL_HANDLE && commandBuffer_ != VK_NULL_HANDLE; }
    VkCommandPool pool() const { return commandPool_; }
    VkCommandBuffer buffer() const { return commandBuffer_; }
    const VkCommandBuffer* bufferAddress() const { return &commandBuffer_; }

private:
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
};
