#pragma once

#include "Camera.h"
#include "FEPickResult.h"
#include "Geometry.h"
#include "RenderBackend.h"
#include "VulkanRenderBackend.h"
#include "VulkanSurface.h"

#include <QElapsedTimer>
#include <QLabel>
#include <QMatrix4x4>
#include <QPoint>
#include <QPointF>
#include <QRubberBand>
#include <QRect>
#include <QTimer>
#include <QVector3D>
#include <QWidget>
#include <QWindow>

#include <array>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class VulkanViewport : public QWidget {
    Q_OBJECT

public:
    explicit VulkanViewport(QWidget* parent = nullptr);
    ~VulkanViewport() override;

    void startRendering();
    void stopRendering();
    void renderFrame();
    void setMesh(const Mesh& mesh);
    void setObjectColor(const glm::vec3& color);
    void setModelDisplayMode(ModelDisplayMode mode);
    void setVertexColors(const std::vector<float>& colors);
    void setUseVertexColor(bool use);
    void setVertexScalars(const std::vector<float>& scalars, float minVal, float maxVal, int numBands);
    void setEdgeScalars(const std::vector<float>& scalars, float minVal, float maxVal, int numBands);
    void setOverlayMesh(const Mesh& mesh);
    void setOverlayVisible(bool visible);
    void setSliceLines(const std::vector<float>& lineVertices);
    void clearSliceLines();
    void setIsoSurfaceMesh(const Mesh& mesh);
    void clearIsoSurface();
    void setClipPlanePreview(const glm::vec3& bbMin,
                             const glm::vec3& bbMax,
                             const glm::vec3& origin,
                             const glm::vec3& normal);
    void clearClipPlanePreview();
    void setTriangleToElementMap(const std::vector<int>& map);
    void setVertexToNodeMap(const std::vector<int>& map);
    void setTriangleToPartMap(const std::vector<int>& map);
    void setEdgeToPartMap(const std::vector<int>& map);
    void setPartVisibility(int partIndex, bool visible);
    void setElementVisibility(int elementId, bool visible);
    void setElementsVisibility(const std::vector<int>& elementIds, bool visible);
    void setAllElementsVisible();
    void setPickMode(PickMode mode);
    void setInteractionMode(ViewportInteractionMode mode);
    void setShowLabels(bool show);
    void selectByIds(PickMode mode, const std::vector<int>& ids);
    void highlightParts(const std::vector<int>& partIndices);
    void fitToModel(const glm::vec3& center, float size);
    void setStandardView(StandardView view);
    void setBackgroundGradient(const QVector3D& topColor, const QVector3D& bottomColor);
    void setViewportGridVisible(bool visible);

    const RenderBackendInfo& backendInfo() const { return backend_.info(); }
    QString lastError() const { return lastError_; }
    QString renderDiagnostics() const { return backend_.renderDiagnostics(); }
    float currentFps() const { return fps_; }
    float frameTimeMs() const { return frameTimeMs_; }

