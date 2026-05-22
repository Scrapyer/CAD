#include "MetalRenderBackend.h"

#include "MetalAttachmentResourceBuilder.h"
#include "MetalClearFramePass.h"
#include "MetalDeviceFactory.h"
#include "MetalDrawableFrameSubmitter.h"
#include "MetalLayerHost.h"
#include "MetalLineUpload.h"
#include "MetalMeshFramePass.h"
#include "MetalMeshFramePassBuilder.h"
#include "MetalMeshScalarUpdater.h"
#include "MetalPipelineFactory.h"
#include "MetalPickPass.h"
#include "MetalRenderPassFactory.h"
#include "MetalShaderSources.h"
#include "MetalShaderTypes.h"
#include "MetalSurfaceUploadBuilder.h"
#include "MetalSurfaceUploader.h"

#include <QWindow>

#include <algorithm>
#include <array>
#include <vector>

#import <Metal/Metal.h>

MetalRenderBackend::~MetalRenderBackend()
{
    destroy();
}

bool MetalRenderBackend::isSystemAvailable()
{
    return isMetalSystemDeviceAvailable();
}

void MetalRenderBackend::initialize()
{
    if (initialized_) {
        return;
    }

    MetalDeviceContext context = createMetalDeviceContext();
    info_ = context.info;
    lastError_ = context.error;
    if (context.device && context.commandQueue) {
        device_.adopt(context.device);
        commandQueue_.adopt(context.commandQueue);
        lastError_.clear();
    }
    initialized_ = true;
}

bool MetalRenderBackend::ensureBackgroundPipeline()
{
    const MetalRenderPipelineConfig config{
        kMetalBackgroundShaderSource,
        QStringLiteral("background"),
        static_cast<unsigned long>(MTLPixelFormatBGRA8Unorm),
        static_cast<unsigned long>(MTLPixelFormatDepth32Float),
        MetalPipelineVertexLayout::None,
        false
    };
    return ensureMetalRenderPipelineState(device_.handle(),
                                          config,
                                          backgroundPipelineState_,
                                          lastError_);
}

bool MetalRenderBackend::ensureMeshPipeline()
{
    const MetalRenderPipelineConfig config{
        kMetalMeshShaderSource,
        QStringLiteral("mesh"),
        static_cast<unsigned long>(MTLPixelFormatBGRA8Unorm),
        static_cast<unsigned long>(MTLPixelFormatDepth32Float),
        MetalPipelineVertexLayout::Mesh,
        false
    };
    return ensureMetalRenderPipelineState(device_.handle(),
                                          config,
                                          meshPipelineState_,
                                          lastError_);
}

bool MetalRenderBackend::ensureIsoSurfacePipeline()
{
    const MetalRenderPipelineConfig config{
        kMetalMeshShaderSource,
        QStringLiteral("iso surface"),
        static_cast<unsigned long>(MTLPixelFormatBGRA8Unorm),
        static_cast<unsigned long>(MTLPixelFormatDepth32Float),
        MetalPipelineVertexLayout::Mesh,
        true
    };
    return ensureMetalRenderPipelineState(device_.handle(),
                                          config,
                                          isoSurfacePipelineState_,
                                          lastError_);
}

bool MetalRenderBackend::ensureLinePipeline()
{
    const MetalRenderPipelineConfig config{
        kMetalLineShaderSource,
        QStringLiteral("line"),
        static_cast<unsigned long>(MTLPixelFormatBGRA8Unorm),
        static_cast<unsigned long>(MTLPixelFormatDepth32Float),
        MetalPipelineVertexLayout::Line,
        false
    };
    return ensureMetalRenderPipelineState(device_.handle(),
                                          config,
                                          linePipelineState_,
                                          lastError_);
}

bool MetalRenderBackend::ensurePickPipeline()
{
    const MetalRenderPipelineConfig config{
        kMetalPickShaderSource,
        QStringLiteral("pick"),
        static_cast<unsigned long>(MTLPixelFormatRGBA8Unorm),
        static_cast<unsigned long>(MTLPixelFormatDepth32Float),
        MetalPipelineVertexLayout::Pick,
        false
    };
    return ensureMetalRenderPipelineState(device_.handle(),
                                          config,
                                          pickPipelineState_,
                                          lastError_);
}

