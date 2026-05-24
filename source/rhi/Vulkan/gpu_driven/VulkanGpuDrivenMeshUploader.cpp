#include "VulkanGpuDrivenMeshUploader.h"

#include "VulkanGpuDrivenMeshResources.h"
#include "VulkanGpuDrivenUploadBuilder.h"
#include "VulkanStagingUploadContext.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace {
template <typename T>
const void* dataOrNull(const std::vector<T>& values)
{
    return values.empty() ? nullptr : static_cast<const void*>(values.data());
}

template <typename T>
VkDeviceSize vectorSizeBytes(const std::vector<T>& values)
{
    return static_cast<VkDeviceSize>(values.size() * sizeof(T));
}

struct DrawIndexedIndirectCommand {
    uint32_t indexCount = 0;
    uint32_t instanceCount = 1;
    uint32_t firstIndex = 0;
    int32_t vertexOffset = 0;
    uint32_t firstInstance = 0;
};
static_assert(sizeof(DrawIndexedIndirectCommand) == sizeof(VkDrawIndexedIndirectCommand),
              "Unexpected DrawIndexedIndirectCommand layout");

bool uploadPartStatesHostVisible(const VulkanDevice& device,
                                 VulkanGpuDrivenMeshResources& resources,
                                 const std::vector<VulkanGpuDrivenPartState>& partStates,
                                 QString& lastError)
{
    if (partStates.empty()) {
        resources.partStateResource.destroy(device);
        resources.partStateCount = 0;
        resources.partStateCapacity = 0;
        return true;
    }
    if (!resources.partStateResource.uploadHostVisible(device,
                                                       dataOrNull(partStates),
                                                       vectorSizeBytes(partStates),
                                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                       "gpu-driven part state",
                                                       lastError)) {
        return false;
    }
    resources.partStateCount = static_cast<uint32_t>(partStates.size());
    resources.partStateCapacity = resources.partStateCount;
    return true;
}

bool uploadHiddenElementsHostVisible(
    const VulkanDevice& device,
    VulkanGpuDrivenMeshResources& resources,
    const std::vector<VulkanGpuDrivenHiddenElement>& hiddenElements,
    QString& lastError)
{
    if (hiddenElements.empty()) {
        resources.hiddenElementResource.destroy(device);
        resources.hiddenElementCount = 0;
        resources.hiddenElementCapacity = 0;
        return true;
    }
    if (!resources.hiddenElementResource.uploadHostVisible(device,
                                                           dataOrNull(hiddenElements),
                                                           vectorSizeBytes(hiddenElements),
                                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                           "gpu-driven hidden element",
                                                           lastError)) {
        return false;
    }
    resources.hiddenElementCount = static_cast<uint32_t>(hiddenElements.size());
    resources.hiddenElementCapacity = resources.hiddenElementCount;
    return true;
}

void resetV2SidecarResources(const VulkanDevice& device, VulkanGpuDrivenMeshResources& resources)
{
    resources.visiblePointFlagResource.destroy(device);
    resources.pointIndirectCommandResource.destroy(device);
    resources.visiblePointIndexResource.destroy(device);
    resources.triangleMetaV2Resource.destroy(device);
    resources.sourceVertexResource.destroy(device);
    resources.sourceVertexCount = 0;
    resources.triangleV2Count = 0;
    resources.maxVisibleIndexV2Count = 0;
    resources.maxVisiblePointIndexCount = 0;
    resources.staticSurfaceV2Bytes = 0;
}

bool uploadV2PointDrawResources(const VulkanDevice& device,
                                VulkanGpuDrivenMeshResources& resources,
                                VulkanStagingUploadContext& uploadContext,
                                uint32_t sourceVertexCount,
                                QString& lastError)
{
    const uint32_t pointCapacity = std::max<uint32_t>(sourceVertexCount, 1u);
    const VkDeviceSize pointIndexBytes =
        static_cast<VkDeviceSize>(pointCapacity * sizeof(uint32_t));
    if (!resources.visiblePointIndexResource.createDeviceLocal(
            device,
            pointIndexBytes,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            "gpu-driven v2 visible point index",
            lastError)) {
        return false;
    }

    const DrawIndexedIndirectCommand initialCommand{};
    if (!uploadContext.uploadBuffer(device,
                                    resources.pointIndirectCommandResource,
                                    &initialCommand,
                                    sizeof(initialCommand),
                                    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    "gpu-driven v2 point indirect command",
                                    lastError)) {
        return false;
    }

    const VkDeviceSize pointFlagBytes =
        static_cast<VkDeviceSize>(pointCapacity * sizeof(uint32_t));
    if (!resources.visiblePointFlagResource.createDeviceLocal(
            device,
            pointFlagBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            "gpu-driven v2 visible point flag",
            lastError)) {
        return false;
    }

    resources.maxVisiblePointIndexCount = sourceVertexCount;
    return true;
}
} // namespace

