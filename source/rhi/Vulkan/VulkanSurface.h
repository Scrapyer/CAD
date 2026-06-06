#pragma once

#include "ferender_export.h"

#include <vulkan/vulkan.h>

/**
 * @brief VkSurfaceKHR 生命周期封装。
 *
 * 具体平台 surface 由后续 macOS/Windows 工厂创建；这里先统一 ownership。
 */
class FERENDER_EXPORT VulkanSurface {
public:
    VulkanSurface() = default;
    VulkanSurface(VkInstance instance, VkSurfaceKHR surface, bool ownsSurface = true);
    ~VulkanSurface();

    VulkanSurface(const VulkanSurface&) = delete;
    VulkanSurface& operator=(const VulkanSurface&) = delete;

    VulkanSurface(VulkanSurface&& other) noexcept;
    VulkanSurface& operator=(VulkanSurface&& other) noexcept;

    void reset(VkInstance instance = VK_NULL_HANDLE,
               VkSurfaceKHR surface = VK_NULL_HANDLE,
               bool ownsSurface = true);
    VkSurfaceKHR release();

    bool isValid() const { return surface_ != VK_NULL_HANDLE; }
    VkSurfaceKHR handle() const { return surface_; }

private:
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    bool ownsSurface_ = true;
};
