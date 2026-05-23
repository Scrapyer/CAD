#pragma once

#include "Geometry.h"
#include "MetalBufferResource.h"
#include "MetalMeshFramePassBuilder.h"
#include "MetalMeshUploadBuilder.h"
#include "MetalMeshUploader.h"
#include "MetalObjectResource.h"
#include "MetalPickPassBuilder.h"
#include "MetalStateResource.h"
#include "MetalTextureResource.h"
#include "RenderBackend.h"

#include <QMatrix4x4>
#include <QSize>
#include <QString>
#include <QVector3D>

#include <cstddef>
#include <vector>

/**
 * @brief Metal 渲染后端骨架。
 *
 * 当前负责探测系统 Metal 设备、创建 command queue，并通过外部宿主提供的 CAMetalLayer
 * 执行基础 clear/present、渐变背景、深度测试、主网格三角面、普通边线/点、部件显隐、云图标量映射、
 * Element 离屏拾取 draw pass、叠加线、切片交线、等值面、裁剪预览和基础选中高亮线。
 */
class MetalRenderBackend final : public IRenderBackend {
public:
    ~MetalRenderBackend() override;

    void initialize() override;
    const RenderBackendInfo& info() const override { return info_; }

    bool attachLayer(void* metalLayer, const QSize& drawableSize);
    void updateDrawableSize(const QSize& drawableSize);
    void setBackgroundGradient(const QVector3D& topColor, const QVector3D& bottomColor);
    bool uploadMesh(const Mesh& mesh, const MetalMeshUploadOptions& options = {});
    bool uploadVertexScalars(const std::vector<float>& scalars,
                             float minVal,
                             float maxVal,
                             int numBands,
                             bool useScalars);
    bool uploadOverlayLines(const std::vector<float>& lineVertices);
    bool uploadSliceLines(const std::vector<float>& lineVertices);
    bool uploadIsoSurfaceMesh(const Mesh& mesh);
    bool uploadClipPreviewMesh(const Mesh& mesh);
    bool uploadSelectionLines(const std::vector<float>& lineVertices);
    bool renderClearFrame(float red,
                         float green,
                         float blue,
                         float alpha = 1.0f,
                         const QMatrix4x4& axesMvp = QMatrix4x4());
    bool renderMeshFrame(const QMatrix4x4& mvp,
                         const QVector3D& objectColor,
                         ModelDisplayMode displayMode = ModelDisplayMode::SolidWireframe,
                         float red = 0.04f,
                         float green = 0.05f,
                         float blue = 0.07f,
                         float alpha = 1.0f,
                         const QMatrix4x4& axesMvp = QMatrix4x4());
    bool pickElementAt(const QMatrix4x4& mvp, int x, int y, int& elementId);
    void destroyLayer();
    void destroy();

    bool isInitialized() const { return initialized_; }
    bool hasLayer() const { return metalLayer_.isValid(); }
    void* deviceHandle() const { return device_.handle(); }
    const QString& lastError() const { return lastError_; }

    /** @brief 当前系统是否可创建默认 Metal 设备。 */
    static bool isSystemAvailable();

private:
    // Pipeline / resource preparation
    bool ensureBackgroundPipeline();
    bool ensureMeshPipeline();
    bool ensureIsoSurfacePipeline();
    bool ensureLinePipeline();
    bool ensureDepthResources();
    bool ensurePickPipeline();
    bool ensureAxesResources();
    bool ensurePickResources();
    bool ensureMeshFrameResources(const MetalMeshFrameDrawFlags& drawFlags);
    bool ensurePickFrameResources();

    MetalMeshFrameDrawFlags buildMeshFrameDrawFlags(ModelDisplayMode displayMode,
                                                    const QMatrix4x4& axesMvp) const;
    MetalMeshFrameResourceHandles buildMeshFrameResourceHandles() const;
    MetalPickPassResourceHandles buildPickPassResourceHandles() const;
    bool prepareFrameUniformBuffer(MetalMeshFramePassInputs& framePass);

    // Upload helpers
    bool uploadLineBuffer(const std::vector<float>& lineVertices,
                          MetalBufferResource& buffer,
                          int& vertexCount,
                          const QString& label);

    // Resource teardown
    void destroyLineBuffer(MetalBufferResource& buffer, int& vertexCount);
    void destroyMeshResources();
    void destroyEdgeResources();
    void destroyIsoSurfaceResources();
    void destroyClipPreviewResources();
    void destroyOverlayResources();
    void destroySliceResources();
    void destroySelectionResources();
    void destroyAxesResources();
    void destroyDepthResources();
    void destroyPickResources();

    RenderBackendInfo info_;
    QString lastError_;
    bool initialized_ = false;
    QSize drawableSize_;

    // Outer Metal objects
    MetalObjectResource device_;
    MetalObjectResource commandQueue_;
    MetalObjectResource metalLayer_;

    // Pipeline and state objects
    MetalStateResource backgroundPipelineState_;
    MetalStateResource meshPipelineState_;
    MetalStateResource isoSurfacePipelineState_;
    MetalStateResource linePipelineState_;
    MetalStateResource pickPipelineState_;
    MetalStateResource depthStencilState_;
    MetalStateResource overlayDepthStencilState_;

    // View and pick attachments
    MetalTextureResource depthTexture_;
    MetalTextureResource pickColorTexture_;
    MetalTextureResource pickDepthTexture_;
    MetalBufferResource pickReadbackBuffer_;

    // Main mesh resources
    MetalBufferResource meshVertexBuffer_;
    MetalBufferResource meshIndexBuffer_;
    MetalBufferResource pointVertexBuffer_;
    int meshVertexCount_ = 0;
    int meshIndexCount_ = 0;
    bool meshUseVertexScalars_ = false;
    float meshScalarMin_ = 0.0f;
    float meshScalarMax_ = 1.0f;
    int meshNumBands_ = 10;
    QVector3D backgroundTopColor_{0.38f, 0.45f, 0.58f};
    QVector3D backgroundBottomColor_{0.68f, 0.74f, 0.82f};
    std::vector<MetalMeshVertex> meshVertexCpuCache_;
    std::vector<unsigned int> meshScalarSourceIndices_;

    // Edge and overlay resources
    MetalBufferResource edgeVertexBuffer_;
    MetalBufferResource edgeIndexBuffer_;
    int edgeVertexCount_ = 0;
    int edgeIndexCount_ = 0;

    // Post-processing / helper geometry resources
    MetalBufferResource isoSurfaceVertexBuffer_;
    MetalBufferResource isoSurfaceIndexBuffer_;
    MetalBufferResource clipPreviewVertexBuffer_;
    MetalBufferResource clipPreviewIndexBuffer_;
    MetalBufferResource clipPreviewLineVertexBuffer_;
    int isoSurfaceIndexCount_ = 0;
    int clipPreviewIndexCount_ = 0;
    int clipPreviewLineVertexCount_ = 0;

    // Dynamic line resources
    MetalBufferResource overlayVertexBuffer_;
    MetalBufferResource sliceVertexBuffer_;
    MetalBufferResource selectionVertexBuffer_;
    MetalBufferResource axesLineVertexBuffer_;
    MetalBufferResource axesSolidVertexBuffer_;
    MetalBufferResource frameUniformBuffer_;
    size_t frameUniformSlotSize_ = 0;
    int frameUniformSlotIndex_ = 0;
    int overlayVertexCount_ = 0;
    int sliceVertexCount_ = 0;
    int selectionVertexCount_ = 0;
    int axesLineVertexCount_ = 0;
    int axesSolidVertexCount_ = 0;
};