bool MetalRenderBackend::ensureAxesResources()
{
    if (axesLineVertexBuffer_.isValid() && axesLineVertexCount_ >= 6) {
        return true;
    }
    initialize();
    if (!initialized_ || !device_.isValid()) {
        if (lastError_.isEmpty()) {
            lastError_ = QStringLiteral("Metal backend is not initialized");
        }
        return false;
    }

    const std::array<float, 18> axesVertices = {
        0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f
    };
    if (!axesLineVertexBuffer_.upload(device_.handle(),
                                      axesVertices.data(),
                                      axesVertices.size() * sizeof(float),
                                      QStringLiteral("axes line"),
                                      lastError_)) {
        axesLineVertexCount_ = 0;
        return false;
    }
    axesLineVertexCount_ = static_cast<int>(axesVertices.size() / 3);
    return true;
}

bool MetalRenderBackend::ensureDepthResources()
{
    return ensureMetalFrameAttachmentResources(device_.handle(),
                                               drawableSize_,
                                               depthStencilState_,
                                               overlayDepthStencilState_,
                                               depthTexture_,
                                               lastError_);
}

bool MetalRenderBackend::ensurePickResources()
{
    return ensureMetalPickAttachmentResources(device_.handle(),
                                              drawableSize_,
                                              pickColorTexture_,
                                              pickDepthTexture_,
                                              pickReadbackBuffer_,
                                              lastError_);
}

bool MetalRenderBackend::initializeLayer(QWindow* window,
                                         int width,
                                         int height,
                                         qreal devicePixelRatio)
{
    lastError_.clear();
    initialize();
    if (!initialized_ || !device_.isValid() || !commandQueue_.isValid()) {
        if (lastError_.isEmpty()) {
            lastError_ = QStringLiteral("Metal backend is not initialized");
        }
        return false;
    }

    void* layer = nullptr;
    if (!prepareMetalLayerForWindow(window, device_.handle(), layer, lastError_)) {
        return false;
    }

    destroyLayer();
    metalLayer_.retain(layer);
    resizeLayer(width, height, devicePixelRatio);
    return true;
}

void MetalRenderBackend::resizeLayer(int width, int height, qreal devicePixelRatio)
{
    if (!metalLayer_.isValid()) {
        return;
    }

    resizeMetalLayerDrawable(metalLayer_.handle(),
                             width,
                             height,
                             devicePixelRatio,
                             drawableSize_);
    destroyDepthResources();
    destroyPickResources();
}

bool MetalRenderBackend::uploadMesh(const Mesh& mesh, const MetalMeshUploadOptions& options)
{
    lastError_.clear();
    initialize();
    if (!initialized_ || !device_.isValid()) {
        if (lastError_.isEmpty()) {
            lastError_ = QStringLiteral("Metal backend is not initialized");
        }
        return false;
    }

    destroyMeshResources();
    destroyEdgeResources();
    if (mesh.vertices.empty() || mesh.indices.empty() || mesh.vertices.size() % 6 != 0) {
        return true;
    }

    meshUseVertexScalars_ = options.useVertexColor && !options.vertexScalars.empty();
    meshScalarMin_ = options.scalarMin;
    meshScalarMax_ = options.scalarMax;
    meshNumBands_ = std::max(1, options.numBands);
    MetalMeshUploadData uploadData = buildMetalMeshUploadData(mesh, options);
    meshScalarSourceIndices_ = uploadData.scalarSourceIndices;
    const MetalMeshBufferTargets targets{
        &meshVertexBuffer_,
        &meshIndexBuffer_,
        &pointVertexBuffer_,
        &edgeVertexBuffer_,
        &edgeIndexBuffer_,
        &meshVertexCount_,
        &meshIndexCount_,
        &edgeVertexCount_,
        &edgeIndexCount_
    };
    if (!uploadMetalMeshBuffers(device_.handle(), mesh, uploadData, targets, lastError_)) {
        destroyMeshResources();
        destroyEdgeResources();
        return false;
    }

    return true;
}

