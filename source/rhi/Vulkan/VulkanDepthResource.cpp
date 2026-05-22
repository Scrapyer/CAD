#include "VulkanDepthResource.h"

#include "VulkanContext.h"
#include "VulkanDevice.h"

#include <array>

bool VulkanDepthResource::create(const VulkanDevice& device,
                                 uint32_t width,
                                 uint32_t height,
                                 VkFormat format,
                                 const char* debugName,
                                 QString& lastError)
{
    destroy(device);
    if (format == VK_FORMAT_UNDEFINED) {
        lastError = QStringLiteral("Vulkan depth format is undefined");
        return false;
    }

    format_ = format;
    if (!createImage(device, width, height, format, lastError)) {
        return false;
    }

    const VkImageAspectFlags aspectMask = hasStencilComponent(format)
        ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
        : VK_IMAGE_ASPECT_DEPTH_BIT;
    if (!createImageView(device, aspectMask, debugName, lastError)) {
        destroy(device);
        return false;
    }

    extent_ = {width, height};
    return true;
}

void VulkanDepthResource::destroy(const VulkanDevice& device)
{
    VkDevice vkDevice = device.device();
    if (vkDevice != VK_NULL_HANDLE) {
        if (imageView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(vkDevice, imageView_, nullptr);
        }
        if (image_ != VK_NULL_HANDLE) {
            vkDestroyImage(vkDevice, image_, nullptr);
        }
        if (memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(vkDevice, memory_, nullptr);
        }
    }
    format_ = VK_FORMAT_UNDEFINED;
    image_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
    imageView_ = VK_NULL_HANDLE;
    extent_ = {0, 0};
}

VkFormat VulkanDepthResource::findDepthFormat(const VulkanDevice& device)
{
    const std::array<VkFormat, 3> candidates = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT
    };

    for (VkFormat format : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(device.physicalDevice(), format, &properties);
        if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return format;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

bool VulkanDepthResource::hasStencilComponent(VkFormat format)
{
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
        format == VK_FORMAT_D24_UNORM_S8_UINT;
}

bool VulkanDepthResource::createImage(const VulkanDevice& device,
                                      uint32_t width,
                                      uint32_t height,
                                      VkFormat format,
                                      QString& lastError)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateImage(device.device(), &imageInfo, nullptr, &image_);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkCreateImage(depth) failed: ") +
            VulkanContext::formatResult(result);
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device.device(), image_, &requirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(
        device,
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocInfo.memoryTypeIndex == UINT32_MAX) {
        lastError = QStringLiteral("No compatible Vulkan depth image memory type found");
        destroy(device);
        return false;
    }

    result = vkAllocateMemory(device.device(), &allocInfo, nullptr, &memory_);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkAllocateMemory(depth) failed: ") +
            VulkanContext::formatResult(result);
        destroy(device);
        return false;
    }

    result = vkBindImageMemory(device.device(), image_, memory_, 0);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkBindImageMemory(depth) failed: ") +
            VulkanContext::formatResult(result);
        destroy(device);
        return false;
    }
    return true;
}

bool VulkanDepthResource::createImageView(const VulkanDevice& device,
                                          VkImageAspectFlags aspectMask,
                                          const char* debugName,
                                          QString& lastError)
{
    VkImageViewCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = image_;
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = format_;
    createInfo.subresourceRange.aspectMask = aspectMask;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    VkResult result = vkCreateImageView(device.device(), &createInfo, nullptr, &imageView_);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkCreateImageView(%1 depth) failed: %2")
            .arg(QString::fromUtf8(debugName))
            .arg(VulkanContext::formatResult(result));
        return false;
    }
    return true;
}

uint32_t VulkanDepthResource::findMemoryType(const VulkanDevice& device,
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