bool uploadVulkanGpuDrivenMeshResources(
    const VulkanDevice& device,
    VulkanGpuDrivenMeshResources& resources,
    VulkanStagingUploadContext& uploadContext,
    const VulkanGpuDrivenUploadData& uploadData,
    VkDescriptorSetLayout scalarSetLayout,
    bool uploadSurfaceV1,
    QString& lastError)
{
    resources.destroy(device);
    if (!uploadData.hasSurface()) {
        return true;
    }

    if (uploadSurfaceV1) {
        if (!uploadContext.uploadBuffer(device,
                                        resources.vertexResource,
                                        dataOrNull(uploadData.vertices),
                                        vectorSizeBytes(uploadData.vertices),
                                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                        "gpu-driven mesh vertex",
                                        lastError)) {
            resources.destroy(device);
            return false;
        }

        if (!uploadContext.uploadBuffer(device,
                                        resources.triangleMetaResource,
                                        dataOrNull(uploadData.triangles),
                                        vectorSizeBytes(uploadData.triangles),
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                        "gpu-driven triangle metadata",
                                        lastError)) {
            resources.destroy(device);
            return false;
        }
    }

    if (!uploadData.edgeVertices.empty() &&
        !uploadContext.uploadBuffer(device,
                                    resources.edgeVertexResource,
                                    dataOrNull(uploadData.edgeVertices),
                                    vectorSizeBytes(uploadData.edgeVertices),
                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                    "gpu-driven edge vertex",
                                    lastError)) {
        resources.destroy(device);
        return false;
    }

    if (!uploadData.edges.empty() &&
        !uploadContext.uploadBuffer(device,
                                    resources.edgeMetaResource,
                                    dataOrNull(uploadData.edges),
                                    vectorSizeBytes(uploadData.edges),
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                    "gpu-driven edge metadata",
                                    lastError)) {
        resources.destroy(device);
        return false;
    }

    if (!uploadPartStatesHostVisible(device, resources, uploadData.partStates, lastError)) {
        resources.destroy(device);
        return false;
    }

    if (!uploadHiddenElementsHostVisible(device, resources, uploadData.hiddenElements, lastError)) {
        resources.destroy(device);
        return false;
    }

    const VkDeviceSize visibleIndexBytes = static_cast<VkDeviceSize>(
        std::max<uint32_t>(uploadData.maxVisibleIndexCount, 1) * sizeof(uint32_t));
    if (!resources.visibleIndexResource.createDeviceLocal(
            device,
            visibleIndexBytes,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            "gpu-driven visible index",
            lastError)) {
        resources.destroy(device);
        return false;
    }

    const DrawIndexedIndirectCommand initialCommand{};
    if (!uploadContext.uploadBuffer(device,
                                    resources.indirectCommandResource,
                                    &initialCommand,
                                    sizeof(initialCommand),
                                    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    "gpu-driven indirect command",
                                    lastError)) {
        resources.destroy(device);
        return false;
    }

    std::array<VkDrawIndexedIndirectCommand, 3> readbackInitial{};
    if (!resources.visibilityReadbackResource.uploadHostVisible(
            device,
            readbackInitial.data(),
            static_cast<VkDeviceSize>(readbackInitial.size() * sizeof(readbackInitial[0])),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            "gpu-driven visibility readback",
            lastError)) {
        resources.destroy(device);
        return false;
    }

    if (uploadData.maxVisibleEdgeIndexCount > 0) {
        const VkDeviceSize visibleEdgeIndexBytes = static_cast<VkDeviceSize>(
            uploadData.maxVisibleEdgeIndexCount * sizeof(uint32_t));
        if (!resources.visibleEdgeIndexResource.createDeviceLocal(
                device,
                visibleEdgeIndexBytes,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                "gpu-driven visible edge index",
                lastError)) {
            resources.destroy(device);
            return false;
        }

        if (!uploadContext.uploadBuffer(device,
                                        resources.edgeIndirectCommandResource,
                                        &initialCommand,
                                        sizeof(initialCommand),
                                        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                            VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                        "gpu-driven edge indirect command",
                                        lastError)) {
            resources.destroy(device);
            return false;
        }
    }

    VulkanGpuDrivenVisibilityUniforms uniforms{};
    uniforms.triangleCount = static_cast<uint32_t>(uploadData.triangles.size());
    uniforms.hiddenElementCount = static_cast<uint32_t>(uploadData.hiddenElements.size());
    uniforms.partStateCount = static_cast<uint32_t>(uploadData.partStates.size());
    uniforms.edgeCount = static_cast<uint32_t>(uploadData.edges.size());
    if (!resources.frameUniformResource.uploadHostVisible(device,
                                                          &uniforms,
                                                          sizeof(uniforms),
                                                          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                                                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                          "gpu-driven visibility uniforms",
                                                          lastError)) {
        resources.destroy(device);
        return false;
    }

    if (uploadSurfaceV1) {
        const float zeroScalar = 0.0f;
        const void* scalarData = uploadData.expandedScalars.empty()
            ? static_cast<const void*>(&zeroScalar)
            : static_cast<const void*>(uploadData.expandedScalars.data());
        const VkDeviceSize scalarSize = static_cast<VkDeviceSize>(
            std::max<size_t>(uploadData.expandedScalars.size(), 1) * sizeof(float));
        if (!resources.scalarResource.uploadHostVisible(device,
                                                        scalarData,
                                                        scalarSize,
                                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                        "gpu-driven mesh scalar storage",
                                                        lastError)) {
            resources.destroy(device);
            return false;
        }
        if (scalarSetLayout != VK_NULL_HANDLE &&
            !resources.scalarDescriptor.createStorageBufferSet(device,
                                                               scalarSetLayout,
                                                               resources.scalarResource.buffer(),
                                                               scalarSize,
                                                               lastError)) {
            resources.destroy(device);
            return false;
        }
    }

    resources.vertexCount = uploadSurfaceV1 ? static_cast<uint32_t>(uploadData.vertices.size()) : 0;
    resources.triangleCount = static_cast<uint32_t>(uploadData.triangles.size());
    resources.edgeVertexCount = static_cast<uint32_t>(uploadData.edgeVertices.size());
    resources.edgeCount = static_cast<uint32_t>(uploadData.edges.size());
    resources.maxVisibleIndexCount = uploadData.maxVisibleIndexCount;
    resources.maxVisiblePointIndexCount = 0;
    resources.maxVisibleEdgeIndexCount = uploadData.maxVisibleEdgeIndexCount;
    resources.scalarCount = uploadSurfaceV1 ? static_cast<uint32_t>(uploadData.expandedScalars.size()) : 0;
    resources.staticSurfaceV1Bytes = uploadSurfaceV1
        ? vectorSizeBytes(uploadData.vertices) +
            vectorSizeBytes(uploadData.triangles) +
            vectorSizeBytes(uploadData.expandedScalars)
        : 0;
    return true;
}

