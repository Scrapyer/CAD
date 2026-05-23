#include "RenderViewport.h"

#include "ColorBarOverlay.h"
#include "GLWidget.h"
#if defined(FERENDER_HAS_METAL_RHI)
#include "MetalViewport.h"
#endif
#include "RenderBackendFactory.h"
#include "RenderSettings.h"
#include "Theme.h"
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
#include "VulkanViewport.h"
#endif

#include <QResizeEvent>
#include <QVBoxLayout>

RenderViewport::RenderViewport(QWidget* parent)
    : QWidget(parent),
      glWidget_(new GLWidget(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(glWidget_);
    colorBarOverlay_ = new ColorBarOverlay(this);
    colorBarOverlay_->resize(size());
    colorBarOverlay_->hide();

    connect(glWidget_, &GLWidget::glInitialized,
            this, &RenderViewport::renderInitialized);
    connect(glWidget_, &GLWidget::selectionChanged,
            this, &RenderViewport::selectionChanged);
    connect(glWidget_, &GLWidget::partsPicked,
            this, &RenderViewport::partsPicked);
    connect(glWidget_, &GLWidget::contextMenuRequested,
            this, &RenderViewport::contextMenuRequested);

    requestedBackendKind_ = RenderSettings::preferredBackend();
    activateBackend(requestedBackendKind_);
}

RenderViewport::~RenderViewport() = default;

void RenderViewport::setMesh(const Mesh& mesh)
{
    currentMesh_ = mesh;
    hasCurrentMesh_ = !mesh.vertices.empty() && !mesh.indices.empty();
    triangleToElement_.clear();
    vertexToNode_.clear();
    triangleToPart_.clear();
    edgeToPart_.clear();
    partVisibility_.clear();
    glWidget_->setMesh(mesh);
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->setMesh(mesh);
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->setMesh(mesh);
    }
#endif
}
void RenderViewport::setVertexColors(const std::vector<float>& colors)
{
    vertexColors_ = colors;
    vertexScalars_.clear();
    useVertexColor_ = true;
    glWidget_->setVertexColors(colors);
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->setVertexColors(colors);
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->setVertexColors(colors);
    }
#endif
}
void RenderViewport::setObjectColor(const glm::vec3& c)
{
    objectColor_ = c;
    glWidget_->setObjectColor(c);
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->setObjectColor(c);
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->setObjectColor(c);
    }
#endif
}
void RenderViewport::setModelDisplayMode(ModelDisplayMode mode)
{
    displayMode_ = mode;
    glWidget_->setModelDisplayMode(mode);
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->setModelDisplayMode(mode);
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->setModelDisplayMode(mode);
    }
#endif
}
void RenderViewport::fitToModel(const glm::vec3& center, float size)
{
    modelCenter_ = center;
    modelSize_ = size;
    hasModelFit_ = true;
    glWidget_->fitToModel(center, size);
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->fitToModel(center, size);
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->fitToModel(center, size);
    }
#endif
}

void RenderViewport::setStandardView(StandardView view)
{
    standardView_ = view;
    hasStandardView_ = true;
    glWidget_->setStandardView(view);
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->setStandardView(view);
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->setStandardView(view);
    }
#endif
}

void RenderViewport::applyTheme(const Theme& theme)
{
    glWidget_->applyTheme(theme);
    backgroundTopColor_ = QVector3D(theme.bgTopR, theme.bgTopG, theme.bgTopB);
    backgroundBottomColor_ = QVector3D(theme.bgBotR, theme.bgBotG, theme.bgBotB);
    colorBarTextColor_ = QColor(theme.barTextR, theme.barTextG, theme.barTextB);
    colorBarOverlay_->setTextColor(colorBarTextColor_);
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->setBackgroundGradient(backgroundTopColor_, backgroundBottomColor_);
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->setBackgroundGradient(backgroundTopColor_, backgroundBottomColor_);
    }
