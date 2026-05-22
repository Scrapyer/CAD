#include "VulkanContext.h"

#include <cstring>

namespace {

uint32_t requestedVulkanApiVersion()
{
#if defined(VK_API_VERSION_1_4)
    return VK_API_VERSION_1_4;
#elif defined(VK_API_VERSION_1_3)
    return VK_API_VERSION_1_3;
#elif defined(VK_API_VERSION_1_2)
    return VK_API_VERSION_1_2;
#else
    return VK_API_VERSION_1_1;
#endif
}

uint32_t supportedVulkanApiVersion()
{
#if defined(VK_VERSION_1_1)
    uint32_t version = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion(&version) == VK_SUCCESS) {
        return version;
    }
#endif
    return VK_API_VERSION_1_0;
}

std::vector<VkExtensionProperties> enumerateInstanceExtensions()
{
    uint32_t count = 0;
    VkResult result = vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    if (result != VK_SUCCESS || count == 0) {
        return {};
    }

    std::vector<VkExtensionProperties> extensions(count);
    result = vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
    if (result != VK_SUCCESS) {
        return {};
    }
    return extensions;
}

bool hasInstanceExtension(const std::vector<VkExtensionProperties>& extensions, const char* name)
{
    for (const VkExtensionProperties& extension : extensions) {
        if (std::strcmp(extension.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

bool appendUniqueExtension(std::vector<const char*>& extensions, const char* name)
{
    for (const char* extension : extensions) {
        if (std::strcmp(extension, name) == 0) {
            return true;
        }
    }
    extensions.push_back(name);
    return false;
}

} // namespace

VulkanContext::~VulkanContext()
{
    destroy();
}

bool VulkanContext::initialize()
{
    return initialize(CreateInfo{});
}

bool VulkanContext::initialize(const CreateInfo& createInfo)
{
    if (instance_ != VK_NULL_HANDLE) {
        return true;
    }

    apiVersion_ = applicationApiVersion();
    apiVersionText_ = formatVersion(apiVersion_);
    lastError_.clear();

    const std::vector<VkExtensionProperties> availableExtensions = enumerateInstanceExtensions();
    std::vector<const char*> enabledExtensions = createInfo.requiredExtensions;
    VkInstanceCreateFlags instanceFlags = 0;

    for (const char* extension : enabledExtensions) {
        if (!hasInstanceExtension(availableExtensions, extension)) {
            lastError_ = QStringLiteral("Missing Vulkan instance extension: ") + QString::fromUtf8(extension);
            return false;
        }
    }

#if defined(VK_KHR_portability_enumeration)
    if (createInfo.enablePortabilityEnumeration &&
        hasInstanceExtension(availableExtensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        appendUniqueExtension(enabledExtensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        instanceFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
#endif

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = createInfo.applicationName;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = createInfo.engineName;
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = apiVersion_;

    VkInstanceCreateInfo vkCreateInfo{};
    vkCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    vkCreateInfo.flags = instanceFlags;
    vkCreateInfo.pApplicationInfo = &appInfo;
    vkCreateInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
    vkCreateInfo.ppEnabledExtensionNames = enabledExtensions.empty() ? nullptr : enabledExtensions.data();

    VkResult result = vkCreateInstance(&vkCreateInfo, nullptr, &instance_);
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkCreateInstance failed: ") + formatResult(result);
        return false;
    }

    return true;
}

void VulkanContext::destroy()
{
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

QString VulkanContext::formatVersion(uint32_t version)
{
    return QString("%1.%2.%3")
        .arg(VK_VERSION_MAJOR(version))
        .arg(VK_VERSION_MINOR(version))
        .arg(VK_VERSION_PATCH(version));
}

QString VulkanContext::formatResult(VkResult result)
{
    return QString::number(static_cast<int>(result));
}

uint32_t VulkanContext::applicationApiVersion()
{
    const uint32_t requested = requestedVulkanApiVersion();
    const uint32_t supported = supportedVulkanApiVersion();
    return supported < requested ? supported : requested;
}
