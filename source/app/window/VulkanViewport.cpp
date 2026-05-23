#include "VulkanViewport.h"

#include "ScreenSpacePicking.h"
#include "ViewportGridMetrics.h"
#include "VulkanMacOSSurfaceFactory.h"

#include <QEvent>
#include <QFont>
#include <QMouseEvent>
#include <QRectF>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <unordered_set>

namespace {
template <typename T>
void alignSize(std::vector<T>& arr, int targetSize, const T& fillValue) {
    if (targetSize < 0) targetSize = 0;
    if (static_cast<int>(arr.size()) > targetSize) {
        arr.resize(static_cast<size_t>(targetSize));
    } else if (static_cast<int>(arr.size()) < targetSize) {
        arr.resize(static_cast<size_t>(targetSize), fillValue);
    }
}

constexpr int kAxesLabelSize = 30;
constexpr int kAxesMargin = 8;
constexpr int kAxesViewportSize = 152;
constexpr int kMaxIdLabels = 160;
constexpr float kAxesClickPadding = 10.0f;

glm::mat4 depthZeroToOneRemap()
{
    glm::mat4 remap(1.0f);
    remap[2][2] = 0.5f;
    remap[3][2] = 0.5f;
    return remap;
}

void applyStandardViewToCamera(Camera& camera, StandardView view)
{
    switch (view) {
    case StandardView::Front:
        camera.yaw = 0.0f;
        camera.pitch = 0.0f;
        break;
    case StandardView::Back:
        camera.yaw = 180.0f;
        camera.pitch = 0.0f;
        break;
    case StandardView::Left:
        camera.yaw = -90.0f;
        camera.pitch = 0.0f;
        break;
    case StandardView::Right:
        camera.yaw = 90.0f;
        camera.pitch = 0.0f;
        break;
    case StandardView::Top:
        camera.yaw = 0.0f;
        camera.pitch = 89.0f;
        break;
    case StandardView::Bottom:
        camera.yaw = 0.0f;
        camera.pitch = -89.0f;
        break;
    }
}

Mesh makeClipPlanePreviewMesh(const glm::vec3& bbMin,
                              const glm::vec3& bbMax,
                              const glm::vec3& origin,
                              const glm::vec3& normal)
{
    Mesh mesh;

    glm::vec3 span = bbMax - bbMin;
    float maxSpan = std::max({std::abs(span.x), std::abs(span.y), std::abs(span.z), 1.0f});
    glm::vec3 mn = bbMin - glm::vec3(maxSpan * 0.03f);
    glm::vec3 mx = bbMax + glm::vec3(maxSpan * 0.03f);

    int axis = 0;
    float ax = std::abs(normal.x);
    float ay = std::abs(normal.y);
    float az = std::abs(normal.z);
    if (ay > ax && ay >= az) {
        axis = 1;
    } else if (az > ax && az > ay) {
        axis = 2;
    }

    glm::vec3 p0;
    glm::vec3 p1;
    glm::vec3 p2;
    glm::vec3 p3;
    if (axis == 0) {
        const float x = origin.x;
        p0 = {x, mn.y, mn.z};
        p1 = {x, mx.y, mn.z};
        p2 = {x, mx.y, mx.z};
        p3 = {x, mn.y, mx.z};
    } else if (axis == 1) {
        const float y = origin.y;
        p0 = {mn.x, y, mn.z};
        p1 = {mx.x, y, mn.z};
        p2 = {mx.x, y, mx.z};
        p3 = {mn.x, y, mx.z};
    } else {
        const float z = origin.z;
        p0 = {mn.x, mn.y, z};
        p1 = {mx.x, mn.y, z};
        p2 = {mx.x, mx.y, z};
        p3 = {mn.x, mx.y, z};
    }

    mesh.addFlatQuad(p0, p1, p2, p3);
    auto pushLine = [&](const glm::vec3& a, const glm::vec3& b) {
        mesh.edgeVertices.push_back(a.x);
        mesh.edgeVertices.push_back(a.y);
        mesh.edgeVertices.push_back(a.z);
        mesh.edgeVertices.push_back(b.x);
        mesh.edgeVertices.push_back(b.y);
        mesh.edgeVertices.push_back(b.z);
    };
    pushLine(p0, p1);
    pushLine(p1, p2);
    pushLine(p2, p3);
    pushLine(p3, p0);
    return mesh;
}
} // namespace

VulkanViewport::VulkanViewport(QWidget* parent)
    : QWidget(parent),
      nativeWindow_(new QWindow),
      windowContainer_(QWidget::createWindowContainer(nativeWindow_, this))
{
    nativeWindow_->setTitle(QStringLiteral("FEModelViewer Vulkan Viewport"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(windowContainer_);
    rubberBand_ = new QRubberBand(QRubberBand::Rectangle);

    const std::array<QString, 3> axesNames = {
        QStringLiteral("X"),
        QStringLiteral("Y"),
        QStringLiteral("Z")
    };
    const std::array<QString, 3> axesColors = {
        QStringLiteral("rgb(242, 77, 77)"),
        QStringLiteral("rgb(89, 230, 89)"),
        QStringLiteral("rgb(89, 140, 255)")
    };
    for (size_t i = 0; i < axesLabels_.size(); ++i) {
        axesLabels_[i] = new QLabel(axesNames[i], this);
        axesLabels_[i]->setAlignment(Qt::AlignCenter);
        axesLabels_[i]->setAttribute(Qt::WA_TransparentForMouseEvents);
        axesLabels_[i]->setAttribute(Qt::WA_TranslucentBackground);
        axesLabels_[i]->setFixedSize(kAxesLabelSize, kAxesLabelSize);
        QFont labelFont = axesLabels_[i]->font();
        labelFont.setBold(true);
        labelFont.setPixelSize(17);
        axesLabels_[i]->setFont(labelFont);
        axesLabels_[i]->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }")
                                          .arg(axesColors[i]));
    }
    windowContainer_->installEventFilter(this);
    nativeWindow_->installEventFilter(this);

    frameTimer_.setTimerType(Qt::PreciseTimer);
    frameTimer_.setInterval(0);
    connect(&frameTimer_, &QTimer::timeout, this, &VulkanViewport::renderFrame);
    fpsTimer_.start();
}

VulkanViewport::~VulkanViewport()
{
    stopRendering();
    backend_.destroySwapchain();
    delete rubberBand_;
    rubberBand_ = nullptr;
}

void VulkanViewport::startRendering()
{
    resetFrameStats();
    if (!frameTimer_.isActive()) {
        frameTimer_.start();
    }
    renderFrame();
}

void VulkanViewport::stopRendering()
{
    frameTimer_.stop();
}

void VulkanViewport::renderFrame()
{
    QElapsedTimer frameTimer;
    frameTimer.start();

    if (!initializeIfNeeded()) {
        return;
    }
    if (swapchainDirty_ && !recreateSwapchain()) {
        return;
    }
    if (!uploadMeshIfNeeded()) {
        return;
    }
    if (!uploadOverlayIfNeeded()) {
        return;
    }
    if (!uploadSliceIfNeeded()) {
        return;
    }
    if (!uploadIsoSurfaceIfNeeded()) {
        return;
    }
    if (!uploadClipPreviewIfNeeded()) {
        return;
    }

    const ViewportGridMetrics gridMetrics =
        computeViewportGridMetrics(modelSize_, cam_.distance, viewportGridVisible_);
    backend_.setViewportGridParams(gridMetrics.alpha,
                                   gridMetrics.minorStep,
                                   gridMetrics.fineAlpha);
    const QMatrix4x4 axesMvp = currentAxesMvp();
    const bool rendered = hasMesh_
        ? backend_.renderMeshFrame(currentMvp(),
                                   0.04f,
                                   0.05f,
                                   0.07f,
                                   1.0f,
                                   axesMvp,
                                   displayMode_,
                                   static_cast<float>(std::max<qreal>(devicePixelRatioF(), 1.0)))
        : backend_.renderClearFrame(0.04f,
                                    0.05f,
                                    0.07f,
                                    1.0f,
                                    axesMvp,
                                    static_cast<float>(std::max<qreal>(devicePixelRatioF(), 1.0)));
    if (!rendered) {
        if (backend_.needsSwapchainRecreate()) {
            swapchainDirty_ = true;
            lastError_.clear();
            return;
        }
        lastError_ = backend_.lastError();
        return;
    }
    if (backend_.needsSwapchainRecreate()) {
        swapchainDirty_ = true;
    }

    updateAxesLabels();
    updateIdLabels();
    updateFrameStats(frameTimer.nsecsElapsed());
}

void VulkanViewport::setMesh(const Mesh& mesh)
{
    mesh_ = mesh;
    selection_.clear();
    triangleToElement_.clear();
    vertexToNode_.clear();
    triangleToPart_.clear();
    edgeToPart_.clear();
    partTriangles_.clear();
    partElementIds_.clear();
    elementToPart_.clear();
    partColors_.clear();
    partVisibility_.clear();
    hiddenElementIds_.clear();
    vertexColors_.clear();
    vertexScalars_.clear();
    edgeScalars_.clear();
    useVertexColor_ = false;
    hasMesh_ = (!mesh_.vertices.empty() && !mesh_.indices.empty()) ||
        (!mesh_.edgeVertices.empty() && !mesh_.edgeIndices.empty());
    meshDirty_ = true;
    emit selectionChanged(pickMode_, 0, {});
    if (pickMode_ == PickMode::Part) {
        emit partsPicked({});
    }
    updateIdLabels();
    renderFrame();
}

