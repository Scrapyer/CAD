#pragma once

#include "VulkanBufferResource.h"
#include "VulkanDepthResource.h"
#include "VulkanFramebufferResource.h"

#include <QString>

#include <vulkan/vulkan.h>

#include <cstdint>

class VulkanDevice;

/**
 * @brief Vulkan 离屏拾取附件和 readback buffer 生命周期封装。
 *
 * 管理 pick color image、pick depth attachment、framebuffer 和单像素 readback buffer。
 */
class VulkanPickResources {
public:
    VulkanPickResources() = default;
    ~VulkanPickResources() = default;

    VulkanPickResources(const VulkanPickResources&) = delete;
    VulkanPickResources& operator=(const VulkanPickResources&) = delete;

    bool create(const VulkanDevice& device,
                VkRenderPass renderPass,
                VkFormat depthFormat,
                uint32_t width,
                uint32_t height,
                QString& lastError);
    bool ensureReadbackBuffer(const VulkanDevice& device, QString& lastError);
    bool readPixel(const VulkanDevice& device, unsigned char pixel[4], QString& lastError) const;
    void destroy(const VulkanDevice& device);

    bool isValid() const { return framebuffer_.isValid() && colorImage_ != VK_NULL_HANDLE; }
    VkFramebuffer framebuffer() const { return framebuffer_.framebuffer(0); }
    VkImage colorImage() const { return colorImage_; }
    VkBuffer readbackBuffer() const { return readbackResource_.buffer(); }
    VkExtent2D extent() const { return extent_; }

private:
    bool createColorImage(const VulkanDevice& device,
                          uint32_t width,
                          uint32_t height,
                          QString& lastError);
    bool createColorImageView(const VulkanDevice& device, QString& lastError);
    uint32_t findMemoryType(const VulkanDevice& device,
                            uint32_t typeFilter,
                            VkMemoryPropertyFlags properties) const;

    VkImage colorImage_ = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory_ = VK_NULL_HANDLE;
    VkImageView colorImageView_ = VK_NULL_HANDLE;
    VulkanDepthResource depthResource_;
    VulkanFramebufferResource framebuffer_;
    VulkanBufferResource readbackResource_;
    VkExtent2D extent_ = {0, 0};
};
