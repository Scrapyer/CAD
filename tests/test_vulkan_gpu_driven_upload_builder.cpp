#include "Geometry.h"
#include "VulkanGpuDrivenUploadBuilder.h"
#include "VulkanRenderBackend.h"

#include <QVector3D>

#include <cstddef>
#include <cmath>

namespace {
bool near(float a, float b)
{
    return std::fabs(a - b) < 1.0e-6f;
}

Mesh makeQuadMesh()
{
    Mesh mesh;
    mesh.vertices = {
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f
    };
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}
} // namespace

int main()
{
    Mesh mesh = makeQuadMesh();
    mesh.edgeVertices = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        1.0f, 1.0f, 0.0f
    };
    mesh.edgeIndices = {0, 1, 1, 2};
    mesh.edgeToElement = {10, 20};

    VulkanMeshUploadOptions options;
    options.objectColor = QVector3D(0.25f, 0.5f, 0.75f);
    options.triangleToElement = {10, 20};
    options.triangleToPart = {0, 1};
    options.partColors = {
        QVector3D(1.0f, 0.0f, 0.0f),
        QVector3D(0.0f, 1.0f, 0.0f)
    };
    options.partVisibility[1] = false;
    options.hiddenElementIds.insert(20);
    options.hiddenElementIds.insert(5);
    options.vertexScalars = {0.0f, 10.0f, 20.0f, 30.0f};
    options.edgeToPart = {0, 1};
    options.edgeScalars = {4.0f, 5.0f, 6.0f};

    const VulkanGpuDrivenUploadData uploadData =
        buildVulkanGpuDrivenUploadData(mesh, options);

    if (!uploadData.hasSurface()) {
        return 1;
    }
    if (uploadData.vertices.size() != 6) {
        return 2;
    }
    if (uploadData.triangles.size() != 2) {
        return 3;
    }
    if (uploadData.maxVisibleIndexCount != 6) {
        return 4;
    }

    const VulkanGpuDrivenTriangleMeta& first = uploadData.triangles[0];
    if (first.index0 != 0 || first.index1 != 1 || first.index2 != 2) {
        return 5;
    }
    if (first.elementId != 10 || first.partId != 0) {
        return 6;
    }
    if (!near(first.boundsMin[0], 0.0f) ||
        !near(first.boundsMin[1], 0.0f) ||
        !near(first.boundsMax[0], 1.0f) ||
        !near(first.boundsMax[1], 1.0f)) {
        return 7;
    }
    if (!near(uploadData.vertices[0].color[0], 1.0f) ||
        !near(uploadData.vertices[0].color[1], 0.0f) ||
        !near(uploadData.vertices[0].pickColor[0], 11.0f / 255.0f)) {
        return 8;
    }

    const VulkanGpuDrivenTriangleMeta& second = uploadData.triangles[1];
    if (second.index0 != 3 || second.index1 != 4 || second.index2 != 5) {
        return 9;
    }
    if (second.elementId != 20 || second.partId != 1) {
        return 10;
    }
    if (!near(uploadData.vertices[3].color[0], 0.0f) ||
        !near(uploadData.vertices[3].color[1], 1.0f) ||
        !near(uploadData.vertices[3].pickColor[0], 21.0f / 255.0f)) {
        return 11;
    }

    const uint32_t expectedSources[] = {0, 1, 2, 0, 2, 3};
    if (uploadData.scalarSourceIndices.size() != 6) {
        return 12;
    }
    if (uploadData.expandedScalars.size() != 6) {
        return 18;
    }
    for (size_t i = 0; i < uploadData.scalarSourceIndices.size(); ++i) {
        if (uploadData.scalarSourceIndices[i] != expectedSources[i]) {
            return 13;
        }
    }
    if (!near(uploadData.expandedScalars[0], 0.0f) ||
        !near(uploadData.expandedScalars[1], 10.0f) ||
        !near(uploadData.expandedScalars[2], 20.0f) ||
        !near(uploadData.expandedScalars[5], 30.0f)) {
        return 19;
    }

    if (uploadData.edgeVertices.size() != 3 ||
        uploadData.edges.size() != 2 ||
        uploadData.maxVisibleEdgeIndexCount != 4) {
        return 23;
    }
    if (!near(uploadData.edgeVertices[1].position[0], 1.0f) ||
        !near(uploadData.edgeVertices[1].scalar, 5.0f)) {
        return 24;
    }
    if (uploadData.edges[0].index0 != 0 ||
        uploadData.edges[0].index1 != 1 ||
        uploadData.edges[0].elementId != 10 ||
        uploadData.edges[0].partId != 0 ||
        uploadData.edges[1].elementId != 20 ||
        uploadData.edges[1].partId != 1) {
        return 25;
    }

    if (uploadData.partStates.size() != 2) {
        return 14;
    }
    if (uploadData.partStates[0].visible != 1 ||
        uploadData.partStates[1].visible != 0) {
        return 15;
    }
    if (!near(uploadData.partStates[1].color[1], 1.0f)) {
        return 16;
    }

    if (uploadData.hiddenElements.size() != 2 ||
        uploadData.hiddenElements[0].elementId != 5 ||
        uploadData.hiddenElements[1].elementId != 20) {
        return 17;
    }

    const VulkanGpuDrivenUploadV2Data uploadV2Data =
        buildVulkanGpuDrivenUploadV2Data(mesh, options);
    if (!uploadV2Data.hasSurface()) {
        return 26;
    }
    if (uploadV2Data.sourceVertices.size() != 4 ||
        uploadV2Data.triangles.size() != 2 ||
        uploadV2Data.maxVisibleIndexCount != 6) {
        return 27;
    }
    if (uploadV2Data.triangles[0].index0 != 0 ||
        uploadV2Data.triangles[0].index1 != 1 ||
        uploadV2Data.triangles[0].index2 != 2 ||
        uploadV2Data.triangles[1].index0 != 0 ||
        uploadV2Data.triangles[1].index1 != 2 ||
        uploadV2Data.triangles[1].index2 != 3) {
        return 28;
    }
    if (uploadV2Data.triangles[1].elementId != 20 ||
        uploadV2Data.triangles[1].partId != 1) {
        return 29;
    }
    if (!near(uploadV2Data.sourceVertices[0].position[0], 0.0f) ||
        !near(uploadV2Data.sourceVertices[2].position[1], 1.0f) ||
        !near(uploadV2Data.sourceVertices[3].scalar, 30.0f) ||
        !near(uploadV2Data.sourceVertices[0].color[0], 0.25f)) {
        return 30;
    }
    const size_t v1SurfaceBytes =
        uploadData.vertices.size() * sizeof(VulkanGpuDrivenMeshVertex) +
        uploadData.triangles.size() * sizeof(VulkanGpuDrivenTriangleMeta) +
        uploadData.expandedScalars.size() * sizeof(float);
    if (uploadV2Data.staticSurfaceBytes() >= v1SurfaceBytes) {
        return 31;
    }
    if (uploadV2Data.edgeVertices.size() != 3 ||
        uploadV2Data.edges.size() != 2 ||
        uploadV2Data.edges[1].index0 != 1 ||
        uploadV2Data.edges[1].index1 != 2) {
        return 32;
    }
    if (uploadV2Data.hiddenElements.size() != 2 ||
        uploadV2Data.partStates.size() != 2 ||
        uploadV2Data.partStates[1].visible != 0) {
        return 33;
    }

    VulkanMeshUploadOptions stateOptions = options;
    stateOptions.partVisibility[2] = false;
    stateOptions.hiddenElementIds.insert(3);
    const VulkanGpuDrivenVisibilityStateData stateData =
        buildVulkanGpuDrivenVisibilityStateData(stateOptions, 2, 4);
    if (stateData.partStates.size() != 4 ||
        stateData.partStates[1].visible != 0 ||
        stateData.partStates[2].visible != 0 ||
        stateData.partStates[3].visible != 1) {
        return 20;
    }
    if (stateData.hiddenElements.size() != 3 ||
        stateData.hiddenElements[0].elementId != 3 ||
        stateData.hiddenElements[1].elementId != 5 ||
        stateData.hiddenElements[2].elementId != 20) {
        return 21;
    }
    if (stateData.uniforms.triangleCount != 2 ||
        stateData.uniforms.hiddenElementCount != 3 ||
        stateData.uniforms.partStateCount != 4 ||
        stateData.uniforms.enablePartVisibility != 1) {
        return 22;
    }

    return 0;
}