void VulkanViewport::setObjectColor(const glm::vec3& color)
{
    objectColor_ = color;
    meshDirty_ = true;
    renderFrame();
}

void VulkanViewport::setModelDisplayMode(ModelDisplayMode mode)
{
    if (displayMode_ == mode) {
        return;
    }
    displayMode_ = mode;
    resetFrameStats();
    renderFrame();
}

void VulkanViewport::setVertexColors(const std::vector<float>& colors)
{
    vertexColors_ = colors;
    vertexScalars_.clear();
    edgeScalars_.clear();
    useVertexColor_ = true;
    meshDirty_ = true;
    renderFrame();
}

void VulkanViewport::setUseVertexColor(bool use)
{
    useVertexColor_ = use;
    if (!edgeScalars_.empty()) {
        meshDirty_ = true;
    }
    updateScalarBufferOrMarkDirty(useVertexColor_ && !vertexScalars_.empty());
    renderFrame();
}

void VulkanViewport::setVertexScalars(const std::vector<float>& scalars,
                                      float minVal,
                                      float maxVal,
                                      int numBands)
{
    vertexScalars_ = scalars;
    vertexColors_.clear();
    scalarMin_ = minVal;
    scalarMax_ = maxVal;
    numBands_ = std::max(1, numBands);
    useVertexColor_ = true;
    updateScalarBufferOrMarkDirty(!vertexScalars_.empty());
    renderFrame();
}

void VulkanViewport::setEdgeScalars(const std::vector<float>& scalars,
                                    float minVal,
                                    float maxVal,
                                    int numBands)
{
    edgeScalars_ = scalars;
    scalarMin_ = minVal;
    scalarMax_ = maxVal;
    numBands_ = std::max(1, numBands);
    useVertexColor_ = true;
    meshDirty_ = true;
    renderFrame();
}

void VulkanViewport::setOverlayMesh(const Mesh& mesh)
{
    overlayMesh_ = mesh;
    overlayDirty_ = true;
    renderFrame();
}

void VulkanViewport::setOverlayVisible(bool visible)
{
    overlayVisible_ = visible;
    overlayDirty_ = true;
    renderFrame();
}

void VulkanViewport::setSliceLines(const std::vector<float>& lineVertices)
{
    sliceLineVertices_ = lineVertices;
    sliceDirty_ = true;
    renderFrame();
}

void VulkanViewport::clearSliceLines()
{
    sliceLineVertices_.clear();
    sliceDirty_ = true;
    renderFrame();
}

void VulkanViewport::setIsoSurfaceMesh(const Mesh& mesh)
{
    isoSurfaceMesh_ = mesh;
    isoSurfaceDirty_ = true;
    renderFrame();
}

void VulkanViewport::clearIsoSurface()
{
    isoSurfaceMesh_ = Mesh{};
    isoSurfaceDirty_ = true;
    renderFrame();
}

void VulkanViewport::setClipPlanePreview(const glm::vec3& bbMin,
                                         const glm::vec3& bbMax,
                                         const glm::vec3& origin,
                                         const glm::vec3& normal)
{
    clipPreviewMesh_ = makeClipPlanePreviewMesh(bbMin, bbMax, origin, normal);
    clipPreviewDirty_ = true;
    renderFrame();
}

void VulkanViewport::clearClipPlanePreview()
{
    clipPreviewMesh_ = Mesh{};
    clipPreviewDirty_ = true;
    renderFrame();
}

void VulkanViewport::setTriangleToElementMap(const std::vector<int>& map)
{
    triangleToElement_ = map;
    int triCount = static_cast<int>(mesh_.indices.size() / 3);
    alignSize(triangleToElement_, triCount, -1);
    rebuildPartLookup();
    meshDirty_ = true;
    renderFrame();
}

void VulkanViewport::setVertexToNodeMap(const std::vector<int>& map)
{
    vertexToNode_ = map;
    int vertexCount = static_cast<int>(mesh_.vertices.size() / 6);
    alignSize(vertexToNode_, vertexCount, -1);
}

void VulkanViewport::setTriangleToPartMap(const std::vector<int>& map)
{
    triangleToPart_ = map;
    int triCount = static_cast<int>(mesh_.indices.size() / 3);
    alignSize(triangleToPart_, triCount, -1);

    int numParts = 0;
    for (int part : triangleToPart_) {
        if (part >= 0) {
            numParts = std::max(numParts, part + 1);
        }
    }

    static const glm::vec3 palette[] = {
        {0.61f, 0.86f, 0.63f},
        {0.54f, 0.71f, 0.98f},
        {0.98f, 0.70f, 0.53f},
        {0.82f, 0.62f, 0.98f},
        {0.58f, 0.89f, 0.83f},
        {0.98f, 0.89f, 0.69f},
        {0.94f, 0.56f, 0.66f},
        {0.71f, 0.71f, 0.98f},
    };
    constexpr int paletteSize = static_cast<int>(sizeof(palette) / sizeof(palette[0]));

    partColors_.resize(static_cast<size_t>(numParts));
    for (int i = 0; i < numParts; ++i) {
        partColors_[static_cast<size_t>(i)] = palette[i % paletteSize];
    }

    rebuildPartLookup();
    meshDirty_ = true;
    renderFrame();
}

void VulkanViewport::setEdgeToPartMap(const std::vector<int>& map)
{
    edgeToPart_ = map;
    int edgeCount = static_cast<int>(mesh_.edgeIndices.size() / 2);
    alignSize(edgeToPart_, edgeCount, -1);
    rebuildPartLookup();
    meshDirty_ = true;
    renderFrame();
}

void VulkanViewport::setPartVisibility(int partIndex, bool visible)
{
    partVisibility_[partIndex] = visible;
    bool selectionWasChanged = false;
    for (auto it = selection_.selectedElements.begin(); it != selection_.selectedElements.end();) {
        if (!isElementVisibleForSelection(*it)) {
            it = selection_.selectedElements.erase(it);
            selectionWasChanged = true;
        } else {
            ++it;
        }
    }
    for (auto it = selection_.selectedNodes.begin(); it != selection_.selectedNodes.end();) {
        if (!isNodeVisibleForSelection(*it)) {
            it = selection_.selectedNodes.erase(it);
            selectionWasChanged = true;
        } else {
            ++it;
        }
    }
    if (selectionWasChanged) {
        const std::vector<int> ids = currentSelectionIds();
        emit selectionChanged(pickMode_, static_cast<int>(ids.size()), ids);
        if (pickMode_ == PickMode::Part) {
            emit partsPicked(pickedPartIndices());
        }
        updateIdLabels();
    }
    meshDirty_ = true;
    renderFrame();
}

void VulkanViewport::setElementVisibility(int elementId, bool visible)
{
    if (elementId < 0) {
        return;
    }
    if (visible) {
        hiddenElementIds_.erase(elementId);
    } else {
        hiddenElementIds_.insert(elementId);
    }
    meshDirty_ = true;
    renderFrame();
}

void VulkanViewport::setElementsVisibility(const std::vector<int>& elementIds, bool visible)
{
    bool changed = false;
    for (int elementId : elementIds) {
        if (elementId < 0) {
            continue;
        }
        if (visible) {
            changed = hiddenElementIds_.erase(elementId) > 0 || changed;
        } else {
            changed = hiddenElementIds_.insert(elementId).second || changed;
        }
    }
    if (!changed) {
        return;
    }
    meshDirty_ = true;
    renderFrame();
}

void VulkanViewport::setAllElementsVisible()
{
    if (hiddenElementIds_.empty()) {
        return;
    }
    hiddenElementIds_.clear();
    meshDirty_ = true;
    renderFrame();
}

void VulkanViewport::setPickMode(PickMode mode)
{
    pickMode_ = mode;
    selection_.clear();
    emit selectionChanged(pickMode_, 0, {});
    rebuildSelectionHighlight();
    updateIdLabels();
}

void VulkanViewport::setInteractionMode(ViewportInteractionMode mode)
{
    interactionMode_ = mode;
    rotating_ = false;
    panning_ = false;
    zooming_ = false;
    leftPressForPick_ = false;
    rightPressForDeselect_ = false;
    boxSelecting_ = false;
    boxDeselecting_ = false;
    if (rubberBand_) {
        rubberBand_->hide();
    }
}

void VulkanViewport::setShowLabels(bool show)
{
    if (showLabels_ == show) {
        return;
    }
    showLabels_ = show;
    updateIdLabels();
}

void VulkanViewport::selectByIds(PickMode mode, const std::vector<int>& ids)
{
    pickMode_ = mode;
    selection_.clear();

    if (mode == PickMode::Node) {
        std::unordered_set<int> validNodes(vertexToNode_.begin(), vertexToNode_.end());
        for (int id : ids) {
            if (validNodes.count(id) > 0 && isNodeVisibleForSelection(id)) {
                selection_.selectedNodes.insert(id);
            }
        }
    } else if (mode == PickMode::Part) {
        for (int part : ids) {
            selectPart(part);
        }
    } else {
        std::unordered_set<int> validElements(triangleToElement_.begin(), triangleToElement_.end());
        for (int id : ids) {
            if (validElements.count(id) > 0 && isElementVisibleForSelection(id)) {
                selection_.selectedElements.insert(id);
            }
        }
    }

    const std::vector<int> selectedIds = currentSelectionIds();
    emit selectionChanged(pickMode_, static_cast<int>(selectedIds.size()), selectedIds);
    if (pickMode_ == PickMode::Part) {
        emit partsPicked(pickedPartIndices());
    }
    rebuildSelectionHighlight();
    updateIdLabels();
    renderFrame();
}

