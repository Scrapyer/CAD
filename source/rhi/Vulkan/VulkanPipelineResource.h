#pragma once

#include <QString>

#include <vulkan/vulkan.h>

class VulkanDevice;

/**
 * @brief Vulkan graphics pipeline 与 pipeline layout 生命周期封装。
 *
 * 当前用于传统固定管线路径，把各 pass 的 pipeline/layout 从 renderer 主类中收敛出来；
 * 后续可继续承接 pipeline cache、复用 layout 描述和更多 pass 级配置。
 */
class VulkanPipelineResource {
public:
    VulkanPipelineResource() = default;
    ~VulkanPipelineResource() = default;

    VulkanPipelineResource(const VulkanPipelineResource&) = delete;
    VulkanPipelineResource& operator=(const VulkanPipelineResource&) = delete;

    bool createGraphics(const VulkanDevice& device,
                        const VkPipelineLayoutCreateInfo& layoutInfo,
                        const VkGraphicsPipelineCreateInfo& pipelineInfo,
                        const char* debugName,
                        QString& lastError);
    void destroy(const VulkanDevice& device);

    bool isValid() const { return pipeline_ != VK_NULL_HANDLE && layout_ != VK_NULL_HANDLE; }
    VkPipeline pipeline() const { return pipeline_; }
    VkPipelineLayout layout() const { return layout_; }

private:
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};
