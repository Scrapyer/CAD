#pragma once

#include "Geometry.h"
#include "MetalBufferResource.h"
#include "MetalMeshUploadBuilder.h"

#include <QString>

struct MetalMeshBufferTargets {
    MetalBufferResource* meshVertexBuffer = nullptr;
    MetalBufferResource* meshIndexBuffer = nullptr;
    MetalBufferResource* pointVertexBuffer = nullptr;
    MetalBufferResource* edgeVertexBuffer = nullptr;
    MetalBufferResource* edgeIndexBuffer = nullptr;
    int* meshVertexCount = nullptr;
    int* meshIndexCount = nullptr;
    int* pointVertexCount = nullptr;
    int* edgeVertexCount = nullptr;
    int* edgeIndexCount = nullptr;
};

bool uploadMetalMeshBuffers(void* device,
                            void* commandQueue,
                            const Mesh& mesh,
                            const MetalMeshUploadData& uploadData,
                            const MetalMeshBufferTargets& targets,
                            QString& lastError);
