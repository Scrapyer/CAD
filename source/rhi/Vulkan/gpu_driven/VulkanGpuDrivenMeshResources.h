#pragma once

#include "VulkanBufferResource.h"
#include "VulkanDescriptorResource.h"

#include <vulkan/vulkan.h>

#include <cstdint>

class VulkanDevice;

/**
 * @brief GPU-driven 主 mesh 资源组。
 *
 * 当前只负责生命周期和容量状态；compute/indirect pass 接入后会由该资源组托管
 * 完整顶点、三角元数据、部件状态、隐藏单元表、可见 index buffer 和 indirect command。
 */
class VulkanGpuDrivenMeshResources {
public:
    VulkanGpuDrivenMeshResources() = default;
    ~VulkanGpuDrivenMeshResources() = default;

    VulkanGpuDrivenMeshResources(const VulkanGpuDrivenMeshResources&) = delete;
    VulkanGpuDrivenMeshResources& operator=(const VulkanGpuDrivenMeshResources&) = delete;

    void destroy(const VulkanDevice& device);

    bool isReady() const
    {
        const bool hasSurfaceMetadata =
            (vertexResource.isValid() && triangleMetaResource.isValid() && vertexCount > 0) ||
            hasV2Sidecar();
        return hasSurfaceMetadata &&
            visibleIndexResource.isValid() &&
            indirectCommandResource.isValid() &&
            triangleCount > 0 &&
            maxVisibleIndexCount > 0;
    }

    VkDeviceSize visibleIndexRangeBytes() const
    {
        return static_cast<VkDeviceSize>(
            (maxVisibleIndexCount > 0 ? maxVisibleIndexCount : 1) * sizeof(uint32_t));
    }

    bool hasUniquePointIndices() const
    {
        return visiblePointIndexResource.isValid() &&
            pointIndirectCommandResource.isValid() &&
            visiblePointFlagResource.isValid() &&
            maxVisiblePointIndexCount > 0;
    }

    VkDeviceSize visiblePointIndexRangeBytes() const
    {
        return static_cast<VkDeviceSize>(
            (maxVisiblePointIndexCount > 0 ? maxVisiblePointIndexCount : 1) * sizeof(uint32_t));
    }

    VkDeviceSize visiblePointFlagRangeBytes() const
    {
        return static_cast<VkDeviceSize>(
            (sourceVertexCount > 0 ? sourceVertexCount : 1) * sizeof(uint32_t));
    }

    bool hasV2Sidecar() const
    {
        return sourceVertexResource.isValid() &&
            triangleMetaV2Resource.isValid() &&
            sourceVertexCount > 0 &&
            triangleV2Count > 0 &&
            maxVisibleIndexV2Count > 0;
    }

    VulkanBufferResource vertexResource;
    VulkanBufferResource sourceVertexResource;
    VulkanBufferResource triangleMetaResource;
    VulkanBufferResource triangleMetaV2Resource;
    VulkanBufferResource edgeVertexResource;
    VulkanBufferResource edgeMetaResource;
    VulkanBufferResource partStateResource;
    VulkanBufferResource hiddenElementResource;
    VulkanBufferResource visibleIndexResource;
    VulkanBufferResource indirectCommandResource;
    VulkanBufferResource visiblePointIndexResource;
    VulkanBufferResource pointIndirectCommandResource;
    VulkanBufferResource visiblePointFlagResource;
    VulkanBufferResource visibleEdgeIndexResource;
    VulkanBufferResource edgeIndirectCommandResource;
    VulkanBufferResource visibilityReadbackResource;
    VulkanBufferResource frameUniformResource;
    VulkanBufferResource scalarResource;
    VulkanDescriptorResource scalarDescriptor;
    VulkanDescriptorResource surfaceDescriptorV2;

    uint32_t vertexCount = 0;
    uint32_t sourceVertexCount = 0;
    uint32_t triangleCount = 0;
    uint32_t triangleV2Count = 0;
    uint32_t edgeVertexCount = 0;
    uint32_t edgeCount = 0;
    uint32_t partStateCount = 0;
    uint32_t partStateCapacity = 0;
    uint32_t hiddenElementCount = 0;
    uint32_t hiddenElementCapacity = 0;
    uint32_t maxVisibleIndexCount = 0;
    uint32_t maxVisibleIndexV2Count = 0;
    uint32_t maxVisiblePointIndexCount = 0;
    uint32_t maxVisibleEdgeIndexCount = 0;
    uint32_t scalarCount = 0;
    VkDeviceSize staticSurfaceV1Bytes = 0;
    VkDeviceSize staticSurfaceV2Bytes = 0;

    bool hasEdges() const
    {
        return edgeVertexResource.isValid() &&
            edgeMetaResource.isValid() &&
            visibleEdgeIndexResource.isValid() &&
            edgeIndirectCommandResource.isValid() &&
            edgeVertexCount > 0 &&
            edgeCount > 0 &&
            maxVisibleEdgeIndexCount > 0;
    }

    VkDeviceSize visibleEdgeIndexRangeBytes() const
    {
        return static_cast<VkDeviceSize>(
            (maxVisibleEdgeIndexCount > 0 ? maxVisibleEdgeIndexCount : 1) * sizeof(uint32_t));
    }
};