void VulkanViewport::highlightParts(const std::vector<int>& partIndices)
{
    selectByIds(PickMode::Part, partIndices);
}

void VulkanViewport::fitToModel(const glm::vec3& center, float size)
{
    modelSize_ = std::max(size, 1.0e-4f);
    cam_.target = center;
    cam_.distance = modelSize_ * 1.5f;
    cam_.maxDist = modelSize_ * 10.0f;
    cam_.minDist = modelSize_ * 0.05f;
    cam_.panSensitivity = 0.001f;
    cam_.yaw = 30.0f;
    cam_.pitch = 25.0f;
    selectionMarkerSize_ = modelSize_ * 0.015f;
    renderFrame();
}

void VulkanViewport::setStandardView(StandardView view)
{
    applyStandardViewToCamera(cam_, view);
    rebuildSelectionHighlight();
    updateAxesLabels();
    renderFrame();
}

void VulkanViewport::setBackgroundGradient(const QVector3D& topColor, const QVector3D& bottomColor)
{
    backend_.setBackgroundGradient(topColor, bottomColor);
    renderFrame();
}

void VulkanViewport::setViewportGridVisible(bool visible)
{
    viewportGridVisible_ = visible;
    renderFrame();
}

bool VulkanViewport::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == windowContainer_ || watched == nativeWindow_) {
        if (handleMouseEvent(event) || handleWheelEvent(event)) {
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void VulkanViewport::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    swapchainDirty_ = true;
    updateAxesLabels();
}

void VulkanViewport::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    swapchainDirty_ = true;
    updateAxesLabels();
    renderFrame();
}

bool VulkanViewport::initializeIfNeeded()
{
    lastError_.clear();
    if (!contextInitialized_) {
        if (!backend_.initializeContext(VulkanMacOSSurfaceFactory::requiredInstanceExtensions())) {
            lastError_ = backend_.lastError();
            return false;
        }
        contextInitialized_ = true;
    }

    if (!nativeWindow_->handle()) {
        nativeWindow_->create();
    }

    if (!surface_.isValid()) {
        QString surfaceError;
        surface_ = VulkanMacOSSurfaceFactory::createSurface(backend_.instance(), nativeWindow_, &surfaceError);
        if (!surface_.isValid()) {
            lastError_ = surfaceError;
            return false;
        }
    }

    if (!backend_.initializeDevice(surface_.handle())) {
        lastError_ = backend_.lastError();
        return false;
    }

    if (!initializedEmitted_) {
        initializedEmitted_ = true;
        emit initialized();
    }

    return true;
}

bool VulkanViewport::recreateSwapchain()
{
    const QSize size = nativeWindow_->size().isValid() ? nativeWindow_->size() : this->size();
    const uint32_t width = static_cast<uint32_t>(std::max(1, size.width()));
    const uint32_t height = static_cast<uint32_t>(std::max(1, size.height()));

    backend_.destroySwapchain();
    if (!backend_.initializeSwapchain(surface_.handle(), width, height, false)) {
        lastError_ = backend_.lastError();
        return false;
    }

    meshDirty_ = true;
    overlayDirty_ = true;
    sliceDirty_ = true;
    isoSurfaceDirty_ = true;
    clipPreviewDirty_ = true;
    swapchainDirty_ = false;
    return true;
}

bool VulkanViewport::uploadMeshIfNeeded()
{
    if (!meshDirty_) {
        return true;
    }

    VulkanMeshUploadOptions options;
    options.objectColor = QVector3D(objectColor_.x, objectColor_.y, objectColor_.z);
    options.triangleToElement = triangleToElement_;
    options.triangleToPart = triangleToPart_;
    options.edgeToPart = edgeToPart_;
    options.partVisibility = partVisibility_;
    options.hiddenElementIds = hiddenElementIds_;
    options.useVertexColor = useVertexColor_;
    options.vertexColors = vertexColors_;
    options.vertexScalars = vertexScalars_;
    options.edgeScalars = edgeScalars_;
    options.scalarMin = scalarMin_;
    options.scalarMax = scalarMax_;
    options.numBands = numBands_;
    options.partColors.reserve(partColors_.size());
    for (const glm::vec3& color : partColors_) {
        options.partColors.emplace_back(color.x, color.y, color.z);
    }

    if (!backend_.uploadMesh(mesh_, options)) {
        lastError_ = backend_.lastError();
        return false;
    }
    if (!rebuildSelectionHighlight()) {
        return false;
    }
    meshDirty_ = false;
    overlayDirty_ = true;
    return true;
}

bool VulkanViewport::uploadOverlayIfNeeded()
{
    if (!overlayDirty_) {
        return true;
    }

    const std::vector<float>* lineVertices = nullptr;
    if (overlayVisible_ && !overlayMesh_.edgeVertices.empty()) {
        lineVertices = &overlayMesh_.edgeVertices;
    }
    const std::vector<float> empty;
    if (!backend_.uploadOverlayLines(lineVertices ? *lineVertices : empty)) {
        lastError_ = backend_.lastError();
        return false;
    }
    overlayDirty_ = false;
    return true;
}

bool VulkanViewport::uploadSliceIfNeeded()
{
    if (!sliceDirty_) {
        return true;
    }

    if (!backend_.uploadSliceLines(sliceLineVertices_)) {
        lastError_ = backend_.lastError();
        return false;
    }
    sliceDirty_ = false;
    return true;
}

bool VulkanViewport::uploadIsoSurfaceIfNeeded()
{
    if (!isoSurfaceDirty_) {
        return true;
    }

    if (!backend_.uploadIsoSurfaceMesh(isoSurfaceMesh_)) {
        lastError_ = backend_.lastError();
        return false;
    }
    isoSurfaceDirty_ = false;
    return true;
}

bool VulkanViewport::uploadClipPreviewIfNeeded()
{
    if (!clipPreviewDirty_) {
        return true;
    }

    if (!backend_.uploadClipPreviewMesh(clipPreviewMesh_)) {
        lastError_ = backend_.lastError();
        return false;
    }
    clipPreviewDirty_ = false;
    return true;
}

bool VulkanViewport::updateScalarBufferOrMarkDirty(bool enableScalars)
{
    if (!contextInitialized_ || !backend_.isInitialized() || !hasMesh_ || meshDirty_) {
        meshDirty_ = true;
        return false;
    }

    if (backend_.uploadVertexScalars(
            vertexScalars_,
            scalarMin_,
            scalarMax_,
            numBands_,
            enableScalars)) {
        return true;
    }

    lastError_ = backend_.lastError();
    meshDirty_ = true;
    return false;
}

