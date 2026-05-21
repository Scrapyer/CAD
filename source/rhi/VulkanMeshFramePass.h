#pragma once

#include <QMatrix4x4>

#include <vulkan/vulkan.h>

#include <cstdint>

class VulkanBufferResource;
class VulkanDescriptorResource;
class VulkanPipelineResource;

/**
 * @brief Vulkan 主网格帧录制 pass。
 *
 * 该对象只负责把 renderer 已持有的资源录制到 command buffer，不拥有任何 Vulkan
 * handle。资源生命周期仍由 VulkanClearFrameRenderer 和各 Resource 封装管理。
 */
class VulkanMeshFramePass {
public:
    struct Resources {
        const VulkanPipelineResource* meshPipeline = nullptr;
        const VulkanPipelineResource* isoSurfacePipeline = nullptr;
        const VulkanPipelineResource* linePipeline = nullptr;
        const VulkanDescriptorResource* meshScalarDescriptor = nullptr;

        const VulkanBufferResource* meshVertexResource = nullptr;
        const VulkanBufferResource* meshIndexResource = nullptr;
        uint32_t meshIndexCount = 0;
        bool meshUseVertexScalars = false;
        float meshScalarMin = 0.0f;
        float meshScalarMax = 1.0f;
        int meshNumBands = 10;

        const VulkanBufferResource* isoSurfaceVertexResource = nullptr;
        const VulkanBufferResource* isoSurfaceIndexResource = nullptr;
        uint32_t isoSurfaceIndexCount = 0;

        const VulkanBufferResource* clipPreviewVertexResource = nullptr;
        const VulkanBufferResource* clipPreviewIndexResource = nullptr;
        uint32_t clipPreviewIndexCount = 0;
        const VulkanBufferResource* clipPreviewLineVertexResource = nullptr;
        uint32_t clipPreviewLineVertexCount = 0;

        const VulkanBufferResource* overlayLineVertexResource = nullptr;
        uint32_t overlayLineVertexCount = 0;
        const VulkanBufferResource* edgeVertexResource = nullptr;
        const VulkanBufferResource* edgeIndexResource = nullptr;
        uint32_t edgeIndexCount = 0;
        const VulkanBufferResource* sliceLineVertexResource = nullptr;
        uint32_t sliceLineVertexCount = 0;
        const VulkanBufferResource* selectionLineVertexResource = nullptr;
        uint32_t selectionLineVertexCount = 0;
    };

    static void record(VkCommandBuffer commandBuffer,
                       VkExtent2D extent,
                       const QMatrix4x4& mvp,
                       const Resources& resources);
};
