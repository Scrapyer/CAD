#include "VulkanGpuDrivenMeshResources.h"

#include "VulkanDevice.h"

void VulkanGpuDrivenMeshResources::destroy(const VulkanDevice& device)
{
    scalarDescriptor.destroy(device);
    surfaceDescriptorV2.destroy(device);
    scalarResource.destroy(device);
    frameUniformResource.destroy(device);
    visibilityReadbackResource.destroy(device);
    edgeIndirectCommandResource.destroy(device);
    visibleEdgeIndexResource.destroy(device);
    visiblePointFlagResource.destroy(device);
    pointIndirectCommandResource.destroy(device);
    visiblePointIndexResource.destroy(device);
    indirectCommandResource.destroy(device);
    visibleIndexResource.destroy(device);
    hiddenElementResource.destroy(device);
    partStateResource.destroy(device);
    edgeMetaResource.destroy(device);
    edgeVertexResource.destroy(device);
    triangleMetaV2Resource.destroy(device);
    triangleMetaResource.destroy(device);
    sourceVertexResource.destroy(device);
    vertexResource.destroy(device);

    vertexCount = 0;
    sourceVertexCount = 0;
    triangleCount = 0;
    triangleV2Count = 0;
    edgeVertexCount = 0;
    edgeCount = 0;
    partStateCount = 0;
    partStateCapacity = 0;
    hiddenElementCount = 0;
    hiddenElementCapacity = 0;
    maxVisibleIndexCount = 0;
    maxVisibleIndexV2Count = 0;
    maxVisiblePointIndexCount = 0;
    maxVisibleEdgeIndexCount = 0;
    scalarCount = 0;
    staticSurfaceV1Bytes = 0;
    staticSurfaceV2Bytes = 0;
}
