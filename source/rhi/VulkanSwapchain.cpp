#include "VulkanSwapchain.h"

#include "VulkanContext.h"
#include "VulkanDevice.h"

#include <algorithm>

namespace {

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

SwapchainSupportDetails querySwapchainSupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
    SwapchainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &details.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    if (formatCount > 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    if (presentModeCount > 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            physicalDevice, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
{
    for (const VkSurfaceFormatKHR& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats.front();
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& presentModes, bool vsync)
{
    if (!vsync) {
        for (VkPresentModeKHR presentMode : presentModes) {
            if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return presentMode;
            }
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height)
{
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }

    VkExtent2D extent{width, height};
    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return extent;
}

VkCompositeAlphaFlagBitsKHR chooseCompositeAlpha(const VkSurfaceCapabilitiesKHR& capabilities)
{
    const VkCompositeAlphaFlagBitsKHR candidates[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
    };

    for (VkCompositeAlphaFlagBitsKHR candidate : candidates) {
        if (capabilities.supportedCompositeAlpha & candidate) {
            return candidate;
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

} // namespace

VulkanSwapchain::~VulkanSwapchain() = default;

bool VulkanSwapchain::initialize(
    const VulkanContext& context,
    const VulkanDevice& device,
    const CreateInfo& createInfo)
{
    lastError_.clear();

    if (!context.isInitialized() || !device.isInitialized()) {
        lastError_ = QStringLiteral("Vulkan context/device is not initialized");
        return false;
    }
    if (createInfo.surface == VK_NULL_HANDLE) {
        lastError_ = QStringLiteral("Vulkan surface is null");
        return false;
    }

    const SwapchainSupportDetails support =
        querySwapchainSupport(device.physicalDevice(), createInfo.surface);
    if (support.formats.empty() || support.presentModes.empty()) {
        lastError_ = QStringLiteral("Vulkan surface has no swapchain support");
        return false;
    }

    const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.formats);
    const VkPresentModeKHR presentMode = choosePresentMode(support.presentModes, createInfo.vsync);
    const VkExtent2D swapExtent = chooseExtent(support.capabilities, createInfo.width, createInfo.height);

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
        imageCount = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchainCreateInfo{};
    swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainCreateInfo.surface = createInfo.surface;
    swapchainCreateInfo.minImageCount = imageCount;
    swapchainCreateInfo.imageFormat = surfaceFormat.format;
    swapchainCreateInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapchainCreateInfo.imageExtent = swapExtent;
    swapchainCreateInfo.imageArrayLayers = 1;
    swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    const VulkanQueueFamilyIndices queueFamilies = device.queueFamilies();
    const uint32_t queueFamilyIndices[] = {queueFamilies.graphics, queueFamilies.present};
    if (queueFamilies.graphics != queueFamilies.present) {
        swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchainCreateInfo.queueFamilyIndexCount = 2;
        swapchainCreateInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    swapchainCreateInfo.preTransform = support.capabilities.currentTransform;
    swapchainCreateInfo.compositeAlpha = chooseCompositeAlpha(support.capabilities);
    swapchainCreateInfo.presentMode = presentMode;
    swapchainCreateInfo.clipped = VK_TRUE;
    swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

    VkResult result = vkCreateSwapchainKHR(device.device(), &swapchainCreateInfo, nullptr, &swapchain_);
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkCreateSwapchainKHR failed: ") + VulkanContext::formatResult(result);
        return false;
    }

    vkGetSwapchainImagesKHR(device.device(), swapchain_, &imageCount, nullptr);
    images_.resize(imageCount);
    vkGetSwapchainImagesKHR(device.device(), swapchain_, &imageCount, images_.data());

    imageFormat_ = surfaceFormat.format;
    extent_ = swapExtent;
    return true;
}

void VulkanSwapchain::destroy(const VulkanDevice& device)
{
    if (swapchain_ != VK_NULL_HANDLE && device.device() != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device.device(), swapchain_, nullptr);
    }
    swapchain_ = VK_NULL_HANDLE;
    imageFormat_ = VK_FORMAT_UNDEFINED;
    extent_ = {0, 0};
    images_.clear();
}
