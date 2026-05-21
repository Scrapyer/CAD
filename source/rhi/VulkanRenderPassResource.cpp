#include "VulkanRenderPassResource.h"

#include "VulkanContext.h"
#include "VulkanDevice.h"

bool VulkanRenderPassResource::create(const VulkanDevice& device,
                                      const VkRenderPassCreateInfo& createInfo,
                                      const char* debugName,
                                      QString& lastError)
{
    destroy(device);
    VkResult result = vkCreateRenderPass(device.device(), &createInfo, nullptr, &renderPass_);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkCreateRenderPass(%1) failed: %2")
            .arg(QString::fromUtf8(debugName))
            .arg(VulkanContext::formatResult(result));
        renderPass_ = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void VulkanRenderPassResource::destroy(const VulkanDevice& device)
{
    VkDevice vkDevice = device.device();
    if (vkDevice != VK_NULL_HANDLE && renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vkDevice, renderPass_, nullptr);
    }
    renderPass_ = VK_NULL_HANDLE;
}
