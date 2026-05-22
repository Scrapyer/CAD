#pragma once

#include "MetalShaderTypes.h"

#include <QSize>

struct MetalMeshFramePassDraw {
    bool enabled = false;
    void* pipelineState = nullptr;
    void* vertexBuffer = nullptr;
    void* indexBuffer = nullptr;
    int vertexCount = 0;
    int indexCount = 0;
    MetalMeshUniforms uniforms{};
};

struct MetalMeshFramePassInputs {
    void* encoder = nullptr;
    void* backgroundPipelineState = nullptr;
    void* depthStencilState = nullptr;
    void* overlayDepthStencilState = nullptr;
    QSize drawableSize;
    MetalMeshFramePassDraw surface;
    MetalMeshFramePassDraw edges;
    MetalMeshFramePassDraw points;
    MetalMeshFramePassDraw isoSurface;
    MetalMeshFramePassDraw clipPreview;
    MetalMeshFramePassDraw overlay;
    MetalMeshFramePassDraw slice;
    MetalMeshFramePassDraw clipPreviewLines;
    MetalMeshFramePassDraw selection;
    MetalMeshFramePassDraw axes;
};

void encodeMetalMeshFramePass(const MetalMeshFramePassInputs& inputs);
