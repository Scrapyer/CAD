#include "VulkanPickResources.h"

#include "VulkanContext.h"
#include "VulkanDevice.h"

#include <array>

bool VulkanPickResources::create(const VulkanDevice& device,
                                 VkRenderPass renderPass,
                                 VkFormat depthFormat,
                                 uint32_t width,
                                 uint32_t height,
                                 QString& lastError)
{
    if (isValid() && extent_.width == width && extent_.height == height) {
        return true;
    }

    destroy(device);
    if (!createColorImage(device, width, height, lastError)) {
        destroy(device);
        return false;
    }
    if (!createColorImageView(device, lastError)) {
        destroy(device);
        return false;
    }
    if (!depthResource_.create(device, width, height, depthFormat, "pick", lastError)) {
        destroy(device);
        return false;
    }

    const std::vector<VkImageView> attachments = {
        colorImageView_,
        depthResource_.imageView()
    };
    if (!framebuffer_.createSingle(device,
                                   renderPass,
                                   VkExtent2D{width, height},
                                   attachments,
                                   "pick",
                                   lastError)) {
        destroy(device);
        return false;
    }

    extent_ = {width, height};
    return true;
}

bool VulkanPickResources::ensureReadbackBuffer(const VulkanDevice& device, QString& lastError)
{
    if (readbackResource_.isValid() && readbackResource_.size() >= 4) {
        return true;
    }

    const unsigned char pixel[4] = {0, 0, 0, 0};
    return readbackResource_.uploadHostVisible(device,
                                               pixel,
                                               sizeof(pixel),
                                               VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                               "pick readback",
                                               lastError);
}

bool VulkanPickResources::readPixel(const VulkanDevice& device,
                                    unsigned char pixel[4],
                                    QString& lastError) const
{
    return readbackResource_.readHostVisible(device,
                                             pixel,
                                             4,
                                             "pick readback",
                                             lastError);
}

void VulkanPickResources::destroy(const VulkanDevice& device)
{
    VkDevice vkDevice = device.device();
    readbackResource_.destroy(device);
    framebuffer_.destroy(device);
    depthResource_.destroy(device);
    if (vkDevice != VK_NULL_HANDLE) {
        if (colorImageView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(vkDevice, colorImageView_, nullptr);
        }
        if (colorImage_ != VK_NULL_HANDLE) {
            vkDestroyImage(vkDevice, colorImage_, nullptr);
        }
        if (colorMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(vkDevice, colorMemory_, nullptr);
        }
    }
    colorImage_ = VK_NULL_HANDLE;
    colorMemory_ = VK_NULL_HANDLE;
    colorImageView_ = VK_NULL_HANDLE;
    extent_ = {0, 0};
}

bool VulkanPickResources::createColorImage(const VulkanDevice& device,
                                           uint32_t width,
                                           uint32_t height,
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
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateImage(device.device(), &imageInfo, nullptr, &colorImage_);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkCreateImage(pick color) failed: ") +
            VulkanContext::formatResult(result);
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device.device(), colorImage_, &requirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(
        device,
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocInfo.memoryTypeIndex == UINT32_MAX) {
        lastError = QStringLiteral("No compatible Vulkan pick color memory type found");
        return false;
    }

    result = vkAllocateMemory(device.device(), &allocInfo, nullptr, &colorMemory_);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkAllocateMemory(pick color) failed: ") +
            VulkanContext::formatResult(result);
        return false;
    }

    result = vkBindImageMemory(device.device(), colorImage_, colorMemory_, 0);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkBindImageMemory(pick color) failed: ") +
            VulkanContext::formatResult(result);
        return false;
    }
    return true;
}

bool VulkanPickResources::createColorImageView(const VulkanDevice& device, QString& lastError)
{
    VkImageViewCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = colorImage_;
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    VkResult result = vkCreateImageView(device.device(), &createInfo, nullptr, &colorImageView_);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkCreateImageView(pick color) failed: ") +
            VulkanContext::formatResult(result);
        return false;
    }
    return true;
}

uint32_t VulkanPickResources::findMemoryType(const VulkanDevice& device,
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
