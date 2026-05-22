#pragma once

#include "MetalBufferResource.h"
#include "MetalSurfaceUploadBuilder.h"

#include <QString>

struct MetalSurfaceBufferTargets {
    MetalBufferResource* vertexBuffer = nullptr;
    MetalBufferResource* indexBuffer = nullptr;
    int* indexCount = nullptr;
};

struct MetalClipPreviewLineTargets {
    MetalBufferResource* vertexBuffer = nullptr;
    int* vertexCount = nullptr;
};

bool uploadMetalSurfaceBuffers(void* device,
                               const MetalSurfaceUploadData& uploadData,
                               const MetalSurfaceBufferTargets& targets,
                               const QString& label,
                               QString& lastError);

bool uploadMetalClipPreviewBuffers(void* device,
                                   const Mesh& mesh,
                                   const MetalSurfaceUploadData& uploadData,
                                   const MetalSurfaceBufferTargets& surfaceTargets,
                                   const MetalClipPreviewLineTargets& lineTargets,
                                   QString& lastError);
