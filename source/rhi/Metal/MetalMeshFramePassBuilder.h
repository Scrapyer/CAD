#pragma once

#include "MetalMeshFramePass.h"

#include <QMatrix4x4>
#include <QVector3D>

struct MetalMeshFrameDrawFlags {
    bool surface = false;
    bool edges = false;
    bool points = false;
    bool isoSurface = false;
    bool clipPreview = false;
    bool overlay = false;
    bool slice = false;
    bool clipPreviewLines = false;
    bool selection = false;
    bool axes = false;
    bool axesSolid = false;
};

struct MetalMeshFrameResourceHandles {
    void* backgroundPipelineState = nullptr;
    void* meshPipelineState = nullptr;
    void* isoSurfacePipelineState = nullptr;
    void* linePipelineState = nullptr;
    void* depthStencilState = nullptr;
    void* overlayDepthStencilState = nullptr;
    QSize drawableSize;
    QVector3D backgroundTopColor{0.38f, 0.45f, 0.58f};
    QVector3D backgroundBottomColor{0.68f, 0.74f, 0.82f};
    bool backgroundGridVisible = true;
    float backgroundGridMinorStep = 24.0f;
    float backgroundGridFineAlpha = 0.0f;
    void* meshVertexBuffer = nullptr;
    void* meshIndexBuffer = nullptr;
    int meshVertexCount = 0;
    int meshIndexCount = 0;
    void* pointVertexBuffer = nullptr;
    void* edgeVertexBuffer = nullptr;
    void* edgeIndexBuffer = nullptr;
    int edgeVertexCount = 0;
    int edgeIndexCount = 0;
    void* isoSurfaceVertexBuffer = nullptr;
    void* isoSurfaceIndexBuffer = nullptr;
    int isoSurfaceIndexCount = 0;
    void* clipPreviewVertexBuffer = nullptr;
    void* clipPreviewIndexBuffer = nullptr;
    int clipPreviewIndexCount = 0;
    void* overlayVertexBuffer = nullptr;
    int overlayVertexCount = 0;
    void* sliceVertexBuffer = nullptr;
    int sliceVertexCount = 0;
    void* clipPreviewLineVertexBuffer = nullptr;
    int clipPreviewLineVertexCount = 0;
    void* selectionVertexBuffer = nullptr;
    int selectionVertexCount = 0;
    void* axesLineVertexBuffer = nullptr;
    int axesLineVertexCount = 0;
    void* axesSolidVertexBuffer = nullptr;
    int axesSolidVertexCount = 0;
};

struct MetalMeshFrameUniformSet {
    MetalMeshUniforms mesh{};
    MetalMeshUniforms line{};
    MetalMeshUniforms overlay{};
    MetalMeshUniforms slice{};
    MetalMeshUniforms isoSurface{};
    MetalMeshUniforms clipPreview{};
    MetalMeshUniforms clipPreviewLine{};
    MetalMeshUniforms selection{};
    MetalMeshUniforms axes{};
    MetalMeshUniforms axesSolid{};
};

MetalMeshFrameUniformSet buildMetalMeshFrameUniformSet(const QMatrix4x4& mvp,
                                                       const QVector3D& objectColor,
                                                       float scalarMin,
                                                       float scalarMax,
                                                       int numBands,
                                                       bool useVertexScalars,
                                                       const QMatrix4x4& axesMvp);

MetalMeshFramePassInputs buildMetalMeshFramePassInputs(const MetalMeshFrameDrawFlags& flags,
                                                       const MetalMeshFrameResourceHandles& resources,
                                                       const MetalMeshFrameUniformSet& uniforms);
