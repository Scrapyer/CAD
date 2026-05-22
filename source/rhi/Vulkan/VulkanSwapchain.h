#pragma once

#include <QString>

#include <vulkan/vulkan.h>

#include <vector>

class VulkanContext;
class VulkanDevice;

/**
 * @brief Vulkan swapchain 生命周期封装。
 *
 * 当前不直接接入 GLWidget；后续引入 Vulkan 宿主窗口后，将由该类管理
 * surface 对应的 swapchain image。
 */
class VulkanSwapchain {
public:
    struct CreateInfo {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        uint32_t width = 1;
        uint32_t height = 1;
        bool vsync = true;
    };

    VulkanSwapchain() = default;
    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    bool initialize(const VulkanContext& context, const VulkanDevice& device, const CreateInfo& createInfo);
    void destroy(const VulkanDevice& device);

    bool isInitialized() const { return swapchain_ != VK_NULL_HANDLE; }
    VkSwapchainKHR swapchain() const { return swapchain_; }
    VkFormat imageFormat() const { return imageFormat_; }
    VkExtent2D extent() const { return extent_; }
    const std::vector<VkImage>& images() const { return images_; }
    const QString& lastError() const { return lastError_; }

private:
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat imageFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_ = {0, 0};
    std::vector<VkImage> images_;
    QString lastError_;
};
