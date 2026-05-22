#pragma once

#include "MetalPickPass.h"

#include <QMatrix4x4>

struct MetalPickPassResourceHandles {
    void* commandQueue = nullptr;
    void* colorTexture = nullptr;
    void* depthTexture = nullptr;
    void* readbackBuffer = nullptr;
    void* depthStencilState = nullptr;
    void* pickPipelineState = nullptr;
    void* meshVertexBuffer = nullptr;
    void* meshIndexBuffer = nullptr;
    int meshIndexCount = 0;
};

MetalPickPassInputs buildMetalPickPassInputs(const MetalPickPassResourceHandles& resources,
                                             const QMatrix4x4& mvp,
                                             int pickX,
                                             int pickY);
