#include "VulkanDevice.h"

#include "VulkanContext.h"

#include <cstring>
#include <set>

namespace {

std::vector<VkPhysicalDevice> enumeratePhysicalDevices(VkInstance instance)
{
    uint32_t count = 0;
    VkResult result = vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (result != VK_SUCCESS || count == 0) {
        return {};
    }

    std::vector<VkPhysicalDevice> devices(count);
    result = vkEnumeratePhysicalDevices(instance, &count, devices.data());
    if (result != VK_SUCCESS) {
        return {};
    }
    return devices;
}

std::vector<VkExtensionProperties> enumerateDeviceExtensions(VkPhysicalDevice device)
{
    uint32_t count = 0;
    VkResult result = vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    if (result != VK_SUCCESS || count == 0) {
        return {};
    }

    std::vector<VkExtensionProperties> extensions(count);
    result = vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
    if (result != VK_SUCCESS) {
        return {};
    }
    return extensions;
}

bool hasDeviceExtension(const std::vector<VkExtensionProperties>& extensions, const char* name)
{
    for (const VkExtensionProperties& extension : extensions) {
        if (std::strcmp(extension.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

VulkanDevice::~VulkanDevice()
{
    destroy();
}

bool VulkanDevice::initialize(const VulkanContext& context, VkSurfaceKHR surface)
{
    if (device_ != VK_NULL_HANDLE) {
        return surface == VK_NULL_HANDLE || surface_ == surface;
    }

    lastError_.clear();

    if (!context.isInitialized()) {
        lastError_ = QStringLiteral("VulkanContext is not initialized");
        return false;
    }

    if (!selectPhysicalDevice(context, surface)) {
        return false;
    }
    surface_ = surface;

    queueFamilies_ = findQueueFamilies(physicalDevice_, surface);
    const bool requirePresent = surface != VK_NULL_HANDLE;
    if (!queueFamilies_.hasGraphics() || (requirePresent && !queueFamilies_.hasPresent())) {
        lastError_ = QStringLiteral("No suitable Vulkan queue family");
        return false;
    }
    if (!queueFamilies_.hasPresent()) {
        queueFamilies_.present = queueFamilies_.graphics;
    }

    const float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        queueFamilies_.graphics,
        queueFamilies_.present
    };

    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    const std::vector<const char*> enabledExtensions =
        collectDeviceExtensions(physicalDevice_, requirePresent);
    if (requirePresent && enabledExtensions.empty()) {
        lastError_ = QStringLiteral("Missing required Vulkan swapchain device extension");
        return false;
    }

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
    createInfo.ppEnabledExtensionNames = enabledExtensions.empty() ? nullptr : enabledExtensions.data();

    VkResult result = vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_);
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkCreateDevice failed: ") + VulkanContext::formatResult(result);
        return false;
    }

    vkGetDeviceQueue(device_, queueFamilies_.graphics, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, queueFamilies_.present, 0, &presentQueue_);

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
    info_.renderer = QString::fromUtf8(properties.deviceName);
    info_.version = VulkanContext::formatVersion(properties.apiVersion);
    info_.vendor = QStringLiteral("vendorId=0x%1 deviceId=0x%2")
        .arg(properties.vendorID, 0, 16)
        .arg(properties.deviceID, 0, 16);
    info_.shadingLanguageVersion = QStringLiteral("SPIR-V");

    return true;
}

void VulkanDevice::destroy()
{
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    physicalDevice_ = VK_NULL_HANDLE;
    surface_ = VK_NULL_HANDLE;
    graphicsQueue_ = VK_NULL_HANDLE;
    presentQueue_ = VK_NULL_HANDLE;
    queueFamilies_ = {};
}

bool VulkanDevice::selectPhysicalDevice(const VulkanContext& context, VkSurfaceKHR surface)
{
    const std::vector<VkPhysicalDevice> devices = enumeratePhysicalDevices(context.instance());
    if (devices.empty()) {
        lastError_ = QStringLiteral("No Vulkan physical device");
        return false;
    }

    const bool requirePresent = surface != VK_NULL_HANDLE;
    for (VkPhysicalDevice device : devices) {
        const VulkanQueueFamilyIndices indices = findQueueFamilies(device, surface);
        if (!indices.hasGraphics() || (requirePresent && !indices.hasPresent())) {
            continue;
        }
        if (requirePresent && collectDeviceExtensions(device, true).empty()) {
            continue;
        }

        physicalDevice_ = device;
        return true;
    }

    lastError_ = QStringLiteral("No suitable Vulkan physical device");
    return false;
}

VulkanQueueFamilyIndices VulkanDevice::findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) const
{
    VulkanQueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    if (queueFamilyCount == 0) {
        return indices;
    }

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics = i;
        }

        if (surface != VK_NULL_HANDLE) {
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
            if (presentSupport) {
                indices.present = i;
            }
        }

        if (indices.hasGraphics() && (surface == VK_NULL_HANDLE || indices.hasPresent())) {
            break;
        }
    }

    return indices;
}

std::vector<const char*> VulkanDevice::collectDeviceExtensions(VkPhysicalDevice device, bool requireSwapchain) const
{
    const std::vector<VkExtensionProperties> availableExtensions = enumerateDeviceExtensions(device);
    std::vector<const char*> extensions;

    if (requireSwapchain) {
        if (!hasDeviceExtension(availableExtensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            return {};
        }
        extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

#if defined(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)
    if (hasDeviceExtension(availableExtensions, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
    }
#endif

    return extensions;
}
