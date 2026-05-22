#pragma once

#include "VulkanSurface.h"

#include <QString>

#include <vulkan/vulkan.h>

#include <vector>

class QWindow;

/**
 * @brief macOS Vulkan surface 工厂。
 *
 * Qt 6.8.3 macOS 包禁用了 QVulkanInstance/QVulkanWindow，因此这里直接
 * 通过 QWindow 的原生 NSView/CAMetalLayer 创建 VkSurfaceKHR。
 */
class VulkanMacOSSurfaceFactory {
public:
    static std::vector<const char*> requiredInstanceExtensions();
    static VulkanSurface createSurface(VkInstance instance, QWindow* window, QString* error = nullptr);
};
