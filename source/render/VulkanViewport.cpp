#include "VulkanViewport.h"

#include "VulkanMacOSSurfaceFactory.h"

#include <QEvent>
#include <QFont>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <unordered_set>

namespace {
constexpr int kAxesLabelSize = 24;
constexpr int kAxesMargin = 8;
constexpr int kAxesViewportSize = 120;

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
    rubberBand_ = new QRubberBand(QRubberBand::Rectangle, windowContainer_);

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
        axesLabels_[i] = new QLabel(axesNames[i], windowContainer_);
        axesLabels_[i]->setAlignment(Qt::AlignCenter);
        axesLabels_[i]->setAttribute(Qt::WA_TransparentForMouseEvents);
        axesLabels_[i]->setFixedSize(kAxesLabelSize, kAxesLabelSize);
        QFont labelFont = axesLabels_[i]->font();
        labelFont.setBold(true);
        labelFont.setPixelSize(14);
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
}

void VulkanViewport::startRendering()
{
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

    const QMatrix4x4 axesMvp = currentAxesMvp();
    const bool rendered = hasMesh_
        ? backend_.renderMeshFrame(currentMvp(), 0.04f, 0.05f, 0.07f, 1.0f, axesMvp, displayMode_)
        : backend_.renderClearFrame(0.04f, 0.05f, 0.07f, 1.0f);
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
    hasMesh_ = !mesh_.vertices.empty() && !mesh_.indices.empty();
    meshDirty_ = true;
    emit selectionChanged(pickMode_, 0, {});
    if (pickMode_ == PickMode::Part) {
        emit partsPicked({});
    }
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
    renderFrame();
}

void VulkanViewport::setVertexColors(const std::vector<float>& colors)
{
    vertexColors_ = colors;
    vertexScalars_.clear();
    useVertexColor_ = true;
    meshDirty_ = true;
    renderFrame();
}

void VulkanViewport::setUseVertexColor(bool use)
{
    useVertexColor_ = use;
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
    updateScalarBufferOrMarkDirty(true);
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
    rebuildPartLookup();
    meshDirty_ = true;
    renderFrame();
}

void VulkanViewport::setVertexToNodeMap(const std::vector<int>& map)
{
    vertexToNode_ = map;
}

void VulkanViewport::setTriangleToPartMap(const std::vector<int>& map)
{
    triangleToPart_ = map;

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
    meshDirty_ = true;
    renderFrame();
}

void VulkanViewport::setPartVisibility(int partIndex, bool visible)
{
    partVisibility_[partIndex] = visible;
    bool selectionWasChanged = false;
    if (!visible && partIndex >= 0 && partIndex < static_cast<int>(partElementIds_.size())) {
        for (int element : partElementIds_[static_cast<size_t>(partIndex)]) {
            selectionWasChanged = selection_.selectedElements.erase(element) > 0 || selectionWasChanged;
        }
    }
    if (selectionWasChanged) {
        const std::vector<int> ids = currentSelectionIds();
        emit selectionChanged(pickMode_, static_cast<int>(ids.size()), ids);
        if (pickMode_ == PickMode::Part) {
            emit partsPicked(pickedPartIndices());
        }
    }
    meshDirty_ = true;
    renderFrame();
}

void VulkanViewport::setPickMode(PickMode mode)
{
    pickMode_ = mode;
    selection_.clear();
    emit selectionChanged(pickMode_, 0, {});
    rebuildSelectionHighlight();
}

