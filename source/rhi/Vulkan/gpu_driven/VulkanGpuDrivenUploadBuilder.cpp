#include "VulkanGpuDrivenUploadBuilder.h"

#include "Geometry.h"
#include "VulkanRenderBackend.h"

#include <QVector3D>

#include <algorithm>
#include <limits>

namespace {
QVector3D idToPickColor(int id)
{
    if (id < 0) {
        return QVector3D(0.0f, 0.0f, 0.0f);
    }
    const int encoded = id + 1;
    const int r = encoded & 0xFF;
    const int g = (encoded >> 8) & 0xFF;
    const int b = (encoded >> 16) & 0xFF;
    return QVector3D(
        static_cast<float>(r) / 255.0f,
        static_cast<float>(g) / 255.0f,
        static_cast<float>(b) / 255.0f);
}

QVector3D partColor(const VulkanMeshUploadOptions& options, int part)
{
    if (part >= 0 && part < static_cast<int>(options.partColors.size())) {
        return options.partColors[static_cast<size_t>(part)];
    }
    return options.objectColor;
}

QVector3D vertexColor(const VulkanMeshUploadOptions& options, size_t sourceIndex, int part)
{
    if (options.useVertexColor && sourceIndex * 3 + 2 < options.vertexColors.size()) {
        return QVector3D(options.vertexColors[sourceIndex * 3 + 0],
                         options.vertexColors[sourceIndex * 3 + 1],
                         options.vertexColors[sourceIndex * 3 + 2]);
    }
    return partColor(options, part);
}

int maxPartIndex(const Mesh& mesh, const VulkanMeshUploadOptions& options, size_t triangleCount)
{
    int maxPart = -1;
    for (size_t tri = 0; tri < triangleCount; ++tri) {
        if (tri < options.triangleToPart.size()) {
            maxPart = std::max(maxPart, options.triangleToPart[tri]);
        }
    }
    for (int part : options.edgeToPart) {
        maxPart = std::max(maxPart, part);
    }
    for (const auto& [part, visible] : options.partVisibility) {
        (void)visible;
        maxPart = std::max(maxPart, part);
    }
    if (!options.partColors.empty()) {
        maxPart = std::max(maxPart, static_cast<int>(options.partColors.size() - 1));
    }
    (void)mesh;
    return maxPart;
}

std::vector<VulkanGpuDrivenPartState> makePartStates(
    const VulkanMeshUploadOptions& options,
    int maxPart)
{
    std::vector<VulkanGpuDrivenPartState> states;
    if (maxPart < 0) {
        return states;
    }

    states.resize(static_cast<size_t>(maxPart + 1));
    for (int part = 0; part <= maxPart; ++part) {
        VulkanGpuDrivenPartState state{};
        const auto visibilityIt = options.partVisibility.find(part);
        state.visible = (visibilityIt == options.partVisibility.end() || visibilityIt->second) ? 1u : 0u;
        const QVector3D color = partColor(options, part);
        state.color[0] = color.x();
        state.color[1] = color.y();
        state.color[2] = color.z();
        states[static_cast<size_t>(part)] = state;
    }
    return states;
}

std::vector<VulkanGpuDrivenHiddenElement> makeHiddenElements(
    const VulkanMeshUploadOptions& options)
{
    std::vector<VulkanGpuDrivenHiddenElement> hiddenElements;
    std::vector<int> ids(options.hiddenElementIds.begin(), options.hiddenElementIds.end());
    std::sort(ids.begin(), ids.end());
    hiddenElements.reserve(ids.size());
    for (int id : ids) {
        VulkanGpuDrivenHiddenElement item{};
        item.elementId = id;
        hiddenElements.push_back(item);
    }
    return hiddenElements;
}
} // namespace