bool MetalRenderBackend::uploadVertexScalars(const std::vector<float>& scalars,
                                             float minVal,
                                             float maxVal,
                                             int numBands,
                                             bool useScalars)
{
    lastError_.clear();
    meshUseVertexScalars_ = useScalars && !scalars.empty();
    meshScalarMin_ = minVal;
    meshScalarMax_ = maxVal;
    meshNumBands_ = std::max(1, numBands);
    if (!meshUseVertexScalars_) {
        return true;
    }
    if (!meshVertexBuffer_.isValid() || meshScalarSourceIndices_.empty()) {
        lastError_ = QStringLiteral("Metal mesh scalar buffer is not initialized");
        return false;
    }

    @autoreleasepool {
        return updateMetalMeshVertexScalars(meshVertexBuffer_.handle(),
                                            meshVertexCount_,
                                            meshScalarSourceIndices_,
                                            scalars,
                                            lastError_);
    }
}

bool MetalRenderBackend::uploadLineBuffer(const std::vector<float>& lineVertices,
                                          MetalBufferResource& buffer,
                                          int& vertexCount,
                                          const QString& label)
{
    lastError_.clear();
    initialize();
    if (!initialized_ || !device_.isValid()) {
        if (lastError_.isEmpty()) {
            lastError_ = QStringLiteral("Metal backend is not initialized");
        }
        return false;
    }

    return uploadMetalLineVertices(device_.handle(), lineVertices, buffer, vertexCount, label, lastError_);
}

bool MetalRenderBackend::uploadOverlayLines(const std::vector<float>& lineVertices)
{
    return uploadLineBuffer(lineVertices,
                            overlayVertexBuffer_,
                            overlayVertexCount_,
                            QStringLiteral("overlay"));
}

bool MetalRenderBackend::uploadSliceLines(const std::vector<float>& lineVertices)
{
    return uploadLineBuffer(lineVertices,
                            sliceVertexBuffer_,
                            sliceVertexCount_,
                            QStringLiteral("slice"));
}

bool MetalRenderBackend::uploadIsoSurfaceMesh(const Mesh& mesh)
{
    lastError_.clear();
    initialize();
    if (!initialized_ || !device_.isValid()) {
        if (lastError_.isEmpty()) {
            lastError_ = QStringLiteral("Metal backend is not initialized");
        }
        return false;
    }

    destroyIsoSurfaceResources();
    if (mesh.vertices.empty() || mesh.indices.empty() || mesh.vertices.size() % 6 != 0) {
        return true;
    }

    MetalSurfaceUploadData uploadData =
        buildMetalSurfaceUploadData(mesh, QVector3D(0.2f, 0.8f, 0.4f));
    const MetalSurfaceBufferTargets targets{
        &isoSurfaceVertexBuffer_,
        &isoSurfaceIndexBuffer_,
        &isoSurfaceIndexCount_
    };
    if (!uploadMetalSurfaceBuffers(device_.handle(),
                                   uploadData,
                                   targets,
                                   QStringLiteral("iso surface"),
                                   lastError_)) {
        destroyIsoSurfaceResources();
        return false;
    }

    return true;
}

bool MetalRenderBackend::uploadClipPreviewMesh(const Mesh& mesh)
{
    lastError_.clear();
    initialize();
    if (!initialized_ || !device_.isValid()) {
        if (lastError_.isEmpty()) {
            lastError_ = QStringLiteral("Metal backend is not initialized");
        }
        return false;
    }

    destroyClipPreviewResources();
    if (mesh.vertices.empty() || mesh.indices.empty() || mesh.vertices.size() % 6 != 0) {
        return true;
    }

    MetalSurfaceUploadData uploadData =
        buildMetalSurfaceUploadData(mesh, QVector3D(0.35f, 0.55f, 1.0f));
    const MetalSurfaceBufferTargets surfaceTargets{
        &clipPreviewVertexBuffer_,
        &clipPreviewIndexBuffer_,
        &clipPreviewIndexCount_
    };
    const MetalClipPreviewLineTargets lineTargets{
        &clipPreviewLineVertexBuffer_,
        &clipPreviewLineVertexCount_
    };
    if (!uploadMetalClipPreviewBuffers(device_.handle(),
                                       mesh,
                                       uploadData,
                                       surfaceTargets,
                                       lineTargets,
                                       lastError_)) {
        destroyClipPreviewResources();
        return false;
    }

    return true;
}

bool MetalRenderBackend::uploadSelectionLines(const std::vector<float>& lineVertices)
{
    return uploadLineBuffer(lineVertices,
                            selectionVertexBuffer_,
                            selectionVertexCount_,
                            QStringLiteral("selection"));
}

