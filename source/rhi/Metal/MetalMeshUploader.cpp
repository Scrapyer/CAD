#include "MetalMeshUploader.h"

namespace {

void resetMetalMeshUploadTargets(const MetalMeshBufferTargets& targets)
{
    if (targets.meshVertexBuffer) {
        targets.meshVertexBuffer->destroy();
    }
    if (targets.meshIndexBuffer) {
        targets.meshIndexBuffer->destroy();
    }
    if (targets.pointVertexBuffer) {
        targets.pointVertexBuffer->destroy();
    }
    if (targets.edgeVertexBuffer) {
        targets.edgeVertexBuffer->destroy();
    }
    if (targets.edgeIndexBuffer) {
        targets.edgeIndexBuffer->destroy();
    }
    if (targets.meshVertexCount) {
        *targets.meshVertexCount = 0;
    }
    if (targets.meshIndexCount) {
        *targets.meshIndexCount = 0;
    }
    if (targets.edgeVertexCount) {
        *targets.edgeVertexCount = 0;
    }
    if (targets.edgeIndexCount) {
        *targets.edgeIndexCount = 0;
    }
}

bool validateMetalMeshUploadTargets(const MetalMeshBufferTargets& targets, QString& lastError)
{
    if (!targets.meshVertexBuffer ||
        !targets.meshIndexBuffer ||
        !targets.pointVertexBuffer ||
        !targets.edgeVertexBuffer ||
        !targets.edgeIndexBuffer ||
        !targets.meshVertexCount ||
        !targets.meshIndexCount ||
        !targets.edgeVertexCount ||
        !targets.edgeIndexCount) {
        lastError = QStringLiteral("Metal mesh upload target is incomplete");
        return false;
    }
    return true;
}

} // namespace

bool uploadMetalMeshBuffers(void* device,
                            const Mesh& mesh,
                            const MetalMeshUploadData& uploadData,
                            const MetalMeshBufferTargets& targets,
                            QString& lastError)
{
    if (!validateMetalMeshUploadTargets(targets, lastError)) {
        return false;
    }

    resetMetalMeshUploadTargets(targets);
    if (uploadData.vertices.empty() || uploadData.indices.empty()) {
        return true;
    }

    *targets.meshVertexCount = static_cast<int>(uploadData.vertices.size());
    *targets.meshIndexCount = static_cast<int>(uploadData.indices.size());
    *targets.edgeVertexCount = uploadData.edgeVertexCount;
    *targets.edgeIndexCount = static_cast<int>(uploadData.edgeIndices.size());

    if (!targets.meshVertexBuffer->upload(device,
                                          uploadData.vertices.data(),
                                          uploadData.vertices.size() * sizeof(MetalMeshVertex),
                                          QStringLiteral("mesh vertex"),
                                          lastError) ||
        !targets.meshIndexBuffer->upload(device,
                                         uploadData.indices.data(),
                                         uploadData.indices.size() * sizeof(unsigned int),
                                         QStringLiteral("mesh index"),
                                         lastError) ||
        !targets.pointVertexBuffer->upload(device,
                                           uploadData.pointPositions.data(),
                                           uploadData.pointPositions.size() * sizeof(float),
                                           QStringLiteral("mesh point"),
                                           lastError)) {
        resetMetalMeshUploadTargets(targets);
        return false;
    }

    if (*targets.edgeVertexCount > 0 && *targets.edgeIndexCount > 0) {
        if (!targets.edgeVertexBuffer->upload(device,
                                              mesh.edgeVertices.data(),
                                              mesh.edgeVertices.size() * sizeof(float),
                                              QStringLiteral("edge vertex"),
                                              lastError) ||
            !targets.edgeIndexBuffer->upload(device,
                                             uploadData.edgeIndices.data(),
                                             uploadData.edgeIndices.size() * sizeof(unsigned int),
                                             QStringLiteral("edge index"),
                                             lastError)) {
            resetMetalMeshUploadTargets(targets);
            return false;
        }
    }

    return true;
}