VulkanGpuDrivenUploadData buildVulkanGpuDrivenUploadData(
    const Mesh& mesh,
    const VulkanMeshUploadOptions& options)
{
    VulkanGpuDrivenUploadData uploadData;

    const bool hasSurfaceMesh =
        !mesh.vertices.empty() && !mesh.indices.empty() && mesh.vertices.size() % 6 == 0;
    if (!hasSurfaceMesh) {
        uploadData.hiddenElements = makeHiddenElements(options);
        return uploadData;
    }

    const size_t sourceVertexCount = mesh.vertices.size() / 6;
    const size_t triangleCount = mesh.indices.size() / 3;
    const bool hasEdgeMesh =
        !mesh.edgeVertices.empty() && !mesh.edgeIndices.empty() && mesh.edgeVertices.size() % 3 == 0;
    uploadData.vertices.reserve(triangleCount * 3);
    uploadData.triangles.reserve(triangleCount);
    uploadData.scalarSourceIndices.reserve(triangleCount * 3);
    uploadData.expandedScalars.reserve(triangleCount * 3);

    for (size_t tri = 0; tri < triangleCount; ++tri) {
        const uint32_t source0 = mesh.indices[tri * 3 + 0];
        const uint32_t source1 = mesh.indices[tri * 3 + 1];
        const uint32_t source2 = mesh.indices[tri * 3 + 2];
        if (source0 >= sourceVertexCount ||
            source1 >= sourceVertexCount ||
            source2 >= sourceVertexCount) {
            continue;
        }

        const int part = tri < options.triangleToPart.size()
            ? options.triangleToPart[tri]
            : -1;
        const int elementId = tri < options.triangleToElement.size()
            ? options.triangleToElement[tri]
            : static_cast<int>(tri);
        const QVector3D pickColor = idToPickColor(elementId);

        VulkanGpuDrivenTriangleMeta meta{};
        meta.elementId = elementId;
        meta.partId = part;
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float minZ = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();
        float maxZ = std::numeric_limits<float>::lowest();

        uint32_t* metaIndices[3] = {&meta.index0, &meta.index1, &meta.index2};
        for (size_t corner = 0; corner < 3; ++corner) {
            const uint32_t sourceIndex = mesh.indices[tri * 3 + corner];
            const size_t base = static_cast<size_t>(sourceIndex) * 6;
            VulkanGpuDrivenMeshVertex vertex{};
            vertex.position[0] = mesh.vertices[base + 0];
            vertex.position[1] = mesh.vertices[base + 1];
            vertex.position[2] = mesh.vertices[base + 2];
            vertex.normal[0] = mesh.vertices[base + 3];
            vertex.normal[1] = mesh.vertices[base + 4];
            vertex.normal[2] = mesh.vertices[base + 5];
            const QVector3D color = vertexColor(options, sourceIndex, part);
            vertex.color[0] = color.x();
            vertex.color[1] = color.y();
            vertex.color[2] = color.z();
            vertex.pickColor[0] = pickColor.x();
            vertex.pickColor[1] = pickColor.y();
            vertex.pickColor[2] = pickColor.z();

            *metaIndices[corner] = static_cast<uint32_t>(uploadData.vertices.size());
            uploadData.vertices.push_back(vertex);
            uploadData.scalarSourceIndices.push_back(sourceIndex);
            uploadData.expandedScalars.push_back(sourceIndex < options.vertexScalars.size()
                ? options.vertexScalars[sourceIndex]
                : 0.0f);

            minX = std::min(minX, vertex.position[0]);
            minY = std::min(minY, vertex.position[1]);
            minZ = std::min(minZ, vertex.position[2]);
            maxX = std::max(maxX, vertex.position[0]);
            maxY = std::max(maxY, vertex.position[1]);
            maxZ = std::max(maxZ, vertex.position[2]);
        }

        meta.boundsMin[0] = minX;
        meta.boundsMin[1] = minY;
        meta.boundsMin[2] = minZ;
        meta.boundsMax[0] = maxX;
        meta.boundsMax[1] = maxY;
        meta.boundsMax[2] = maxZ;
        uploadData.triangles.push_back(meta);
    }

    uploadData.maxVisibleIndexCount = static_cast<uint32_t>(uploadData.triangles.size() * 3);
    if (hasEdgeMesh) {
        const size_t edgeVertexCount = mesh.edgeVertices.size() / 3;
        uploadData.edgeVertices.reserve(edgeVertexCount);
        for (size_t sourceIndex = 0; sourceIndex < edgeVertexCount; ++sourceIndex) {
            const size_t base = sourceIndex * 3;
            VulkanGpuDrivenLineVertex vertex{};
            vertex.position[0] = mesh.edgeVertices[base + 0];
            vertex.position[1] = mesh.edgeVertices[base + 1];
            vertex.position[2] = mesh.edgeVertices[base + 2];
            vertex.scalar = sourceIndex < options.edgeScalars.size()
                ? options.edgeScalars[sourceIndex]
                : 0.0f;
            uploadData.edgeVertices.push_back(vertex);
        }

        const size_t edgeCount = mesh.edgeIndices.size() / 2;
        uploadData.edges.reserve(edgeCount);
        for (size_t edge = 0; edge < edgeCount; ++edge) {
            const uint32_t source0 = mesh.edgeIndices[edge * 2 + 0];
            const uint32_t source1 = mesh.edgeIndices[edge * 2 + 1];
            if (source0 >= edgeVertexCount || source1 >= edgeVertexCount) {
                continue;
            }
            VulkanGpuDrivenEdgeMeta meta{};
            meta.index0 = source0;
            meta.index1 = source1;
            meta.elementId = edge < mesh.edgeToElement.size()
                ? mesh.edgeToElement[edge]
                : -1;
            meta.partId = edge < options.edgeToPart.size()
                ? options.edgeToPart[edge]
                : -1;
            uploadData.edges.push_back(meta);
        }
        uploadData.maxVisibleEdgeIndexCount = static_cast<uint32_t>(uploadData.edges.size() * 2);
    }
    uploadData.partStates = makePartStates(options, maxPartIndex(mesh, options, triangleCount));
    uploadData.hiddenElements = makeHiddenElements(options);
    return uploadData;
}

