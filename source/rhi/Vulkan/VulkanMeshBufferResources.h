#pragma once

#include "VulkanBufferResource.h"
#include "VulkanDescriptorResource.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

class VulkanDevice;

/**
 * @brief Vulkan 主 mesh 相关 buffer 和 scalar descriptor 资源组。
 *
 * 将主网格、普通边线、per-vertex scalar SSBO 及其描述符收敛到一个资源组，
 * 让帧渲染器只负责调度上传/绘制，不直接维护这一组零散状态。
 */
class VulkanMeshBufferResources {
public:
    VulkanMeshBufferResources() = default;
    ~VulkanMeshBufferResources() = default;

    VulkanMeshBufferResources(const VulkanMeshBufferResources&) = delete;
    VulkanMeshBufferResources& operator=(const VulkanMeshBufferResources&) = delete;

    void destroy(const VulkanDevice& device);

    bool hasMesh() const
    {
        return meshVertexResource.isValid() &&
            meshIndexResource.isValid() &&
            meshIndexCount > 0;
    }

    VkDeviceSize scalarRangeBytes() const
    {
        return static_cast<VkDeviceSize>(
            (meshScalarCount > 0 ? meshScalarCount : 1) * sizeof(float));
    }

    VulkanBufferResource meshVertexResource;
    VulkanBufferResource meshIndexResource;
    uint32_t meshIndexCount = 0;
    VulkanBufferResource pointVertexResource;
    uint32_t pointVertexCount = 0;

    bool meshUseVertexScalars = false;
    float meshScalarMin = 0.0f;
    float meshScalarMax = 1.0f;
    int meshNumBands = 10;
    std::vector<uint32_t> meshScalarSourceIndices;
    VulkanBufferResource meshScalarResource;
    uint32_t meshScalarCount = 0;
    VulkanDescriptorResource meshScalarDescriptor;

    bool edgeUseVertexScalars = false;
    float edgeScalarMin = 0.0f;
    float edgeScalarMax = 1.0f;
    int edgeNumBands = 10;
    VulkanBufferResource edgeVertexResource;
    VulkanBufferResource edgeIndexResource;
    uint32_t edgeIndexCount = 0;
};