#endif
}

void RenderViewport::setViewportGridVisible(bool visible)
{
    viewportGridVisible_ = visible;
    glWidget_->setViewportGridVisible(visible);
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->setViewportGridVisible(visible);
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->setViewportGridVisible(visible);
    }
#endif
}

void RenderViewport::setColorBarVisible(bool visible)
{
    colorBarVisible_ = visible;
    glWidget_->setColorBarVisible(visible);
    updateColorBarOverlay();
}
void RenderViewport::setColorBarRange(float min, float max)
{
    colorBarMin_ = min;
    colorBarMax_ = max;
    glWidget_->setColorBarRange(min, max);
    colorBarOverlay_->setRange(min, max);
}
void RenderViewport::setColorBarTitle(const QString& title)
{
    colorBarTitle_ = title;
    glWidget_->setColorBarTitle(title);
    colorBarOverlay_->setTitle(title);
}
void RenderViewport::setColorBarExtremes(int minId, float minVal, int maxId, float maxVal)
{
    colorBarMinId_ = minId;
    colorBarMinValue_ = minVal;
    colorBarMaxId_ = maxId;
    colorBarMaxValue_ = maxVal;
    hasColorBarExtremes_ = true;
    glWidget_->setColorBarExtremes(minId, minVal, maxId, maxVal);
    colorBarOverlay_->setExtremes(minId, minVal, maxId, maxVal);
}
void RenderViewport::setColorBarIdLabel(const QString& label)
{
    colorBarIdLabel_ = label;
    glWidget_->setColorBarIdLabel(label);
    colorBarOverlay_->setIdLabel(label);
}
void RenderViewport::setTriangleToElementMap(const std::vector<int>& map)
{
    triangleToElement_ = map;
    glWidget_->setTriangleToElementMap(map);
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->setTriangleToElementMap(map);
    }
#endif
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->setTriangleToElementMap(map);
    }
#endif
}
void RenderViewport::setTriangleToFaceMap(const std::vector<int>& map) { glWidget_->setTriangleToFaceMap(map); }
void RenderViewport::setVertexToNodeMap(const std::vector<int>& map)
{
    vertexToNode_ = map;
    glWidget_->setVertexToNodeMap(map);
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->setVertexToNodeMap(map);
    }
#endif
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->setVertexToNodeMap(map);
    }
#endif
}
void RenderViewport::setPickMode(PickMode mode)
{
    currentPickMode_ = mode;
    glWidget_->setPickMode(mode);
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->setPickMode(mode);
    }
#endif
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->setPickMode(mode);
    }
#endif
}
void RenderViewport::setShowLabels(bool show)
{
    showLabels_ = show;
    glWidget_->setShowLabels(show);
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->setShowLabels(show);
    }
#endif
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->setShowLabels(show);
    }
#endif
}
void RenderViewport::selectByIds(PickMode mode, const std::vector<int>& ids)
{
    currentPickMode_ = mode;
    glWidget_->selectByIds(mode, ids);
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->selectByIds(mode, ids);
    }
#endif
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->selectByIds(mode, ids);
    }
#endif
}
void RenderViewport::setOverlayMesh(const Mesh& mesh)
{
    overlayMesh_ = mesh;
    glWidget_->setOverlayMesh(mesh);
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->setOverlayMesh(mesh);
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->setOverlayMesh(mesh);
    }
#endif
}
void RenderViewport::setOverlayVisible(bool visible)
{
    overlayVisible_ = visible;
    glWidget_->setOverlayVisible(visible);
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->setOverlayVisible(visible);
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->setOverlayVisible(visible);
    }
#endif
}
void RenderViewport::setUseVertexColor(bool use)
{
    useVertexColor_ = use;
    glWidget_->setUseVertexColor(use);
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->setUseVertexColor(use);
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->setUseVertexColor(use);
    }
