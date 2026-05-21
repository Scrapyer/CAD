#include "RenderViewport.h"

#include "ColorBarOverlay.h"
#include "GLWidget.h"
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
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->setObjectColor(c);
    }
#endif
}
void RenderViewport::fitToModel(const glm::vec3& center, float size)
{
    modelCenter_ = center;
    modelSize_ = size;
    hasModelFit_ = true;
    glWidget_->fitToModel(center, size);
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->fitToModel(center, size);
    }
#endif
}
void RenderViewport::applyTheme(const Theme& theme)
{
    glWidget_->applyTheme(theme);
    colorBarTextColor_ = QColor(theme.barTextR, theme.barTextG, theme.barTextB);
    colorBarOverlay_->setTextColor(colorBarTextColor_);
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
}
void RenderViewport::setShowLabels(bool show) { glWidget_->setShowLabels(show); }
void RenderViewport::selectByIds(PickMode mode, const std::vector<int>& ids)
{
    currentPickMode_ = mode;
    glWidget_->selectByIds(mode, ids);
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (vulkanViewport_) {
        vulkanViewport_->selectByIds(mode, ids);
    }
#endif
}
void RenderViewport::setOverlayMesh(const Mesh& mesh)
{
    overlayMesh_ = mesh;
    glWidget_->setOverlayMesh(mesh);
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
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (activeBackendKind_ == RenderBackendKind::Vulkan && vulkanViewport_) {
        return vulkanViewport_->backendInfo().renderer;
    }
#endif
    return glWidget_->glRenderer();
}

QString RenderViewport::glVersion() const
{
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (activeBackendKind_ == RenderBackendKind::Vulkan && vulkanViewport_) {
        return vulkanViewport_->backendInfo().version;
    }
#endif
    return glWidget_->glVersion();
}

QString RenderViewport::glslVersion() const
{
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (activeBackendKind_ == RenderBackendKind::Vulkan && vulkanViewport_) {
        return vulkanViewport_->backendInfo().shadingLanguageVersion;
    }
#endif
    return glWidget_->glslVersion();
}

QString RenderViewport::gpuVendor() const
{
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (activeBackendKind_ == RenderBackendKind::Vulkan && vulkanViewport_) {
        return vulkanViewport_->backendInfo().vendor;
    }
#endif
    return glWidget_->gpuVendor();
}

int RenderViewport::vertexCount() const { return glWidget_->vertexCount(); }
int RenderViewport::triangleCount() const { return glWidget_->triangleCount(); }
float RenderViewport::currentFps() const
{
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    if (activeBackendKind_ == RenderBackendKind::Vulkan && vulkanViewport_) {
        return vulkanViewport_->currentFps();
    }
#endif
    return glWidget_->currentFps();
}

float RenderViewport::frameTimeMs() const
{
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
}
void RenderViewport::refresh()
{
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
        activeBackendKind_ == RenderBackendKind::Vulkan &&
        vulkanViewport_ != nullptr &&
        vulkanViewport_->isVisible();
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
    }

    if (resolved == RenderBackendKind::Vulkan) {
#if defined(FERENDER_HAS_VULKAN_RHI) && defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
        if (!vulkanViewport_) {
            vulkanViewport_ = new VulkanViewport(this);
            layout()->addWidget(vulkanViewport_);
            connect(vulkanViewport_, &VulkanViewport::initialized,
                    this, &RenderViewport::renderInitialized);
            connect(vulkanViewport_, &VulkanViewport::selectionChanged,
                    this, &RenderViewport::selectionChanged);
            connect(vulkanViewport_, &VulkanViewport::partsPicked,
                    this, &RenderViewport::partsPicked);
        }
        if (hasCurrentMesh_) {
            vulkanViewport_->setMesh(currentMesh_);
        }
        vulkanViewport_->setPickMode(currentPickMode_);
        vulkanViewport_->setObjectColor(objectColor_);
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
        glWidget_->hide();
        vulkanViewport_->show();
        vulkanViewport_->startRendering();
        if (!vulkanViewport_->backendInfo().renderer.isEmpty()) {
            emit renderInitialized();
        }
#endif
    } else {
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

    activeBackendKind_ = resolved;
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