bool uploadVulkanGpuDrivenMeshV2SidecarResources(
    const VulkanDevice& device,
    VulkanGpuDrivenMeshResources& resources,
    VulkanStagingUploadContext& uploadContext,
    const VulkanGpuDrivenUploadV2Data& uploadData,
    QString& lastError)
{
    resetV2SidecarResources(device, resources);
    if (!uploadData.hasSurface()) {
        return true;
    }

    if (!uploadContext.uploadBuffer(device,
                                    resources.sourceVertexResource,
                                    dataOrNull(uploadData.sourceVertices),
                                    vectorSizeBytes(uploadData.sourceVertices),
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                    "gpu-driven v2 source vertex",
                                    lastError)) {
        resetV2SidecarResources(device, resources);
        return false;
    }

    if (!uploadContext.uploadBuffer(device,
                                    resources.triangleMetaV2Resource,
                                    dataOrNull(uploadData.triangles),
                                    vectorSizeBytes(uploadData.triangles),
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                    "gpu-driven v2 triangle metadata",
                                    lastError)) {
        resetV2SidecarResources(device, resources);
        return false;
    }

    resources.sourceVertexCount = static_cast<uint32_t>(uploadData.sourceVertices.size());
    resources.triangleV2Count = static_cast<uint32_t>(uploadData.triangles.size());
    resources.maxVisibleIndexV2Count = uploadData.maxVisibleIndexCount;
    resources.staticSurfaceV2Bytes = static_cast<VkDeviceSize>(uploadData.staticSurfaceBytes());
    if (!uploadV2PointDrawResources(device,
                                    resources,
                                    uploadContext,
                                    resources.sourceVertexCount,
                                    lastError)) {
        resetV2SidecarResources(device, resources);
        return false;
    }
    return true;
}

