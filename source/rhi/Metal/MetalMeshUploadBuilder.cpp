#include "MetalMeshUploadBuilder.h"

#include "MetalPickUtils.h"

namespace {

bool isPartVisible(const MetalMeshUploadOptions& options, int part)
{
    const auto it = options.partVisibility.find(part);
    return it == options.partVisibility.end() || it->second;
}

bool isElementVisible(const MetalMeshUploadOptions& options, int elementId)
{
    return elementId < 0 || options.hiddenElementIds.count(elementId) == 0;
}

QVector3D triangleColor(const MetalMeshUploadOptions& options, int part)
{
    if (part >= 0 && part < static_cast<int>(options.partColors.size())) {
        return options.partColors[static_cast<size_t>(part)];
    }
    return options.objectColor;
}

QVector3D vertexColor(const MetalMeshUploadOptions& options, size_t sourceIndex, int part)
{
    if (options.useVertexColor && sourceIndex * 3 + 2 < options.vertexColors.size()) {
        return QVector3D(options.vertexColors[sourceIndex * 3 + 0],
                         options.vertexColors[sourceIndex * 3 + 1],
                         options.vertexColors[sourceIndex * 3 + 2]);
    }
    return triangleColor(options, part);
}

} // namespace

MetalMeshUploadData buildMetalMeshUploadData(const Mesh& mesh,
                                             const MetalMeshUploadOptions& options)
{
    MetalMeshUploadData uploadData;
    const size_t sourceVertexCount = mesh.vertices.size() / 6;
    const size_t triangleCount = mesh.indices.size() / 3;
    uploadData.vertices.reserve(triangleCount * 3);
    uploadData.indices.reserve(triangleCount * 3);
    uploadData.scalarSourceIndices.reserve(triangleCount * 3);
    std::vector<unsigned char> visiblePointVertices(sourceVertexCount, 0);

    for (size_t tri = 0; tri < triangleCount; ++tri) {
        const int part = tri < options.triangleToPart.size()
            ? options.triangleToPart[tri]
            : -1;
        if (!isPartVisible(options, part)) {
            continue;
        }

        const int elementId = tri < options.triangleToElement.size()
            ? options.triangleToElement[tri]
            : static_cast<int>(tri);
        if (!isElementVisible(options, elementId)) {
            continue;
        }
        const QVector3D pickColor = metalIdToPickColor(elementId);
        for (size_t corner = 0; corner < 3; ++corner) {
            const unsigned int sourceIndex = mesh.indices[tri * 3 + corner];
            if (sourceIndex >= sourceVertexCount) {
                continue;
            }
            visiblePointVertices[sourceIndex] = 1;
            const size_t base = static_cast<size_t>(sourceIndex) * 6;
            MetalMeshVertex vertex{};
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
            vertex.scalar = sourceIndex < options.vertexScalars.size()
                ? options.vertexScalars[sourceIndex]
                : 0.0f;
            uploadData.indices.push_back(static_cast<unsigned int>(uploadData.vertices.size()));
            uploadData.vertices.push_back(vertex);
            uploadData.scalarSourceIndices.push_back(sourceIndex);
        }
    }

    uploadData.edgeVertexCount = static_cast<int>(mesh.edgeVertices.size() / 3);
    if (!mesh.edgeVertices.empty() && !mesh.edgeIndices.empty() && mesh.edgeVertices.size() % 3 == 0) {
        uploadData.edgeVertices.resize(static_cast<size_t>(uploadData.edgeVertexCount));
        const bool useEdgeScalars =
            options.useVertexColor &&
            options.edgeScalars.size() == static_cast<size_t>(uploadData.edgeVertexCount);
        for (int i = 0; i < uploadData.edgeVertexCount; ++i) {
            MetalLineVertex vertex{};
            vertex.position[0] = mesh.edgeVertices[static_cast<size_t>(i) * 3 + 0];
            vertex.position[1] = mesh.edgeVertices[static_cast<size_t>(i) * 3 + 1];
            vertex.position[2] = mesh.edgeVertices[static_cast<size_t>(i) * 3 + 2];
            vertex.scalar = useEdgeScalars ? options.edgeScalars[static_cast<size_t>(i)] : 0.0f;
            uploadData.edgeVertices[static_cast<size_t>(i)] = vertex;
        }
        const size_t edgeCount = mesh.edgeIndices.size() / 2;
        uploadData.edgeIndices.reserve(mesh.edgeIndices.size());
        for (size_t edge = 0; edge < edgeCount; ++edge) {
            const int part = edge < options.edgeToPart.size()
                ? options.edgeToPart[edge]
                : -1;
            const int elementId = edge < mesh.edgeToElement.size()
                ? mesh.edgeToElement[edge]
                : -1;
            if (!isElementVisible(options, elementId)) {
                continue;
            }
            if (!isPartVisible(options, part)) {
                continue;
            }
            uploadData.edgeIndices.push_back(mesh.edgeIndices[edge * 2]);
            uploadData.edgeIndices.push_back(mesh.edgeIndices[edge * 2 + 1]);
        }
    }

    uploadData.pointVertices.reserve(sourceVertexCount);
    for (size_t sourceIndex = 0; sourceIndex < sourceVertexCount; ++sourceIndex) {
        if (!visiblePointVertices[sourceIndex]) {
            continue;
        }
        const size_t base = sourceIndex * 6;
        MetalLineVertex point{};
        point.position[0] = mesh.vertices[base + 0];
        point.position[1] = mesh.vertices[base + 1];
        point.position[2] = mesh.vertices[base + 2];
        point.scalar = 0.0f;
        uploadData.pointVertices.push_back(point);
    }
    uploadData.pointVertexCount = static_cast<int>(uploadData.pointVertices.size());

    return uploadData;
}
