#pragma once

#include "RenderBackend.h"

#include <QString>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

class VulkanContext;

struct VulkanQueueFamilyIndices {
    uint32_t graphics = UINT32_MAX;
    uint32_t present = UINT32_MAX;

    bool hasGraphics() const { return graphics != UINT32_MAX; }
    bool hasPresent() const { return present != UINT32_MAX; }
};

/**
 * @brief Vulkan 物理设备、逻辑设备和基础队列封装。
 */
class VulkanDevice {
public:
    VulkanDevice() = default;
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    bool initialize(const VulkanContext& context, VkSurfaceKHR surface = VK_NULL_HANDLE);
    void destroy();

    bool isInitialized() const { return device_ != VK_NULL_HANDLE; }
    VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    VkDevice device() const { return device_; }
    VkSurfaceKHR surface() const { return surface_; }
    VkQueue graphicsQueue() const { return graphicsQueue_; }
    VkQueue presentQueue() const { return presentQueue_; }
    const VulkanQueueFamilyIndices& queueFamilies() const { return queueFamilies_; }
    const RenderBackendInfo& info() const { return info_; }
    const QString& lastError() const { return lastError_; }

private:
    bool selectPhysicalDevice(const VulkanContext& context, VkSurfaceKHR surface);
    VulkanQueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) const;
    std::vector<const char*> collectDeviceExtensions(VkPhysicalDevice device, bool requireSwapchain) const;

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    VulkanQueueFamilyIndices queueFamilies_;
    RenderBackendInfo info_;
    QString lastError_;
};
