#pragma once

#include "VulkanGpuDrivenTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

struct Mesh;
struct VulkanMeshUploadOptions;

/**
 * @brief CPU 侧构建的 GPU-driven 上传数据。
 *
 * 该结构只描述数据，不持有 Vulkan handle。它用于把现有 Mesh 和显隐/颜色映射转换成
 * 后续 compute culling 可以直接消费的完整三角面数据。
 */
struct VulkanGpuDrivenUploadData {
    std::vector<VulkanGpuDrivenMeshVertex> vertices;
    std::vector<VulkanGpuDrivenTriangleMeta> triangles;
    std::vector<VulkanGpuDrivenLineVertex> edgeVertices;
    std::vector<VulkanGpuDrivenEdgeMeta> edges;
    std::vector<VulkanGpuDrivenPartState> partStates;
    std::vector<VulkanGpuDrivenHiddenElement> hiddenElements;
    std::vector<uint32_t> scalarSourceIndices;
    std::vector<float> expandedScalars;
    uint32_t maxVisibleIndexCount = 0;
    uint32_t maxVisibleEdgeIndexCount = 0;

    bool hasSurface() const
    {
        return !vertices.empty() && !triangles.empty() && maxVisibleIndexCount > 0;
    }
};

struct VulkanGpuDrivenUploadV2Data {
    std::vector<VulkanGpuDrivenSourceVertex> sourceVertices;
    std::vector<VulkanGpuDrivenTriangleMeta> triangles;
    std::vector<VulkanGpuDrivenLineVertex> edgeVertices;
    std::vector<VulkanGpuDrivenEdgeMeta> edges;
    std::vector<VulkanGpuDrivenPartState> partStates;
    std::vector<VulkanGpuDrivenHiddenElement> hiddenElements;
    uint32_t maxVisibleIndexCount = 0;
    uint32_t maxVisibleEdgeIndexCount = 0;

    bool hasSurface() const
    {
        return !sourceVertices.empty() && !triangles.empty() && maxVisibleIndexCount > 0;
    }

    size_t staticSurfaceBytes() const
    {
        return sourceVertices.size() * sizeof(VulkanGpuDrivenSourceVertex) +
            triangles.size() * sizeof(VulkanGpuDrivenTriangleMeta);
    }
};

struct VulkanGpuDrivenVisibilityStateData {
    std::vector<VulkanGpuDrivenPartState> partStates;
    std::vector<VulkanGpuDrivenHiddenElement> hiddenElements;
    VulkanGpuDrivenVisibilityUniforms uniforms;
};

VulkanGpuDrivenUploadData buildVulkanGpuDrivenUploadData(
    const Mesh& mesh,
    const VulkanMeshUploadOptions& options);

VulkanGpuDrivenUploadV2Data buildVulkanGpuDrivenUploadV2Data(
    const Mesh& mesh,
    const VulkanMeshUploadOptions& options);

VulkanGpuDrivenVisibilityStateData buildVulkanGpuDrivenVisibilityStateData(
    const VulkanMeshUploadOptions& options,
    uint32_t triangleCount,
    uint32_t minimumPartStateCount = 0);