bool uploadVulkanGpuDrivenMeshV2Resources(
    const VulkanDevice& device,
    VulkanGpuDrivenMeshResources& resources,
    VulkanStagingUploadContext& uploadContext,
    const VulkanGpuDrivenUploadV2Data& uploadData,
    QString& lastError)
{
    resources.destroy(device);
    if (!uploadData.hasSurface()) {
        return true;
    }

    if (!uploadContext.uploadBuffer(device,
                                    resources.sourceVertexResource,
                                    dataOrNull(uploadData.sourceVertices),
                                    vectorSizeBytes(uploadData.sourceVertices),
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                    "gpu-driven v2 source vertex",
                                    lastError)) {
        resources.destroy(device);
        return false;
    }

    if (!uploadContext.uploadBuffer(device,
                                    resources.triangleMetaV2Resource,
                                    dataOrNull(uploadData.triangles),
                                    vectorSizeBytes(uploadData.triangles),
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                    "gpu-driven v2 triangle metadata",
                                    lastError)) {
        resources.destroy(device);
        return false;
    }

    if (!uploadData.edgeVertices.empty() &&
        !uploadContext.uploadBuffer(device,
                                    resources.edgeVertexResource,
                                    dataOrNull(uploadData.edgeVertices),
                                    vectorSizeBytes(uploadData.edgeVertices),
                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                    "gpu-driven edge vertex",
                                    lastError)) {
        resources.destroy(device);
        return false;
    }

    if (!uploadData.edges.empty() &&
        !uploadContext.uploadBuffer(device,
                                    resources.edgeMetaResource,
                                    dataOrNull(uploadData.edges),
                                    vectorSizeBytes(uploadData.edges),
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                    "gpu-driven edge metadata",
                                    lastError)) {
        resources.destroy(device);
        return false;
    }

    if (!uploadPartStatesHostVisible(device, resources, uploadData.partStates, lastError)) {
        resources.destroy(device);
        return false;
    }

    if (!uploadHiddenElementsHostVisible(device, resources, uploadData.hiddenElements, lastError)) {
        resources.destroy(device);
        return false;
    }

    const VkDeviceSize visibleIndexBytes = static_cast<VkDeviceSize>(
        std::max<uint32_t>(uploadData.maxVisibleIndexCount, 1) * sizeof(uint32_t));
    if (!resources.visibleIndexResource.createDeviceLocal(
            device,
            visibleIndexBytes,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            "gpu-driven visible index",
            lastError)) {
        resources.destroy(device);
        return false;
    }

    const DrawIndexedIndirectCommand initialCommand{};
    if (!uploadContext.uploadBuffer(device,
                                    resources.indirectCommandResource,
                                    &initialCommand,
                                    sizeof(initialCommand),
                                    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    "gpu-driven indirect command",
                                    lastError)) {
        resources.destroy(device);
        return false;
    }

    if (!uploadV2PointDrawResources(device,
                                    resources,
                                    uploadContext,
                                    static_cast<uint32_t>(uploadData.sourceVertices.size()),
                                    lastError)) {
        resources.destroy(device);
        return false;
    }

    std::array<VkDrawIndexedIndirectCommand, 3> readbackInitial{};
    if (!resources.visibilityReadbackResource.uploadHostVisible(
            device,
            readbackInitial.data(),
            static_cast<VkDeviceSize>(readbackInitial.size() * sizeof(readbackInitial[0])),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            "gpu-driven visibility readback",
            lastError)) {
        resources.destroy(device);
        return false;
    }

    if (uploadData.maxVisibleEdgeIndexCount > 0) {
        const VkDeviceSize visibleEdgeIndexBytes = static_cast<VkDeviceSize>(
            uploadData.maxVisibleEdgeIndexCount * sizeof(uint32_t));
        if (!resources.visibleEdgeIndexResource.createDeviceLocal(
                device,
                visibleEdgeIndexBytes,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                "gpu-driven visible edge index",
                lastError)) {
            resources.destroy(device);
            return false;
        }

        if (!uploadContext.uploadBuffer(device,
                                        resources.edgeIndirectCommandResource,
                                        &initialCommand,
                                        sizeof(initialCommand),
                                        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                            VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                        "gpu-driven edge indirect command",
                                        lastError)) {
            resources.destroy(device);
            return false;
        }
    }

    VulkanGpuDrivenVisibilityUniforms uniforms{};
    uniforms.triangleCount = static_cast<uint32_t>(uploadData.triangles.size());
    uniforms.hiddenElementCount = static_cast<uint32_t>(uploadData.hiddenElements.size());
    uniforms.partStateCount = static_cast<uint32_t>(uploadData.partStates.size());
    uniforms.edgeCount = static_cast<uint32_t>(uploadData.edges.size());
    uniforms.sourceVertexCount = static_cast<uint32_t>(uploadData.sourceVertices.size());
    if (!resources.frameUniformResource.uploadHostVisible(device,
                                                          &uniforms,
                                                          sizeof(uniforms),
                                                          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                                                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                          "gpu-driven visibility uniforms",
                                                          lastError)) {
        resources.destroy(device);
        return false;
    }

    resources.vertexCount = 0;
    resources.sourceVertexCount = static_cast<uint32_t>(uploadData.sourceVertices.size());
    resources.triangleCount = static_cast<uint32_t>(uploadData.triangles.size());
    resources.triangleV2Count = static_cast<uint32_t>(uploadData.triangles.size());
    resources.edgeVertexCount = static_cast<uint32_t>(uploadData.edgeVertices.size());
    resources.edgeCount = static_cast<uint32_t>(uploadData.edges.size());
    resources.maxVisibleIndexCount = uploadData.maxVisibleIndexCount;
    resources.maxVisibleIndexV2Count = uploadData.maxVisibleIndexCount;
    resources.maxVisiblePointIndexCount = static_cast<uint32_t>(uploadData.sourceVertices.size());
    resources.maxVisibleEdgeIndexCount = uploadData.maxVisibleEdgeIndexCount;
    resources.scalarCount = 0;
    resources.staticSurfaceV1Bytes = 0;
    resources.staticSurfaceV2Bytes = static_cast<VkDeviceSize>(uploadData.staticSurfaceBytes());
    return true;
}