bool VulkanViewport::handleMouseEvent(QEvent* event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const bool modifierSelection =
            (mouseEvent->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) != 0;
        const bool selectionGesture = modifierSelection ||
            interactionMode_ == ViewportInteractionMode::Pick;
        if (!modifierSelection && mouseEvent->button() == Qt::LeftButton) {
            StandardView view = StandardView::Front;
            if (standardViewFromAxesClick(mouseEvent->position(), &view)) {
                setStandardView(view);
                rotating_ = false;
                panning_ = false;
                zooming_ = false;
                leftPressForPick_ = false;
                rightPressForDeselect_ = false;
                boxSelecting_ = false;
                boxDeselecting_ = false;
                mouseMovedSincePress_ = false;
                return true;
            }
        }

        lastMousePos_ = mouseEvent->position();
        pressMousePos_ = mouseEvent->position();
        boxOrigin_ = mouseEvent->position().toPoint();
        mouseMovedSincePress_ = false;
        leftPressForPick_ = mouseEvent->button() == Qt::LeftButton && selectionGesture;
        rightPressForDeselect_ = mouseEvent->button() == Qt::RightButton;
        boxSelecting_ = leftPressForPick_ && selectionGesture;
        boxDeselecting_ = rightPressForDeselect_ && modifierSelection;
        if (boxSelecting_ || boxDeselecting_) {
            updateRubberBand(boxOrigin_);
        }
        rotating_ = mouseEvent->button() == Qt::LeftButton &&
            !boxSelecting_ &&
            interactionMode_ == ViewportInteractionMode::Rotate;
        zooming_ = mouseEvent->button() == Qt::LeftButton &&
            !boxSelecting_ &&
            interactionMode_ == ViewportInteractionMode::Zoom;
        panning_ = mouseEvent->button() == Qt::MiddleButton ||
            (mouseEvent->button() == Qt::RightButton && !boxDeselecting_) ||
            (mouseEvent->button() == Qt::LeftButton &&
             !boxSelecting_ &&
             interactionMode_ == ViewportInteractionMode::Pan);
        return rotating_ || panning_ || zooming_ || boxSelecting_ || boxDeselecting_;
    }
    case QEvent::MouseMove: {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QPointF delta = mouseEvent->position() - lastMousePos_;
        lastMousePos_ = mouseEvent->position();
        if ((mouseEvent->position() - pressMousePos_).manhattanLength() > 3.0) {
            mouseMovedSincePress_ = true;
        }
        if (boxSelecting_ || boxDeselecting_) {
            updateRubberBand(mouseEvent->position().toPoint());
            return true;
        }
        if (rotating_) {
            cam_.rotate(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
            rebuildSelectionHighlight();
            renderFrame();
            return true;
        }
        if (panning_) {
            cam_.pan(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
            rebuildSelectionHighlight();
            renderFrame();
            return true;
        }
        if (zooming_) {
            cam_.zoom(static_cast<float>(-delta.y()) / 120.0f);
            rebuildSelectionHighlight();
            renderFrame();
            return true;
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const bool shouldPick = leftPressForPick_ &&
            mouseEvent->button() == Qt::LeftButton &&
            !mouseMovedSincePress_;
        const bool shouldDeselect = rightPressForDeselect_ &&
            mouseEvent->button() == Qt::RightButton &&
            !mouseMovedSincePress_;
        rotating_ = false;
        panning_ = false;
        zooming_ = false;
        leftPressForPick_ = false;
        rightPressForDeselect_ = false;
        const bool contextMenuClick =
            mouseEvent->button() == Qt::RightButton &&
            !mouseMovedSincePress_ &&
            (mouseEvent->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) == 0 &&
            !boxSelecting_ &&
            !boxDeselecting_;
        if (contextMenuClick) {
            QWidget* anchor = windowContainer_ ? windowContainer_ : this;
            emit contextMenuRequested(anchor->mapToGlobal(mouseEvent->position().toPoint()));
            return true;
        }
        if (boxSelecting_ || boxDeselecting_) {
            const bool removeSelection = boxDeselecting_;
            boxSelecting_ = false;
            boxDeselecting_ = false;
            rubberBand_->hide();
            const QRect rect = QRect(boxOrigin_, mouseEvent->position().toPoint()).normalized();
            if (rect.width() > 3 && rect.height() > 3) {
                return selectInRect(rect, removeSelection);
            }
            const bool appendSelection =
                (mouseEvent->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) != 0;
            return removeSelection
                ? deselectAtPosition(mouseEvent->position())
                : pickAtPosition(mouseEvent->position(), appendSelection);
        }
        if (shouldPick) {
            const bool appendSelection =
                mouseEvent->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
            return pickAtPosition(mouseEvent->position(), appendSelection);
        }
        if (shouldDeselect) {
            return deselectAtPosition(mouseEvent->position());
        }
        return true;
    }
    default:
        break;
    }
    return false;
}

void VulkanViewport::updateRubberBand(const QPoint& currentPos)
{
    if (!rubberBand_) {
        return;
    }

    QWidget* anchor = windowContainer_ ? windowContainer_ : this;
    const QPoint globalOrigin = anchor->mapToGlobal(boxOrigin_);
    const QPoint globalCurrent = anchor->mapToGlobal(currentPos);
    rubberBand_->setGeometry(QRect(globalOrigin, globalCurrent).normalized());
    rubberBand_->show();
    rubberBand_->raise();
}

bool VulkanViewport::pickAtPosition(const QPointF& position, bool appendSelection)
{
    if (!hasMesh_) {
        return true;
    }
    if (!initializeIfNeeded()) {
        return true;
    }
    if (swapchainDirty_ && !recreateSwapchain()) {
        return true;
    }
    if (!uploadMeshIfNeeded()) {
        return true;
    }

    const QSize size = nativeWindow_->size().isValid() ? nativeWindow_->size() : this->size();
    const qreal dpr = devicePixelRatioF();
    const uint32_t width = static_cast<uint32_t>(std::max<qreal>(1.0, size.width() * dpr));
    const uint32_t height = static_cast<uint32_t>(std::max<qreal>(1.0, size.height() * dpr));
    const uint32_t x = static_cast<uint32_t>(std::clamp<qreal>(position.x() * dpr, 0.0, width - 1.0));
    const uint32_t y = static_cast<uint32_t>(std::clamp<qreal>(position.y() * dpr, 0.0, height - 1.0));

    int elementId = -1;
    if (!backend_.pickElementAt(currentMvp(), width, height, x, y, elementId)) {
        lastError_ = backend_.lastError();
        return true;
    }
    if (elementId < 0) {
        elementId = edgeElementAtPosition(position, currentGlmMvp(), 8.0f);
    }

    if (!appendSelection) {
        selection_.clear();
    }

    if (pickMode_ == PickMode::Node) {
        const int nodeId = closestNodeForElement(elementId, position);
        if (nodeId >= 0) {
            if (appendSelection) {
                selection_.toggleNode(nodeId);
            } else {
                selection_.selectedNodes.insert(nodeId);
            }
        }
    } else if (pickMode_ == PickMode::Part) {
        int partIndex = -1;
        if (elementId >= 0) {
            const auto it = elementToPart_.find(elementId);
            if (it != elementToPart_.end()) {
                partIndex = it->second;
            }
        }
        if (partIndex >= 0) {
            if (appendSelection) {
                if (isPartFullySelected(partIndex)) {
                    deselectPart(partIndex);
                } else {
                    selectPart(partIndex);
                }
            } else {
                selectPart(partIndex);
            }
        }
    } else {
        if (elementId >= 0) {
            if (appendSelection) {
                selection_.toggleElement(elementId);
            } else {
                selection_.selectedElements.insert(elementId);
            }
        }
    }

    const std::vector<int> ids = currentSelectionIds();
    emit selectionChanged(pickMode_, static_cast<int>(ids.size()), ids);
    if (pickMode_ == PickMode::Part) {
        emit partsPicked(pickedPartIndices());
    }
    rebuildSelectionHighlight();
    updateIdLabels();
    renderFrame();
    return true;
}

bool VulkanViewport::deselectAtPosition(const QPointF& position)
{
    if (!hasMesh_) {
        return true;
    }
    if (!initializeIfNeeded()) {
        return true;
    }
    if (swapchainDirty_ && !recreateSwapchain()) {
        return true;
    }
    if (!uploadMeshIfNeeded()) {
        return true;
    }

    const QSize size = nativeWindow_->size().isValid() ? nativeWindow_->size() : this->size();
    const qreal dpr = devicePixelRatioF();
    const uint32_t width = static_cast<uint32_t>(std::max<qreal>(1.0, size.width() * dpr));
    const uint32_t height = static_cast<uint32_t>(std::max<qreal>(1.0, size.height() * dpr));
    const uint32_t x = static_cast<uint32_t>(std::clamp<qreal>(position.x() * dpr, 0.0, width - 1.0));
    const uint32_t y = static_cast<uint32_t>(std::clamp<qreal>(position.y() * dpr, 0.0, height - 1.0));

    int elementId = -1;
    if (!backend_.pickElementAt(currentMvp(), width, height, x, y, elementId)) {
        lastError_ = backend_.lastError();
        return true;
    }
    if (elementId < 0) {
        elementId = edgeElementAtPosition(position, currentGlmMvp(), 8.0f);
    }

    if (pickMode_ == PickMode::Node) {
        const int nodeId = closestNodeForElement(elementId, position);
        if (nodeId >= 0) {
            selection_.selectedNodes.erase(nodeId);
        }
    } else if (pickMode_ == PickMode::Part) {
        if (elementId >= 0) {
            const auto it = elementToPart_.find(elementId);
            if (it != elementToPart_.end()) {
                deselectPart(it->second);
            }
        }
    } else if (elementId >= 0) {
        selection_.selectedElements.erase(elementId);
    }

    const std::vector<int> ids = currentSelectionIds();
    emit selectionChanged(pickMode_, static_cast<int>(ids.size()), ids);
    if (pickMode_ == PickMode::Part) {
        emit partsPicked(pickedPartIndices());
    }
    rebuildSelectionHighlight();
    updateIdLabels();
    renderFrame();
    return true;
}

bool VulkanViewport::selectInRect(const QRect& rect, bool removeSelection)
{
    if (!hasMesh_ || rect.isEmpty()) {
        return true;
    }
    if (!initializeIfNeeded()) {
        return true;
    }
    if (swapchainDirty_ && !recreateSwapchain()) {
        return true;
    }
    if (!uploadMeshIfNeeded()) {
        return true;
    }

    const QSize size = nativeWindow_->size().isValid() ? nativeWindow_->size() : this->size();
    const float width = static_cast<float>(std::max(1, size.width()));
    const float height = static_cast<float>(std::max(1, size.height()));
    float ndcLeft = (2.0f * static_cast<float>(rect.left()) / width) - 1.0f;
    float ndcRight = (2.0f * static_cast<float>(rect.right()) / width) - 1.0f;
    float ndcTop = (2.0f * static_cast<float>(rect.top()) / height) - 1.0f;
    float ndcBottom = (2.0f * static_cast<float>(rect.bottom()) / height) - 1.0f;
    if (ndcLeft > ndcRight) {
        std::swap(ndcLeft, ndcRight);
    }
    if (ndcTop > ndcBottom) {
        std::swap(ndcTop, ndcBottom);
    }

    const glm::mat4 mvp = currentGlmMvp();
    auto pointInside = [&mvp, ndcLeft, ndcRight, ndcTop, ndcBottom](const glm::vec3& point) -> bool {
        const glm::vec4 clip = mvp * glm::vec4(point, 1.0f);
        if (clip.w <= 0.0f) {
            return false;
        }
        const float x = clip.x / clip.w;
        const float y = clip.y / clip.w;
        return x >= ndcLeft && x <= ndcRight && y >= ndcTop && y <= ndcBottom;
    };
    auto vertexInside = [this, &mvp, ndcLeft, ndcRight, ndcTop, ndcBottom](unsigned int vertexIndex) -> bool {
        const size_t base = static_cast<size_t>(vertexIndex) * 6;
        if (base + 2 >= mesh_.vertices.size()) {
            return false;
        }
        const glm::vec4 world(mesh_.vertices[base],
                              mesh_.vertices[base + 1],
                              mesh_.vertices[base + 2],
                              1.0f);
        const glm::vec4 clip = mvp * world;
        if (clip.w <= 0.0f) {
            return false;
        }
        const float x = clip.x / clip.w;
        const float y = clip.y / clip.w;
        return x >= ndcLeft && x <= ndcRight && y >= ndcTop && y <= ndcBottom;
    };

    if (pickMode_ == PickMode::Node) {
        std::unordered_set<int> touchedNodes;
        const size_t triCount = std::min(triangleToElement_.size(), mesh_.indices.size() / 3);
        for (size_t tri = 0; tri < triCount; ++tri) {
            if (!isTriangleVisibleForSelection(tri)) {
                continue;
            }
            for (int corner = 0; corner < 3; ++corner) {
                const unsigned int vertex = mesh_.indices[tri * 3 + static_cast<size_t>(corner)];
                if (!vertexInside(vertex)) {
                    continue;
                }
                const int nodeId = vertex < vertexToNode_.size()
                    ? vertexToNode_[vertex]
                    : static_cast<int>(vertex);
                if (nodeId >= 0 && touchedNodes.insert(nodeId).second) {
                    if (removeSelection) {
                        selection_.selectedNodes.erase(nodeId);
                    } else {
                        selection_.selectedNodes.insert(nodeId);
                    }
                }
            }
        }
        const size_t elemEdgeCount =
            std::min(mesh_.elemEdgeToElement.size(), mesh_.elemEdgeVertices.size() / 6);
        for (size_t edge = 0; edge < elemEdgeCount; ++edge) {
            const int elementId = mesh_.elemEdgeToElement[edge];
            if (!isElementVisibleForSelection(elementId) ||
                edge >= mesh_.elemEdgeNodeIds.size()) {
                continue;
            }
            const size_t base = edge * 6;
            const glm::vec3 p0(mesh_.elemEdgeVertices[base],
                               mesh_.elemEdgeVertices[base + 1],
                               mesh_.elemEdgeVertices[base + 2]);
            const glm::vec3 p1(mesh_.elemEdgeVertices[base + 3],
                               mesh_.elemEdgeVertices[base + 4],
                               mesh_.elemEdgeVertices[base + 5]);
            const auto [node0, node1] = mesh_.elemEdgeNodeIds[edge];
            if (node0 >= 0 && pointInside(p0) && touchedNodes.insert(node0).second) {
                if (removeSelection) selection_.selectedNodes.erase(node0);
                else selection_.selectedNodes.insert(node0);
            }
            if (node1 >= 0 && pointInside(p1) && touchedNodes.insert(node1).second) {
                if (removeSelection) selection_.selectedNodes.erase(node1);
                else selection_.selectedNodes.insert(node1);
            }
        }
    } else if (pickMode_ == PickMode::Part) {
        std::unordered_set<int> hitParts;
        const size_t triCount = std::min(triangleToPart_.size(), mesh_.indices.size() / 3);
        for (size_t tri = 0; tri < triCount; ++tri) {
            if (!isTriangleVisibleForSelection(tri)) {
                continue;
            }
            bool anyInside = false;
            for (int corner = 0; corner < 3; ++corner) {
                if (vertexInside(mesh_.indices[tri * 3 + static_cast<size_t>(corner)])) {
                    anyInside = true;
                    break;
                }
            }
            const int part = triangleToPart_[tri];
            if (anyInside && part >= 0) {
                hitParts.insert(part);
            }
        }
        const size_t elemEdgeCount =
            std::min(mesh_.elemEdgeToElement.size(), mesh_.elemEdgeVertices.size() / 6);
        for (size_t edge = 0; edge < elemEdgeCount; ++edge) {
            const int elementId = mesh_.elemEdgeToElement[edge];
            if (!isElementVisibleForSelection(elementId)) {
                continue;
            }
            const size_t base = edge * 6;
            const glm::vec3 p0(mesh_.elemEdgeVertices[base],
                               mesh_.elemEdgeVertices[base + 1],
                               mesh_.elemEdgeVertices[base + 2]);
            const glm::vec3 p1(mesh_.elemEdgeVertices[base + 3],
                               mesh_.elemEdgeVertices[base + 4],
                               mesh_.elemEdgeVertices[base + 5]);
            if (pointInside(p0) || pointInside(p1)) {
                const auto partIt = elementToPart_.find(elementId);
                if (partIt != elementToPart_.end()) {
                    hitParts.insert(partIt->second);
                }
            }
        }
        for (int part : hitParts) {
            if (removeSelection) {
                deselectPart(part);
            } else {
                selectPart(part);
            }
        }
    } else {
        const size_t triCount = std::min(triangleToElement_.size(), mesh_.indices.size() / 3);
        for (size_t tri = 0; tri < triCount; ++tri) {
            if (!isTriangleVisibleForSelection(tri)) {
                continue;
            }
            bool anyInside = false;
            for (int corner = 0; corner < 3; ++corner) {
                if (vertexInside(mesh_.indices[tri * 3 + static_cast<size_t>(corner)])) {
                    anyInside = true;
                    break;
                }
            }
            const int elementId = triangleToElement_[tri];
            if (anyInside && elementId >= 0 && isElementVisibleForSelection(elementId)) {
                if (removeSelection) {
                    selection_.selectedElements.erase(elementId);
                } else {
                    selection_.selectedElements.insert(elementId);
                }
            }
        }
        const size_t elemEdgeCount =
            std::min(mesh_.elemEdgeToElement.size(), mesh_.elemEdgeVertices.size() / 6);
        for (size_t edge = 0; edge < elemEdgeCount; ++edge) {
            const int elementId = mesh_.elemEdgeToElement[edge];
            if (elementId < 0 || !isElementVisibleForSelection(elementId)) {
                continue;
            }
            const size_t base = edge * 6;
            const glm::vec3 p0(mesh_.elemEdgeVertices[base],
                               mesh_.elemEdgeVertices[base + 1],
                               mesh_.elemEdgeVertices[base + 2]);
            const glm::vec3 p1(mesh_.elemEdgeVertices[base + 3],
                               mesh_.elemEdgeVertices[base + 4],
                               mesh_.elemEdgeVertices[base + 5]);
            if (pointInside(p0) || pointInside(p1)) {
                if (removeSelection) {
                    selection_.selectedElements.erase(elementId);
                } else {
                    selection_.selectedElements.insert(elementId);
                }
            }
        }
    }

    const std::vector<int> ids = currentSelectionIds();
    emit selectionChanged(pickMode_, static_cast<int>(ids.size()), ids);
    if (pickMode_ == PickMode::Part) {
        emit partsPicked(pickedPartIndices());
    }
    rebuildSelectionHighlight();
    updateIdLabels();
    renderFrame();
    return true;
}

bool VulkanViewport::handleWheelEvent(QEvent* event)
{
    if (event->type() != QEvent::Wheel) {
        return false;
    }

    auto* wheelEvent = static_cast<QWheelEvent*>(event);
    cam_.zoom(static_cast<float>(wheelEvent->angleDelta().y()) / 120.0f);
    rebuildSelectionHighlight();
    renderFrame();
    return true;
}

int VulkanViewport::edgeElementAtPosition(const QPointF& position,
                                          const glm::mat4& mvp,
                                          float thresholdPx) const
{
    const QSize size = nativeWindow_->size().isValid() ? nativeWindow_->size() : this->size();
    return ScreenSpacePicking::edgeElementAtPoint(
        mesh_,
        position,
        mvp,
        static_cast<float>(std::max(1, size.width())),
        static_cast<float>(std::max(1, size.height())),
        false,
        thresholdPx,
        [this](int elementId) { return isElementVisibleForSelection(elementId); });
}

glm::mat4 VulkanViewport::currentGlmMvp() const
{
    const QSize size = nativeWindow_->size().isValid() ? nativeWindow_->size() : this->size();
    const float aspect = (size.height() > 0)
        ? static_cast<float>(std::max(1, size.width())) / static_cast<float>(size.height())
        : 1.0f;
    const float sceneSize = std::max(modelSize_, 1.0e-4f);
    const float nearPlane = std::max(std::min(cam_.distance * 0.01f, sceneSize * 0.01f),
                                     sceneSize * 1.0e-5f);
    const float farPlane = std::max(cam_.distance + sceneSize * 2.0f,
                                    nearPlane + sceneSize * 0.1f);

    glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, nearPlane, farPlane);
    projection[1][1] *= -1.0f; // Vulkan NDC 的 Y 方向与 OpenGL 不同。
    return projection * cam_.viewMatrix();
}