bool MetalRenderBackend::renderClearFrame(float red, float green, float blue, float alpha)
{
    lastError_.clear();
    if (!initialized_ || !device_.isValid() || !commandQueue_.isValid()) {
        lastError_ = QStringLiteral("Metal backend is not initialized");
        return false;
    }
    if (!metalLayer_.isValid()) {
        lastError_ = QStringLiteral("Metal layer is not initialized");
        return false;
    }

    @autoreleasepool {
        return executeMetalClearFramePass(metalLayer_.handle(),
                                          commandQueue_.handle(),
                                          red,
                                          green,
                                          blue,
                                          alpha,
                                          lastError_);
    }
}

MetalMeshFrameDrawFlags MetalRenderBackend::buildMeshFrameDrawFlags(
    ModelDisplayMode displayMode,
    const QMatrix4x4& axesMvp) const
{
    MetalMeshFrameDrawFlags drawFlags;
    drawFlags.surface = displayMode == ModelDisplayMode::Solid ||
        displayMode == ModelDisplayMode::SolidWireframe;
    drawFlags.edges =
        (displayMode == ModelDisplayMode::Wireframe ||
         displayMode == ModelDisplayMode::SolidWireframe) &&
        edgeIndexCount_ > 0 && edgeVertexBuffer_.isValid() && edgeIndexBuffer_.isValid();
    drawFlags.points = displayMode == ModelDisplayMode::Points && pointVertexBuffer_.isValid();
    drawFlags.overlay = overlayVertexCount_ > 0 && overlayVertexBuffer_.isValid();
    drawFlags.slice = sliceVertexCount_ > 0 && sliceVertexBuffer_.isValid();
    drawFlags.isoSurface =
        isoSurfaceIndexCount_ > 0 &&
        isoSurfaceVertexBuffer_.isValid() &&
        isoSurfaceIndexBuffer_.isValid();
    drawFlags.clipPreview =
        clipPreviewIndexCount_ > 0 &&
        clipPreviewVertexBuffer_.isValid() &&
        clipPreviewIndexBuffer_.isValid();
    drawFlags.clipPreviewLines =
        clipPreviewLineVertexCount_ > 0 && clipPreviewLineVertexBuffer_.isValid();
    drawFlags.selection =
        selectionVertexCount_ > 0 && selectionVertexBuffer_.isValid();
    drawFlags.axes =
        !axesMvp.isIdentity() && drawableSize_.width() > 16 && drawableSize_.height() > 16;
    return drawFlags;
}

bool MetalRenderBackend::ensureMeshFrameResources(const MetalMeshFrameDrawFlags& drawFlags)
{
    if (!ensureBackgroundPipeline()) {
        return false;
    }
    if (drawFlags.surface && !ensureMeshPipeline()) {
        return false;
    }
    if ((drawFlags.edges ||
         drawFlags.points ||
         drawFlags.overlay ||
         drawFlags.slice ||
         drawFlags.clipPreviewLines ||
         drawFlags.selection ||
         drawFlags.axes) &&
        !ensureLinePipeline()) {
        return false;
    }
    if (drawFlags.axes && !ensureAxesResources()) {
        return false;
    }
    if ((drawFlags.isoSurface || drawFlags.clipPreview) && !ensureIsoSurfacePipeline()) {
        return false;
    }
    return ensureDepthResources();
}

