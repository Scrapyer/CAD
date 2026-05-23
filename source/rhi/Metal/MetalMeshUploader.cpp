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
    if (targets.pointVertexCount) {
        *targets.pointVertexCount = 0;
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
        !targets.pointVertexCount ||
        !targets.edgeVertexCount ||
        !targets.edgeIndexCount) {
        lastError = QStringLiteral("Metal mesh upload target is incomplete");
        return false;
    }
    return true;
}

} // namespace

bool uploadMetalMeshBuffers(void* device,
                            void* commandQueue,
                            const Mesh& mesh,
                            const MetalMeshUploadData& uploadData,
                            const MetalMeshBufferTargets& targets,
                            QString& lastError)
{
    if (!validateMetalMeshUploadTargets(targets, lastError)) {
        return false;
    }

    resetMetalMeshUploadTargets(targets);
    if (uploadData.vertices.empty() && uploadData.edgeVertices.empty()) {
        return true;
    }

    *targets.meshVertexCount = static_cast<int>(uploadData.vertices.size());
    *targets.meshIndexCount = static_cast<int>(uploadData.indices.size());
    *targets.pointVertexCount = uploadData.pointVertexCount;
    *targets.edgeVertexCount = uploadData.edgeVertexCount;
    *targets.edgeIndexCount = static_cast<int>(uploadData.edgeIndices.size());

    std::vector<MetalPrivateBufferUpload> uploads;
    if (!uploadData.vertices.empty() && !uploadData.indices.empty()) {
        uploads.push_back({targets.meshVertexBuffer,
                           uploadData.vertices.data(),
                           uploadData.vertices.size() * sizeof(MetalMeshVertex),
                           QStringLiteral("mesh vertex")});
        uploads.push_back({targets.meshIndexBuffer,
                           uploadData.indices.data(),
                           uploadData.indices.size() * sizeof(unsigned int),
                           QStringLiteral("mesh index")});
        uploads.push_back({targets.pointVertexBuffer,
                           uploadData.pointVertices.data(),
                           uploadData.pointVertices.size() * sizeof(MetalLineVertex),
                           QStringLiteral("mesh point")});
    }
    if (*targets.edgeVertexCount > 0 && *targets.edgeIndexCount > 0) {
        uploads.push_back({targets.edgeVertexBuffer,
                           uploadData.edgeVertices.data(),
                           uploadData.edgeVertices.size() * sizeof(MetalLineVertex),
                           QStringLiteral("edge vertex")});
        uploads.push_back({targets.edgeIndexBuffer,
                           uploadData.edgeIndices.data(),
                           uploadData.edgeIndices.size() * sizeof(unsigned int),
                           QStringLiteral("edge index")});
    }

    if (!MetalBufferResource::uploadPrivateBatch(device, commandQueue, uploads, lastError)) {
        resetMetalMeshUploadTargets(targets);
        return false;
    }

    return true;
}
