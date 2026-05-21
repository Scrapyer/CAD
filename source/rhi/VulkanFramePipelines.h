#pragma once

#include "VulkanPipelineResource.h"

class VulkanDevice;

/**
 * @brief VulkanClearFrameRenderer 使用的 graphics pipeline 资源组。
 *
 * 先把 pipeline 生命周期从 renderer 成员列表中集中起来；后续可继续把具体
 * createGraphicsPipeline 逻辑迁入专门 builder。
 */
class VulkanFramePipelines {
public:
    VulkanFramePipelines() = default;
    ~VulkanFramePipelines() = default;

    VulkanFramePipelines(const VulkanFramePipelines&) = delete;
    VulkanFramePipelines& operator=(const VulkanFramePipelines&) = delete;

    void destroy(const VulkanDevice& device);

    VulkanPipelineResource background;
    VulkanPipelineResource triangle;
    VulkanPipelineResource mesh;
    VulkanPipelineResource isoSurface;
    VulkanPipelineResource line;
    VulkanPipelineResource pick;
};
