#pragma once

#include "MetalStateResource.h"

#include <QString>

enum class MetalPipelineVertexLayout {
    None,
    Mesh,
    Line,
    Axis,
    Pick
};

struct MetalRenderPipelineConfig {
    const char* shaderSource = nullptr;
    QString label;
    unsigned long colorPixelFormat = 0;
    unsigned long depthPixelFormat = 0;
    MetalPipelineVertexLayout vertexLayout = MetalPipelineVertexLayout::None;
    bool alphaBlending = false;
};

bool createMetalRenderPipelineState(void* device,
                                    const MetalRenderPipelineConfig& config,
                                    void*& pipelineState,
                                    QString& lastError);

bool ensureMetalRenderPipelineState(void* device,
                                    const MetalRenderPipelineConfig& config,
                                    MetalStateResource& pipelineState,
                                    QString& lastError);
