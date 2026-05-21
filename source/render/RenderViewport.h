#pragma once

#include "FEPickResult.h"
#include "Geometry.h"
#include "RenderBackend.h"
#include "ferender_export.h"

#include <QString>
#include <QWidget>
#include <QColor>

#include <glm/glm.hpp>

#include <memory>
#include <unordered_map>
#include <vector>

struct Theme;
class ColorBarOverlay;
class GLWidget;
class QResizeEvent;
class VulkanViewport;

/**
 * @brief 渲染视口宿主。
 *
 * MainWindow 只依赖该宿主层；OpenGL/Vulkan 等具体视口由这里按全局 RHI 设置分发。
 * OpenGL 路径承载完整交互功能，macOS Vulkan 路径承载主网格绘制和基础点选。
 */
class FERENDER_EXPORT RenderViewport : public QWidget {
    Q_OBJECT

public:
    explicit RenderViewport(QWidget* parent = nullptr);
    ~RenderViewport() override;

    void setMesh(const Mesh& mesh);
    void setVertexColors(const std::vector<float>& colors);
    void setObjectColor(const glm::vec3& c);
    void fitToModel(const glm::vec3& center, float size);
    void applyTheme(const Theme& theme);

    void setColorBarVisible(bool visible);
    void setColorBarRange(float min, float max);
    void setColorBarTitle(const QString& title);
    void setColorBarExtremes(int minId, float minVal, int maxId, float maxVal);
    void setColorBarIdLabel(const QString& label);

    void setTriangleToElementMap(const std::vector<int>& map);
    void setTriangleToFaceMap(const std::vector<int>& map);
    void setVertexToNodeMap(const std::vector<int>& map);
    void setPickMode(PickMode mode);
    void setShowLabels(bool show);
    void selectByIds(PickMode mode, const std::vector<int>& ids);

    void setOverlayMesh(const Mesh& mesh);
    void setOverlayVisible(bool visible);
    void setUseVertexColor(bool use);
    void setSliceLines(const std::vector<float>& lineVertices);
    void clearSliceLines();
    void setIsoSurfaceMesh(const Mesh& mesh);
    void clearIsoSurface();
    void setClipPlanePreview(const glm::vec3& bbMin,
                             const glm::vec3& bbMax,
                             const glm::vec3& origin,
                             const glm::vec3& normal);
    void clearClipPlanePreview();
    void setVertexScalars(const std::vector<float>& scalars, float minVal, float maxVal, int numBands);
    void setTriangleToPartMap(const std::vector<int>& map);
    void setEdgeToPartMap(const std::vector<int>& map);
    const std::vector<glm::vec3>& partColors() const;

    void setPreferredRenderBackend(RenderBackendKind kind);
    RenderBackendKind requestedRenderBackendKind() const;
    RenderBackendKind activeRenderBackendKind() const;

    QString glRenderer() const;
    QString glVersion() const;
    QString glslVersion() const;
    QString gpuVendor() const;
    int vertexCount() const;
    int triangleCount() const;
    float currentFps() const;
    float frameTimeMs() const;

public slots:
    void setPartVisibility(int partIndex, bool visible);
    void highlightParts(const std::vector<int>& partIndices);
    void refresh();

signals:
    void renderInitialized();
    void selectionChanged(PickMode mode, int count, const std::vector<int>& ids);
    void partsPicked(const std::vector<int>& partIndices);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void activateBackend(RenderBackendKind kind);
    bool canUseVulkanViewport() const;
    void updateColorBarOverlay();

    GLWidget* glWidget_ = nullptr;
    ColorBarOverlay* colorBarOverlay_ = nullptr;
    VulkanViewport* vulkanViewport_ = nullptr;
    RenderBackendKind requestedBackendKind_ = RenderBackendKind::OpenGL;
    RenderBackendKind activeBackendKind_ = RenderBackendKind::OpenGL;
    PickMode currentPickMode_ = PickMode::Node;
    Mesh currentMesh_;
    bool hasCurrentMesh_ = false;
    glm::vec3 modelCenter_{0.0f, 0.0f, 0.0f};
    glm::vec3 objectColor_{0.48f, 0.72f, 0.76f};
    float modelSize_ = 1.0f;
    bool hasModelFit_ = false;
    bool colorBarVisible_ = false;
    float colorBarMin_ = 0.0f;
    float colorBarMax_ = 1.0f;
    QString colorBarTitle_ = QStringLiteral("Result");
    bool hasColorBarExtremes_ = false;
    int colorBarMinId_ = -1;
    int colorBarMaxId_ = -1;
    float colorBarMinValue_ = 0.0f;
    float colorBarMaxValue_ = 0.0f;
    QString colorBarIdLabel_ = QStringLiteral("ID");
    QColor colorBarTextColor_{30, 30, 30};
    bool useVertexColor_ = false;
    Mesh overlayMesh_;
    bool overlayVisible_ = false;
    std::vector<float> sliceLineVertices_;
    Mesh isoSurfaceMesh_;
    bool isoSurfaceVisible_ = false;
    bool clipPreviewVisible_ = false;
    glm::vec3 clipPreviewBbMin_{0.0f};
    glm::vec3 clipPreviewBbMax_{0.0f};
    glm::vec3 clipPreviewOrigin_{0.0f};
    glm::vec3 clipPreviewNormal_{0.0f, 0.0f, 1.0f};
    std::vector<float> vertexColors_;
    std::vector<float> vertexScalars_;
    float scalarMin_ = 0.0f;
    float scalarMax_ = 1.0f;
    int numBands_ = 10;
    std::vector<int> triangleToElement_;
    std::vector<int> vertexToNode_;
    std::vector<int> triangleToPart_;
    std::vector<int> edgeToPart_;
    std::unordered_map<int, bool> partVisibility_;
};
