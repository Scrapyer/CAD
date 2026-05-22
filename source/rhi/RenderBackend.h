#pragma once

#include "ferender_export.h"

#include <QMatrix3x3>
#include <QMatrix4x4>
#include <QString>
#include <QVector3D>

/**
 * @brief 渲染后端硬件/驱动信息。
 */
struct FERENDER_EXPORT RenderBackendInfo {
    QString renderer;
    QString version;
    QString shadingLanguageVersion;
    QString vendor;
};

enum class RenderBackendKind {
    OpenGL,
    Vulkan,
    Metal
};

enum class ModelDisplayMode {
    Solid,
    Wireframe,
    SolidWireframe,
    Points
};

struct PickDrawItem {
    int startIndex = 0;
    int indexCount = 0;
    float color[3] = {0.0f, 0.0f, 0.0f};
};

enum class ScenePrimitive {
    Triangles,
    Lines,
    Points
};

enum class SceneDrawKind {
    Arrays,
    Elements
};

enum class ScenePolygonMode {
    Fill,
    Line
};

struct SceneFrameUniforms {
    QMatrix4x4 mvp;
    QMatrix4x4 model;
    QMatrix3x3 normalMatrix;
    QVector3D lightDir;
    QVector3D viewPos;
    bool contourMode = false;
    float scalarMin = 0.0f;
    float scalarMax = 1.0f;
    int numBands = 16;
    float surfaceAlpha = 1.0f;
    int triPartTextureUnit = 0;
};

struct SceneDrawUniforms {
    QVector3D color;
    bool wireframe = false;
    bool useVertexColor = false;
    float wireAlpha = 1.0f;
    bool overrideContourMode = false;
    bool contourMode = false;
    bool overrideSurfaceAlpha = false;
    float surfaceAlpha = 1.0f;
};

struct ScenePassState {
    bool applyBlend = false;
    bool blendEnabled = false;
    bool restoredBlendEnabled = false;

    bool applyDepthTest = false;
    bool depthTestEnabled = true;
    bool restoredDepthTestEnabled = true;

    bool applyDepthWrite = false;
    bool depthWriteEnabled = true;
    bool restoredDepthWriteEnabled = true;

    bool applyCullFace = false;
    bool cullFaceEnabled = true;
    bool restoredCullFaceEnabled = true;

    bool applyLineWidth = false;
    float lineWidth = 1.0f;
    float restoredLineWidth = 1.0f;

    bool applyPointSize = false;
    float pointSize = 1.0f;
    float restoredPointSize = 1.0f;

    bool applyPolygonMode = false;
    ScenePolygonMode polygonMode = ScenePolygonMode::Fill;
    ScenePolygonMode restoredPolygonMode = ScenePolygonMode::Fill;

    bool applyPolygonOffsetFill = false;
    bool polygonOffsetFillEnabled = false;
    bool restoredPolygonOffsetFillEnabled = false;
    float polygonOffsetFactor = 1.0f;
    float polygonOffsetUnits = 1.0f;
};

/**
 * @brief 渲染后端接口。
 *
 * GLWidget 负责 Qt Widget 生命周期和交互；后端负责具体图形 API。
 * 当前 OpenGL 后端承担实际绘制；Vulkan 后端先作为传统图形管线的 RHI 落脚点逐步接入；
 * Metal 枚举先用于配置预留，后端接入后再标记可用。
 */
class FERENDER_EXPORT IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    /** @brief 在图形上下文 current 后初始化后端。 */
    virtual void initialize() = 0;

    /** @brief 返回后端硬件/驱动信息。 */
    virtual const RenderBackendInfo& info() const = 0;
};