VulkanGpuDrivenUploadV2Data buildVulkanGpuDrivenUploadV2Data(
    const Mesh& mesh,
    const VulkanMeshUploadOptions& options)
{
    VulkanGpuDrivenUploadV2Data uploadData;

    const bool hasSurfaceMesh =
        !mesh.vertices.empty() && !mesh.indices.empty() && mesh.vertices.size() % 6 == 0;
    if (!hasSurfaceMesh) {
        uploadData.hiddenElements = makeHiddenElements(options);
        return uploadData;
    }

    const size_t sourceVertexCount = mesh.vertices.size() / 6;
    const size_t triangleCount = mesh.indices.size() / 3;
    const bool hasEdgeMesh =
        !mesh.edgeVertices.empty() && !mesh.edgeIndices.empty() && mesh.edgeVertices.size() % 3 == 0;

    uploadData.sourceVertices.reserve(sourceVertexCount);
    for (size_t sourceIndex = 0; sourceIndex < sourceVertexCount; ++sourceIndex) {
        const size_t base = sourceIndex * 6;
        VulkanGpuDrivenSourceVertex vertex{};
        vertex.position[0] = mesh.vertices[base + 0];
        vertex.position[1] = mesh.vertices[base + 1];
        vertex.position[2] = mesh.vertices[base + 2];
        vertex.position[3] = 1.0f;
        vertex.normal[0] = mesh.vertices[base + 3];
        vertex.normal[1] = mesh.vertices[base + 4];
        vertex.normal[2] = mesh.vertices[base + 5];
        vertex.normal[3] = 0.0f;
        const QVector3D color = vertexColor(options, sourceIndex, -1);
        vertex.color[0] = color.x();
        vertex.color[1] = color.y();
        vertex.color[2] = color.z();
        vertex.scalar = sourceIndex < options.vertexScalars.size()
            ? options.vertexScalars[sourceIndex]
            : 0.0f;
        uploadData.sourceVertices.push_back(vertex);
    }

    uploadData.triangles.reserve(triangleCount);
    for (size_t tri = 0; tri < triangleCount; ++tri) {
        const uint32_t source0 = mesh.indices[tri * 3 + 0];
        const uint32_t source1 = mesh.indices[tri * 3 + 1];
        const uint32_t source2 = mesh.indices[tri * 3 + 2];
        if (source0 >= sourceVertexCount ||
            source1 >= sourceVertexCount ||
            source2 >= sourceVertexCount) {
            continue;
        }

        const int part = tri < options.triangleToPart.size()
            ? options.triangleToPart[tri]
            : -1;
        const int elementId = tri < options.triangleToElement.size()
            ? options.triangleToElement[tri]
            : static_cast<int>(tri);

        VulkanGpuDrivenTriangleMeta meta{};
        meta.index0 = source0;
        meta.index1 = source1;
        meta.index2 = source2;
        meta.elementId = elementId;
        meta.partId = part;

        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float minZ = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();
        float maxZ = std::numeric_limits<float>::lowest();

        const uint32_t sourceIndices[3] = {source0, source1, source2};
        for (uint32_t sourceIndex : sourceIndices) {
            const VulkanGpuDrivenSourceVertex& vertex = uploadData.sourceVertices[sourceIndex];
            minX = std::min(minX, vertex.position[0]);
            minY = std::min(minY, vertex.position[1]);
            minZ = std::min(minZ, vertex.position[2]);
            maxX = std::max(maxX, vertex.position[0]);
            maxY = std::max(maxY, vertex.position[1]);
            maxZ = std::max(maxZ, vertex.position[2]);
        }

        meta.boundsMin[0] = minX;
        meta.boundsMin[1] = minY;
        meta.boundsMin[2] = minZ;
        meta.boundsMax[0] = maxX;
        meta.boundsMax[1] = maxY;
        meta.boundsMax[2] = maxZ;
        uploadData.triangles.push_back(meta);
    }

    uploadData.maxVisibleIndexCount = static_cast<uint32_t>(uploadData.triangles.size() * 3);
    if (hasEdgeMesh) {
        const size_t edgeVertexCount = mesh.edgeVertices.size() / 3;
        uploadData.edgeVertices.reserve(edgeVertexCount);
        for (size_t sourceIndex = 0; sourceIndex < edgeVertexCount; ++sourceIndex) {
            const size_t base = sourceIndex * 3;
            VulkanGpuDrivenLineVertex vertex{};
            vertex.position[0] = mesh.edgeVertices[base + 0];
            vertex.position[1] = mesh.edgeVertices[base + 1];
            vertex.position[2] = mesh.edgeVertices[base + 2];
            vertex.scalar = sourceIndex < options.edgeScalars.size()
                ? options.edgeScalars[sourceIndex]
                : 0.0f;
            uploadData.edgeVertices.push_back(vertex);
        }

        const size_t edgeCount = mesh.edgeIndices.size() / 2;
        uploadData.edges.reserve(edgeCount);
        for (size_t edge = 0; edge < edgeCount; ++edge) {
            const uint32_t source0 = mesh.edgeIndices[edge * 2 + 0];
            const uint32_t source1 = mesh.edgeIndices[edge * 2 + 1];
            if (source0 >= edgeVertexCount || source1 >= edgeVertexCount) {
                continue;
            }
            VulkanGpuDrivenEdgeMeta meta{};
            meta.index0 = source0;
            meta.index1 = source1;
            meta.elementId = edge < mesh.edgeToElement.size()
                ? mesh.edgeToElement[edge]
                : -1;
            meta.partId = edge < options.edgeToPart.size()
                ? options.edgeToPart[edge]
                : -1;
            uploadData.edges.push_back(meta);
        }
        uploadData.maxVisibleEdgeIndexCount = static_cast<uint32_t>(uploadData.edges.size() * 2);
    }

    uploadData.partStates = makePartStates(options, maxPartIndex(mesh, options, triangleCount));
    uploadData.hiddenElements = makeHiddenElements(options);
    return uploadData;
}

