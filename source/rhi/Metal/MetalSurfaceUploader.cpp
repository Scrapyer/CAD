#include "MetalSurfaceUploader.h"

#include "MetalLineUpload.h"

namespace {

void resetMetalSurfaceTargets(const MetalSurfaceBufferTargets& targets)
{
    if (targets.vertexBuffer) {
        targets.vertexBuffer->destroy();
    }
    if (targets.indexBuffer) {
        targets.indexBuffer->destroy();
    }
    if (targets.indexCount) {
        *targets.indexCount = 0;
    }
}

void resetMetalClipPreviewLineTargets(const MetalClipPreviewLineTargets& targets)
{
    if (targets.vertexBuffer) {
        targets.vertexBuffer->destroy();
    }
    if (targets.vertexCount) {
        *targets.vertexCount = 0;
    }
}

bool validateMetalSurfaceTargets(const MetalSurfaceBufferTargets& targets, QString& lastError)
{
    if (!targets.vertexBuffer || !targets.indexBuffer || !targets.indexCount) {
        lastError = QStringLiteral("Metal surface upload target is incomplete");
        return false;
    }
    return true;
}

bool validateMetalClipPreviewLineTargets(const MetalClipPreviewLineTargets& targets,
                                         QString& lastError)
{
    if (!targets.vertexBuffer || !targets.vertexCount) {
        lastError = QStringLiteral("Metal clip preview line upload target is incomplete");
        return false;
    }
    return true;
}

} // namespace

bool uploadMetalSurfaceBuffers(void* device,
                               void* commandQueue,
                               const MetalSurfaceUploadData& uploadData,
                               const MetalSurfaceBufferTargets& targets,
                               const QString& label,
                               QString& lastError)
{
    if (!validateMetalSurfaceTargets(targets, lastError)) {
        return false;
    }

    resetMetalSurfaceTargets(targets);
    if (uploadData.vertices.empty() || uploadData.indices.empty()) {
        return true;
    }

    *targets.indexCount = static_cast<int>(uploadData.indices.size());
    const std::vector<MetalPrivateBufferUpload> uploads = {
        {targets.vertexBuffer,
         uploadData.vertices.data(),
         uploadData.vertices.size() * sizeof(MetalMeshVertex),
         QStringLiteral("%1 vertex").arg(label)},
        {targets.indexBuffer,
         uploadData.indices.data(),
         uploadData.indices.size() * sizeof(unsigned int),
         QStringLiteral("%1 index").arg(label)}
    };
    if (!MetalBufferResource::uploadPrivateBatch(device, commandQueue, uploads, lastError)) {
        resetMetalSurfaceTargets(targets);
        return false;
    }

    return true;
}

bool uploadMetalClipPreviewBuffers(void* device,
                                   void* commandQueue,
                                   const Mesh& mesh,
                                   const MetalSurfaceUploadData& uploadData,
                                   const MetalSurfaceBufferTargets& surfaceTargets,
                                   const MetalClipPreviewLineTargets& lineTargets,
                                   QString& lastError)
{
    if (!validateMetalClipPreviewLineTargets(lineTargets, lastError)) {
        return false;
    }
    if (!uploadMetalSurfaceBuffers(device,
                                   commandQueue,
                                   uploadData,
                                   surfaceTargets,
                                   QStringLiteral("clip preview"),
                                   lastError)) {
        resetMetalClipPreviewLineTargets(lineTargets);
        return false;
    }

    resetMetalClipPreviewLineTargets(lineTargets);
    if (!mesh.edgeVertices.empty()) {
        if (!uploadMetalLineVertices(device,
                                     mesh.edgeVertices,
                                     *lineTargets.vertexBuffer,
                                     *lineTargets.vertexCount,
                                     QStringLiteral("clip preview"),
                                     lastError)) {
            resetMetalSurfaceTargets(surfaceTargets);
            resetMetalClipPreviewLineTargets(lineTargets);
            return false;
        }
    }

    return true;
}
