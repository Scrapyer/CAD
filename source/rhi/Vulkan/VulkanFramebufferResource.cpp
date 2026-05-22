#include "VulkanFramebufferResource.h"

#include "VulkanContext.h"
#include "VulkanDevice.h"

bool VulkanFramebufferResource::create(
    const VulkanDevice& device,
    VkRenderPass renderPass,
    VkExtent2D extent,
    const std::vector<std::vector<VkImageView>>& attachmentSets,
    const char* debugName,
    QString& lastError)
{
    destroy(device);

    if (renderPass == VK_NULL_HANDLE) {
        lastError = QStringLiteral("Vulkan framebuffer(%1) render pass is null")
            .arg(QString::fromUtf8(debugName));
        return false;
    }

    framebuffers_.resize(attachmentSets.size(), VK_NULL_HANDLE);
    for (size_t i = 0; i < attachmentSets.size(); ++i) {
        const std::vector<VkImageView>& attachments = attachmentSets[i];

        VkFramebufferCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        createInfo.renderPass = renderPass;
        createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        createInfo.pAttachments = attachments.data();
        createInfo.width = extent.width;
        createInfo.height = extent.height;
        createInfo.layers = 1;

        VkResult result = vkCreateFramebuffer(device.device(), &createInfo, nullptr, &framebuffers_[i]);
        if (result != VK_SUCCESS) {
            lastError = QStringLiteral("vkCreateFramebuffer(%1) failed: %2")
                .arg(QString::fromUtf8(debugName))
                .arg(VulkanContext::formatResult(result));
            destroy(device);
            return false;
        }
    }

    return true;
}

bool VulkanFramebufferResource::createSingle(const VulkanDevice& device,
                                             VkRenderPass renderPass,
                                             VkExtent2D extent,
                                             const std::vector<VkImageView>& attachments,
                                             const char* debugName,
                                             QString& lastError)
{
    std::vector<std::vector<VkImageView>> attachmentSets;
    attachmentSets.push_back(attachments);
    return create(device, renderPass, extent, attachmentSets, debugName, lastError);
}

void VulkanFramebufferResource::destroy(const VulkanDevice& device)
{
    VkDevice vkDevice = device.device();
    if (vkDevice != VK_NULL_HANDLE) {
        for (VkFramebuffer framebuffer : framebuffers_) {
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(vkDevice, framebuffer, nullptr);
            }
        }
    }
    framebuffers_.clear();
}