bool updateVulkanGpuDrivenVisibilityState(
    const VulkanDevice& device,
    VulkanGpuDrivenMeshResources& resources,
    const VulkanGpuDrivenVisibilityStateData& stateData,
    bool& descriptorDirty,
    QString& lastError)
{
    if (!resources.isReady()) {
        lastError = QStringLiteral("Vulkan GPU-driven mesh resources are not initialized");
        return false;
    }

    const uint32_t nextPartStateCount =
        static_cast<uint32_t>(stateData.partStates.size());
    if (nextPartStateCount == 0) {
        if (resources.partStateResource.isValid()) {
            resources.partStateResource.destroy(device);
            descriptorDirty = true;
        }
        resources.partStateCount = 0;
        resources.partStateCapacity = 0;
    } else if (resources.partStateResource.isValid() &&
               nextPartStateCount <= resources.partStateCapacity) {
        if (!resources.partStateResource.updateHostVisible(
                device,
                dataOrNull(stateData.partStates),
                vectorSizeBytes(stateData.partStates),
                "gpu-driven part state",
                lastError)) {
            return false;
        }
        resources.partStateCount = nextPartStateCount;
    } else {
        if (!uploadPartStatesHostVisible(device, resources, stateData.partStates, lastError)) {
            return false;
        }
        descriptorDirty = true;
    }

    const uint32_t nextHiddenElementCount =
        static_cast<uint32_t>(stateData.hiddenElements.size());
    if (nextHiddenElementCount == 0) {
        if (resources.hiddenElementResource.isValid()) {
            resources.hiddenElementResource.destroy(device);
            descriptorDirty = true;
        }
        resources.hiddenElementCount = 0;
        resources.hiddenElementCapacity = 0;
    } else if (resources.hiddenElementResource.isValid() &&
               nextHiddenElementCount <= resources.hiddenElementCapacity) {
        if (!resources.hiddenElementResource.updateHostVisible(
                device,
                dataOrNull(stateData.hiddenElements),
                vectorSizeBytes(stateData.hiddenElements),
                "gpu-driven hidden element",
                lastError)) {
            return false;
        }
        resources.hiddenElementCount = nextHiddenElementCount;
    } else {
        if (!uploadHiddenElementsHostVisible(device, resources, stateData.hiddenElements, lastError)) {
            return false;
        }
        descriptorDirty = true;
    }

    VulkanGpuDrivenVisibilityUniforms uniforms = stateData.uniforms;
    uniforms.triangleCount = resources.triangleCount;
    uniforms.hiddenElementCount = resources.hiddenElementCount;
    uniforms.partStateCount = resources.partStateCount;
    uniforms.edgeCount = resources.edgeCount;
    uniforms.sourceVertexCount = resources.sourceVertexCount;
    if (!resources.frameUniformResource.updateHostVisible(device,
                                                          &uniforms,
                                                          sizeof(uniforms),
                                                          "gpu-driven visibility uniforms",
                                                          lastError)) {
        return false;
    }
    return true;
}