void VulkanViewport::rebuildPartLookup()
{
    partTriangles_.clear();
    partElementIds_.clear();
    elementToPart_.clear();

    int numParts = 0;
    for (int part : triangleToPart_) {
        if (part >= 0) {
            numParts = std::max(numParts, part + 1);
        }
    }
    for (int part : edgeToPart_) {
        if (part >= 0) {
            numParts = std::max(numParts, part + 1);
        }
    }
    if (numParts == 0) {
        return;
    }

    partTriangles_.resize(static_cast<size_t>(numParts));
    std::vector<std::unordered_set<int>> partSets(static_cast<size_t>(numParts));
    const size_t triCount = std::min(triangleToElement_.size(), triangleToPart_.size());
    for (size_t tri = 0; tri < triCount; ++tri) {
        const int part = triangleToPart_[tri];
        const int element = triangleToElement_[tri];
        if (part < 0 || element < 0 || part >= numParts) {
            continue;
        }
        partTriangles_[static_cast<size_t>(part)].push_back(static_cast<int>(tri));
        partSets[static_cast<size_t>(part)].insert(element);
    }
    const size_t edgeCount = std::min(edgeToPart_.size(), mesh_.edgeToElement.size());
    for (size_t edge = 0; edge < edgeCount; ++edge) {
        const int part = edgeToPart_[edge];
        const int element = mesh_.edgeToElement[edge];
        if (part < 0 || element < 0 || part >= numParts) {
            continue;
        }
        partSets[static_cast<size_t>(part)].insert(element);
    }

    partElementIds_.resize(static_cast<size_t>(numParts));
    for (int part = 0; part < numParts; ++part) {
        auto& ids = partElementIds_[static_cast<size_t>(part)];
        ids.assign(partSets[static_cast<size_t>(part)].begin(), partSets[static_cast<size_t>(part)].end());
        std::sort(ids.begin(), ids.end());
        for (int element : ids) {
            elementToPart_.emplace(element, part);
        }
    }
}