void VulkanViewport::selectByIds(PickMode mode, const std::vector<int>& ids)
{
    pickMode_ = mode;
    selection_.clear();

    if (mode == PickMode::Node) {
        std::unordered_set<int> validNodes(vertexToNode_.begin(), vertexToNode_.end());
        for (int id : ids) {
            if (validNodes.count(id) > 0) {
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
    options.useVertexColor = useVertexColor_;
    options.vertexColors = vertexColors_;
    options.vertexScalars = vertexScalars_;
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
        lastMousePos_ = mouseEvent->position();
        pressMousePos_ = mouseEvent->position();
        boxOrigin_ = mouseEvent->position().toPoint();
        mouseMovedSincePress_ = false;
        leftPressForPick_ = mouseEvent->button() == Qt::LeftButton;
        rightPressForDeselect_ = mouseEvent->button() == Qt::RightButton;
        const bool selectionGesture = (mouseEvent->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) != 0;
        boxSelecting_ = leftPressForPick_ && selectionGesture;
        boxDeselecting_ = rightPressForDeselect_ && selectionGesture;
        if (boxSelecting_ || boxDeselecting_) {
            rubberBand_->setGeometry(QRect(boxOrigin_, QSize()));
            rubberBand_->show();
        }
        rotating_ = mouseEvent->button() == Qt::LeftButton && !boxSelecting_;
        panning_ = mouseEvent->button() == Qt::MiddleButton ||
            (mouseEvent->button() == Qt::RightButton && !boxDeselecting_);
        return rotating_ || panning_ || boxSelecting_ || boxDeselecting_;
    }
    case QEvent::MouseMove: {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QPointF delta = mouseEvent->position() - lastMousePos_;
        lastMousePos_ = mouseEvent->position();
        if ((mouseEvent->position() - pressMousePos_).manhattanLength() > 3.0) {
            mouseMovedSincePress_ = true;
        }
        if (boxSelecting_ || boxDeselecting_) {
            rubberBand_->setGeometry(QRect(boxOrigin_, mouseEvent->position().toPoint()).normalized());
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
        leftPressForPick_ = false;
        rightPressForDeselect_ = false;
        if (boxSelecting_ || boxDeselecting_) {
            const bool removeSelection = boxDeselecting_;
            boxSelecting_ = false;
            boxDeselecting_ = false;
            rubberBand_->hide();
            const QRect rect = QRect(boxOrigin_, mouseEvent->position().toPoint()).normalized();
            if (rect.width() > 3 && rect.height() > 3) {
                return selectInRect(rect, removeSelection);
            }
            return removeSelection
                ? deselectAtPosition(mouseEvent->position())
                : pickAtPosition(mouseEvent->position(), true);
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
        const size_t vertexCount = mesh_.vertices.size() / 6;
        const size_t mapCount = std::min(vertexToNode_.size(), vertexCount);
        for (size_t vertex = 0; vertex < mapCount; ++vertex) {
            if (!vertexInside(static_cast<unsigned int>(vertex))) {
                continue;
            }
            const int nodeId = vertexToNode_[vertex];
            if (nodeId >= 0 && touchedNodes.insert(nodeId).second) {
                if (removeSelection) {
                    selection_.selectedNodes.erase(nodeId);
                } else {
                    selection_.selectedNodes.insert(nodeId);
                }
            }
        }
    } else if (pickMode_ == PickMode::Part) {
        std::unordered_set<int> hitParts;
        const size_t triCount = std::min(triangleToPart_.size(), mesh_.indices.size() / 3);
        for (size_t tri = 0; tri < triCount; ++tri) {
            bool anyInside = false;
            for (int corner = 0; corner < 3; ++corner) {
                if (vertexInside(mesh_.indices[tri * 3 + static_cast<size_t>(corner)])) {
                    anyInside = true;
                    break;
                }
            }
            const int part = triangleToPart_[tri];
            if (anyInside && part >= 0) {
                const auto visibilityIt = partVisibility_.find(part);
                if (visibilityIt == partVisibility_.end() || visibilityIt->second) {
                    hitParts.insert(part);
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
    }

    const std::vector<int> ids = currentSelectionIds();
    emit selectionChanged(pickMode_, static_cast<int>(ids.size()), ids);
    if (pickMode_ == PickMode::Part) {
        emit partsPicked(pickedPartIndices());
    }
    rebuildSelectionHighlight();
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
    if (numParts == 0 || triangleToElement_.empty()) {
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
    if (elementId < 0 || triangleToElement_.empty()) {
        return -1;
    }

    const QSize size = nativeWindow_->size().isValid() ? nativeWindow_->size() : this->size();
    const float width = static_cast<float>(std::max(1, size.width()));
    const float height = static_cast<float>(std::max(1, size.height()));
    const float ndcX = static_cast<float>((2.0 * position.x() / width) - 1.0);
    const float ndcY = static_cast<float>((2.0 * position.y() / height) - 1.0);
    const glm::mat4 mvp = currentGlmMvp();

    float minDist2 = 1.0e30f;
    int closestNode = -1;
    const size_t triCount = std::min(triangleToElement_.size(), mesh_.indices.size() / 3);
    for (size_t tri = 0; tri < triCount; ++tri) {
        if (triangleToElement_[tri] != elementId) {
            continue;
        }
        for (int vertexInTri = 0; vertexInTri < 3; ++vertexInTri) {
            const unsigned int vertexIndex = mesh_.indices[tri * 3 + static_cast<size_t>(vertexInTri)];
            const size_t base = static_cast<size_t>(vertexIndex) * 6;
            if (base + 2 >= mesh_.vertices.size()) {
                continue;
            }
            const glm::vec4 world(mesh_.vertices[base],
                                  mesh_.vertices[base + 1],
                                  mesh_.vertices[base + 2],
                                  1.0f);
            const glm::vec4 clip = mvp * world;
            if (clip.w <= 0.0f) {
                continue;
            }
            const float sx = clip.x / clip.w;
            const float sy = clip.y / clip.w;
            const float dx = sx - ndcX;
            const float dy = sy - ndcY;
            const float dist2 = dx * dx + dy * dy;
            if (dist2 < minDist2) {
                minDist2 = dist2;
                closestNode = vertexIndex < vertexToNode_.size()
                    ? vertexToNode_[vertexIndex]
                    : static_cast<int>(vertexIndex);
            }
        }
    }
    return closestNode;
}

void VulkanViewport::selectPart(int partIndex)
{
    if (partIndex < 0 || partIndex >= static_cast<int>(partElementIds_.size())) {
        return;
    }
    for (int element : partElementIds_[static_cast<size_t>(partIndex)]) {
        selection_.selectedElements.insert(element);
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
    for (int element : elements) {
        if (!selection_.isElementSelected(element)) {
            return false;
        }
    }
    return true;
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

bool VulkanViewport::isElementVisibleForSelection(int elementId) const
{
    const auto partIt = elementToPart_.find(elementId);
    if (partIt == elementToPart_.end()) {
        return true;
    }
    const auto visibilityIt = partVisibility_.find(partIt->second);
    return visibilityIt == partVisibility_.end() || visibilityIt->second;
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
    return axesProj * axesView;
}

QMatrix4x4 VulkanViewport::currentAxesMvp() const
{
    const glm::mat4 axesMvp = currentAxesGlmMvp();
    return QMatrix4x4(glm::value_ptr(glm::transpose(axesMvp)));
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
        glm::vec3(1.12f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.12f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.12f)
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

void VulkanViewport::updateFrameStats(qint64 frameNs)
{
    frameTimeMs_ = static_cast<float>(frameNs) / 1000000.0f;
    ++frameCounter_;
    const qint64 elapsedMs = fpsTimer_.elapsed();
    if (elapsedMs >= 1000) {
        fps_ = static_cast<float>(frameCounter_) * 1000.0f / static_cast<float>(elapsedMs);
        frameCounter_ = 0;
        fpsTimer_.restart();
    }
}
