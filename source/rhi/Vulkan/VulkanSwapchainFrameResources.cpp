#include "VulkanSwapchainFrameResources.h"

#include "VulkanContext.h"
#include "VulkanDevice.h"
#include "VulkanSwapchain.h"

bool VulkanSwapchainFrameResources::create(const VulkanDevice& device,
                                           const VulkanSwapchain& swapchain,
                                           VkRenderPass renderPass,
                                           VkImageView depthImageView,
                                           QString& lastError)
{
    destroy(device);
    if (depthImageView == VK_NULL_HANDLE) {
        lastError = QStringLiteral("Vulkan depth image view is not initialized");
        return false;
    }
    if (!createImageViews(device, swapchain, lastError)) {
        return false;
    }

    std::vector<std::vector<VkImageView>> attachmentSets;
    attachmentSets.reserve(imageViews_.size());
    for (VkImageView imageView : imageViews_) {
        attachmentSets.push_back({imageView, depthImageView});
    }
    return framebuffers_.create(device,
                                renderPass,
                                swapchain.extent(),
                                attachmentSets,
                                "swapchain",
                                lastError);
}

void VulkanSwapchainFrameResources::destroy(const VulkanDevice& device)
{
    VkDevice vkDevice = device.device();
    framebuffers_.destroy(device);
    if (vkDevice != VK_NULL_HANDLE) {
        for (VkImageView imageView : imageViews_) {
            if (imageView != VK_NULL_HANDLE) {
                vkDestroyImageView(vkDevice, imageView, nullptr);
            }
        }
    }
    imageViews_.clear();
}

bool VulkanSwapchainFrameResources::createImageViews(const VulkanDevice& device,
                                                     const VulkanSwapchain& swapchain,
                                                     QString& lastError)
{
    imageViews_.resize(swapchain.images().size(), VK_NULL_HANDLE);
    for (size_t i = 0; i < swapchain.images().size(); ++i) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapchain.images()[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapchain.imageFormat();
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        VkResult result = vkCreateImageView(device.device(), &createInfo, nullptr, &imageViews_[i]);
        if (result != VK_SUCCESS) {
            lastError = QStringLiteral("vkCreateImageView(swapchain) failed: ") +
                VulkanContext::formatResult(result);
            return false;
        }
    }
    return true;
}