MetalMeshFrameResourceHandles MetalRenderBackend::buildMeshFrameResourceHandles() const
{
    MetalMeshFrameResourceHandles frameResources;
    frameResources.backgroundPipelineState = backgroundPipelineState_.handle();
    frameResources.meshPipelineState = meshPipelineState_.handle();
    frameResources.isoSurfacePipelineState = isoSurfacePipelineState_.handle();
    frameResources.linePipelineState = linePipelineState_.handle();
    frameResources.depthStencilState = depthStencilState_.handle();
    frameResources.overlayDepthStencilState = overlayDepthStencilState_.handle();
    frameResources.drawableSize = drawableSize_;
    frameResources.meshVertexBuffer = meshVertexBuffer_.handle();
    frameResources.meshIndexBuffer = meshIndexBuffer_.handle();
    frameResources.meshVertexCount = meshVertexCount_;
    frameResources.meshIndexCount = meshIndexCount_;
    frameResources.pointVertexBuffer = pointVertexBuffer_.handle();
    frameResources.edgeVertexBuffer = edgeVertexBuffer_.handle();
    frameResources.edgeIndexBuffer = edgeIndexBuffer_.handle();
    frameResources.edgeVertexCount = edgeVertexCount_;
    frameResources.edgeIndexCount = edgeIndexCount_;
    frameResources.isoSurfaceVertexBuffer = isoSurfaceVertexBuffer_.handle();
    frameResources.isoSurfaceIndexBuffer = isoSurfaceIndexBuffer_.handle();
    frameResources.isoSurfaceIndexCount = isoSurfaceIndexCount_;
    frameResources.clipPreviewVertexBuffer = clipPreviewVertexBuffer_.handle();
    frameResources.clipPreviewIndexBuffer = clipPreviewIndexBuffer_.handle();
    frameResources.clipPreviewIndexCount = clipPreviewIndexCount_;
    frameResources.overlayVertexBuffer = overlayVertexBuffer_.handle();
    frameResources.overlayVertexCount = overlayVertexCount_;
    frameResources.sliceVertexBuffer = sliceVertexBuffer_.handle();
    frameResources.sliceVertexCount = sliceVertexCount_;
    frameResources.clipPreviewLineVertexBuffer = clipPreviewLineVertexBuffer_.handle();
    frameResources.clipPreviewLineVertexCount = clipPreviewLineVertexCount_;
    frameResources.selectionVertexBuffer = selectionVertexBuffer_.handle();
    frameResources.selectionVertexCount = selectionVertexCount_;
    frameResources.axesLineVertexBuffer = axesLineVertexBuffer_.handle();
    frameResources.axesLineVertexCount = axesLineVertexCount_;
    return frameResources;
}

bool MetalRenderBackend::ensurePickFrameResources()
{
    return ensurePickPipeline() && ensureDepthResources() && ensurePickResources();
}

MetalPickPassResourceHandles MetalRenderBackend::buildPickPassResourceHandles() const
{
    MetalPickPassResourceHandles resources;
    resources.commandQueue = commandQueue_.handle();
    resources.colorTexture = pickColorTexture_.handle();
    resources.depthTexture = pickDepthTexture_.handle();
    resources.readbackBuffer = pickReadbackBuffer_.handle();
    resources.depthStencilState = depthStencilState_.handle();
    resources.pickPipelineState = pickPipelineState_.handle();
    resources.meshVertexBuffer = meshVertexBuffer_.handle();
    resources.meshIndexBuffer = meshIndexBuffer_.handle();
    resources.meshIndexCount = meshIndexCount_;
    return resources;
}

bool MetalRenderBackend::renderMeshFrame(const QMatrix4x4& mvp,
                                         const QVector3D& objectColor,
                                         ModelDisplayMode displayMode,
                                         float red,
                                         float green,
                                         float blue,
                                         float alpha,
                                         const QMatrix4x4& axesMvp)
{
    lastError_.clear();
    if (meshIndexCount_ <= 0 || !meshVertexBuffer_.isValid() || !meshIndexBuffer_.isValid()) {
        return renderClearFrame(red, green, blue, alpha);
    }
    if (!initialized_ || !device_.isValid() || !commandQueue_.isValid()) {
        lastError_ = QStringLiteral("Metal backend is not initialized");
        return false;
    }
    if (!metalLayer_.isValid()) {
        lastError_ = QStringLiteral("Metal layer is not initialized");
        return false;
    }
    const MetalMeshFrameDrawFlags drawFlags = buildMeshFrameDrawFlags(displayMode, axesMvp);
    if (!ensureMeshFrameResources(drawFlags)) {
        return false;
    }

    const MetalMeshFrameUniformSet frameUniforms =
        buildMetalMeshFrameUniformSet(mvp,
                                      objectColor,
                                      meshScalarMin_,
                                      meshScalarMax_,
                                      meshNumBands_,
                                      meshUseVertexScalars_,
                                      axesMvp);
    const MetalMeshFrameResourceHandles frameResources = buildMeshFrameResourceHandles();
    MetalMeshFramePassInputs framePass =
        buildMetalMeshFramePassInputs(drawFlags, frameResources, frameUniforms);

    @autoreleasepool {
        return submitMetalDrawableFrame(metalLayer_.handle(),
                                        commandQueue_.handle(),
                                        depthTexture_.handle(),
                                        red,
                                        green,
                                        blue,
                                        alpha,
                                        framePass,
                                        lastError_);
    }
}

