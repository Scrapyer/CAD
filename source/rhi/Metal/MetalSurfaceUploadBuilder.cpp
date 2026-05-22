#include "MetalSurfaceUploadBuilder.h"

MetalSurfaceUploadData buildMetalSurfaceUploadData(const Mesh& mesh, const QVector3D& color)
{
    MetalSurfaceUploadData uploadData;
    const size_t sourceVertexCount = mesh.vertices.size() / 6;
    uploadData.vertices.reserve(sourceVertexCount);

    for (size_t sourceIndex = 0; sourceIndex < sourceVertexCount; ++sourceIndex) {
        const size_t base = sourceIndex * 6;
        MetalMeshVertex vertex{};
        vertex.position[0] = mesh.vertices[base + 0];
        vertex.position[1] = mesh.vertices[base + 1];
        vertex.position[2] = mesh.vertices[base + 2];
        vertex.normal[0] = mesh.vertices[base + 3];
        vertex.normal[1] = mesh.vertices[base + 4];
        vertex.normal[2] = mesh.vertices[base + 5];
        vertex.color[0] = color.x();
        vertex.color[1] = color.y();
        vertex.color[2] = color.z();
        vertex.pickColor[0] = 0.0f;
        vertex.pickColor[1] = 0.0f;
        vertex.pickColor[2] = 0.0f;
        uploadData.vertices.push_back(vertex);
    }

    uploadData.indices.assign(mesh.indices.begin(), mesh.indices.end());
    return uploadData;
}