#endif
}
void RenderViewport::setSliceLines(const std::vector<float>& lineVertices)
{
    sliceLineVertices_ = lineVertices;
    glWidget_->setSliceLines(lineVertices);
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->setSliceLines(lineVertices);
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->setSliceLines(lineVertices);
    }
#endif
}
void RenderViewport::clearSliceLines()
{
    sliceLineVertices_.clear();
    glWidget_->clearSliceLines();
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->clearSliceLines();
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->clearSliceLines();
    }
#endif
}
void RenderViewport::setIsoSurfaceMesh(const Mesh& mesh)
{
    isoSurfaceMesh_ = mesh;
    isoSurfaceVisible_ = !mesh.vertices.empty() && !mesh.indices.empty();
    glWidget_->setIsoSurfaceMesh(mesh);
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->setIsoSurfaceMesh(mesh);
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->setIsoSurfaceMesh(mesh);
    }
#endif
}
void RenderViewport::clearIsoSurface()
{
    isoSurfaceMesh_ = Mesh{};
    isoSurfaceVisible_ = false;
    glWidget_->clearIsoSurface();
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->clearIsoSurface();
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->clearIsoSurface();
    }
#endif
}
void RenderViewport::setClipPlanePreview(const glm::vec3& bbMin,
                                         const glm::vec3& bbMax,
                                         const glm::vec3& origin,
                                         const glm::vec3& normal)
{
    clipPreviewVisible_ = true;
    clipPreviewBbMin_ = bbMin;
    clipPreviewBbMax_ = bbMax;
    clipPreviewOrigin_ = origin;
    clipPreviewNormal_ = normal;
    glWidget_->setClipPlanePreview(bbMin, bbMax, origin, normal);
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->setClipPlanePreview(bbMin, bbMax, origin, normal);
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->setClipPlanePreview(bbMin, bbMax, origin, normal);
    }
#endif
}
void RenderViewport::clearClipPlanePreview()
{
    clipPreviewVisible_ = false;
    glWidget_->clearClipPlanePreview();
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->clearClipPlanePreview();
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->clearClipPlanePreview();
    }
#endif
}
void RenderViewport::setVertexScalars(const std::vector<float>& scalars,
                                      float minVal,
                                      float maxVal,
                                      int numBands)
{
    vertexScalars_ = scalars;
    vertexColors_.clear();
    scalarMin_ = minVal;
    scalarMax_ = maxVal;
    numBands_ = numBands;
    useVertexColor_ = true;
    glWidget_->setVertexScalars(scalars, minVal, maxVal, numBands);
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->setVertexScalars(scalars, minVal, maxVal, numBands);
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->setVertexScalars(scalars, minVal, maxVal, numBands);
    }
#endif
}
void RenderViewport::setTriangleToPartMap(const std::vector<int>& map)
{
    triangleToPart_ = map;
    glWidget_->setTriangleToPartMap(map);
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->setTriangleToPartMap(map);
    }
#endif
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->setTriangleToPartMap(map);
    }
#endif
}
void RenderViewport::setEdgeToPartMap(const std::vector<int>& map)
{
    edgeToPart_ = map;
    glWidget_->setEdgeToPartMap(map);
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->setEdgeToPartMap(map);
    }
#endif
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->setEdgeToPartMap(map);
    }
#endif
}
const std::vector<glm::vec3>& RenderViewport::partColors() const { return glWidget_->partColors(); }

void RenderViewport::setPreferredRenderBackend(RenderBackendKind kind)
{
    RenderSettings::setPreferredBackend(kind);
    requestedBackendKind_ = kind;
    updateColorBarOverlay();
}

RenderBackendKind RenderViewport::requestedRenderBackendKind() const
{
    return requestedBackendKind_;
}

RenderBackendKind RenderViewport::activeRenderBackendKind() const
{
    return activeBackendKind_;
}

