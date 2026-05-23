#include "VulkanMeshBufferResources.h"

#include "VulkanDevice.h"

void VulkanMeshBufferResources::destroy(const VulkanDevice& device)
{
    meshScalarDescriptor.destroy(device);
    meshIndexResource.destroy(device);
    edgeIndexResource.destroy(device);
    meshVertexResource.destroy(device);
    pointVertexResource.destroy(device);
    meshScalarResource.destroy(device);
    edgeVertexResource.destroy(device);

    meshIndexCount = 0;
    pointVertexCount = 0;
    meshUseVertexScalars = false;
    meshScalarMin = 0.0f;
    meshScalarMax = 1.0f;
    meshNumBands = 10;
    meshScalarSourceIndices.clear();
    meshScalarCount = 0;
    edgeUseVertexScalars = false;
    edgeScalarMin = 0.0f;
    edgeScalarMax = 1.0f;
    edgeNumBands = 10;
    edgeIndexCount = 0;
}
