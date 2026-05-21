#pragma once

#include <QString>

#include <vulkan/vulkan.h>

#include <cstdint>

class VulkanDevice;

/**
 * @brief Vulkan buffer 资源封装。
 *
 * 动态资源可直接使用 host-visible 内存；静态几何资源通过 staging buffer
 * 上传到 device-local 内存，避免长期占用 CPU 可见内存路径。
 */
class VulkanBufferResource {
public:
    VulkanBufferResource() = default;
    ~VulkanBufferResource() = default;

    VulkanBufferResource(const VulkanBufferResource&) = delete;
    VulkanBufferResource& operator=(const VulkanBufferResource&) = delete;

    bool uploadHostVisible(const VulkanDevice& device,
                           const void* data,
                           VkDeviceSize size,
                           VkBufferUsageFlags usage,
                           const char* debugName,
                           QString& lastError);
    bool uploadDeviceLocal(const VulkanDevice& device,
                           const void* data,
                           VkDeviceSize size,
                           VkBufferUsageFlags usage,
                           VkCommandPool commandPool,
                           VkQueue queue,
                           const char* debugName,
                           QString& lastError);
    bool updateHostVisible(const VulkanDevice& device,
                           const void* data,
                           VkDeviceSize size,
                           const char* debugName,
                           QString& lastError);
    bool readHostVisible(const VulkanDevice& device,
                         void* data,
                         VkDeviceSize size,
                         const char* debugName,
                         QString& lastError) const;
    void destroy(const VulkanDevice& device);

    bool isValid() const { return buffer_ != VK_NULL_HANDLE; }
    VkBuffer buffer() const { return buffer_; }
    VkDeviceSize size() const { return size_; }

private:
    bool create(const VulkanDevice& device,
                VkDeviceSize size,
                VkBufferUsageFlags usage,
                VkMemoryPropertyFlags properties,
                QString& lastError);
    bool copyBuffer(const VulkanDevice& device,
                    VkCommandPool commandPool,
                    VkQueue queue,
                    VkBuffer source,
                    VkBuffer destination,
                    VkDeviceSize size,
                    const char* debugName,
                    QString& lastError) const;
    uint32_t findMemoryType(const VulkanDevice& device,
                            uint32_t typeFilter,
                            VkMemoryPropertyFlags properties) const;

    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
};