VulkanGpuDrivenVisibilityStateData buildVulkanGpuDrivenVisibilityStateData(
    const VulkanMeshUploadOptions& options,
    uint32_t triangleCount,
    uint32_t minimumPartStateCount)
{
    VulkanGpuDrivenVisibilityStateData stateData;

    int maxPart = -1;
    for (int part : options.triangleToPart) {
        maxPart = std::max(maxPart, part);
    }
    for (int part : options.edgeToPart) {
        maxPart = std::max(maxPart, part);
    }
    for (const auto& [part, visible] : options.partVisibility) {
        (void)visible;
        maxPart = std::max(maxPart, part);
    }
    if (!options.partColors.empty()) {
        maxPart = std::max(maxPart, static_cast<int>(options.partColors.size() - 1));
    }
    if (minimumPartStateCount > 0) {
        maxPart = std::max(maxPart, static_cast<int>(minimumPartStateCount - 1));
    }

    stateData.partStates = makePartStates(options, maxPart);
    stateData.hiddenElements = makeHiddenElements(options);
    stateData.uniforms.triangleCount = triangleCount;
    stateData.uniforms.hiddenElementCount =
        static_cast<uint32_t>(stateData.hiddenElements.size());
    stateData.uniforms.enablePartVisibility = 1;
    stateData.uniforms.partStateCount =
        static_cast<uint32_t>(stateData.partStates.size());
    return stateData;
}