int VulkanViewport::closestNodeForElement(int elementId, const QPointF& position) const
{
    const QSize size = nativeWindow_->size().isValid() ? nativeWindow_->size() : this->size();
    return ScreenSpacePicking::closestNodeForElement(mesh_,
                                                     triangleToElement_,
                                                     vertexToNode_,
                                                     elementId,
                                                     position,
                                                     currentGlmMvp(),
                                                     static_cast<float>(std::max(1, size.width())),
                                                     static_cast<float>(std::max(1, size.height())),
                                                     false);
}

void VulkanViewport::selectPart(int partIndex)
{
    if (partIndex < 0 || partIndex >= static_cast<int>(partElementIds_.size())) {
        return;
    }
    if (!isPartVisible(partIndex)) {
        return;
    }
    for (int element : partElementIds_[static_cast<size_t>(partIndex)]) {
        if (isElementVisibleForSelection(element)) {
            selection_.selectedElements.insert(element);
        }
    }
}

void VulkanViewport::deselectPart(int partIndex)
{
    if (partIndex < 0 || partIndex >= static_cast<int>(partElementIds_.size())) {
        return;
    }
    for (int element : partElementIds_[static_cast<size_t>(partIndex)]) {
        selection_.selectedElements.erase(element);
    }
}

bool VulkanViewport::isPartFullySelected(int partIndex) const
{
    if (partIndex < 0 || partIndex >= static_cast<int>(partElementIds_.size())) {
        return false;
    }
    const auto& elements = partElementIds_[static_cast<size_t>(partIndex)];
    if (elements.empty()) {
        return false;
    }
    bool hasVisibleElement = false;
    for (int element : elements) {
        if (!isElementVisibleForSelection(element)) {
            continue;
        }
        hasVisibleElement = true;
        if (!selection_.isElementSelected(element)) {
            return false;
        }
    }
    return hasVisibleElement;
}

std::vector<int> VulkanViewport::pickedPartIndices() const
{
    std::vector<int> parts;
    for (int part = 0; part < static_cast<int>(partElementIds_.size()); ++part) {
        if (isPartFullySelected(part)) {
            parts.push_back(part);
        }
    }
    return parts;
}

