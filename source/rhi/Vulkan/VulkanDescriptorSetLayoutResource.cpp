#include "VulkanDescriptorSetLayoutResource.h"

#include "VulkanContext.h"
#include "VulkanDevice.h"

bool VulkanDescriptorSetLayoutResource::create(const VulkanDevice& device,
                                               const VkDescriptorSetLayoutCreateInfo& createInfo,
                                               const char* debugName,
                                               QString& lastError)
{
    destroy(device);
    VkResult result = vkCreateDescriptorSetLayout(device.device(), &createInfo, nullptr, &layout_);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkCreateDescriptorSetLayout(%1) failed: %2")
            .arg(QString::fromUtf8(debugName))
            .arg(VulkanContext::formatResult(result));
        layout_ = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void VulkanDescriptorSetLayoutResource::destroy(const VulkanDevice& device)
{
    VkDevice vkDevice = device.device();
    if (vkDevice != VK_NULL_HANDLE && layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vkDevice, layout_, nullptr);
    }
    layout_ = VK_NULL_HANDLE;
}
