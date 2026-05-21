#pragma once

#include <QString>

#include <vulkan/vulkan.h>

class VulkanDevice;

/**
 * @brief Vulkan descriptor set 资源封装。
 *
 * 当前覆盖 mesh scalar SSBO 的 storage-buffer descriptor，后续可扩展到多 descriptor
 * set 和更多资源绑定。
 */
class VulkanDescriptorResource {
public:
    VulkanDescriptorResource() = default;
    ~VulkanDescriptorResource() = default;

    VulkanDescriptorResource(const VulkanDescriptorResource&) = delete;
    VulkanDescriptorResource& operator=(const VulkanDescriptorResource&) = delete;

    bool createStorageBufferSet(const VulkanDevice& device,
                                VkDescriptorSetLayout layout,
                                VkBuffer buffer,
                                VkDeviceSize range,
                                QString& lastError);
    void destroy(const VulkanDevice& device);

    bool isValid() const { return descriptorSet_ != VK_NULL_HANDLE; }
    VkDescriptorSet descriptorSet() const { return descriptorSet_; }

private:
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
};