std::vector<int> VulkanViewport::currentSelectionIds() const
{
    std::vector<int> ids;
    if (pickMode_ == PickMode::Node) {
        ids.assign(selection_.selectedNodes.begin(), selection_.selectedNodes.end());
    } else {
        ids.assign(selection_.selectedElements.begin(), selection_.selectedElements.end());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool VulkanViewport::isPartVisible(int partIndex) const
{
    if (partIndex < 0) {
        return true;
    }
    const auto visibilityIt = partVisibility_.find(partIndex);
    return visibilityIt == partVisibility_.end() || visibilityIt->second;
}

bool VulkanViewport::isTriangleVisibleForSelection(size_t triangleIndex) const
{
    const size_t triangleCount = std::min(triangleToElement_.size(), mesh_.indices.size() / 3);
    if (triangleIndex >= triangleCount) {
        return false;
    }
    const int elementId = triangleToElement_[triangleIndex];
    if (elementId >= 0 && hiddenElementIds_.count(elementId) > 0) {
        return false;
    }
    const int part = triangleIndex < triangleToPart_.size()
        ? triangleToPart_[triangleIndex]
        : -1;
    return isPartVisible(part);
}

bool VulkanViewport::isElementVisibleForSelection(int elementId) const
{
    if (elementId >= 0 && hiddenElementIds_.count(elementId) > 0) {
        return false;
    }
    const auto partIt = elementToPart_.find(elementId);
    if (partIt == elementToPart_.end()) {
        return true;
    }
    return isPartVisible(partIt->second);
}

bool VulkanViewport::isNodeVisibleForSelection(int nodeId) const
{
    const size_t triCount = std::min(triangleToElement_.size(), mesh_.indices.size() / 3);
    for (size_t tri = 0; tri < triCount; ++tri) {
        if (!isTriangleVisibleForSelection(tri)) {
            continue;
        }
        for (int corner = 0; corner < 3; ++corner) {
            const unsigned int vertex = mesh_.indices[tri * 3 + static_cast<size_t>(corner)];
            const int mappedNode = vertex < vertexToNode_.size()
                ? vertexToNode_[vertex]
                : static_cast<int>(vertex);
            if (mappedNode == nodeId) {
                return true;
            }
        }
    }
    const size_t elemEdgeCount = std::min(mesh_.elemEdgeToElement.size(), mesh_.elemEdgeNodeIds.size());
    for (size_t edge = 0; edge < elemEdgeCount; ++edge) {
        const int elementId = mesh_.elemEdgeToElement[edge];
        if (!isElementVisibleForSelection(elementId)) {
            continue;
        }
        const auto [node0, node1] = mesh_.elemEdgeNodeIds[edge];
        if (node0 == nodeId || node1 == nodeId) {
            return true;
        }
    }
    return false;
}

void VulkanViewport::appendPartOutlineHighlight(std::vector<float>& lineVertices) const
{
    struct EdgeAdjacency {
        unsigned int va = 0;
        unsigned int vb = 0;
        std::vector<int> adjacentTriangles;
    };

    auto edgeKey = [this](unsigned int a, unsigned int b) -> int64_t {
        const int nodeA = a < vertexToNode_.size() ? vertexToNode_[a] : static_cast<int>(a);
        const int nodeB = b < vertexToNode_.size() ? vertexToNode_[b] : static_cast<int>(b);
        return (static_cast<int64_t>(std::min(nodeA, nodeB)) << 32) |
            static_cast<uint32_t>(std::max(nodeA, nodeB));
    };
    auto pushIndexedEdge = [this, &lineVertices](unsigned int a, unsigned int b) {
        const size_t baseA = static_cast<size_t>(a) * 6;
        const size_t baseB = static_cast<size_t>(b) * 6;
        if (baseA + 2 >= mesh_.vertices.size() || baseB + 2 >= mesh_.vertices.size()) {
            return;
        }
        lineVertices.push_back(mesh_.vertices[baseA]);
        lineVertices.push_back(mesh_.vertices[baseA + 1]);
        lineVertices.push_back(mesh_.vertices[baseA + 2]);
        lineVertices.push_back(mesh_.vertices[baseB]);
        lineVertices.push_back(mesh_.vertices[baseB + 1]);
        lineVertices.push_back(mesh_.vertices[baseB + 2]);
    };
    auto triangleNormal = [this](int triangle) -> glm::vec3 {
        const size_t base = static_cast<size_t>(triangle) * 3;
        if (base + 2 >= mesh_.indices.size()) {
            return glm::vec3(0.0f);
        }
        const unsigned int i0 = mesh_.indices[base];
        const unsigned int i1 = mesh_.indices[base + 1];
        const unsigned int i2 = mesh_.indices[base + 2];
        const size_t b0 = static_cast<size_t>(i0) * 6;
        const size_t b1 = static_cast<size_t>(i1) * 6;
        const size_t b2 = static_cast<size_t>(i2) * 6;
        if (b0 + 2 >= mesh_.vertices.size() ||
            b1 + 2 >= mesh_.vertices.size() ||
            b2 + 2 >= mesh_.vertices.size()) {
            return glm::vec3(0.0f);
        }
        const glm::vec3 p0(mesh_.vertices[b0], mesh_.vertices[b0 + 1], mesh_.vertices[b0 + 2]);
        const glm::vec3 p1(mesh_.vertices[b1], mesh_.vertices[b1 + 1], mesh_.vertices[b1 + 2]);
        const glm::vec3 p2(mesh_.vertices[b2], mesh_.vertices[b2 + 1], mesh_.vertices[b2 + 2]);
        const glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
        const float length = glm::length(n);
        return length > 1.0e-12f ? n / length : glm::vec3(0.0f);
    };

    std::unordered_set<int> selectedParts;
    for (int part = 0; part < static_cast<int>(partElementIds_.size()); ++part) {
        const auto visibilityIt = partVisibility_.find(part);
        if (visibilityIt != partVisibility_.end() && !visibilityIt->second) {
            continue;
        }
        for (int element : partElementIds_[static_cast<size_t>(part)]) {
            if (selection_.isElementSelected(element)) {
                selectedParts.insert(part);
                break;
            }
        }
    }
    if (selectedParts.empty()) {
        return;
    }

    std::unordered_map<int64_t, EdgeAdjacency> edgeAdjacency;
    const int triangleCount = static_cast<int>(mesh_.indices.size() / 3);
    edgeAdjacency.reserve(static_cast<size_t>(triangleCount) * 2);
    for (int triangle = 0; triangle < triangleCount; ++triangle) {
        if (!isTriangleVisibleForSelection(static_cast<size_t>(triangle))) {
            continue;
        }
        for (int edge = 0; edge < 3; ++edge) {
            const unsigned int va = mesh_.indices[static_cast<size_t>(triangle) * 3 + edge];
            const unsigned int vb = mesh_.indices[static_cast<size_t>(triangle) * 3 + ((edge + 1) % 3)];
            EdgeAdjacency& adjacency = edgeAdjacency[edgeKey(va, vb)];
            if (adjacency.adjacentTriangles.empty()) {
                adjacency.va = va;
                adjacency.vb = vb;
            }
            adjacency.adjacentTriangles.push_back(triangle);
        }
    }

    constexpr float featureAngleThreshold = 0.5f; // cos(60°)
    const glm::vec3 eye = cam_.eye();
    std::unordered_set<int64_t> visitedEdges;
    for (int part : selectedParts) {
        if (part < 0 || part >= static_cast<int>(partTriangles_.size())) {
            continue;
        }
        for (int triangle : partTriangles_[static_cast<size_t>(part)]) {
            if (!isTriangleVisibleForSelection(static_cast<size_t>(triangle))) {
                continue;
            }
            for (int edge = 0; edge < 3; ++edge) {
                const unsigned int va = mesh_.indices[static_cast<size_t>(triangle) * 3 + edge];
                const unsigned int vb = mesh_.indices[static_cast<size_t>(triangle) * 3 + ((edge + 1) % 3)];
                const int64_t key = edgeKey(va, vb);
                if (!visitedEdges.insert(key).second) {
                    continue;
                }
                const auto adjacencyIt = edgeAdjacency.find(key);
                if (adjacencyIt == edgeAdjacency.end()) {
                    continue;
                }

                int selectedTriangleCount = 0;
                int otherTriangleCount = 0;
                int selectedTriangle0 = -1;
                int selectedTriangle1 = -1;
                for (int adjacentTriangle : adjacencyIt->second.adjacentTriangles) {
                    const int adjacentPart = adjacentTriangle < static_cast<int>(triangleToPart_.size())
                        ? triangleToPart_[static_cast<size_t>(adjacentTriangle)]
                        : -1;
                    if (adjacentPart >= 0 && selectedParts.count(adjacentPart) > 0) {
                        if (selectedTriangleCount == 0) {
                            selectedTriangle0 = adjacentTriangle;
                        } else if (selectedTriangleCount == 1) {
                            selectedTriangle1 = adjacentTriangle;
                        }
                        ++selectedTriangleCount;
                    } else {
                        ++otherTriangleCount;
                    }
                }

                if (otherTriangleCount > 0 || selectedTriangleCount == 1) {
                    pushIndexedEdge(adjacencyIt->second.va, adjacencyIt->second.vb);
                    continue;
                }
                if (selectedTriangleCount < 2 || selectedTriangle0 < 0 || selectedTriangle1 < 0) {
                    continue;
                }

                const glm::vec3 n0 = triangleNormal(selectedTriangle0);
                const glm::vec3 n1 = triangleNormal(selectedTriangle1);
                if (glm::dot(n0, n1) < featureAngleThreshold) {
                    pushIndexedEdge(adjacencyIt->second.va, adjacencyIt->second.vb);
                    continue;
                }

                const size_t baseA = static_cast<size_t>(adjacencyIt->second.va) * 6;
                const size_t baseB = static_cast<size_t>(adjacencyIt->second.vb) * 6;
                if (baseA + 2 >= mesh_.vertices.size() || baseB + 2 >= mesh_.vertices.size()) {
                    continue;
                }
                const glm::vec3 a(mesh_.vertices[baseA], mesh_.vertices[baseA + 1], mesh_.vertices[baseA + 2]);
                const glm::vec3 b(mesh_.vertices[baseB], mesh_.vertices[baseB + 1], mesh_.vertices[baseB + 2]);
                const glm::vec3 mid = (a + b) * 0.5f;
                const glm::vec3 viewDir = eye - mid;
                if (glm::dot(n0, viewDir) * glm::dot(n1, viewDir) <= 0.0f) {
                    pushIndexedEdge(adjacencyIt->second.va, adjacencyIt->second.vb);
                }
            }
        }
    }
}

bool VulkanViewport::rebuildSelectionHighlight()
{
    if (backend_.device() == VK_NULL_HANDLE) {
        return true;
    }

    std::vector<float> lineVertices;
    auto appendLine = [&lineVertices](const glm::vec3& a, const glm::vec3& b) {
        lineVertices.push_back(a.x);
        lineVertices.push_back(a.y);
        lineVertices.push_back(a.z);
        lineVertices.push_back(b.x);
        lineVertices.push_back(b.y);
        lineVertices.push_back(b.z);
    };

    if (pickMode_ == PickMode::Node && !selection_.selectedNodes.empty()) {
        std::unordered_set<int> emittedNodes;
        const size_t vertexCount = mesh_.vertices.size() / 6;
        const size_t mapCount = std::min(vertexToNode_.size(), vertexCount);
        for (size_t vertex = 0; vertex < mapCount; ++vertex) {
            const int nodeId = vertexToNode_[vertex];
            if (!selection_.isNodeSelected(nodeId) || !emittedNodes.insert(nodeId).second) {
                continue;
            }
            const size_t base = vertex * 6;
            const glm::vec3 p(mesh_.vertices[base], mesh_.vertices[base + 1], mesh_.vertices[base + 2]);
            const float s = std::max(selectionMarkerSize_, 1.0e-4f);
            appendLine(p - glm::vec3(s, 0.0f, 0.0f), p + glm::vec3(s, 0.0f, 0.0f));
            appendLine(p - glm::vec3(0.0f, s, 0.0f), p + glm::vec3(0.0f, s, 0.0f));
            appendLine(p - glm::vec3(0.0f, 0.0f, s), p + glm::vec3(0.0f, 0.0f, s));
        }
    } else if (pickMode_ == PickMode::Part && !selection_.selectedElements.empty()) {
        appendPartOutlineHighlight(lineVertices);
    } else if (!selection_.selectedElements.empty()) {
        const size_t edgeCount = std::min(mesh_.elemEdgeToElement.size(), mesh_.elemEdgeVertices.size() / 6);
        lineVertices.reserve(edgeCount * 6);
        for (size_t edge = 0; edge < edgeCount; ++edge) {
            const int elementId = mesh_.elemEdgeToElement[edge];
            if (!selection_.isElementSelected(elementId) || !isElementVisibleForSelection(elementId)) {
                continue;
            }
            const size_t base = edge * 6;
            for (size_t i = 0; i < 6; ++i) {
                lineVertices.push_back(mesh_.elemEdgeVertices[base + i]);
            }
        }
    }

    if (!backend_.uploadSelectionLines(lineVertices)) {
        lastError_ = backend_.lastError();
        return false;
    }
    return true;
}

QMatrix4x4 VulkanViewport::currentMvp() const
{
    const glm::mat4 mvp = currentGlmMvp();
    return QMatrix4x4(glm::value_ptr(glm::transpose(mvp)));
}

glm::mat4 VulkanViewport::currentAxesGlmMvp() const
{
    glm::mat3 rot = glm::mat3(cam_.viewMatrix());
    glm::vec3 axesEye = glm::vec3(rot[0][2], rot[1][2], rot[2][2]) * 2.5f;
    glm::mat4 axesView = glm::lookAt(axesEye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 axesProj = glm::ortho(-1.3f, 1.3f, -1.3f, 1.3f, 0.01f, 10.0f);
    axesProj[1][1] *= -1.0f;
    return depthZeroToOneRemap() * axesProj * axesView;
}

QMatrix4x4 VulkanViewport::currentAxesMvp() const
{
    const glm::mat4 axesMvp = currentAxesGlmMvp();
    return QMatrix4x4(glm::value_ptr(glm::transpose(axesMvp)));
}

bool VulkanViewport::standardViewFromAxesClick(const QPointF& position, StandardView* view) const
{
    if (!view) {
        return false;
    }

    const QSize size = windowContainer_ ? windowContainer_->size() : this->size();
    const int availableWidth = std::max(0, size.width() - kAxesMargin * 2);
    const int availableHeight = std::max(0, size.height() - kAxesMargin * 2);
    const int axesSize = std::min(kAxesViewportSize, std::min(availableWidth, availableHeight));
    if (axesSize <= 0) {
        return false;
    }

    const glm::mat4 axesMvp = currentAxesGlmMvp();
    const int axesLeft = kAxesMargin;
    const int axesTop = size.height() - axesSize - kAxesMargin;
    auto project = [&](glm::vec3 pt) -> QPointF {
        const glm::vec4 clip = axesMvp * glm::vec4(pt, 1.0f);
        if (std::abs(clip.w) <= 1.0e-6f) {
            return QPointF(-10000.0, -10000.0);
        }
        const float ndcX = clip.x / clip.w;
        const float ndcY = clip.y / clip.w;
        const int x = axesLeft + static_cast<int>((ndcX * 0.5f + 0.5f) * axesSize);
        const int y = axesTop + static_cast<int>((ndcY * 0.5f + 0.5f) * axesSize);
        return QPointF(x, y);
    };

    const QPointF origin = project(glm::vec3(0.0f));
    const std::array<glm::vec3, 3> axisDirs = {
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)
    };
    const std::array<StandardView, 3> axisViews = {
        StandardView::Right,
        StandardView::Top,
        StandardView::Front
    };

    int bestAxis = -1;
    float bestDist2 = 1.0e30f;
    const float lineThreshold2 = 13.0f * 13.0f;
    for (size_t i = 0; i < axisDirs.size(); ++i) {
        const QPointF end = project(axisDirs[i] * 1.15f);
        const QRectF labelRect(end.x() - kAxesLabelSize / 2.0f - kAxesClickPadding,
                               end.y() - kAxesLabelSize / 2.0f - kAxesClickPadding,
                               kAxesLabelSize + kAxesClickPadding * 2.0f,
                               kAxesLabelSize + kAxesClickPadding * 2.0f);
        if (labelRect.contains(position)) {
            *view = axisViews[i];
            return true;
        }

        const float dist2 = ScreenSpacePicking::distanceSquaredToSegment(position, origin, end);
        if (dist2 <= lineThreshold2 && dist2 < bestDist2) {
            bestAxis = static_cast<int>(i);
            bestDist2 = dist2;
        }
    }

    if (bestAxis >= 0) {
        *view = axisViews[static_cast<size_t>(bestAxis)];
        return true;
    }
    return false;
}

void VulkanViewport::updateAxesLabels()
{
    const QSize size = windowContainer_ ? windowContainer_->size() : this->size();
    const int availableWidth = std::max(0, size.width() - kAxesMargin * 2);
    const int availableHeight = std::max(0, size.height() - kAxesMargin * 2);
    const int axesSize = std::min(kAxesViewportSize, std::min(availableWidth, availableHeight));
    if (axesSize <= 0) {
        for (QLabel* label : axesLabels_) {
            if (label) {
                label->hide();
            }
        }
        return;
    }

    const glm::mat4 axesMvp = currentAxesGlmMvp();
    const int axesLeft = kAxesMargin;
    const int axesTop = size.height() - axesSize - kAxesMargin;
    const std::array<glm::vec3, 3> labelPositions = {
        glm::vec3(1.15f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.15f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.15f)
    };

    for (size_t i = 0; i < axesLabels_.size(); ++i) {
        QLabel* label = axesLabels_[i];
        if (!label) {
            continue;
        }

        const glm::vec4 clip = axesMvp * glm::vec4(labelPositions[i], 1.0f);
        if (std::abs(clip.w) <= 1.0e-6f) {
            label->hide();
            continue;
        }

        const float ndcX = clip.x / clip.w;
        const float ndcY = clip.y / clip.w;
        const int x = axesLeft + static_cast<int>((ndcX * 0.5f + 0.5f) * axesSize) - kAxesLabelSize / 2;
        const int y = axesTop + static_cast<int>((ndcY * 0.5f + 0.5f) * axesSize) - kAxesLabelSize / 2;
        label->move(x, y);
        label->show();
        label->raise();
    }
}

void VulkanViewport::updateIdLabels()
{
    auto hideAllLabels = [this]() {
        for (QLabel* label : idLabels_) {
            if (label) {
                label->hide();
            }
        }
    };

    if (!showLabels_ || !hasMesh_ || !selection_.hasSelection()) {
        hideAllLabels();
        return;
    }

    const QSize size = windowContainer_ ? windowContainer_->size() : this->size();
    const float width = static_cast<float>(std::max(1, size.width()));
    const float height = static_cast<float>(std::max(1, size.height()));
    const glm::mat4 mvp = currentGlmMvp();

    struct LabelCandidate {
        QString text;
        QPoint point;
    };
    std::vector<LabelCandidate> labels;
    labels.reserve(std::min(kMaxIdLabels, static_cast<int>(currentSelectionIds().size())));

    auto project = [&mvp, width, height](const glm::vec3& position, QPoint& point) {
        const glm::vec4 clip = mvp * glm::vec4(position, 1.0f);
        if (clip.w <= 0.0f) {
            return false;
        }
        const float ndcX = clip.x / clip.w;
        const float ndcY = clip.y / clip.w;
        if (ndcX < -1.05f || ndcX > 1.05f || ndcY < -1.05f || ndcY > 1.05f) {
            return false;
        }
        point = QPoint(static_cast<int>((ndcX * 0.5f + 0.5f) * width),
                       static_cast<int>((ndcY * 0.5f + 0.5f) * height));
        return true;
    };

    auto appendLabel = [&labels, &project](const QString& text, const glm::vec3& position) {
        if (static_cast<int>(labels.size()) >= kMaxIdLabels) {
            return;
        }
        QPoint point;
        if (project(position, point)) {
            labels.push_back({text, point});
        }
    };

    if (pickMode_ == PickMode::Node) {
        std::unordered_map<int, size_t> nodeToVertex;
        const size_t vertexCount = mesh_.vertices.size() / 6;
        const size_t mapCount = std::min(vertexToNode_.size(), vertexCount);
        for (size_t vertex = 0; vertex < mapCount; ++vertex) {
            const int nodeId = vertexToNode_[vertex];
            if (nodeId >= 0 && nodeToVertex.find(nodeId) == nodeToVertex.end()) {
                nodeToVertex.emplace(nodeId, vertex);
            }
        }
        for (int nodeId : currentSelectionIds()) {
            const auto it = nodeToVertex.find(nodeId);
            if (it == nodeToVertex.end()) {
                continue;
            }
            const size_t base = it->second * 6;
            if (base + 2 >= mesh_.vertices.size()) {
                continue;
            }
            appendLabel(QString::number(nodeId),
                        glm::vec3(mesh_.vertices[base],
                                  mesh_.vertices[base + 1],
                                  mesh_.vertices[base + 2]));
        }
    } else if (pickMode_ == PickMode::Part) {
        for (int part : pickedPartIndices()) {
            if (part < 0 || part >= static_cast<int>(partTriangles_.size())) {
                continue;
            }
            const auto visibilityIt = partVisibility_.find(part);
            if (visibilityIt != partVisibility_.end() && !visibilityIt->second) {
                continue;
            }
            glm::vec3 sum(0.0f);
            int count = 0;
            for (int tri : partTriangles_[static_cast<size_t>(part)]) {
                if (!isTriangleVisibleForSelection(static_cast<size_t>(tri))) {
                    continue;
                }
                const size_t triBase = static_cast<size_t>(tri) * 3;
                if (triBase + 2 >= mesh_.indices.size()) {
                    continue;
                }
                for (int corner = 0; corner < 3; ++corner) {
                    const size_t vertex = static_cast<size_t>(mesh_.indices[triBase + corner]);
                    const size_t base = vertex * 6;
                    if (base + 2 >= mesh_.vertices.size()) {
                        continue;
                    }
                    sum += glm::vec3(mesh_.vertices[base],
                                     mesh_.vertices[base + 1],
                                     mesh_.vertices[base + 2]);
                    ++count;
                }
            }
            if (count > 0) {
                appendLabel(QStringLiteral("Part %1").arg(part + 1), sum / static_cast<float>(count));
            }
        }
    } else {
        struct Accum {
            glm::vec3 sum{0.0f};
            int count = 0;
        };
        std::unordered_map<int, Accum> centroids;
        const size_t triCount = std::min(triangleToElement_.size(), mesh_.indices.size() / 3);
        for (size_t tri = 0; tri < triCount; ++tri) {
            const int elementId = triangleToElement_[tri];
            if (!selection_.isElementSelected(elementId) ||
                !isElementVisibleForSelection(elementId)) {
                continue;
            }
            Accum& accum = centroids[elementId];
            for (int corner = 0; corner < 3; ++corner) {
                const size_t vertex = static_cast<size_t>(mesh_.indices[tri * 3 + corner]);
                const size_t base = vertex * 6;
                if (base + 2 >= mesh_.vertices.size()) {
                    continue;
                }
                accum.sum += glm::vec3(mesh_.vertices[base],
                                       mesh_.vertices[base + 1],
                                       mesh_.vertices[base + 2]);
                ++accum.count;
            }
        }
        std::vector<int> elementIds;
        elementIds.reserve(centroids.size());
        for (const auto& [elementId, accum] : centroids) {
            if (accum.count > 0) {
                elementIds.push_back(elementId);
            }
        }
        std::sort(elementIds.begin(), elementIds.end());
        for (int elementId : elementIds) {
            const Accum& accum = centroids[elementId];
            appendLabel(QString::number(elementId), accum.sum / static_cast<float>(accum.count));
        }
    }

    while (idLabels_.size() < labels.size()) {
        auto* label = new QLabel(this);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        label->setAlignment(Qt::AlignCenter);
        QFont font = label->font();
        font.setBold(true);
        font.setPixelSize(11);
        label->setFont(font);
        label->setStyleSheet(QStringLiteral(
            "QLabel { color: rgb(255, 205, 64); background: rgba(0, 0, 0, 150); "
            "border-radius: 3px; padding: 1px 4px; }"));
        label->hide();
        idLabels_.push_back(label);
    }

    for (size_t i = 0; i < idLabels_.size(); ++i) {
        QLabel* label = idLabels_[i];
        if (!label) {
            continue;
        }
        if (i >= labels.size()) {
            label->hide();
            continue;
        }
        label->setText(labels[i].text);
        label->adjustSize();
        label->move(labels[i].point - QPoint(label->width() / 2, label->height() + 8));
        label->show();
        label->raise();
    }
}

void VulkanViewport::updateFrameStats(qint64 frameNs)
{
    frameTimeMs_ = static_cast<float>(frameNs) / 1000000.0f;
    ++frameCounter_;
    const qint64 elapsedMs = fpsTimer_.elapsed();
    if (elapsedMs >= 250) {
        fps_ = static_cast<float>(frameCounter_) * 1000.0f / static_cast<float>(elapsedMs);
        frameCounter_ = 0;
        fpsTimer_.restart();
    }
}

void VulkanViewport::resetFrameStats()
{
    frameCounter_ = 0;
    fps_ = 0.0f;
    frameTimeMs_ = 0.0f;
    fpsTimer_.restart();
}