signals:
    void initialized();
    void selectionChanged(PickMode mode, int count, const std::vector<int>& ids);
    void partsPicked(const std::vector<int>& partIndices);
    void contextMenuRequested(const QPoint& globalPos);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    bool initializeIfNeeded();
    bool recreateSwapchain();
    bool uploadMeshIfNeeded();
    VulkanMeshUploadOptions currentMeshUploadOptions() const;
    bool displayModeNeedsCpuFilteredMesh() const;
    bool applyGpuDrivenVisibilityStateForSolidMode();
    void markVisibilityStateChanged();
    void ensureCpuFilteredMeshForPicking();
    bool uploadOverlayIfNeeded();
    bool uploadSliceIfNeeded();
    bool uploadIsoSurfaceIfNeeded();
    bool uploadClipPreviewIfNeeded();
    bool updateScalarBufferOrMarkDirty(bool enableScalars);
    bool handleMouseEvent(QEvent* event);
    bool handleWheelEvent(QEvent* event);
    bool pickAtPosition(const QPointF& position, bool appendSelection);
    bool deselectAtPosition(const QPointF& position);
    bool selectInRect(const QRect& rect, bool removeSelection);
    int edgeElementAtPosition(const QPointF& position, const glm::mat4& mvp, float thresholdPx) const;
    QMatrix4x4 currentMvp() const;
    glm::mat4 currentAxesGlmMvp() const;
    QMatrix4x4 currentAxesMvp() const;
    glm::mat4 currentGlmMvp() const;
    bool standardViewFromAxesClick(const QPointF& position, StandardView* view) const;
    void rebuildPartLookup();
    int closestNodeForElement(int elementId, const QPointF& position) const;
    void selectPart(int partIndex);
    void deselectPart(int partIndex);
    bool isPartFullySelected(int partIndex) const;
    std::vector<int> pickedPartIndices() const;
    std::vector<int> currentSelectionIds() const;
    bool isPartVisible(int partIndex) const;
    bool isTriangleVisibleForSelection(size_t triangleIndex) const;
    bool isElementVisibleForSelection(int elementId) const;
    bool isNodeVisibleForSelection(int nodeId) const;
    void appendPartOutlineHighlight(std::vector<float>& lineVertices) const;
    void appendElementOutlineHighlight(std::vector<float>& lineVertices) const;
    bool rebuildSelectionHighlight();
    void updateRubberBand(const QPoint& currentPos);
    void updateAxesLabels();
    void updateIdLabels();
    void updateFrameStats(qint64 frameNs);
    void resetFrameStats();

    QWindow* nativeWindow_ = nullptr;
    QWidget* windowContainer_ = nullptr;
    VulkanRenderBackend backend_;
    VulkanSurface surface_;
    QTimer frameTimer_;
    QElapsedTimer fpsTimer_;
    int frameCounter_ = 0;
    float fps_ = 0.0f;
    float frameTimeMs_ = 0.0f;
    bool contextInitialized_ = false;
    bool initializedEmitted_ = false;
    bool swapchainDirty_ = true;
    bool meshDirty_ = false;
    bool gpuDrivenVisibilityStateDiverged_ = false;
    bool hasMesh_ = false;
    bool rotating_ = false;
    bool panning_ = false;
    bool zooming_ = false;
    bool leftPressForPick_ = false;
    bool rightPressForDeselect_ = false;
    bool mouseMovedSincePress_ = false;
    bool boxSelecting_ = false;
    bool boxDeselecting_ = false;
    QPointF lastMousePos_;
    QPointF pressMousePos_;
    QPoint boxOrigin_;
    QRubberBand* rubberBand_ = nullptr;
    std::array<QLabel*, 3> axesLabels_{};
    std::vector<QLabel*> idLabels_;
    Camera cam_;
    PickMode pickMode_ = PickMode::Element;
    ViewportInteractionMode interactionMode_ = ViewportInteractionMode::Pick;
    bool showLabels_ = false;
    FESelection selection_;
    glm::vec3 objectColor_{0.48f, 0.72f, 0.76f};
    ModelDisplayMode displayMode_ = ModelDisplayMode::SolidWireframe;
    float modelSize_ = 1.0f;
    bool viewportGridVisible_ = true;
    bool useVertexColor_ = false;
    std::vector<float> vertexColors_;
    std::vector<float> vertexScalars_;
    std::vector<float> edgeScalars_;
    float scalarMin_ = 0.0f;
    float scalarMax_ = 1.0f;
    int numBands_ = 10;
    float selectionMarkerSize_ = 0.02f;
    Mesh overlayMesh_;
    bool overlayVisible_ = false;
    bool overlayDirty_ = false;
    std::vector<float> sliceLineVertices_;
    bool sliceDirty_ = false;
    Mesh isoSurfaceMesh_;
    bool isoSurfaceDirty_ = false;
    Mesh clipPreviewMesh_;
    bool clipPreviewDirty_ = false;
    std::vector<int> triangleToElement_;
    std::vector<int> vertexToNode_;
    std::vector<int> triangleToPart_;
    std::vector<int> edgeToPart_;
    std::vector<std::vector<int>> partTriangles_;
    std::vector<std::vector<int>> partElementIds_;
    std::unordered_map<int, int> elementToPart_;
    std::vector<glm::vec3> partColors_;
    std::unordered_map<int, bool> partVisibility_;
    std::unordered_set<int> hiddenElementIds_;
    Mesh mesh_;
    QString lastError_;
};
