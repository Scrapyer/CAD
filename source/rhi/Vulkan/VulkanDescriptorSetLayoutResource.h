#pragma once

#include <QString>

#include <vulkan/vulkan.h>

class VulkanDevice;

/**
 * @brief Vulkan descriptor set layout 生命周期封装。
 *
 * layout 通常和 pipeline layout 同生命周期；descriptor pool/set 则可随具体 buffer
 * 重建，因此这里单独管理 layout，避免把两类生命周期绑死。
 */
class VulkanDescriptorSetLayoutResource {
public:
    VulkanDescriptorSetLayoutResource() = default;
    ~VulkanDescriptorSetLayoutResource() = default;

    VulkanDescriptorSetLayoutResource(const VulkanDescriptorSetLayoutResource&) = delete;
    VulkanDescriptorSetLayoutResource& operator=(const VulkanDescriptorSetLayoutResource&) = delete;

    bool create(const VulkanDevice& device,
                const VkDescriptorSetLayoutCreateInfo& createInfo,
                const char* debugName,
                QString& lastError);
    void destroy(const VulkanDevice& device);

    bool isValid() const { return layout_ != VK_NULL_HANDLE; }
    VkDescriptorSetLayout handle() const { return layout_; }

private:
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
};
