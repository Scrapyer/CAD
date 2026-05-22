#pragma once

#include "MetalShaderTypes.h"

#include <QString>

struct MetalPickPassInputs {
    void* commandQueue = nullptr;
    void* colorTexture = nullptr;
    void* depthTexture = nullptr;
    void* readbackBuffer = nullptr;
    void* depthStencilState = nullptr;
    void* pickPipelineState = nullptr;
    void* meshVertexBuffer = nullptr;
    void* meshIndexBuffer = nullptr;
    int meshIndexCount = 0;
    int pickX = 0;
    int pickY = 0;
    MetalMeshUniforms uniforms{};
};

bool executeMetalPickPass(const MetalPickPassInputs& inputs, int& elementId, QString& lastError);
