#pragma once

#include "VulkanFramebufferResource.h"

#include <QString>

#include <vulkan/vulkan.h>

#include <vector>

class VulkanDevice;
class VulkanSwapchain;

/**
 * @brief Vulkan swapchain image view 与 framebuffer 集合封装。
 *
 * resize / recreate 时外层只需要销毁和重建本对象，swapchain image view 与
 * framebuffer 的数量、顺序和 attachment 关系由这里维护。
 */
class VulkanSwapchainFrameResources {
public:
    VulkanSwapchainFrameResources() = default;
    ~VulkanSwapchainFrameResources() = default;

    VulkanSwapchainFrameResources(const VulkanSwapchainFrameResources&) = delete;
    VulkanSwapchainFrameResources& operator=(const VulkanSwapchainFrameResources&) = delete;

    bool create(const VulkanDevice& device,
                const VulkanSwapchain& swapchain,
                VkRenderPass renderPass,
                VkImageView depthImageView,
                QString& lastError);
    void destroy(const VulkanDevice& device);

    bool isValid() const { return framebuffers_.isValid(); }
    size_t count() const { return framebuffers_.count(); }
    VkFramebuffer framebuffer(size_t index) const { return framebuffers_.framebuffer(index); }

private:
    bool createImageViews(const VulkanDevice& device,
                          const VulkanSwapchain& swapchain,
                          QString& lastError);

    std::vector<VkImageView> imageViews_;
    VulkanFramebufferResource framebuffers_;
};
