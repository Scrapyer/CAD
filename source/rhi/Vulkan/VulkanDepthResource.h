#pragma once

#include <QString>

#include <vulkan/vulkan.h>

#include <cstdint>

class VulkanDevice;

/**
 * @brief Vulkan depth image / memory / view 生命周期封装。
 *
 * 主视口和离屏拾取都需要 depth attachment；该类集中管理 image、memory 和
 * image view，避免渲染器直接维护三组裸句柄。
 */
class VulkanDepthResource {
public:
    VulkanDepthResource() = default;
    ~VulkanDepthResource() = default;

    VulkanDepthResource(const VulkanDepthResource&) = delete;
    VulkanDepthResource& operator=(const VulkanDepthResource&) = delete;

    bool create(const VulkanDevice& device,
                uint32_t width,
                uint32_t height,
                VkFormat format,
                const char* debugName,
                QString& lastError);
    void destroy(const VulkanDevice& device);

    bool isValid() const { return imageView_ != VK_NULL_HANDLE; }
    VkImageView imageView() const { return imageView_; }
    VkFormat format() const { return format_; }
    VkExtent2D extent() const { return extent_; }

    static VkFormat findDepthFormat(const VulkanDevice& device);
    static bool hasStencilComponent(VkFormat format);

private:
    bool createImage(const VulkanDevice& device,
                     uint32_t width,
                     uint32_t height,
                     VkFormat format,
                     QString& lastError);
    bool createImageView(const VulkanDevice& device,
                         VkImageAspectFlags aspectMask,
                         const char* debugName,
                         QString& lastError);
    uint32_t findMemoryType(const VulkanDevice& device,
                            uint32_t typeFilter,
                            VkMemoryPropertyFlags properties) const;

    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView imageView_ = VK_NULL_HANDLE;
    VkExtent2D extent_ = {0, 0};
};
