#pragma once

#include <QString>

#include <vulkan/vulkan.h>

#include <vector>

class VulkanDevice;

/**
 * @brief Vulkan framebuffer 集合生命周期封装。
 *
 * 主 swapchain framebuffer 和拾取离屏 framebuffer 都通过该类管理，避免 renderer
 * 直接维护裸 `VkFramebuffer` 容器。
 */
class VulkanFramebufferResource {
public:
    VulkanFramebufferResource() = default;
    ~VulkanFramebufferResource() = default;

    VulkanFramebufferResource(const VulkanFramebufferResource&) = delete;
    VulkanFramebufferResource& operator=(const VulkanFramebufferResource&) = delete;

    bool create(const VulkanDevice& device,
                VkRenderPass renderPass,
                VkExtent2D extent,
                const std::vector<std::vector<VkImageView>>& attachmentSets,
                const char* debugName,
                QString& lastError);
    bool createSingle(const VulkanDevice& device,
                      VkRenderPass renderPass,
                      VkExtent2D extent,
                      const std::vector<VkImageView>& attachments,
                      const char* debugName,
                      QString& lastError);
    void destroy(const VulkanDevice& device);

    bool isValid() const { return !framebuffers_.empty(); }
    size_t count() const { return framebuffers_.size(); }
    VkFramebuffer framebuffer(size_t index) const { return framebuffers_[index]; }

private:
    std::vector<VkFramebuffer> framebuffers_;
};