QString RenderViewport::glRenderer() const
{
#if defined(FERENDER_HAS_METAL_RHI)
    if (activeBackendKind_ == RenderBackendKind::Metal && metalViewport_) {
        return metalViewport_->backendInfo().renderer;
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (activeBackendKind_ == RenderBackendKind::Vulkan && vulkanViewport_) {
        return vulkanViewport_->backendInfo().renderer;
    }
#endif
    return glWidget_->glRenderer();
}

QString RenderViewport::glVersion() const
{
#if defined(FERENDER_HAS_METAL_RHI)
    if (activeBackendKind_ == RenderBackendKind::Metal && metalViewport_) {
        return metalViewport_->backendInfo().version;
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (activeBackendKind_ == RenderBackendKind::Vulkan && vulkanViewport_) {
        return vulkanViewport_->backendInfo().version;
    }
#endif
    return glWidget_->glVersion();
}

QString RenderViewport::glslVersion() const
{
#if defined(FERENDER_HAS_METAL_RHI)
    if (activeBackendKind_ == RenderBackendKind::Metal && metalViewport_) {
        return metalViewport_->backendInfo().shadingLanguageVersion;
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (activeBackendKind_ == RenderBackendKind::Vulkan && vulkanViewport_) {
        return vulkanViewport_->backendInfo().shadingLanguageVersion;
    }
#endif
    return glWidget_->glslVersion();
}

QString RenderViewport::gpuVendor() const
{
#if defined(FERENDER_HAS_METAL_RHI)
    if (activeBackendKind_ == RenderBackendKind::Metal && metalViewport_) {
        return metalViewport_->backendInfo().vendor;
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (activeBackendKind_ == RenderBackendKind::Vulkan && vulkanViewport_) {
        return vulkanViewport_->backendInfo().vendor;
    }
#endif
    return glWidget_->gpuVendor();
}

QString RenderViewport::renderDiagnostics() const
{
#if defined(FERENDER_HAS_METAL_RHI)
    if (activeBackendKind_ == RenderBackendKind::Metal && metalViewport_) {
        const QSize drawableSize = metalViewport_->drawableSize();
        QString text = QStringLiteral("Layer: %1 | Drawable: %2x%3 | DPR: %4")
                           .arg(metalViewport_->hasNativeLayer() ? QStringLiteral("OK")
                                                                 : QStringLiteral("--"))
                           .arg(drawableSize.width())
                           .arg(drawableSize.height())
                           .arg(metalViewport_->devicePixelRatioF(), 0, 'f', 2);
        const QString error = metalViewport_->lastError();
        if (!error.isEmpty()) {
            text += QStringLiteral(" | Error: ") + error;
        }
        return text;
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (activeBackendKind_ == RenderBackendKind::Vulkan && vulkanViewport_) {
        QString text = QStringLiteral("Viewport: %1x%2 | DPR: %3")
                           .arg(vulkanViewport_->width())
                           .arg(vulkanViewport_->height())
                           .arg(vulkanViewport_->devicePixelRatioF(), 0, 'f', 2);
        const QString error = vulkanViewport_->lastError();
        if (!error.isEmpty()) {
            text += QStringLiteral(" | Error: ") + error;
        }
        return text;
    }
#endif
    return QStringLiteral("Viewport: %1x%2 | DPR: %3")
        .arg(width())
        .arg(height())
        .arg(devicePixelRatioF(), 0, 'f', 2);
}

int RenderViewport::vertexCount() const { return glWidget_->vertexCount(); }
int RenderViewport::triangleCount() const { return glWidget_->triangleCount(); }
float RenderViewport::currentFps() const
{
#if defined(FERENDER_HAS_METAL_RHI)
    if (activeBackendKind_ == RenderBackendKind::Metal && metalViewport_) {
        return metalViewport_->currentFps();
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (activeBackendKind_ == RenderBackendKind::Vulkan && vulkanViewport_) {
        return vulkanViewport_->currentFps();
    }
#endif
    return glWidget_->currentFps();
}

float RenderViewport::frameTimeMs() const
{
#if defined(FERENDER_HAS_METAL_RHI)
    if (activeBackendKind_ == RenderBackendKind::Metal && metalViewport_) {
        return metalViewport_->frameTimeMs();
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (activeBackendKind_ == RenderBackendKind::Vulkan && vulkanViewport_) {
        return vulkanViewport_->frameTimeMs();
    }
#endif
    return glWidget_->frameTimeMs();
}

void RenderViewport::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateColorBarOverlay();
}

void RenderViewport::setPartVisibility(int partIndex, bool visible)
{
    partVisibility_[partIndex] = visible;
    glWidget_->setPartVisibility(partIndex, visible);
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->setPartVisibility(partIndex, visible);
    }
#endif
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->setPartVisibility(partIndex, visible);
    }
#endif
}
void RenderViewport::highlightParts(const std::vector<int>& partIndices)
{
    currentPickMode_ = PickMode::Part;
    glWidget_->highlightParts(partIndices);
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->highlightParts(partIndices);
    }
#endif
#if defined(FERENDER_HAS_METAL_RHI)
    if (metalViewport_) {
        metalViewport_->selectByIds(PickMode::Part, partIndices);
    }
#endif
}
void RenderViewport::refresh()
{
#if defined(FERENDER_HAS_METAL_RHI)
    if (activeBackendKind_ == RenderBackendKind::Metal && metalViewport_) {
        metalViewport_->renderFrame();
        return;
    }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (activeBackendKind_ == RenderBackendKind::Vulkan && vulkanViewport_) {
        vulkanViewport_->renderFrame();
        return;
    }
#endif
    glWidget_->update();
}

void RenderViewport::updateColorBarOverlay()
{
    if (!colorBarOverlay_) {
        return;
    }

    colorBarOverlay_->resize(size());
    colorBarOverlay_->raise();
    const bool showOverlay =
        colorBarVisible_ &&
        ((activeBackendKind_ == RenderBackendKind::Vulkan &&
          vulkanViewport_ != nullptr &&
          vulkanViewport_->isVisible()) ||
         (activeBackendKind_ == RenderBackendKind::Metal &&
          metalViewport_ != nullptr &&
          metalViewport_->isVisible()));
    colorBarOverlay_->setVisible(showOverlay);
    if (showOverlay) {
        colorBarOverlay_->setRange(colorBarMin_, colorBarMax_);
        colorBarOverlay_->setTitle(colorBarTitle_);
        colorBarOverlay_->setIdLabel(colorBarIdLabel_);
        colorBarOverlay_->setTextColor(colorBarTextColor_);
        if (hasColorBarExtremes_) {
            colorBarOverlay_->setExtremes(colorBarMinId_,
                                          colorBarMinValue_,
                                          colorBarMaxId_,
                                          colorBarMaxValue_);
        }
    }
}

void RenderViewport::activateBackend(RenderBackendKind kind)
{
    RenderBackendKind resolved = RenderBackendKind::OpenGL;
    if (kind == RenderBackendKind::Vulkan && canUseVulkanViewport()) {
        resolved = RenderBackendKind::Vulkan;
    } else if (kind == RenderBackendKind::Metal && canUseMetalViewport()) {
        resolved = RenderBackendKind::Metal;
    }
    activeBackendKind_ = resolved;

    if (resolved == RenderBackendKind::Metal) {
#if defined(FERENDER_HAS_METAL_RHI)
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
        if (vulkanViewport_) {
            vulkanViewport_->stopRendering();
            vulkanViewport_->hide();
        }
#endif
        if (!metalViewport_) {
            metalViewport_ = new MetalViewport(this);
            layout()->addWidget(metalViewport_);
            connect(metalViewport_, &MetalViewport::initialized,
                    this, &RenderViewport::renderInitialized);
            connect(metalViewport_, &MetalViewport::selectionChanged,
                    this, &RenderViewport::selectionChanged);
            connect(metalViewport_, &MetalViewport::partsPicked,
                    this, &RenderViewport::partsPicked);
            connect(metalViewport_, &MetalViewport::contextMenuRequested,
                    this, &RenderViewport::contextMenuRequested);
        }
        if (hasCurrentMesh_) {
            metalViewport_->setMesh(currentMesh_);
        }
        metalViewport_->setBackgroundGradient(backgroundTopColor_, backgroundBottomColor_);
        metalViewport_->setViewportGridVisible(viewportGridVisible_);
        metalViewport_->setObjectColor(objectColor_);
        metalViewport_->setModelDisplayMode(displayMode_);
        metalViewport_->setOverlayMesh(overlayMesh_);
        metalViewport_->setOverlayVisible(overlayVisible_);
        if (!sliceLineVertices_.empty()) {
            metalViewport_->setSliceLines(sliceLineVertices_);
        } else {
            metalViewport_->clearSliceLines();
        }
        if (isoSurfaceVisible_) {
            metalViewport_->setIsoSurfaceMesh(isoSurfaceMesh_);
        } else {
            metalViewport_->clearIsoSurface();
        }
        if (clipPreviewVisible_) {
            metalViewport_->setClipPlanePreview(clipPreviewBbMin_,
                                                clipPreviewBbMax_,
                                                clipPreviewOrigin_,
                                                clipPreviewNormal_);
        } else {
            metalViewport_->clearClipPlanePreview();
        }
        metalViewport_->setUseVertexColor(useVertexColor_);
        if (useVertexColor_ && !vertexScalars_.empty()) {
            metalViewport_->setVertexScalars(vertexScalars_, scalarMin_, scalarMax_, numBands_);
        } else if (useVertexColor_ && !vertexColors_.empty()) {
            metalViewport_->setVertexColors(vertexColors_);
        }
        metalViewport_->setPickMode(currentPickMode_);
        metalViewport_->setShowLabels(showLabels_);
        if (!triangleToElement_.empty()) {
            metalViewport_->setTriangleToElementMap(triangleToElement_);
        }
        if (!vertexToNode_.empty()) {
            metalViewport_->setVertexToNodeMap(vertexToNode_);
        }
        if (!triangleToPart_.empty()) {
            metalViewport_->setTriangleToPartMap(triangleToPart_);
        }
        if (!edgeToPart_.empty()) {
            metalViewport_->setEdgeToPartMap(edgeToPart_);
        }
        for (const auto& [partIndex, visible] : partVisibility_) {
            metalViewport_->setPartVisibility(partIndex, visible);
        }
        if (hasModelFit_) {
            metalViewport_->fitToModel(modelCenter_, modelSize_);
        }
        if (hasStandardView_) {
            metalViewport_->setStandardView(standardView_);
        }
        glWidget_->hide();
        metalViewport_->show();
        metalViewport_->startRendering();
        if (!metalViewport_->backendInfo().renderer.isEmpty()) {
            emit renderInitialized();
        }
#endif
    } else if (resolved == RenderBackendKind::Vulkan) {
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
        if (metalViewport_) {
            metalViewport_->stopRendering();
            metalViewport_->hide();
        }
        if (!vulkanViewport_) {
            vulkanViewport_ = new VulkanViewport(this);
            layout()->addWidget(vulkanViewport_);
            connect(vulkanViewport_, &VulkanViewport::initialized,
                    this, &RenderViewport::renderInitialized);
            connect(vulkanViewport_, &VulkanViewport::selectionChanged,
                    this, &RenderViewport::selectionChanged);
            connect(vulkanViewport_, &VulkanViewport::partsPicked,
                    this, &RenderViewport::partsPicked);
            connect(vulkanViewport_, &VulkanViewport::contextMenuRequested,
                    this, &RenderViewport::contextMenuRequested);
        }
        if (hasCurrentMesh_) {
            vulkanViewport_->setMesh(currentMesh_);
        }
        vulkanViewport_->setBackgroundGradient(backgroundTopColor_, backgroundBottomColor_);
        vulkanViewport_->setViewportGridVisible(viewportGridVisible_);
        vulkanViewport_->setPickMode(currentPickMode_);
        vulkanViewport_->setShowLabels(showLabels_);
        vulkanViewport_->setObjectColor(objectColor_);
        vulkanViewport_->setModelDisplayMode(displayMode_);
        vulkanViewport_->setOverlayMesh(overlayMesh_);
        vulkanViewport_->setOverlayVisible(overlayVisible_);
        if (!sliceLineVertices_.empty()) {
            vulkanViewport_->setSliceLines(sliceLineVertices_);
        } else {
            vulkanViewport_->clearSliceLines();
        }
        if (isoSurfaceVisible_) {
            vulkanViewport_->setIsoSurfaceMesh(isoSurfaceMesh_);
        } else {
            vulkanViewport_->clearIsoSurface();
        }
        if (clipPreviewVisible_) {
            vulkanViewport_->setClipPlanePreview(clipPreviewBbMin_,
                                                 clipPreviewBbMax_,
                                                 clipPreviewOrigin_,
                                                 clipPreviewNormal_);
        } else {
            vulkanViewport_->clearClipPlanePreview();
        }
        vulkanViewport_->setUseVertexColor(useVertexColor_);
        if (useVertexColor_ && !vertexScalars_.empty()) {
            vulkanViewport_->setVertexScalars(vertexScalars_, scalarMin_, scalarMax_, numBands_);
        } else if (useVertexColor_ && !vertexColors_.empty()) {
            vulkanViewport_->setVertexColors(vertexColors_);
        }
        if (!triangleToElement_.empty()) {
            vulkanViewport_->setTriangleToElementMap(triangleToElement_);
        }
        if (!vertexToNode_.empty()) {
            vulkanViewport_->setVertexToNodeMap(vertexToNode_);
        }
        if (!triangleToPart_.empty()) {
            vulkanViewport_->setTriangleToPartMap(triangleToPart_);
        }
        if (!edgeToPart_.empty()) {
            vulkanViewport_->setEdgeToPartMap(edgeToPart_);
        }
        for (const auto& [partIndex, visible] : partVisibility_) {
            vulkanViewport_->setPartVisibility(partIndex, visible);
        }
        if (hasModelFit_) {
            vulkanViewport_->fitToModel(modelCenter_, modelSize_);
        }
        if (hasStandardView_) {
            vulkanViewport_->setStandardView(standardView_);
        }
        glWidget_->hide();
        vulkanViewport_->show();
        vulkanViewport_->startRendering();
        if (!vulkanViewport_->backendInfo().renderer.isEmpty()) {
            emit renderInitialized();
        }
#endif
    } else {
#if defined(FERENDER_HAS_METAL_RHI)
        if (metalViewport_) {
            metalViewport_->stopRendering();
            metalViewport_->hide();
        }
#endif
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
        if (vulkanViewport_) {
            vulkanViewport_->stopRendering();
            vulkanViewport_->hide();
        }
#endif
        glWidget_->setPreferredRenderBackend(RenderBackendKind::OpenGL);
        glWidget_->show();
        glWidget_->update();
        if (!glWidget_->glRenderer().isEmpty()) {
            emit renderInitialized();
        }
    }

    updateColorBarOverlay();
}

bool RenderViewport::canUseVulkanViewport() const
{
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    return isRenderBackendAvailable(RenderBackendKind::Vulkan);
#else
    return false;
#endif
}

bool RenderViewport::canUseMetalViewport() const
{
#if defined(FERENDER_HAS_METAL_RHI)
    return isRenderBackendAvailable(RenderBackendKind::Metal);
#else
    return false;
#endif
}
