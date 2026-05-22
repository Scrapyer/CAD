#pragma once

#include "MetalShaderTypes.h"

#include <QSize>

#include <cstddef>

struct MetalMeshFramePassDraw {
    bool enabled = false;
    void* pipelineState = nullptr;
    void* vertexBuffer = nullptr;
    void* indexBuffer = nullptr;
    int vertexCount = 0;
    int indexCount = 0;
    MetalMeshUniforms uniforms{};
    size_t uniformOffset = 0;
};

struct MetalMeshFramePassInputs {
    void* encoder = nullptr;
    void* uniformBuffer = nullptr;
    void* backgroundPipelineState = nullptr;
    void* depthStencilState = nullptr;
    void* overlayDepthStencilState = nullptr;
    QSize drawableSize;
    MetalBackgroundUniforms backgroundUniforms{};
    size_t backgroundUniformOffset = 0;
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
    size_t axesUniformOffsets[3] = {0, 0, 0};
};

void encodeMetalMeshFramePass(const MetalMeshFramePassInputs& inputs);