bool MetalRenderBackend::pickElementAt(const QMatrix4x4& mvp, int x, int y, int& elementId)
{
    lastError_.clear();
    elementId = -1;
    if (meshIndexCount_ <= 0 || !meshVertexBuffer_.isValid() || !meshIndexBuffer_.isValid()) {
        return true;
    }
    if (!initialized_ || !device_.isValid() || !commandQueue_.isValid()) {
        lastError_ = QStringLiteral("Metal backend is not initialized");
        return false;
    }
    if (!ensurePickFrameResources()) {
        return false;
    }

    const int pickX = std::clamp(x, 0, std::max(0, drawableSize_.width() - 1));
    const int pickY = std::clamp(y, 0, std::max(0, drawableSize_.height() - 1));
    const MetalPickPassResourceHandles resources = buildPickPassResourceHandles();
    const MetalPickPassInputs pickPass =
        buildMetalPickPassInputs(resources, mvp, pickX, pickY);

    @autoreleasepool {
        if (!executeMetalPickPass(pickPass, elementId, lastError_)) {
            return false;
        }
    }

    return true;
}

void MetalRenderBackend::destroyLayer()
{
    metalLayer_.destroy();
    drawableSize_ = QSize();
    destroyDepthResources();
    destroyPickResources();
}

void MetalRenderBackend::destroyMeshResources()
{
    meshVertexBuffer_.destroy();
    meshIndexBuffer_.destroy();
    pointVertexBuffer_.destroy();
    meshVertexCount_ = 0;
    meshIndexCount_ = 0;
    meshUseVertexScalars_ = false;
    meshScalarMin_ = 0.0f;
    meshScalarMax_ = 1.0f;
    meshNumBands_ = 10;
    meshScalarSourceIndices_.clear();
}

void MetalRenderBackend::destroyEdgeResources()
{
    edgeVertexBuffer_.destroy();
    edgeIndexBuffer_.destroy();
    edgeVertexCount_ = 0;
    edgeIndexCount_ = 0;
}

void MetalRenderBackend::destroyIsoSurfaceResources()
{
    isoSurfaceVertexBuffer_.destroy();
    isoSurfaceIndexBuffer_.destroy();
    isoSurfaceIndexCount_ = 0;
}

void MetalRenderBackend::destroyClipPreviewResources()
{
    clipPreviewVertexBuffer_.destroy();
    clipPreviewIndexBuffer_.destroy();
    clipPreviewLineVertexBuffer_.destroy();
    clipPreviewIndexCount_ = 0;
    clipPreviewLineVertexCount_ = 0;
}

void MetalRenderBackend::destroyLineBuffer(MetalBufferResource& buffer, int& vertexCount)
{
    buffer.destroy();
    vertexCount = 0;
}

void MetalRenderBackend::destroyOverlayResources()
{
    destroyLineBuffer(overlayVertexBuffer_, overlayVertexCount_);
}

void MetalRenderBackend::destroySliceResources()
{
    destroyLineBuffer(sliceVertexBuffer_, sliceVertexCount_);
}

void MetalRenderBackend::destroySelectionResources()
{
    destroyLineBuffer(selectionVertexBuffer_, selectionVertexCount_);
}

void MetalRenderBackend::destroyAxesResources()
{
    destroyLineBuffer(axesLineVertexBuffer_, axesLineVertexCount_);
}

void MetalRenderBackend::destroyDepthResources()
{
    depthTexture_.destroy();
}

void MetalRenderBackend::destroyPickResources()
{
    pickColorTexture_.destroy();
    pickDepthTexture_.destroy();
    pickReadbackBuffer_.destroy();
}

void MetalRenderBackend::destroy()
{
    destroyLayer();
    destroyMeshResources();
    destroyEdgeResources();
    destroyIsoSurfaceResources();
    destroyClipPreviewResources();
    destroyOverlayResources();
    destroySliceResources();
    destroySelectionResources();
    destroyAxesResources();
    backgroundPipelineState_.destroy();
    meshPipelineState_.destroy();
    isoSurfacePipelineState_.destroy();
    linePipelineState_.destroy();
    pickPipelineState_.destroy();
    depthStencilState_.destroy();
    overlayDepthStencilState_.destroy();
    commandQueue_.destroy();
    device_.destroy();
    initialized_ = false;
    info_ = {};
    lastError_.clear();
}
