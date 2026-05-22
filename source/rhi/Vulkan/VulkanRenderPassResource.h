#pragma once

#include <QString>

#include <vulkan/vulkan.h>

class VulkanDevice;

/**
 * @brief Vulkan render pass 生命周期封装。
 *
 * 先承接主视口和拾取 render pass 的 create/destroy，后续 pipeline、frame graph
 * 拆分时可以继续把 attachment 描述上移到更正式的 pass 描述对象。
 */
class VulkanRenderPassResource {
public:
    VulkanRenderPassResource() = default;
    ~VulkanRenderPassResource() = default;

    VulkanRenderPassResource(const VulkanRenderPassResource&) = delete;
    VulkanRenderPassResource& operator=(const VulkanRenderPassResource&) = delete;

    bool create(const VulkanDevice& device,
                const VkRenderPassCreateInfo& createInfo,
                const char* debugName,
                QString& lastError);
    void destroy(const VulkanDevice& device);

    bool isValid() const { return renderPass_ != VK_NULL_HANDLE; }
    VkRenderPass handle() const { return renderPass_; }

private:
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
};
