/**
 * @file GLWidget.cpp
 * @brief OpenGL 渲染窗口组件实现
 */

#include "GLWidget.h"
#include "Theme.h"
#include "ColorBarOverlay.h"
#include "OpenGLRenderBackend.h"
#include "RenderBackendFactory.h"
#include "RenderSettings.h"
#include "ScreenSpacePicking.h"
#include "ViewportGridMetrics.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QFontMetrics>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <map>

// ── 部件颜色调色板（Catppuccin Mocha）──
static const glm::vec3 kPartPalette[] = {
    {0.61f, 0.86f, 0.63f},  // green   #a6e3a1
    {0.54f, 0.71f, 0.98f},  // blue    #89b4fa
    {0.98f, 0.70f, 0.53f},  // peach   #fab387
    {0.82f, 0.62f, 0.98f},  // mauve   #cba6f7
    {0.58f, 0.89f, 0.83f},  // teal    #94e2d5
    {0.98f, 0.89f, 0.69f},  // yellow  #f9e2af
    {0.94f, 0.56f, 0.66f},  // red     #eba0ac
    {0.71f, 0.71f, 0.98f},  // lavender #b4befe
};
static const int kPartPaletteSize = static_cast<int>(sizeof(kPartPalette) / sizeof(kPartPalette[0]));

namespace {
constexpr int kAxesLabelSize = 30;
constexpr int kAxesMargin = 8;
constexpr int kAxesViewportSize = 152;
constexpr float kAxesClickPadding = 10.0f;

template <typename T>
void alignSize(std::vector<T>& arr, int targetSize, const T& fillValue) {
    if (targetSize < 0) {
        targetSize = 0;
    }
    if (static_cast<int>(arr.size()) > targetSize) {
        arr.resize(static_cast<size_t>(targetSize));
    } else if (static_cast<int>(arr.size()) < targetSize) {
        arr.resize(static_cast<size_t>(targetSize), fillValue);
    }
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
}  // namespace

static Mesh makeClipPlanePreviewMesh(const glm::vec3& bbMin,
                                     const glm::vec3& bbMax,
                                     const glm::vec3& origin,
                                     const glm::vec3& normal) {
    Mesh mesh;

    glm::vec3 span = bbMax - bbMin;
    float maxSpan = std::max({std::abs(span.x), std::abs(span.y), std::abs(span.z), 1.0f});
    glm::vec3 mn = bbMin - glm::vec3(maxSpan * 0.03f);
    glm::vec3 mx = bbMax + glm::vec3(maxSpan * 0.03f);

    int axis = 0;
    float ax = std::abs(normal.x);
    float ay = std::abs(normal.y);
    float az = std::abs(normal.z);
    if (ay > ax && ay >= az) axis = 1;
    else if (az > ax && az > ay) axis = 2;

    glm::vec3 p0, p1, p2, p3;
    if (axis == 0) {
        float x = origin.x;
        p0 = {x, mn.y, mn.z};
        p1 = {x, mx.y, mn.z};
        p2 = {x, mx.y, mx.z};
        p3 = {x, mn.y, mx.z};
    } else if (axis == 1) {
        float y = origin.y;
        p0 = {mn.x, y, mn.z};
        p1 = {mx.x, y, mn.z};
        p2 = {mx.x, y, mx.z};
        p3 = {mn.x, y, mx.z};
    } else {
        float z = origin.z;
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

// ============================================================
// 构造函数 & 公有方法
// ============================================================

GLWidget::GLWidget(QWidget* parent) : QOpenGLWidget(parent) {
    requestedBackendKind_ = RenderSettings::preferredBackend();
    activeBackendKind_ = resolveRenderBackendForWidget(requestedBackendKind_);
    renderBackend_ = createRenderBackend(activeBackendKind_);

    // 设置强焦点策略，使 widget 能接收键盘事件
    setFocusPolicy(Qt::StrongFocus);

    // 创建色标覆盖层（raster 绘制，不受 GL 状态影响）
    colorBarOverlay_ = new ColorBarOverlay(this);
}

GLWidget::~GLWidget() = default;

OpenGLRenderBackend* GLWidget::openGLBackend() const
{
    return activeBackendKind_ == RenderBackendKind::OpenGL
        ? static_cast<OpenGLRenderBackend*>(renderBackend_.get())
        : nullptr;
}

RenderBackendKind GLWidget::resolveRenderBackendForWidget(RenderBackendKind requested) const
{
    if (!isRenderBackendAvailable(requested)) {
        return RenderBackendKind::OpenGL;
    }

    // 当前 GLWidget 继承自 QOpenGLWidget，只能承载 OpenGL 上下文。
    // Vulkan/Metal 需要由 RenderViewport 提供独立平台视口宿主。
    if (requested != RenderBackendKind::OpenGL) {
        return RenderBackendKind::OpenGL;
    }

    return requested;
}

void GLWidget::setPreferredRenderBackend(RenderBackendKind kind)
{
    RenderSettings::setPreferredBackend(kind);
    requestedBackendKind_ = kind;

    const RenderBackendKind resolved = resolveRenderBackendForWidget(kind);
    if (resolved == activeBackendKind_) {
        update();
        return;
    }

    activeBackendKind_ = resolved;
    renderBackend_ = createRenderBackend(activeBackendKind_);
    needsUpload_ = true;
    update();
}

void GLWidget::setVertexColors(const std::vector<float>& colors) {
    useVertexColor_ = true;
    if (!meshResource_) return;
    auto* glBackend = openGLBackend();
    glBackend->uploadMeshColorBuffer(*meshResource_,
                                     colors.data(),
                                     static_cast<int>(colors.size() * sizeof(float)));
    update();
}

void GLWidget::setVertexScalars(const std::vector<float>& scalars, float minVal, float maxVal, int numBands) {
    useVertexColor_ = true;
    scalarMin_ = minVal;
    scalarMax_ = maxVal;
    numBands_ = numBands;
    // 先完成待上传的网格，防止 paintGL 中 uploadMesh() 覆盖标量数据
    if (needsUpload_) {
        makeCurrent();
        uploadMesh();
    }
    if (meshResource_) {
        auto* glBackend = openGLBackend();
        glBackend->uploadMeshScalarBuffer(*meshResource_,
                                          scalars.data(),
                                          static_cast<int>(scalars.size() * sizeof(float)));
    }
    update();
}

void GLWidget::setEdgeScalars(const std::vector<float>& scalars, float minVal, float maxVal, int numBands) {
    edgeScalars_ = scalars;
    useVertexColor_ = true;
    scalarMin_ = minVal;
    scalarMax_ = maxVal;
    numBands_ = numBands;
    // 先完成待上传的网格，防止 paintGL 中 uploadMesh() 覆盖标量数据
    if (needsUpload_) {
        makeCurrent();
        uploadMesh();
    }
    if (edgeResource_) {
        auto* glBackend = openGLBackend();
        glBackend->uploadEdgeScalarBuffer(*edgeResource_,
                                          edgeScalars_.data(),
                                          static_cast<int>(edgeScalars_.size() * sizeof(float)));
    }
    update();
}

void GLWidget::setSliceLines(const std::vector<float>& lineVertices) {
    sliceVertCount_ = static_cast<int>(lineVertices.size() / 3);
    if (sliceVertCount_ > 0 && sliceResource_) {
        makeCurrent();
        auto* glBackend = openGLBackend();
        glBackend->uploadLineVertices(
            *sliceResource_,
            lineVertices.data(),
            static_cast<int>(lineVertices.size() * sizeof(float)));
        doneCurrent();
    }
    update();
}

void GLWidget::clearSliceLines() {
    sliceVertCount_ = 0;
    update();
}

void GLWidget::setIsoSurfaceMesh(const Mesh& mesh) {
    isoMesh_ = mesh;
    isoIndexCount_ = static_cast<int>(mesh.indices.size());
    isoNeedsUpload_ = true;
    update();
}

void GLWidget::clearIsoSurface() {
    isoMesh_.vertices.clear();
    isoMesh_.indices.clear();
    isoIndexCount_ = 0;
    update();
}

void GLWidget::setClipPlanePreview(const glm::vec3& bbMin,
                                   const glm::vec3& bbMax,
                                   const glm::vec3& origin,
                                   const glm::vec3& normal) {
    clipPreviewMesh_ = makeClipPlanePreviewMesh(bbMin, bbMax, origin, normal);
    clipPreviewIndexCount_ = static_cast<int>(clipPreviewMesh_.indices.size());
    clipPreviewEdgeVertCount_ = static_cast<int>(clipPreviewMesh_.edgeVertices.size() / 3);
    clipPreviewVisible_ = clipPreviewIndexCount_ > 0;
    clipPreviewNeedsUpload_ = true;
    update();
}

void GLWidget::clearClipPlanePreview() {
    clipPreviewMesh_.vertices.clear();
    clipPreviewMesh_.indices.clear();
    clipPreviewMesh_.edgeVertices.clear();
    clipPreviewIndexCount_ = 0;
    clipPreviewEdgeVertCount_ = 0;
    clipPreviewVisible_ = false;
    clipPreviewNeedsUpload_ = false;
    update();
}

void GLWidget::setOverlayMesh(const Mesh& mesh) {
    overlayMesh_ = mesh;
    overlayNeedsUpload_ = true;
    update();
}

void GLWidget::setOverlayVisible(bool visible) {
    overlayVisible_ = visible;
    update();
}

void GLWidget::setUseVertexColor(bool use) {
    useVertexColor_ = use;
    if (!use) {
        // 退出云图模式：重新把部件索引写入主网格标量缓冲
        needsColorUpload_ = true;
    }
}

void GLWidget::setMesh(const Mesh& mesh) {
    mesh_ = mesh;
    allTriIndices_ = mesh.indices;
    allEdgeIndices_ = mesh.edgeIndices;
    activeEdgeIndexCount_ = static_cast<int>(mesh.edgeIndices.size());
    triToElem_.clear();
    triToPart_.clear();
    edgeToPart_.clear();
    vertexToNode_.clear();
    selection_.clear();
    partVisibility_.clear();
    partColors_.clear();
    edgeScalars_.clear();
    useVertexColor_ = false;
    partTriangles_.clear();
    partElementIds_.clear();
    elemToPart_.clear();
    triPartDirty_ = true;
    edgeAdjDirty_ = true;
    activeIndexCount_ = static_cast<int>(mesh.indices.size());
    partVisibilityDirty_ = false;
    edgeVisibilityDirty_ = false;
    needsColorUpload_ = false;
    needsUpload_ = true;
    partEdgeCacheValid_ = false;
    selectionDirty_ = true;
    selEdgeVertCount_ = 0;
    silhouetteDirty_ = true;
    emit selectionChanged(pickMode_, 0, {});
    if (pickMode_ == PickMode::Part) {
        emit partsPicked({});
    }
    update();
}

void GLWidget::setObjectColor(const glm::vec3& c) { color_ = c; update(); }

void GLWidget::setModelDisplayMode(ModelDisplayMode mode)
{
    if (displayMode_ == mode) {
        return;
    }
    displayMode_ = mode;
    update();
}

void GLWidget::setViewportGridVisible(bool visible)
{
    if (viewportGridVisible_ == visible) {
        return;
    }
    viewportGridVisible_ = visible;
    update();
}

void GLWidget::fitToModel(const glm::vec3& center, float size) {
    modelSize_ = std::max(size, 1.0e-4f);
    cam_.target = center;
    cam_.distance = modelSize_ * 1.5f;
    cam_.maxDist = modelSize_ * 10.0f;
    cam_.minDist = modelSize_ * 0.05f;
    cam_.panSensitivity = 0.001f;
    cam_.yaw = 30.0f;
    cam_.pitch = 25.0f;
    update();
}

void GLWidget::setStandardView(StandardView view)
{
    applyStandardViewToCamera(cam_, view);
    if (pickMode_ == PickMode::Part && selection_.hasSelection()) {
        silhouetteDirty_ = true;
    }
    update();
}

glm::mat4 GLWidget::projectionMatrix(float aspect) const
{
    const float sceneSize = std::max(modelSize_, 1.0e-4f);
    const float nearPlane = std::max(std::min(cam_.distance * 0.01f, sceneSize * 0.01f),
                                     sceneSize * 1.0e-5f);
    const float farPlane = std::max(cam_.distance + sceneSize * 2.0f,
                                    nearPlane + sceneSize * 0.1f);
    return glm::perspective(glm::radians(45.0f), aspect, nearPlane, farPlane);
}

void GLWidget::setTriangleToElementMap(const std::vector<int>& map) {
    triToElem_ = map;
    int triCount = static_cast<int>(mesh_.indices.size() / 3);
    alignSize(triToElem_, triCount, -1);
}

void GLWidget::setVertexToNodeMap(const std::vector<int>& map) {
    vertexToNode_ = map;
    int vertexCount = static_cast<int>(mesh_.vertices.size() / 6);
    alignSize(vertexToNode_, vertexCount, -1);
    edgeAdjDirty_ = true;
}

int GLWidget::vertexCount()   const { return static_cast<int>(mesh_.vertices.size() / 6); }
int GLWidget::triangleCount() const { return static_cast<int>(mesh_.indices.size() / 3); }

// ============================================================
// OpenGL 生命周期回调
// ============================================================

void GLWidget::initializeGL() {
    // 初始化 OpenGL 函数指针（Qt 的 OpenGL 函数加载机制）
    initializeOpenGLFunctions();

    // 初始化渲染后端并保存 GPU 硬件信息
    renderBackend_->initialize();
    const RenderBackendInfo& backendInfo = renderBackend_->info();
    glRenderer_ = backendInfo.renderer;
    glVersion_ = backendInfo.version;
    glslVersion_ = backendInfo.shadingLanguageVersion;
    gpuVendor_ = backendInfo.vendor;

    auto* glBackend = openGLBackend();

    // 编译并链接着色器程序
    shader_ = glBackend->createShaderProgram(this, ":/shaders/scene.vert", ":/shaders/scene.frag");

    // 创建主网格资源（VAO/VBO/IBO/颜色缓冲/标量缓冲）
    meshResource_ = std::make_unique<OpenGLMeshResource>();
    glBackend->createMeshResource(*meshResource_);

    // ── 创建 triToPart texture buffer ──
    triPartTextureBuffer_ = std::make_unique<OpenGLTextureBufferResource>();
    glBackend->createTextureBufferResource(*triPartTextureBuffer_);

    // ── 拾取着色器 ──
    pickShader_ = glBackend->createShaderProgram(this, ":/shaders/pick.vert", ":/shaders/pick.frag");

    // ── 拾取专用 VAO（避免共用 vao_ 导致顶点属性状态泄漏到 QPainter） ──
    pickVertexArray_ = std::make_unique<OpenGLVertexArrayResource>();
    glBackend->createVertexArray(*pickVertexArray_);

    // ── 边线资源（FE 模式专用）──
    edgeResource_ = std::make_unique<OpenGLEdgeResource>();
    glBackend->createEdgeResource(*edgeResource_);

    // ── 选中高亮边线资源 ──
    selectionEdgeResource_ = std::make_unique<OpenGLLineResource>();
    glBackend->createLineResource(*selectionEdgeResource_);
    overlayResource_ = std::make_unique<OpenGLLineResource>();
    glBackend->createLineResource(*overlayResource_);
    sliceResource_ = std::make_unique<OpenGLLineResource>();
    glBackend->createLineResource(*sliceResource_);
    isoResource_ = std::make_unique<OpenGLMeshResource>();
    glBackend->createMeshResource(*isoResource_);
    clipPreviewResource_ = std::make_unique<OpenGLMeshResource>();
    glBackend->createMeshResource(*clipPreviewResource_);
    clipPreviewEdgeResource_ = std::make_unique<OpenGLLineResource>();
    glBackend->createLineResource(*clipPreviewEdgeResource_);

    // ── 渐变背景着色器 + VAO/VBO（一次性创建）──
    bgShader_ = glBackend->createShaderProgram(this, ":/shaders/background.vert", ":/shaders/background.frag");

    {
        // 全屏四边形：pos(2) + color(3)，使用当前主题颜色
        float bgData[] = {
            -1, -1,  bgBotColor_[0], bgBotColor_[1], bgBotColor_[2],
             1, -1,  bgBotColor_[0], bgBotColor_[1], bgBotColor_[2],
             1,  1,  bgTopColor_[0], bgTopColor_[1], bgTopColor_[2],
            -1, -1,  bgBotColor_[0], bgBotColor_[1], bgBotColor_[2],
             1,  1,  bgTopColor_[0], bgTopColor_[1], bgTopColor_[2],
            -1,  1,  bgTopColor_[0], bgTopColor_[1], bgTopColor_[2],
        };
        backgroundGeometry_ = std::make_unique<OpenGLPositionColorGeometry>();
        glBackend->uploadPositionColorGeometry(*backgroundGeometry_, bgData, sizeof(bgData), 2);
    }

    // ── 坐标轴指示器着色器 ──
    axesShader_ = glBackend->createShaderProgram(this, ":/shaders/axes.vert", ":/shaders/axes.frag");

    // ── 生成坐标轴几何数据（实心轴杆圆柱 + 箭头圆锥 + 中心球）──
    // 每顶点 6 float: pos(3) + color(3)
    std::vector<float> lineVerts;   // GL_LINES (unused, kept for count)
    std::vector<float> triVerts;    // GL_TRIANGLES

    const int segs = 24;            // 圆柱/圆锥分段数
    const float shaftLen = 0.70f;   // 轴杆长度
    const float shaftR = 0.028f;    // 轴杆圆柱半径
    const float coneBase = 0.10f;   // 圆锥底面半径
    const float coneLen = 0.30f;    // 圆锥长度
    const float ballR = 0.065f;     // 中心球半径

    struct Axis { glm::vec3 dir; glm::vec3 up; glm::vec3 color; };
    Axis axes[] = {
        {{1,0,0}, {0,1,0}, {0.95f, 0.30f, 0.30f}},  // X 红
        {{0,1,0}, {0,0,1}, {0.35f, 0.90f, 0.35f}},   // Y 绿
        {{0,0,1}, {1,0,0}, {0.35f, 0.55f, 1.00f}},    // Z 蓝
    };

    auto pushTri = [&](glm::vec3 p, glm::vec3 c) {
        triVerts.push_back(p.x); triVerts.push_back(p.y); triVerts.push_back(p.z);
        triVerts.push_back(c.x); triVerts.push_back(c.y); triVerts.push_back(c.z);
    };

    const float PI = 3.14159265f;

    for (auto& a : axes) {
        glm::vec3 right = glm::normalize(glm::cross(a.dir, a.up));
        glm::vec3 up2 = glm::normalize(glm::cross(right, a.dir));

        // ── 实心轴杆（圆柱） ──
        for (int i = 0; i < segs; ++i) {
            float a0 = 2.0f * PI * i / segs;
            float a1 = 2.0f * PI * (i + 1) / segs;
            glm::vec3 offset0 = (right * cosf(a0) + up2 * sinf(a0)) * shaftR;
            glm::vec3 offset1 = (right * cosf(a1) + up2 * sinf(a1)) * shaftR;
            glm::vec3 b0 = offset0;                       // 底圆点
            glm::vec3 b1 = offset1;
            glm::vec3 t0 = a.dir * shaftLen + offset0;    // 顶圆点
            glm::vec3 t1 = a.dir * shaftLen + offset1;
            glm::vec3 cyl = a.color * 0.85f;
            // 两个三角形组成一个矩形面
            pushTri(b0, cyl); pushTri(t0, cyl); pushTri(t1, cyl);
            pushTri(b0, cyl); pushTri(t1, cyl); pushTri(b1, cyl);
        }

        // ── 箭头圆锥 ──
        glm::vec3 tip = a.dir * (shaftLen + coneLen);
        for (int i = 0; i < segs; ++i) {
            float a0 = 2.0f * PI * i / segs;
            float a1 = 2.0f * PI * (i + 1) / segs;
            glm::vec3 cb0 = a.dir * shaftLen + (right * cosf(a0) + up2 * sinf(a0)) * coneBase;
            glm::vec3 cb1 = a.dir * shaftLen + (right * cosf(a1) + up2 * sinf(a1)) * coneBase;
            // 侧面
            pushTri(tip, a.color);
            pushTri(cb0, a.color * 0.75f);
            pushTri(cb1, a.color * 0.75f);
            // 底面
            pushTri(a.dir * shaftLen, a.color * 0.55f);
            pushTri(cb1, a.color * 0.55f);
            pushTri(cb0, a.color * 0.55f);
        }
    }

    // ── 中心球（细分八面体 → 更圆润） ──
    glm::vec3 ballColor(0.82f, 0.82f, 0.85f);
    // 用 UV 球生成
    const int ballRings = 8;
    const int ballSectors = 12;
    for (int r = 0; r < ballRings; ++r) {
        float phi0 = PI * r / ballRings - PI / 2.0f;
        float phi1 = PI * (r + 1) / ballRings - PI / 2.0f;
        for (int s = 0; s < ballSectors; ++s) {
            float theta0 = 2.0f * PI * s / ballSectors;
            float theta1 = 2.0f * PI * (s + 1) / ballSectors;

            glm::vec3 p00(cosf(phi0) * cosf(theta0), sinf(phi0), cosf(phi0) * sinf(theta0));
            glm::vec3 p10(cosf(phi1) * cosf(theta0), sinf(phi1), cosf(phi1) * sinf(theta0));
            glm::vec3 p01(cosf(phi0) * cosf(theta1), sinf(phi0), cosf(phi0) * sinf(theta1));
            glm::vec3 p11(cosf(phi1) * cosf(theta1), sinf(phi1), cosf(phi1) * sinf(theta1));

            p00 *= ballR; p10 *= ballR; p01 *= ballR; p11 *= ballR;

            pushTri(p00, ballColor); pushTri(p10, ballColor); pushTri(p11, ballColor);
            pushTri(p00, ballColor); pushTri(p11, ballColor); pushTri(p01, ballColor);
        }
    }

    // 合并到一个 VBO: [lines | triangles]
    axesLineCount_ = static_cast<int>(lineVerts.size() / 6);
    axesTriCount_ = static_cast<int>(triVerts.size() / 6);

    std::vector<float> allData;
    allData.insert(allData.end(), lineVerts.begin(), lineVerts.end());
    allData.insert(allData.end(), triVerts.begin(), triVerts.end());

    axesGeometry_ = std::make_unique<OpenGLPositionColorGeometry>();
    glBackend->uploadPositionColorGeometry(*axesGeometry_,
                                           allData.data(),
                                           static_cast<int>(allData.size() * sizeof(float)),
                                           3);

    glBackend->initializeDefaultState();

    // 启动 FPS 计时器
    fpsTimer_.start();

    // 上传初始网格数据到 GPU
    uploadMesh();

    // 初始状态不显示色标（加载结果并应用后才显示）
    colorBarVisible_ = false;

    // 通知外部 GL 已初始化（MonitorPanel 会在此时读取硬件信息）
    emit glInitialized();
}

void GLWidget::paintGL() {
    switch (activeBackendKind_) {
    case RenderBackendKind::OpenGL:
    default:
        paintOpenGLFrame();
        break;
    }
}

void GLWidget::paintOpenGLFrame() {
    auto* glBackend = openGLBackend();
    if (!glBackend) {
        return;
    }

    // 恢复 GL 状态（QPainter 可能在上一帧末尾修改了 viewport/深度/混合等）
    glBackend->beginFrame(width(), height(), devicePixelRatio());

    processDeferredPicks();

    if (needsUpload_) uploadMesh();
    rebuildPartVisibilityIbo();
    if (needsColorUpload_) uploadColors();
    if (edgeVisibilityDirty_) rebuildEdgeIbo();

    renderBackground();

    // 无数据时只绘制坐标轴
    if (mesh_.indices.empty() && mesh_.edgeIndices.empty()) {
        drawAxesIndicator();
        return;
    }

    // ── 计算变换矩阵 ──
    float aspect = (height() > 0) ? static_cast<float>(width()) / height() : 1.0f;
    glm::mat4 projection = projectionMatrix(aspect);
    glm::mat4 model = glm::mat4(1.0f);
    glm::mat4 view = cam_.viewMatrix();
    glm::mat4 mvp = projection * view * model;
    glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(model)));

    // ── 设置着色器和公共 uniform ──
    shader_->bind();

    float nm[9];
    const float* src = glm::value_ptr(normalMat);
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r)
            nm[r * 3 + c] = src[c * 3 + r];
    glm::vec3 eyePos = cam_.eye();
    SceneFrameUniforms frameUniforms;
    frameUniforms.mvp = QMatrix4x4(glm::value_ptr(glm::transpose(mvp)));
    frameUniforms.model = QMatrix4x4(glm::value_ptr(glm::transpose(model)));
    frameUniforms.normalMatrix = QMatrix3x3(nm);
    frameUniforms.lightDir = QVector3D(-0.4f, -0.7f, -0.5f);
    frameUniforms.viewPos = QVector3D(eyePos.x, eyePos.y, eyePos.z);
    frameUniforms.contourMode = useVertexColor_ && colorBarVisible_;
    frameUniforms.scalarMin = scalarMin_;
    frameUniforms.scalarMax = scalarMax_;
    frameUniforms.numBands = numBands_;
    frameUniforms.surfaceAlpha = 1.0f;
    frameUniforms.triPartTextureUnit = 0;
    glBackend->setSceneFrameUniforms(*shader_, frameUniforms);

    // 绑定 triToPart texture buffer 到纹理单元 0
    if (triPartTextureBuffer_)
        glBackend->bindTextureBufferToUnit(*triPartTextureBuffer_, 0);

    // ── 逐步渲染 ──
    renderMainMesh();
    renderMeshEdges();
    renderMeshPoints();
    updateSelectionHighlight();
    renderOverlayMesh();
    renderClipPreview();
    renderSliceLines();
    renderIsoSurface();
    renderSelectionHighlight();

    shader_->release();

    drawAxesIndicator();
    render2DOverlays(mvp);
    updateFpsStats();
}

// ============================================================
// paintGL 渲染子步骤
// ============================================================

void GLWidget::processDeferredPicks() {
    if (pickPointPending_) {
        pickPointPending_ = false;
        pickAtPoint(pendingPickPos_, pendingPickCtrl_);
    }
    if (pickRectPending_) {
        pickRectPending_ = false;
        pickInRect(pendingPickRect_);
    }
    if (deselectPointPending_) {
        deselectPointPending_ = false;
        deselectAtPoint(pendingDeselectPos_);
    }
    if (deselectRectPending_) {
        deselectRectPending_ = false;
        deselectInRect(pendingDeselectRect_);
    }
}

void GLWidget::rebuildPartVisibilityIbo() {
    if (!partVisibilityDirty_ || allTriIndices_.empty()) return;
    partVisibilityDirty_ = false;

    std::vector<unsigned int> filtered;
    filtered.reserve(allTriIndices_.size());
    std::vector<float> filteredTriPart;
    int triCount = static_cast<int>(allTriIndices_.size() / 3);
    for (int t = 0; t < triCount; ++t) {
        int part = (t < static_cast<int>(triToPart_.size())) ? triToPart_[t] : -1;
        if (part >= 0) {
            auto it = partVisibility_.find(part);
            if (it != partVisibility_.end() && !it->second)
                continue;
        }
        filtered.push_back(allTriIndices_[t * 3]);
        filtered.push_back(allTriIndices_[t * 3 + 1]);
        filtered.push_back(allTriIndices_[t * 3 + 2]);
        filteredTriPart.push_back(static_cast<float>(part));
    }
    activeIndexCount_ = static_cast<int>(filtered.size());
    auto* glBackend = openGLBackend();
        if (meshResource_) {
            glBackend->uploadMeshIndexBuffer(
                *meshResource_,
                filtered.data(),
                static_cast<int>(filtered.size() * sizeof(unsigned int)));
        }
    if (triPartTextureBuffer_) {
        glBackend->uploadTextureBuffer(
            *triPartTextureBuffer_,
            filteredTriPart.data(),
            static_cast<int>(filteredTriPart.size() * sizeof(float)));
    }
}

void GLWidget::renderBackground() {
    if (!backgroundGeometry_) return;

    auto* glBackend = openGLBackend();
    glBackend->clearDepthBuffer();
    bgShader_->bind();
    const ViewportGridMetrics gridMetrics =
        computeViewportGridMetrics(modelSize_, cam_.distance, viewportGridVisible_);
    bgShader_->setUniformValue("uGridParams",
                               gridMetrics.alpha,
                               gridMetrics.minorStep,
                               gridMetrics.fineAlpha,
                               0.0f);
    ScenePassState passState;
    passState.applyDepthTest = true;
    passState.depthTestEnabled = false;
    passState.restoredDepthTestEnabled = true;
    glBackend->drawArraysPass(*backgroundGeometry_, ScenePrimitive::Triangles, 0, 6, passState);
    bgShader_->release();
}

void GLWidget::renderMainMesh() {
    int count = activeIndexCount_;
    const bool isoActive = isoIndexCount_ > 0;
    if (count <= 0 || isoActive) return;
    if (displayMode_ == ModelDisplayMode::Wireframe ||
        displayMode_ == ModelDisplayMode::Points) {
        return;
    }

    auto* glBackend = openGLBackend();
    SceneDrawUniforms drawUniforms;
    drawUniforms.color = QVector3D(color_.x, color_.y, color_.z);
    drawUniforms.wireframe = false;
    drawUniforms.useVertexColor = useVertexColor_ || !partColors_.empty();
    ScenePassState passState;
    passState.applyPolygonOffsetFill = true;
    passState.polygonOffsetFillEnabled = true;
    passState.restoredPolygonOffsetFillEnabled = false;
    passState.applyPolygonMode = true;
    passState.polygonMode = ScenePolygonMode::Fill;
    passState.restoredPolygonMode = ScenePolygonMode::Fill;
    OpenGLScenePass pass;
    pass.program = shader_;
    pass.drawKind = SceneDrawKind::Elements;
    pass.primitive = ScenePrimitive::Triangles;
    pass.count = count;
    pass.uniforms = drawUniforms;
    pass.state = passState;
    glBackend->drawScenePass(pass, *meshResource_);
}

void GLWidget::renderMeshEdges() {
    if (!edgeResource_) return;
    if (displayMode_ == ModelDisplayMode::Solid ||
        displayMode_ == ModelDisplayMode::Points) {
        return;
    }

    auto* glBackend = openGLBackend();

    int count = activeIndexCount_;
    const float wireAlpha = displayMode_ == ModelDisplayMode::Wireframe ? 1.0f : 0.85f;

    if (activeEdgeIndexCount_ > 0) {
        const int edgeVertCount = static_cast<int>(mesh_.edgeVertices.size() / 3);
        const bool useEdgeContour =
            useVertexColor_ &&
            colorBarVisible_ &&
            edgeVertCount > 0 &&
            static_cast<int>(edgeScalars_.size()) == edgeVertCount;
        float lineW = (count == 0) ? 3.0f : 1.25f;
        float alpha = (count == 0) ? 1.0f : wireAlpha;
        SceneDrawUniforms drawUniforms;
        drawUniforms.color = (count == 0)
            ? QVector3D(color_.x, color_.y, color_.z)
            : QVector3D(0.2f, 0.2f, 0.22f);
        drawUniforms.wireframe = true;
        drawUniforms.useVertexColor = useEdgeContour;
        drawUniforms.overrideContourMode = true;
        drawUniforms.contourMode = useEdgeContour;
        drawUniforms.wireAlpha = alpha;
        ScenePassState passState;
        passState.applyLineWidth = true;
        passState.lineWidth = lineW;
        passState.restoredLineWidth = 1.0f;
        if (alpha < 1.0f) {
            passState.applyBlend = true;
            passState.blendEnabled = true;
            passState.restoredBlendEnabled = false;
        }
        OpenGLScenePass pass;
        pass.program = shader_;
        pass.drawKind = SceneDrawKind::Elements;
        pass.primitive = ScenePrimitive::Lines;
        pass.count = activeEdgeIndexCount_;
        pass.uniforms = drawUniforms;
        pass.state = passState;
        glBackend->drawScenePass(pass, *edgeResource_);
    } else if (count > 0) {
        SceneDrawUniforms drawUniforms;
        drawUniforms.color = QVector3D(0.2f, 0.2f, 0.22f);
        drawUniforms.wireframe = true;
        drawUniforms.useVertexColor = false;
        drawUniforms.overrideContourMode = true;
        drawUniforms.contourMode = false;
        drawUniforms.wireAlpha = wireAlpha;
        ScenePassState passState;
        passState.applyLineWidth = true;
        passState.lineWidth = 1.0f;
        passState.restoredLineWidth = 1.0f;
        passState.applyPolygonMode = true;
        passState.polygonMode = ScenePolygonMode::Line;
        passState.restoredPolygonMode = ScenePolygonMode::Fill;
        if (wireAlpha < 1.0f) {
            passState.applyBlend = true;
            passState.blendEnabled = true;
            passState.restoredBlendEnabled = false;
        }
        OpenGLScenePass pass;
        pass.program = shader_;
        pass.drawKind = SceneDrawKind::Elements;
        pass.primitive = ScenePrimitive::Triangles;
        pass.count = count;
        pass.uniforms = drawUniforms;
        pass.state = passState;
        glBackend->drawScenePass(pass, *meshResource_);
    }
}

void GLWidget::renderMeshPoints()
{
    const int count = activeIndexCount_;
    const bool isoActive = isoIndexCount_ > 0;
    if (displayMode_ != ModelDisplayMode::Points || count <= 0 || isoActive || !meshResource_) {
        return;
    }

    auto* glBackend = openGLBackend();
    SceneDrawUniforms drawUniforms;
    drawUniforms.color = QVector3D(color_.x, color_.y, color_.z);
    drawUniforms.wireframe = false;
    drawUniforms.useVertexColor = useVertexColor_ || !partColors_.empty();
    ScenePassState passState;
    passState.applyPointSize = true;
    passState.pointSize = 4.0f;
    passState.restoredPointSize = 1.0f;
    OpenGLScenePass pass;
    pass.program = shader_;
    pass.drawKind = SceneDrawKind::Elements;
    pass.primitive = ScenePrimitive::Points;
    pass.count = count;
    pass.uniforms = drawUniforms;
    pass.state = passState;
    glBackend->drawScenePass(pass, *meshResource_);
}

void GLWidget::updateSelectionHighlight() {
    if (selectionDirty_) {
        std::vector<float> hlVerts;
        int hlMode = 0;

        if (!selection_.selectedElements.empty() && !triToElem_.empty()) {
            partEdgeCacheValid_ = false;
            rebuildSelectionEdges();
            hlMode = 0;
        } else if (!selection_.selectedNodes.empty()) {
            std::unordered_map<int, int> nodeToFirstVertex;
            if (!vertexToNode_.empty()) {
                for (int i = 0; i < static_cast<int>(vertexToNode_.size()); ++i) {
                    int nid = vertexToNode_[i];
                    if (nid >= 0 && nodeToFirstVertex.find(nid) == nodeToFirstVertex.end())
                        nodeToFirstVertex[nid] = i;
                }
            }
            for (int nid : selection_.selectedNodes) {
                int vi = -1;
                if (!nodeToFirstVertex.empty()) {
                    auto it = nodeToFirstVertex.find(nid);
                    if (it != nodeToFirstVertex.end()) vi = it->second;
                } else {
                    vi = nid;
                }
                if (vi >= 0 && vi * 6 + 2 < static_cast<int>(mesh_.vertices.size())) {
                    hlVerts.push_back(mesh_.vertices[vi * 6]);
                    hlVerts.push_back(mesh_.vertices[vi * 6 + 1]);
                    hlVerts.push_back(mesh_.vertices[vi * 6 + 2]);
                }
            }
            selEdgeVertCount_ = static_cast<int>(hlVerts.size() / 3);
            auto* glBackend = openGLBackend();
            glBackend->uploadLineVertices(
                *selectionEdgeResource_,
                hlVerts.data(),
                static_cast<int>(hlVerts.size() * sizeof(float)));
            hlMode = 1;
        }
        selectionDirty_ = false;
        silhouetteDirty_ = false;
        selHlMode_ = hlMode;
    } else if (silhouetteDirty_ && partEdgeCacheValid_ &&
               pickMode_ == PickMode::Part && selection_.hasSelection()) {
        updateSilhouetteFromCache();
        silhouetteDirty_ = false;
    }
}

void GLWidget::renderOverlayMesh() {
    if (!overlayVisible_ || overlayMesh_.edgeVertices.empty() || !overlayResource_) return;

    if (overlayNeedsUpload_) {
        overlayNeedsUpload_ = false;
        auto* glBackend = openGLBackend();
        glBackend->uploadLineVertices(
            *overlayResource_,
            overlayMesh_.edgeVertices.data(),
            static_cast<int>(overlayMesh_.edgeVertices.size() * sizeof(float)));
        overlayVertCount_ = static_cast<int>(overlayMesh_.edgeVertices.size() / 3);
    }
    auto* glBackend = openGLBackend();
    SceneDrawUniforms drawUniforms;
    drawUniforms.color = QVector3D(0.5f, 0.5f, 0.5f);
    drawUniforms.wireframe = true;
    drawUniforms.useVertexColor = false;
    drawUniforms.wireAlpha = 0.3f;
    drawUniforms.overrideContourMode = true;
    drawUniforms.contourMode = false;
    ScenePassState passState;
    passState.applyBlend = true;
    passState.blendEnabled = true;
    passState.restoredBlendEnabled = false;
    passState.applyLineWidth = true;
    passState.lineWidth = 1.0f;
    passState.restoredLineWidth = 1.0f;
    OpenGLScenePass pass;
    pass.program = shader_;
    pass.drawKind = SceneDrawKind::Arrays;
    pass.primitive = ScenePrimitive::Lines;
    pass.count = overlayVertCount_;
    pass.uniforms = drawUniforms;
    pass.state = passState;
    glBackend->drawScenePass(pass, *overlayResource_);
}

void GLWidget::renderClipPreview() {
    if (!clipPreviewVisible_ ||
        clipPreviewIndexCount_ <= 0 ||
        !clipPreviewResource_ ||
        !clipPreviewEdgeResource_) {
        return;
    }

    if (clipPreviewNeedsUpload_) {
        clipPreviewNeedsUpload_ = false;
        auto* glBackend = openGLBackend();
        glBackend->uploadMeshBuffers(
            *clipPreviewResource_,
            clipPreviewMesh_.vertices.data(),
            static_cast<int>(clipPreviewMesh_.vertices.size() * sizeof(float)),
            clipPreviewMesh_.indices.data(),
            static_cast<int>(clipPreviewMesh_.indices.size() * sizeof(unsigned int)));
        glBackend->uploadLineVertices(
            *clipPreviewEdgeResource_,
            clipPreviewMesh_.edgeVertices.data(),
            static_cast<int>(clipPreviewMesh_.edgeVertices.size() * sizeof(float)));
    }

    auto* glBackend = openGLBackend();

    SceneDrawUniforms drawUniforms;
    drawUniforms.color = QVector3D(0.95f, 0.58f, 0.20f);
    drawUniforms.wireframe = true;
    drawUniforms.useVertexColor = false;
    drawUniforms.wireAlpha = 0.8f;
    drawUniforms.overrideContourMode = true;
    drawUniforms.contourMode = false;
    ScenePassState passState;
    passState.applyBlend = true;
    passState.blendEnabled = true;
    passState.restoredBlendEnabled = false;
    passState.applyDepthTest = true;
    passState.depthTestEnabled = true;
    passState.restoredDepthTestEnabled = true;
    passState.applyDepthWrite = true;
    passState.depthWriteEnabled = false;
    passState.restoredDepthWriteEnabled = true;
    passState.applyLineWidth = true;
    passState.lineWidth = 2.0f;
    passState.restoredLineWidth = 1.0f;
    OpenGLScenePass pass;
    pass.program = shader_;
    pass.drawKind = SceneDrawKind::Arrays;
    pass.primitive = ScenePrimitive::Lines;
    pass.count = clipPreviewEdgeVertCount_;
    pass.uniforms = drawUniforms;
    pass.state = passState;
    glBackend->drawScenePass(pass, *clipPreviewEdgeResource_);
    glBackend->setSceneSurfaceAlpha(*shader_, 1.0f);
}

void GLWidget::renderSliceLines() {
    if (sliceVertCount_ <= 0 || !sliceResource_) return;

    auto* glBackend = openGLBackend();
    SceneDrawUniforms drawUniforms;
    drawUniforms.color = QVector3D(1.0f, 0.2f, 0.2f);
    drawUniforms.wireframe = true;
    drawUniforms.useVertexColor = false;
    drawUniforms.wireAlpha = 1.0f;
    drawUniforms.overrideContourMode = true;
    drawUniforms.contourMode = false;
    ScenePassState passState;
    passState.applyDepthTest = true;
    passState.depthTestEnabled = false;
    passState.restoredDepthTestEnabled = true;
    passState.applyLineWidth = true;
    passState.lineWidth = 2.0f;
    passState.restoredLineWidth = 1.0f;
    OpenGLScenePass pass;
    pass.program = shader_;
    pass.drawKind = SceneDrawKind::Arrays;
    pass.primitive = ScenePrimitive::Lines;
    pass.count = sliceVertCount_;
    pass.uniforms = drawUniforms;
    pass.state = passState;
    glBackend->drawScenePass(pass, *sliceResource_);
}

void GLWidget::renderIsoSurface() {
    if (isoIndexCount_ <= 0 || !isoResource_) return;

    if (isoNeedsUpload_) {
        isoNeedsUpload_ = false;
        auto* glBackend = openGLBackend();
        glBackend->uploadMeshBuffers(
            *isoResource_,
            isoMesh_.vertices.data(),
            static_cast<int>(isoMesh_.vertices.size() * sizeof(float)),
            isoMesh_.indices.data(),
            static_cast<int>(isoMesh_.indices.size() * sizeof(unsigned int)));
    }
    auto* glBackend = openGLBackend();
    SceneDrawUniforms drawUniforms;
    drawUniforms.color = QVector3D(0.2f, 0.8f, 0.4f);
    drawUniforms.wireframe = false;
    drawUniforms.useVertexColor = false;
    drawUniforms.wireAlpha = 0.75f;
    drawUniforms.overrideContourMode = true;
    drawUniforms.contourMode = false;
    drawUniforms.overrideSurfaceAlpha = true;
    drawUniforms.surfaceAlpha = 0.75f;
    ScenePassState passState;
    passState.applyBlend = true;
    passState.blendEnabled = true;
    passState.restoredBlendEnabled = false;
    passState.applyCullFace = true;
    passState.cullFaceEnabled = false;
    passState.restoredCullFaceEnabled = true;
    OpenGLScenePass pass;
    pass.program = shader_;
    pass.drawKind = SceneDrawKind::Elements;
    pass.primitive = ScenePrimitive::Triangles;
    pass.count = isoIndexCount_;
    pass.uniforms = drawUniforms;
    pass.state = passState;
    glBackend->drawScenePass(pass, *isoResource_);
    glBackend->setSceneSurfaceAlpha(*shader_, 1.0f);
}

void GLWidget::renderSelectionHighlight() {
    if (selEdgeVertCount_ <= 0 || !selection_.hasSelection() ||
        !selectionEdgeResource_) {
        return;
    }

    auto* glBackend = openGLBackend();
    SceneDrawUniforms drawUniforms;
    drawUniforms.color = QVector3D(1.0f, 0.78f, 0.0f);
    drawUniforms.wireframe = true;
    drawUniforms.useVertexColor = false;
    drawUniforms.wireAlpha = 1.0f;
    drawUniforms.overrideContourMode = true;
    drawUniforms.contourMode = false;
    ScenePassState passState;
    passState.applyDepthTest = true;
    passState.depthTestEnabled = false;
    passState.restoredDepthTestEnabled = true;

    if (selHlMode_ == 1) {
        passState.applyPointSize = true;
        passState.pointSize = 8.0f;
        passState.restoredPointSize = 1.0f;
        OpenGLScenePass pass;
        pass.program = shader_;
        pass.drawKind = SceneDrawKind::Arrays;
        pass.primitive = ScenePrimitive::Points;
        pass.count = selEdgeVertCount_;
        pass.uniforms = drawUniforms;
        pass.state = passState;
        glBackend->drawScenePass(pass, *selectionEdgeResource_);
    } else {
        passState.applyLineWidth = true;
        passState.lineWidth = 2.5f;
        passState.restoredLineWidth = 1.0f;
        OpenGLScenePass pass;
        pass.program = shader_;
        pass.drawKind = SceneDrawKind::Arrays;
        pass.primitive = ScenePrimitive::Lines;
        pass.count = selEdgeVertCount_;
        pass.uniforms = drawUniforms;
        pass.state = passState;
        glBackend->drawScenePass(pass, *selectionEdgeResource_);
    }
}

void GLWidget::render2DOverlays(const glm::mat4& mvp) {
    QPainter painter(this);
    painter.beginNativePainting();
    painter.endNativePainting();
    painter.setRenderHint(QPainter::Antialiasing);
    drawAxesLabels(painter);
    if (showLabels_ && selection_.hasSelection())
        drawIdLabels(painter, mvp);
    painter.end();
}

void GLWidget::updateFpsStats() {
    frameCount_++;
    qint64 elapsed = fpsTimer_.elapsed();
    if (elapsed >= 500) {
        fps_ = frameCount_ * 1000.0f / elapsed;
        frameTime_ = elapsed / static_cast<float>(frameCount_);
        frameCount_ = 0;
        fpsTimer_.restart();
    }
}



void GLWidget::resizeGL(int w, int h) {
    auto* glBackend = openGLBackend();
    glBackend->setViewport(0, 0, w, h);

    // 重建拾取 framebuffer（尺寸需与视口一致）
    // 注意：resizeGL 由 Qt 调用时 GL 上下文已 current，无需手动 makeCurrent/doneCurrent
    int dpr = devicePixelRatio();
    if (!pickFramebuffer_)
        pickFramebuffer_ = std::make_unique<OpenGLFramebuffer>();
    glBackend->resizeFramebuffer(*pickFramebuffer_, w * dpr, h * dpr);

    // 色标覆盖层跟随窗口大小
    if (colorBarOverlay_)
        colorBarOverlay_->resize(size());
}

// ============================================================
// 鼠标与键盘事件
// ============================================================

void GLWidget::mousePressEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();
    pressPos_ = pos;
    lastPos_ = pos;
    isDragging_ = false;
    isBoxSelecting_ = false;
    isBoxDeselecting_ = false;

    bool hasMod = (e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier));
    const bool pickTool = interactionMode_ == ViewportInteractionMode::Pick;

    if (!hasMod && e->button() == Qt::LeftButton) {
        StandardView view = StandardView::Front;
        if (standardViewFromAxesClick(pos, &view)) {
            setStandardView(view);
            e->accept();
            return;
        }
    }

    if (hasMod || pickTool) {
        if (e->button() == Qt::LeftButton) {
            // 拾取工具或 Ctrl/Shift + 左键 → 框选/点选
            isBoxSelecting_ = true;
            boxOrigin_ = pos;
            if (!rubberBand_)
                rubberBand_ = new QRubberBand(QRubberBand::Rectangle, this);
            rubberBand_->setGeometry(QRect(boxOrigin_, QSize()));
            rubberBand_->show();
        } else if (hasMod && e->button() == Qt::RightButton) {
            // Ctrl/Shift + 右键 → 框选/点选（取消选中）
            isBoxDeselecting_ = true;
            boxOrigin_ = pos;
            if (!rubberBand_)
                rubberBand_ = new QRubberBand(QRubberBand::Rectangle, this);
            rubberBand_->setGeometry(QRect(boxOrigin_, QSize()));
            rubberBand_->show();
        }
    }
}

void GLWidget::mouseMoveEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();

    // 框选模式（选中/取消）：更新矩形
    if ((isBoxSelecting_ || isBoxDeselecting_) && rubberBand_) {
        rubberBand_->setGeometry(QRect(boxOrigin_, pos).normalized());
        return;
    }

    float dx = pos.x() - lastPos_.x();
    float dy = pos.y() - lastPos_.y();
    lastPos_ = pos;

    // 判断是否已经开始拖拽（超过 5 像素阈值）
    if (!isDragging_) {
        QPoint diff = pos - pressPos_;
        if (diff.manhattanLength() > 5)
            isDragging_ = true;
        else
            return;
    }

    // 拾取工具和 Ctrl/Shift 手势用于选取，不做视图导航
    if ((e->buttons() & Qt::LeftButton) &&
        !(e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
        if (interactionMode_ == ViewportInteractionMode::Rotate) {
            cam_.rotate(dx, dy);
        } else if (interactionMode_ == ViewportInteractionMode::Pan) {
            cam_.pan(dx, dy);
        } else if (interactionMode_ == ViewportInteractionMode::Zoom) {
            cam_.zoom(-dy / 120.0f);
        }
    }
    // Ctrl/Shift + 右键用于取消拾取，不平移
    if ((e->buttons() & (Qt::RightButton | Qt::MiddleButton)) &&
        !(e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)))
        cam_.pan(dx, dy);

    // 部件模式轮廓边依赖视角，相机变化时需要刷新
    if (pickMode_ == PickMode::Part && selection_.hasSelection())
        silhouetteDirty_ = true;

    update();
}

void GLWidget::mouseReleaseEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();

    // ── Ctrl/Shift + 左键：添加选中（点选/框选） ──
    if (e->button() == Qt::LeftButton && isBoxSelecting_ && rubberBand_) {
        rubberBand_->hide();
        isBoxSelecting_ = false;

        QRect rect = QRect(boxOrigin_, pos).normalized();
        if (rect.width() > 3 && rect.height() > 3) {
            // 框选
            pickRectPending_ = true;
            pendingPickRect_ = rect;
            update();
        } else {
            // 范围太小视为点选
            pickPointPending_ = true;
            pendingPickPos_ = pos;
            pendingPickCtrl_ = (e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier));
            update();
        }
    }

    // ── Ctrl/Shift + 右键：取消选中（点选/框选） ──
    if (e->button() == Qt::RightButton && isBoxDeselecting_ && rubberBand_) {
        rubberBand_->hide();
        isBoxDeselecting_ = false;

        QRect rect = QRect(boxOrigin_, pos).normalized();
        if (rect.width() > 3 && rect.height() > 3) {
            // 框选取消
            deselectRectPending_ = true;
            pendingDeselectRect_ = rect;
            update();
        } else {
            // 范围太小视为点选取消
            deselectPointPending_ = true;
            pendingDeselectPos_ = pos;
            update();
        }
    }

    if (e->button() == Qt::RightButton &&
        !isDragging_ &&
        !isBoxDeselecting_ &&
        !(e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
        isDragging_ = false;
        emit contextMenuRequested(mapToGlobal(pos));
        e->accept();
        return;
    }

    isDragging_ = false;
}

void GLWidget::wheelEvent(QWheelEvent* e) {
    // 按住中键或右键拖动时忽略滚轮，防止平移与缩放同时触发
    if (e->buttons() & (Qt::MiddleButton | Qt::RightButton)) return;
    cam_.zoom(e->angleDelta().y() / 120.0f);
    if (pickMode_ == PickMode::Part && selection_.hasSelection())
        silhouetteDirty_ = true;
    update();
}

void GLWidget::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        if (selection_.hasSelection()) {
            selection_.clear();
            selectionDirty_ = true;
            partEdgeCacheValid_ = false;
            selEdgeVertCount_ = 0;
            emit selectionChanged(pickMode_, 0, {});
            update();
        } else {
            window()->close();
        }
    } else {
        QOpenGLWidget::keyPressEvent(e);
    }
}

// ============================================================
// 拾取功能
// ============================================================

glm::vec3 GLWidget::idToColor(int id) {
    // 将单元 ID+1 编码为 RGB 颜色（0 表示无命中）
    id += 1;
    int r = (id      ) & 0xFF;
    int g = (id >>  8) & 0xFF;
    int b = (id >> 16) & 0xFF;
    return glm::vec3(r / 255.0f, g / 255.0f, b / 255.0f);
}

int GLWidget::colorToId(unsigned char r, unsigned char g, unsigned char b) {
    if (r == 0 && g == 0 && b == 0) return -1;  // 背景
    int id = r | (g << 8) | (b << 16);
    return id - 1;
}

int GLWidget::edgeElementAtPoint(const QPointF& pos, const glm::mat4& mvp, float thresholdPx) const
{
    return ScreenSpacePicking::edgeElementAtPoint(
        mesh_,
        pos,
        mvp,
        static_cast<float>(width()),
        static_cast<float>(height()),
        true,
        thresholdPx,
        [this](int elementId) { return isElementVisibleForSelection(elementId); });
}

int GLWidget::closestNodeForElement(int elementId, const QPointF& pos, const glm::mat4& mvp) const
{
    return ScreenSpacePicking::closestNodeForElement(mesh_,
                                                     triToElem_,
                                                     vertexToNode_,
                                                     elementId,
                                                     pos,
                                                     mvp,
                                                     static_cast<float>(width()),
                                                     static_cast<float>(height()),
                                                     true);
}

void GLWidget::renderPickBuffer(const glm::mat4& mvp) {
    if (!pickFramebuffer_ || !pickFramebuffer_->isValid() ||
        !pickVertexArray_ || !pickVertexArray_->isValid() ||
        !meshResource_ || !meshResource_->isValid() ||
        triToElem_.empty()) {
        return;
    }

    int dpr = devicePixelRatio();

    // 逐单元绘制，跳过隐藏部件
    std::vector<PickDrawItem> drawItems;
    int triCount = static_cast<int>(triToElem_.size());
    drawItems.reserve(triCount);
    int i = 0;
    while (i < triCount) {
        int elemId = triToElem_[i];
        int start = i;
        while (i < triCount && triToElem_[i] == elemId) ++i;

        if (!isTriangleVisible(start)) {
            continue;
        }

        PickDrawItem item;
        item.startIndex = start * 3;
        item.indexCount = (i - start) * 3;
        glm::vec3 c = idToColor(elemId);
        item.color[0] = c.x;
        item.color[1] = c.y;
        item.color[2] = c.z;
        drawItems.push_back(item);
    }

    auto* glBackend = openGLBackend();
    glBackend->renderPickBuffer(*pickFramebuffer_,
                                *pickVertexArray_,
                                *meshResource_,
                                width() * dpr,
                                height() * dpr,
                                pickShader_->programId(),
                                allTriIndices_.data(),
                                static_cast<int>(allTriIndices_.size()),
                                glm::value_ptr(mvp),
                                drawItems);
}

void GLWidget::pickAtPoint(const QPoint& pos, bool ctrlHeld) {
    // 注意：此函数现在仅在 paintGL() 内调用，GL 上下文已由 Qt 管理，
    // 无需手动 makeCurrent/doneCurrent。

    // 渲染拾取缓冲
    float aspect = (height() > 0) ? static_cast<float>(width()) / height() : 1.0f;
    glm::mat4 projection = projectionMatrix(aspect);
    glm::mat4 view = cam_.viewMatrix();
    glm::mat4 mvp = projection * view;

    int elemId = -1;
    if (pickFramebuffer_ && pickFramebuffer_->isValid() && !triToElem_.empty()) {
        renderPickBuffer(mvp);

        // 读取点击位置像素（使用原始 GL 调用，避免 Qt FBO 状态追踪污染）
        unsigned char pixel[4] = {0};
        int dpr = devicePixelRatio();
        int px = pos.x() * dpr;
        int py = (height() - pos.y()) * dpr;  // OpenGL Y 轴翻转
        auto* glBackend = openGLBackend();
        glBackend->readFramebufferPixel(*pickFramebuffer_, px, py, pixel);
        elemId = colorToId(pixel[0], pixel[1], pixel[2]);
    }
    if (elemId < 0) {
        elemId = edgeElementAtPoint(pos, mvp, 8.0f);
    }

    if (pickMode_ == PickMode::Node) {
        // ── 节点拾取：找到点击处最近的顶点 ──
        int closestNode = closestNodeForElement(elemId, pos, mvp);
        if (!ctrlHeld) {
            selection_.clear();
            if (closestNode >= 0) selection_.selectedNodes.insert(closestNode);
        } else {
            if (closestNode >= 0) selection_.toggleNode(closestNode);
        }

    } else if (pickMode_ == PickMode::Part) {
        // ── 部件拾取：命中单元 → elemToPart_ O(1) 查找部件索引 ──
        int hitPart = -1;
        if (elemId >= 0 && !elemToPart_.empty()) {
            auto it = elemToPart_.find(elemId);
            if (it != elemToPart_.end()) hitPart = it->second;
        }
        if (!ctrlHeld) {
            selection_.clear();
            if (hitPart >= 0) selectPart(hitPart);
        } else {
            if (hitPart >= 0) {
                if (isPartFullySelected(hitPart))
                    deselectPart(hitPart);
                else
                    selectPart(hitPart);
            }
        }

    } else {
        // ── 单元拾取 ──
        if (!ctrlHeld) {
            selection_.clear();
            if (elemId >= 0) selection_.selectedElements.insert(elemId);
        } else {
            if (elemId >= 0) selection_.toggleElement(elemId);
        }
    }

    selectionDirty_ = true;
    {
        std::vector<int> ids;
        PickMode emitMode = pickMode_;
        if (!selection_.selectedNodes.empty()) {
            ids.assign(selection_.selectedNodes.begin(), selection_.selectedNodes.end());
        } else {
            ids.assign(selection_.selectedElements.begin(), selection_.selectedElements.end());
        }
        std::sort(ids.begin(), ids.end());
        emit selectionChanged(emitMode, static_cast<int>(ids.size()), ids);
    }
    // 部件模式：发射选中的部件索引列表，同步模型树
    if (pickMode_ == PickMode::Part) {
        std::vector<int> pickedParts;
        for (int pi = 0; pi < static_cast<int>(partElementIds_.size()); ++pi) {
            if (isPartFullySelected(pi))
                pickedParts.push_back(pi);
        }
        emit partsPicked(pickedParts);
    }
}

void GLWidget::pickInRect(const QRect& rect) {
    if (triToElem_.empty() && mesh_.elemEdgeToElement.empty()) return;

    // 注意：此函数现在仅在 paintGL() 内调用，GL 上下文已由 Qt 管理。

    float aspect = (height() > 0) ? static_cast<float>(width()) / height() : 1.0f;
    glm::mat4 projection = projectionMatrix(aspect);
    glm::mat4 mvp = projection * cam_.viewMatrix();

    // 框选范围转换为 NDC 坐标
    float ndcL = (2.0f * rect.left() / width()) - 1.0f;
    float ndcR = (2.0f * rect.right() / width()) - 1.0f;
    float ndcT = 1.0f - (2.0f * rect.top() / height());
    float ndcB = 1.0f - (2.0f * rect.bottom() / height());
    if (ndcL > ndcR) std::swap(ndcL, ndcR);
    if (ndcB > ndcT) std::swap(ndcB, ndcT);

    auto pointInside = [&](const glm::vec3& p) {
        glm::vec4 clip = mvp * glm::vec4(p, 1.0f);
        if (clip.w <= 0) return false;
        float sx = clip.x / clip.w;
        float sy = clip.y / clip.w;
        return sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT;
    };

    selection_.clear();

    if (pickMode_ == PickMode::Node) {
        // 节点模式：只遍历可见三角形的顶点，避免隐藏部件被框选
        std::unordered_set<int> addedNodes;
        auto pointInside = [&](const glm::vec3& p) {
            glm::vec4 clip = mvp * glm::vec4(p, 1.0f);
            if (clip.w <= 0) return false;
            float sx = clip.x / clip.w;
            float sy = clip.y / clip.w;
            return sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT;
        };
        const int triCount = static_cast<int>(triToElem_.size());
        for (int t = 0; t < triCount; ++t) {
            if (!isTriangleVisible(t)) continue;
            for (int v = 0; v < 3; ++v) {
                unsigned int vi = mesh_.indices[t * 3 + v];
                glm::vec3 p(mesh_.vertices[vi * 6],
                            mesh_.vertices[vi * 6 + 1],
                            mesh_.vertices[vi * 6 + 2]);
                if (pointInside(p)) {
                    int nodeId = (vi < static_cast<int>(vertexToNode_.size())) ? vertexToNode_[vi] : static_cast<int>(vi);
                    if (nodeId >= 0 && addedNodes.insert(nodeId).second)
                        selection_.selectedNodes.insert(nodeId);
                }
            }
        }
        const int elemEdgeCount = std::min(static_cast<int>(mesh_.elemEdgeToElement.size()),
                                           static_cast<int>(mesh_.elemEdgeVertices.size() / 6));
        for (int edge = 0; edge < elemEdgeCount; ++edge) {
            const int elementId = mesh_.elemEdgeToElement[edge];
            if (!isElementVisibleForSelection(elementId) ||
                edge >= static_cast<int>(mesh_.elemEdgeNodeIds.size())) {
                continue;
            }
            const int base = edge * 6;
            const glm::vec3 p0(mesh_.elemEdgeVertices[base],
                               mesh_.elemEdgeVertices[base + 1],
                               mesh_.elemEdgeVertices[base + 2]);
            const glm::vec3 p1(mesh_.elemEdgeVertices[base + 3],
                               mesh_.elemEdgeVertices[base + 4],
                               mesh_.elemEdgeVertices[base + 5]);
            const auto [node0, node1] = mesh_.elemEdgeNodeIds[edge];
            if (node0 >= 0 && pointInside(p0) && addedNodes.insert(node0).second)
                selection_.selectedNodes.insert(node0);
            if (node1 >= 0 && pointInside(p1) && addedNodes.insert(node1).second)
                selection_.selectedNodes.insert(node1);
        }
    } else if (pickMode_ == PickMode::Part) {
        // 部件模式：框内三角形 → 收集部件索引 → 选中这些部件所有单元
        std::unordered_set<int> hitParts;
        int triCount = static_cast<int>(triToElem_.size());
        for (int t = 0; t < triCount; ++t) {
            if (!isTriangleVisible(t)) continue;
            bool anyInside = false;
            for (int v = 0; v < 3; ++v) {
                unsigned int vi = mesh_.indices[t * 3 + v];
                glm::vec4 wp(mesh_.vertices[vi * 6],
                             mesh_.vertices[vi * 6 + 1],
                             mesh_.vertices[vi * 6 + 2], 1.0f);
                glm::vec4 clip = mvp * wp;
                if (clip.w <= 0) continue;
                float sx = clip.x / clip.w;
                float sy = clip.y / clip.w;
                if (sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT) {
                    anyInside = true;
                    break;
                }
            }
            if (anyInside && t < static_cast<int>(triToPart_.size()) && triToPart_[t] >= 0) {
                hitParts.insert(triToPart_[t]);
            }
        }
        for (int p : hitParts) {
            selectPart(p);
        }
        const int elemEdgeCount = std::min(static_cast<int>(mesh_.elemEdgeToElement.size()),
                                           static_cast<int>(mesh_.elemEdgeVertices.size() / 6));
        for (int edge = 0; edge < elemEdgeCount; ++edge) {
            const int elementId = mesh_.elemEdgeToElement[edge];
            if (!isElementVisibleForSelection(elementId)) {
                continue;
            }
            const int base = edge * 6;
            const glm::vec3 p0(mesh_.elemEdgeVertices[base],
                               mesh_.elemEdgeVertices[base + 1],
                               mesh_.elemEdgeVertices[base + 2]);
            const glm::vec3 p1(mesh_.elemEdgeVertices[base + 3],
                               mesh_.elemEdgeVertices[base + 4],
                               mesh_.elemEdgeVertices[base + 5]);
            if (pointInside(p0) || pointInside(p1)) {
                const auto partIt = elemToPart_.find(elementId);
                if (partIt != elemToPart_.end()) {
                    selectPart(partIt->second);
                }
            }
        }
    } else {
        // 单元模式：遍历所有三角形，如果三角形任意一个顶点在框内则选中该单元
        int triCount = static_cast<int>(triToElem_.size());
        for (int t = 0; t < triCount; ++t) {
            if (!isTriangleVisible(t)) continue;
            int elemId = triToElem_[t];
            if (selection_.isElementSelected(elemId)) continue;  // 已选中，跳过

            bool anyInside = false;
            for (int v = 0; v < 3; ++v) {
                unsigned int vi = mesh_.indices[t * 3 + v];
                glm::vec4 wp(mesh_.vertices[vi * 6],
                             mesh_.vertices[vi * 6 + 1],
                             mesh_.vertices[vi * 6 + 2], 1.0f);
                glm::vec4 clip = mvp * wp;
                if (clip.w <= 0) continue;
                float sx = clip.x / clip.w;
                float sy = clip.y / clip.w;
                if (sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT) {
                    anyInside = true;
                    break;
                }
            }
            if (anyInside) {
                selection_.selectedElements.insert(elemId);
            }
        }
        const int elemEdgeCount = std::min(static_cast<int>(mesh_.elemEdgeToElement.size()),
                                           static_cast<int>(mesh_.elemEdgeVertices.size() / 6));
        for (int edge = 0; edge < elemEdgeCount; ++edge) {
            const int elementId = mesh_.elemEdgeToElement[edge];
            if (elementId < 0 ||
                selection_.isElementSelected(elementId) ||
                !isElementVisibleForSelection(elementId)) {
                continue;
            }
            const int base = edge * 6;
            const glm::vec3 p0(mesh_.elemEdgeVertices[base],
                               mesh_.elemEdgeVertices[base + 1],
                               mesh_.elemEdgeVertices[base + 2]);
            const glm::vec3 p1(mesh_.elemEdgeVertices[base + 3],
                               mesh_.elemEdgeVertices[base + 4],
                               mesh_.elemEdgeVertices[base + 5]);
            if (pointInside(p0) || pointInside(p1)) {
                selection_.selectedElements.insert(elementId);
            }
        }
    }

    selectionDirty_ = true;
    {
        std::vector<int> ids;
        PickMode emitMode = pickMode_;
        if (!selection_.selectedNodes.empty()) {
            ids.assign(selection_.selectedNodes.begin(), selection_.selectedNodes.end());
        } else {
            ids.assign(selection_.selectedElements.begin(), selection_.selectedElements.end());
        }
        std::sort(ids.begin(), ids.end());
        emit selectionChanged(emitMode, static_cast<int>(ids.size()), ids);
    }
    // 部件模式：发射选中的部件索引列表，同步模型树
    if (pickMode_ == PickMode::Part) {
        std::vector<int> pickedParts;
        for (int pi = 0; pi < static_cast<int>(partElementIds_.size()); ++pi) {
            if (isPartFullySelected(pi))
                pickedParts.push_back(pi);
        }
        emit partsPicked(pickedParts);
    }
}

void GLWidget::deselectAtPoint(const QPoint& pos) {
    float aspect = (height() > 0) ? static_cast<float>(width()) / height() : 1.0f;
    glm::mat4 projection = projectionMatrix(aspect);
    glm::mat4 view = cam_.viewMatrix();
    glm::mat4 mvp = projection * view;

    int elemId = -1;
    if (pickFramebuffer_ && pickFramebuffer_->isValid() && !triToElem_.empty()) {
        renderPickBuffer(mvp);

        unsigned char pixel[4] = {0};
        int dpr = devicePixelRatio();
        int px = pos.x() * dpr;
        int py = (height() - pos.y()) * dpr;
        auto* glBackend = openGLBackend();
        glBackend->readFramebufferPixel(*pickFramebuffer_, px, py, pixel);
        elemId = colorToId(pixel[0], pixel[1], pixel[2]);
    }
    if (elemId < 0) {
        elemId = edgeElementAtPoint(pos, mvp, 8.0f);
    }

    if (pickMode_ == PickMode::Node) {
        int closestNode = closestNodeForElement(elemId, pos, mvp);
        if (closestNode >= 0) selection_.selectedNodes.erase(closestNode);

    } else if (pickMode_ == PickMode::Part) {
        if (elemId >= 0 && !elemToPart_.empty()) {
            auto it = elemToPart_.find(elemId);
            if (it != elemToPart_.end()) deselectPart(it->second);
        }

    } else {
        if (elemId >= 0) selection_.selectedElements.erase(elemId);
    }

    selectionDirty_ = true;
    {
        std::vector<int> ids;
        if (!selection_.selectedNodes.empty())
            ids.assign(selection_.selectedNodes.begin(), selection_.selectedNodes.end());
        else
            ids.assign(selection_.selectedElements.begin(), selection_.selectedElements.end());
        std::sort(ids.begin(), ids.end());
        emit selectionChanged(pickMode_, static_cast<int>(ids.size()), ids);
    }
    if (pickMode_ == PickMode::Part) {
        std::vector<int> pickedParts;
        for (int pi = 0; pi < static_cast<int>(partElementIds_.size()); ++pi)
            if (isPartFullySelected(pi)) pickedParts.push_back(pi);
        emit partsPicked(pickedParts);
    }
}

void GLWidget::deselectInRect(const QRect& rect) {
    if (triToElem_.empty() && mesh_.elemEdgeToElement.empty()) return;

    float aspect = (height() > 0) ? static_cast<float>(width()) / height() : 1.0f;
    glm::mat4 projection = projectionMatrix(aspect);
    glm::mat4 mvp = projection * cam_.viewMatrix();

    float ndcL = (2.0f * rect.left() / width()) - 1.0f;
    float ndcR = (2.0f * rect.right() / width()) - 1.0f;
    float ndcT = 1.0f - (2.0f * rect.top() / height());
    float ndcB = 1.0f - (2.0f * rect.bottom() / height());
    if (ndcL > ndcR) std::swap(ndcL, ndcR);
    if (ndcB > ndcT) std::swap(ndcB, ndcT);

    auto pointInside = [&](const glm::vec3& p) {
        glm::vec4 clip = mvp * glm::vec4(p, 1.0f);
        if (clip.w <= 0) return false;
        float sx = clip.x / clip.w;
        float sy = clip.y / clip.w;
        return sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT;
    };

    if (pickMode_ == PickMode::Node) {
        std::unordered_set<int> removedNodes;
        auto pointInside = [&](const glm::vec3& p) {
            glm::vec4 clip = mvp * glm::vec4(p, 1.0f);
            if (clip.w <= 0) return false;
            float sx = clip.x / clip.w;
            float sy = clip.y / clip.w;
            return sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT;
        };
        const int triCount = static_cast<int>(triToElem_.size());
        for (int t = 0; t < triCount; ++t) {
            if (!isTriangleVisible(t)) continue;
            for (int v = 0; v < 3; ++v) {
                unsigned int vi = mesh_.indices[t * 3 + v];
                glm::vec3 p(mesh_.vertices[vi * 6],
                            mesh_.vertices[vi * 6 + 1],
                            mesh_.vertices[vi * 6 + 2]);
                if (pointInside(p)) {
                    int nodeId = (vi < static_cast<int>(vertexToNode_.size())) ? vertexToNode_[vi] : static_cast<int>(vi);
                    if (nodeId >= 0 && removedNodes.insert(nodeId).second)
                        selection_.selectedNodes.erase(nodeId);
                }
            }
        }
        const int elemEdgeCount = std::min(static_cast<int>(mesh_.elemEdgeToElement.size()),
                                           static_cast<int>(mesh_.elemEdgeVertices.size() / 6));
        for (int edge = 0; edge < elemEdgeCount; ++edge) {
            const int elementId = mesh_.elemEdgeToElement[edge];
            if (!isElementVisibleForSelection(elementId) ||
                edge >= static_cast<int>(mesh_.elemEdgeNodeIds.size())) {
                continue;
            }
            const int base = edge * 6;
            const glm::vec3 p0(mesh_.elemEdgeVertices[base],
                               mesh_.elemEdgeVertices[base + 1],
                               mesh_.elemEdgeVertices[base + 2]);
            const glm::vec3 p1(mesh_.elemEdgeVertices[base + 3],
                               mesh_.elemEdgeVertices[base + 4],
                               mesh_.elemEdgeVertices[base + 5]);
            const auto [node0, node1] = mesh_.elemEdgeNodeIds[edge];
            if (node0 >= 0 && pointInside(p0) && removedNodes.insert(node0).second)
                selection_.selectedNodes.erase(node0);
            if (node1 >= 0 && pointInside(p1) && removedNodes.insert(node1).second)
                selection_.selectedNodes.erase(node1);
        }
    } else if (pickMode_ == PickMode::Part) {
        std::unordered_set<int> hitParts;
        int triCount = static_cast<int>(triToElem_.size());
        for (int t = 0; t < triCount; ++t) {
            if (!isTriangleVisible(t)) continue;
            bool anyInside = false;
            for (int v = 0; v < 3; ++v) {
                unsigned int vi = mesh_.indices[t * 3 + v];
                glm::vec4 wp(mesh_.vertices[vi * 6], mesh_.vertices[vi * 6 + 1],
                             mesh_.vertices[vi * 6 + 2], 1.0f);
                glm::vec4 clip = mvp * wp;
                if (clip.w <= 0) continue;
                float sx = clip.x / clip.w;
                float sy = clip.y / clip.w;
                if (sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT) { anyInside = true; break; }
            }
            if (anyInside && t < static_cast<int>(triToPart_.size()) && triToPart_[t] >= 0)
                hitParts.insert(triToPart_[t]);
        }
        for (int p : hitParts) deselectPart(p);
        const int elemEdgeCount = std::min(static_cast<int>(mesh_.elemEdgeToElement.size()),
                                           static_cast<int>(mesh_.elemEdgeVertices.size() / 6));
        for (int edge = 0; edge < elemEdgeCount; ++edge) {
            const int elementId = mesh_.elemEdgeToElement[edge];
            if (!isElementVisibleForSelection(elementId)) {
                continue;
            }
            const int base = edge * 6;
            const glm::vec3 p0(mesh_.elemEdgeVertices[base],
                               mesh_.elemEdgeVertices[base + 1],
                               mesh_.elemEdgeVertices[base + 2]);
            const glm::vec3 p1(mesh_.elemEdgeVertices[base + 3],
                               mesh_.elemEdgeVertices[base + 4],
                               mesh_.elemEdgeVertices[base + 5]);
            if (pointInside(p0) || pointInside(p1)) {
                const auto partIt = elemToPart_.find(elementId);
                if (partIt != elemToPart_.end()) {
                    deselectPart(partIt->second);
                }
            }
        }
    } else {
        int triCount = static_cast<int>(triToElem_.size());
        for (int t = 0; t < triCount; ++t) {
            if (!isTriangleVisible(t)) continue;
            int elemId = triToElem_[t];
            if (!selection_.isElementSelected(elemId)) continue;
            bool anyInside = false;
            for (int v = 0; v < 3; ++v) {
                unsigned int vi = mesh_.indices[t * 3 + v];
                glm::vec4 wp(mesh_.vertices[vi * 6], mesh_.vertices[vi * 6 + 1],
                             mesh_.vertices[vi * 6 + 2], 1.0f);
                glm::vec4 clip = mvp * wp;
                if (clip.w <= 0) continue;
                float sx = clip.x / clip.w;
                float sy = clip.y / clip.w;
                if (sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT) { anyInside = true; break; }
            }
            if (anyInside) selection_.selectedElements.erase(elemId);
        }
        const int elemEdgeCount = std::min(static_cast<int>(mesh_.elemEdgeToElement.size()),
                                           static_cast<int>(mesh_.elemEdgeVertices.size() / 6));
        for (int edge = 0; edge < elemEdgeCount; ++edge) {
            const int elementId = mesh_.elemEdgeToElement[edge];
            if (elementId < 0 ||
                !selection_.isElementSelected(elementId) ||
                !isElementVisibleForSelection(elementId)) {
                continue;
            }
            const int base = edge * 6;
            const glm::vec3 p0(mesh_.elemEdgeVertices[base],
                               mesh_.elemEdgeVertices[base + 1],
                               mesh_.elemEdgeVertices[base + 2]);
            const glm::vec3 p1(mesh_.elemEdgeVertices[base + 3],
                               mesh_.elemEdgeVertices[base + 4],
                               mesh_.elemEdgeVertices[base + 5]);
            if (pointInside(p0) || pointInside(p1)) {
                selection_.selectedElements.erase(elementId);
            }
        }
    }

    selectionDirty_ = true;
    {
        std::vector<int> ids;
        if (!selection_.selectedNodes.empty())
            ids.assign(selection_.selectedNodes.begin(), selection_.selectedNodes.end());
        else
            ids.assign(selection_.selectedElements.begin(), selection_.selectedElements.end());
        std::sort(ids.begin(), ids.end());
        emit selectionChanged(pickMode_, static_cast<int>(ids.size()), ids);
    }
    if (pickMode_ == PickMode::Part) {
        std::vector<int> pickedParts;
        for (int pi = 0; pi < static_cast<int>(partElementIds_.size()); ++pi)
            if (isPartFullySelected(pi)) pickedParts.push_back(pi);
        emit partsPicked(pickedParts);
    }
}

// ============================================================
// 私有方法
// ============================================================

void GLWidget::setPickMode(PickMode mode) {
    if (mode == pickMode_) return;
    pickMode_ = mode;

    // 切换拾取模式时清除之前的选中状态
    if (selection_.hasSelection()) {
        selection_.clear();
        selectionDirty_ = true;
        partEdgeCacheValid_ = false;
        selEdgeVertCount_ = 0;
        emit selectionChanged(pickMode_, 0, {});
        if (mode == PickMode::Part)
            emit partsPicked({});
        update();
    }
}

void GLWidget::setInteractionMode(ViewportInteractionMode mode)
{
    interactionMode_ = mode;
    isDragging_ = false;
    isBoxSelecting_ = false;
    isBoxDeselecting_ = false;
    if (rubberBand_) {
        rubberBand_->hide();
    }
}

void GLWidget::rebuildPartLookup()
{
    int numParts = 0;
    for (int part : triToPart_) {
        if (part >= 0) numParts = std::max(numParts, part + 1);
    }
    for (int part : edgeToPart_) {
        if (part >= 0) numParts = std::max(numParts, part + 1);
    }

    partColors_.resize(numParts);
    for (int i = 0; i < numParts; ++i)
        partColors_[i] = kPartPalette[i % kPartPaletteSize];

    partTriangles_.clear();
    partTriangles_.resize(numParts);
    partElementIds_.clear();
    partElementIds_.resize(numParts);
    elemToPart_.clear();

    std::vector<std::unordered_set<int>> partElementSets(numParts);

    const int triCount = std::min(static_cast<int>(triToPart_.size()),
                                  static_cast<int>(triToElem_.size()));
    for (int t = 0; t < triCount; ++t) {
        const int part = triToPart_[t];
        const int element = triToElem_[t];
        if (part < 0 || part >= numParts || element < 0) continue;
        partTriangles_[part].push_back(t);
        partElementSets[part].insert(element);
    }

    const int edgeCount = std::min(static_cast<int>(edgeToPart_.size()),
                                   static_cast<int>(mesh_.edgeToElement.size()));
    for (int edge = 0; edge < edgeCount; ++edge) {
        const int part = edgeToPart_[edge];
        const int element = mesh_.edgeToElement[edge];
        if (part < 0 || part >= numParts || element < 0) continue;
        partElementSets[part].insert(element);
    }

    for (int part = 0; part < numParts; ++part) {
        auto& ids = partElementIds_[part];
        ids.assign(partElementSets[part].begin(), partElementSets[part].end());
        std::sort(ids.begin(), ids.end());
        for (int element : ids) {
            elemToPart_[element] = part;
        }
    }
}

void GLWidget::selectPart(int partIndex) {
    if (partIndex < 0 || partIndex >= static_cast<int>(partElementIds_.size())) return;
    if (!isPartVisible(partIndex)) return;
    for (int eid : partElementIds_[partIndex]) {
        selection_.selectedElements.insert(eid);
    }
}

void GLWidget::deselectPart(int partIndex) {
    if (partIndex < 0 || partIndex >= static_cast<int>(partElementIds_.size())) return;
    for (int eid : partElementIds_[partIndex]) {
        selection_.selectedElements.erase(eid);
    }
}

bool GLWidget::isPartFullySelected(int partIndex) const {
    if (partIndex < 0 || partIndex >= static_cast<int>(partElementIds_.size())) return false;
    const auto& elems = partElementIds_[partIndex];
    if (elems.empty()) return false;
    for (int eid : elems) {
        if (!selection_.isElementSelected(eid)) return false;
    }
    return true;
}

bool GLWidget::isPartVisible(int partIndex) const
{
    if (partIndex < 0) {
        return true;
    }
    auto it = partVisibility_.find(partIndex);
    return it == partVisibility_.end() || it->second;
}

bool GLWidget::isTriangleVisible(int triangleIndex) const
{
    if (triangleIndex < 0 || triangleIndex >= static_cast<int>(triToElem_.size())) {
        return false;
    }
    const int partIndex = triangleIndex < static_cast<int>(triToPart_.size())
        ? triToPart_[triangleIndex]
        : -1;
    return isPartVisible(partIndex);
}

bool GLWidget::isElementVisibleForSelection(int elementId) const
{
    auto it = elemToPart_.find(elementId);
    if (it == elemToPart_.end()) {
        return true;
    }
    return isPartVisible(it->second);
}

bool GLWidget::isNodeVisibleForSelection(int nodeId) const
{
    const int triCount = static_cast<int>(triToElem_.size());
    for (int t = 0; t < triCount; ++t) {
        if (!isTriangleVisible(t)) continue;
        for (int corner = 0; corner < 3; ++corner) {
            const unsigned int vertexIndex = mesh_.indices[t * 3 + corner];
            const int mappedNode = vertexIndex < vertexToNode_.size()
                ? vertexToNode_[vertexIndex]
                : static_cast<int>(vertexIndex);
            if (mappedNode == nodeId) {
                return true;
            }
        }
    }
    const int elemEdgeCount = std::min(static_cast<int>(mesh_.elemEdgeToElement.size()),
                                       static_cast<int>(mesh_.elemEdgeNodeIds.size()));
    for (int edge = 0; edge < elemEdgeCount; ++edge) {
        const int elementId = mesh_.elemEdgeToElement[edge];
        if (!isElementVisibleForSelection(elementId)) continue;
        const auto [node0, node1] = mesh_.elemEdgeNodeIds[edge];
        if (node0 == nodeId || node1 == nodeId) {
            return true;
        }
    }
    return false;
}

void GLWidget::rebuildSelectionEdges() {
    int edgeCount = static_cast<int>(mesh_.elemEdgeToElement.size());
    std::vector<float> verts;

    if (pickMode_ == PickMode::Part && !vertexToNode_.empty()) {
        // 部件模式：使用缓存机制，避免每帧重建 edgeMap
        if (!partEdgeCacheValid_) {
            buildPartEdgeCache();
        }
        updateSilhouetteFromCache();
        return;  // VBO 已在 updateSilhouetteFromCache 中上传
    } else {
        // 单元模式：显示所有选中单元的全部边线
        for (int i = 0; i < edgeCount; ++i) {
            if (!selection_.isElementSelected(mesh_.elemEdgeToElement[i])) continue;

            int base = i * 6;
            for (int j = 0; j < 6; ++j)
                verts.push_back(mesh_.elemEdgeVertices[base + j]);
        }
    }

    selEdgeVertCount_ = static_cast<int>(verts.size() / 3);

    if (selectionEdgeResource_) {
        auto* glBackend = openGLBackend();
        glBackend->uploadLineVertices(
            *selectionEdgeResource_,
            verts.data(),
            static_cast<int>(verts.size() * sizeof(float)));
    }
}

void GLWidget::buildEdgeAdjacency() {
    edgeAdjMap_.clear();
    edgeAdjDirty_ = false;

    int triCount = static_cast<int>(mesh_.indices.size() / 3);
    if (triCount == 0) return;

    // 预分配（每个三角形 3 条边，约 50% 共享 → ~1.5x triCount 条边）
    edgeAdjMap_.reserve(triCount * 2);

    for (int t = 0; t < triCount; ++t) {
        for (int e = 0; e < 3; ++e) {
            unsigned int vi_a = mesh_.indices[t * 3 + e];
            unsigned int vi_b = mesh_.indices[t * 3 + (e + 1) % 3];
            int na = (vi_a < vertexToNode_.size()) ? vertexToNode_[vi_a] : static_cast<int>(vi_a);
            int nb = (vi_b < vertexToNode_.size()) ? vertexToNode_[vi_b] : static_cast<int>(vi_b);
            int64_t key = (static_cast<int64_t>(std::min(na, nb)) << 32) | static_cast<uint32_t>(std::max(na, nb));

            auto& pe = edgeAdjMap_[key];
            if (pe.adjTris.empty()) { pe.va = vi_a; pe.vb = vi_b; }
            pe.adjTris.push_back(t);
        }
    }
}

void GLWidget::buildPartEdgeCache() {
    // 确保边邻接表已构建
    if (edgeAdjDirty_) buildEdgeAdjacency();

    cachedStaticEdgeVerts_.clear();
    cachedSilhouettes_.clear();

    // ── 1. 收集选中且可见的部件索引 ──
    std::unordered_set<int> selectedParts;
    int numParts = static_cast<int>(partElementIds_.size());
    for (int p = 0; p < numParts; ++p) {
        auto vit = partVisibility_.find(p);
        if (vit != partVisibility_.end() && !vit->second) continue;
        for (int eid : partElementIds_[p]) {
            if (selection_.isElementSelected(eid)) {
                selectedParts.insert(p);
                break;
            }
        }
    }

    if (selectedParts.empty()) {
        partEdgeCacheValid_ = true;
        return;
    }

    // ── 2. 只遍历选中部件的三角形，收集边并分类 ──
    const float featureAngleThreshold = 0.5f;  // cos(60°)

    auto triNormal = [&](int t) -> glm::vec3 {
        unsigned int i0 = mesh_.indices[t * 3];
        unsigned int i1 = mesh_.indices[t * 3 + 1];
        unsigned int i2 = mesh_.indices[t * 3 + 2];
        glm::vec3 p0(mesh_.vertices[i0 * 6], mesh_.vertices[i0 * 6 + 1], mesh_.vertices[i0 * 6 + 2]);
        glm::vec3 p1(mesh_.vertices[i1 * 6], mesh_.vertices[i1 * 6 + 1], mesh_.vertices[i1 * 6 + 2]);
        glm::vec3 p2(mesh_.vertices[i2 * 6], mesh_.vertices[i2 * 6 + 1], mesh_.vertices[i2 * 6 + 2]);
        glm::vec3 cr = glm::cross(p1 - p0, p2 - p0);
        float len = glm::length(cr);
        return (len > 1e-12f) ? cr / len : glm::vec3(0.0f);
    };

    auto pushEdgeVerts = [&](unsigned int a, unsigned int b, std::vector<float>& out) {
        out.push_back(mesh_.vertices[a * 6]);
        out.push_back(mesh_.vertices[a * 6 + 1]);
        out.push_back(mesh_.vertices[a * 6 + 2]);
        out.push_back(mesh_.vertices[b * 6]);
        out.push_back(mesh_.vertices[b * 6 + 1]);
        out.push_back(mesh_.vertices[b * 6 + 2]);
    };

    // 用 visited 集合确保每条边只处理一次
    std::unordered_set<int64_t> visitedEdges;

    // 预估容量（减少 rehash）
    int totalSelectedTris = 0;
    for (int p : selectedParts) totalSelectedTris += static_cast<int>(partTriangles_[p].size());
    visitedEdges.reserve(totalSelectedTris * 2);
    cachedStaticEdgeVerts_.reserve(totalSelectedTris * 6);

    for (int p : selectedParts) {
        for (int t : partTriangles_[p]) {
            for (int e = 0; e < 3; ++e) {
                unsigned int vi_a = mesh_.indices[t * 3 + e];
                unsigned int vi_b = mesh_.indices[t * 3 + (e + 1) % 3];
                int na = (vi_a < vertexToNode_.size()) ? vertexToNode_[vi_a] : static_cast<int>(vi_a);
                int nb = (vi_b < vertexToNode_.size()) ? vertexToNode_[vi_b] : static_cast<int>(vi_b);
                int64_t key = (static_cast<int64_t>(std::min(na, nb)) << 32) | static_cast<uint32_t>(std::max(na, nb));

                if (!visitedEdges.insert(key).second) continue;  // 已处理

                auto it = edgeAdjMap_.find(key);
                if (it == edgeAdjMap_.end()) continue;

                const PreEdge& pe = it->second;

                // 分类邻接三角形
                int selectedTriCount = 0;
                int otherTriCount = 0;
                int selTri0 = -1, selTri1 = -1;

                for (int adjT : pe.adjTris) {
                    int adjPart = (adjT < static_cast<int>(triToPart_.size())) ? triToPart_[adjT] : -1;
                    if (adjPart >= 0 && selectedParts.count(adjPart)) {
                        if (selectedTriCount == 0) selTri0 = adjT;
                        else if (selectedTriCount == 1) selTri1 = adjT;
                        selectedTriCount++;
                    } else {
                        otherTriCount++;
                    }
                }

                // 边界边（与非选中部件共享）
                if (otherTriCount > 0) {
                    pushEdgeVerts(pe.va, pe.vb, cachedStaticEdgeVerts_);
                    continue;
                }
                // 开放边（只有一个三角形）
                if (selectedTriCount == 1) {
                    pushEdgeVerts(pe.va, pe.vb, cachedStaticEdgeVerts_);
                    continue;
                }
                // 特征边 or 轮廓边候选
                if (selectedTriCount >= 2 && selTri0 >= 0 && selTri1 >= 0) {
                    glm::vec3 n0 = triNormal(selTri0);
                    glm::vec3 n1 = triNormal(selTri1);
                    if (glm::dot(n0, n1) < featureAngleThreshold) {
                        pushEdgeVerts(pe.va, pe.vb, cachedStaticEdgeVerts_);
                    } else {
                        SilhouetteCandidate sc;
                        sc.ax = mesh_.vertices[pe.va * 6];
                        sc.ay = mesh_.vertices[pe.va * 6 + 1];
                        sc.az = mesh_.vertices[pe.va * 6 + 2];
                        sc.bx = mesh_.vertices[pe.vb * 6];
                        sc.by = mesh_.vertices[pe.vb * 6 + 1];
                        sc.bz = mesh_.vertices[pe.vb * 6 + 2];
                        sc.n0 = n0;
                        sc.n1 = n1;
                        cachedSilhouettes_.push_back(sc);
                    }
                }
            }
        }
    }

    partEdgeCacheValid_ = true;
}

void GLWidget::updateSilhouetteFromCache() {
    // 预分配：静态边 + 最大可能的轮廓边
    size_t staticSize = cachedStaticEdgeVerts_.size();
    std::vector<float> verts;
    verts.reserve(staticSize + cachedSilhouettes_.size() * 6);

    // 复制静态边（边界/特征/开放）
    verts.insert(verts.end(), cachedStaticEdgeVerts_.begin(), cachedStaticEdgeVerts_.end());

    // 添加视角依赖的轮廓边
    glm::vec3 eyePos = cam_.eye();
    for (const auto& sc : cachedSilhouettes_) {
        glm::vec3 edgeMid((sc.ax + sc.bx) * 0.5f,
                          (sc.ay + sc.by) * 0.5f,
                          (sc.az + sc.bz) * 0.5f);
        glm::vec3 viewDir = eyePos - edgeMid;
        float d0 = glm::dot(sc.n0, viewDir);
        float d1 = glm::dot(sc.n1, viewDir);
        if (d0 * d1 <= 0.0f) {
            verts.push_back(sc.ax); verts.push_back(sc.ay); verts.push_back(sc.az);
            verts.push_back(sc.bx); verts.push_back(sc.by); verts.push_back(sc.bz);
        }
    }

    selEdgeVertCount_ = static_cast<int>(verts.size() / 3);

    if (selectionEdgeResource_) {
        auto* glBackend = openGLBackend();
        glBackend->uploadLineVertices(
            *selectionEdgeResource_,
            verts.data(),
            static_cast<int>(verts.size() * sizeof(float)));
    }
}

glm::mat4 GLWidget::axesIndicatorMvp() const
{
    glm::mat3 rot = glm::mat3(cam_.viewMatrix());
    glm::vec3 axesEye = glm::vec3(rot[0][2], rot[1][2], rot[2][2]) * 2.5f;
    glm::mat4 axesView = glm::lookAt(axesEye, glm::vec3(0), glm::vec3(0, 1, 0));
    glm::mat4 axesProj = glm::ortho(-1.3f, 1.3f, -1.3f, 1.3f, 0.01f, 10.0f);
    return axesProj * axesView;
}

void GLWidget::drawAxesIndicator() {
    if (!axesGeometry_) return;

    const int axesSize = kAxesViewportSize;
    const int margin = kAxesMargin;
    const int dpr = devicePixelRatio();

    // 仅旋转的 view 矩阵（固定距离，跟随相机朝向）
    glm::mat4 axesMVP = axesIndicatorMvp();

    // ── 左下角小视口绘制 ──
    auto* glBackend = openGLBackend();
    glBackend->setViewport(margin * dpr, margin * dpr, axesSize * dpr, axesSize * dpr);
    glBackend->clearDepthBuffer();

    axesShader_->bind();
    glBackend->setMvpUniform(*axesShader_,
                             QMatrix4x4(glm::value_ptr(glm::transpose(axesMVP))));

    // 绘制实心几何体（圆柱轴杆 + 圆锥箭头 + 球心）
    if (axesLineCount_ > 0) {
        ScenePassState linePassState;
        linePassState.applyLineWidth = true;
        linePassState.lineWidth = 2.5f;
        linePassState.restoredLineWidth = 1.0f;
        linePassState.applyDepthTest = true;
        linePassState.depthTestEnabled = true;
        linePassState.restoredDepthTestEnabled = true;
        glBackend->drawArraysPass(*axesGeometry_, ScenePrimitive::Lines, 0, axesLineCount_, linePassState);
    }
    ScenePassState triPassState;
    triPassState.applyDepthTest = true;
    triPassState.depthTestEnabled = true;
    triPassState.restoredDepthTestEnabled = true;
    glBackend->drawArraysPass(*axesGeometry_,
                              ScenePrimitive::Triangles,
                              axesLineCount_,
                              axesTriCount_,
                              triPassState);

    axesShader_->release();

    // 恢复主视口
    glBackend->setViewport(0, 0, width() * dpr, height() * dpr);

    // 保存投影参数，供 drawAxesLabels() 使用
    axesMVP_ = axesMVP;
}

void GLWidget::drawAxesLabels(QPainter& painter) {
    const int axesSize = kAxesViewportSize;
    const int margin = kAxesMargin;

    auto project = [&](glm::vec3 pt) -> QPointF {
        glm::vec4 clip = axesMVP_ * glm::vec4(pt, 1.0f);
        float sx = margin + (clip.x / clip.w * 0.5f + 0.5f) * axesSize;
        float sy = height() - margin - (clip.y / clip.w * 0.5f + 0.5f) * axesSize;
        return QPointF(sx, sy);
    };

    struct AxisLabel { glm::vec3 dir; QString name; QColor color; };
    AxisLabel labels[] = {
        {{1,0,0}, "X", QColor(240, 80, 80)},
        {{0,1,0}, "Y", QColor(90, 220, 90)},
        {{0,0,1}, "Z", QColor(90, 140, 255)},
    };

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(17);
    painter.setFont(font);

    for (const auto& l : labels) {
        QPointF pos = project(l.dir * 1.15f);
        painter.setPen(l.color);
        painter.drawText(QRectF(pos.x() - kAxesLabelSize / 2.0f,
                                pos.y() - kAxesLabelSize / 2.0f,
                                kAxesLabelSize,
                                kAxesLabelSize),
                         Qt::AlignCenter, l.name);
    }
}

bool GLWidget::standardViewFromAxesClick(const QPoint& pos, StandardView* view) const
{
    if (!view || width() <= kAxesMargin * 2 || height() <= kAxesMargin * 2) {
        return false;
    }

    const glm::mat4 axesMVP = axesIndicatorMvp();
    auto project = [&](glm::vec3 pt) -> QPointF {
        glm::vec4 clip = axesMVP * glm::vec4(pt, 1.0f);
        if (std::abs(clip.w) <= 1.0e-6f) {
            return QPointF(-10000.0, -10000.0);
        }
        const float sx = kAxesMargin + (clip.x / clip.w * 0.5f + 0.5f) * kAxesViewportSize;
        const float sy = height() - kAxesMargin - (clip.y / clip.w * 0.5f + 0.5f) * kAxesViewportSize;
        return QPointF(sx, sy);
    };

    const QPointF p(pos);
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
        if (labelRect.contains(p)) {
            *view = axisViews[i];
            return true;
        }

        const float dist2 = ScreenSpacePicking::distanceSquaredToSegment(p, origin, end);
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

void GLWidget::setShowLabels(bool show) {
    if (showLabels_ != show) {
        showLabels_ = show;
        update();
    }
}

void GLWidget::selectByIds(PickMode mode, const std::vector<int>& ids) {
    pickMode_ = mode;
    selection_.clear();

    if (mode == PickMode::Node) {
        // 过滤：只保留网格中实际存在的节点 ID
        std::unordered_set<int> validNodes(vertexToNode_.begin(), vertexToNode_.end());
        for (int id : ids) {
            if (validNodes.count(id) && isNodeVisibleForSelection(id))
                selection_.selectedNodes.insert(id);
        }
    } else if (mode == PickMode::Part) {
        for (int pi : ids) selectPart(pi);
    } else {
        // 过滤：只保留渲染网格中存在的单元 ID（含三角面和 1D 边线）
        std::unordered_set<int> validElems(triToElem_.begin(), triToElem_.end());
        for (int eid : mesh_.elemEdgeToElement)
            validElems.insert(eid);
        for (int id : ids) {
            if (validElems.count(id) && isElementVisibleForSelection(id))
                selection_.selectedElements.insert(id);
        }
    }

    selectionDirty_ = true;

    // 发射选中变更信号（只包含实际匹配的 ID）
    std::vector<int> matchedIds;
    if (mode == PickMode::Node) {
        matchedIds.assign(selection_.selectedNodes.begin(), selection_.selectedNodes.end());
    } else if (mode == PickMode::Part) {
        for (int pi = 0; pi < static_cast<int>(partElementIds_.size()); ++pi)
            if (isPartFullySelected(pi)) matchedIds.push_back(pi);
    } else {
        matchedIds.assign(selection_.selectedElements.begin(), selection_.selectedElements.end());
    }
    std::sort(matchedIds.begin(), matchedIds.end());
    emit selectionChanged(mode, static_cast<int>(matchedIds.size()), matchedIds);

    if (mode == PickMode::Part) {
        std::vector<int> pickedParts;
        for (int pi = 0; pi < static_cast<int>(partElementIds_.size()); ++pi)
            if (isPartFullySelected(pi)) pickedParts.push_back(pi);
        emit partsPicked(pickedParts);
    }

    update();
}

void GLWidget::drawIdLabels(QPainter& painter, const glm::mat4& mvp) {
    int w = width();
    int h = height();

    // 世界坐标 → 屏幕坐标
    auto project = [&](const glm::vec3& pos) -> QPointF {
        glm::vec4 clip = mvp * glm::vec4(pos, 1.0f);
        if (clip.w <= 0.0f) return QPointF(-1, -1);
        float nx = clip.x / clip.w;
        float ny = clip.y / clip.w;
        float sx = (nx * 0.5f + 0.5f) * w;
        float sy = (1.0f - (ny * 0.5f + 0.5f)) * h;
        return QPointF(sx, sy);
    };

    QFont font = painter.font();
    font.setPixelSize(11);
    font.setBold(true);
    painter.setFont(font);

    // 描边文字：深色轮廓 + 亮色正文（避免 drawRect 导致 GL 状态崩溃）
    QColor outlineColor(0, 0, 0, 220);
    QColor textColor(255, 200, 0);
    const int offsetY = -14;  // 标签偏移到实体上方

    // 绘制带描边的文字（4方向偏移描边 + 正文叠加）
    auto drawOutlinedText = [&](int x, int y, const QString& text) {
        painter.setPen(outlineColor);
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                if (dx != 0 || dy != 0)
                    painter.drawText(x + dx, y + dy, text);
        painter.setPen(textColor);
        painter.drawText(x, y, text);
    };

    QFontMetrics fm(font);

    if (pickMode_ == PickMode::Node) {
        // ── 节点标签 ──
        if (!selection_.selectedNodes.empty() && !vertexToNode_.empty()) {
            std::unordered_map<int, int> nodeToVert;
            for (int i = 0; i < static_cast<int>(vertexToNode_.size()); ++i) {
                int nid = vertexToNode_[i];
                if (nid >= 0 && nodeToVert.find(nid) == nodeToVert.end())
                    nodeToVert[nid] = i;
            }

            for (int nid : selection_.selectedNodes) {
                auto it = nodeToVert.find(nid);
                if (it == nodeToVert.end()) continue;
                int vi = it->second;
                if (vi * 6 + 2 >= static_cast<int>(mesh_.vertices.size())) continue;

                glm::vec3 pos(mesh_.vertices[vi * 6],
                              mesh_.vertices[vi * 6 + 1],
                              mesh_.vertices[vi * 6 + 2]);
                QPointF sp = project(pos);
                if (sp.x() < 0) continue;

                QString text = QString::number(nid);
                int tx = static_cast<int>(sp.x()) - fm.horizontalAdvance(text) / 2;
                int ty = static_cast<int>(sp.y()) + offsetY;
                drawOutlinedText(tx, ty, text);
            }
        }

    } else if (pickMode_ == PickMode::Part) {
        // ── 部件标签（在部件重心位置显示部件索引） ──
        if (!selection_.selectedElements.empty() && !triToElem_.empty() && !triToPart_.empty()) {
            // 收集选中的部件索引
            std::set<int> selectedParts;
            for (int pi = 0; pi < static_cast<int>(partElementIds_.size()); ++pi) {
                if (isPartFullySelected(pi))
                    selectedParts.insert(pi);
            }

            // 计算每个选中部件的重心
            for (int pi : selectedParts) {
                if (pi < 0 || pi >= static_cast<int>(partTriangles_.size())) continue;
                float sx = 0, sy = 0, sz = 0;
                int count = 0;
                for (int t : partTriangles_[pi]) {
                    if (t * 3 + 2 >= static_cast<int>(mesh_.indices.size())) continue;
                    for (int k = 0; k < 3; ++k) {
                        unsigned int vi = mesh_.indices[t * 3 + k];
                        if (vi * 6 + 2 < mesh_.vertices.size()) {
                            sx += mesh_.vertices[vi * 6];
                            sy += mesh_.vertices[vi * 6 + 1];
                            sz += mesh_.vertices[vi * 6 + 2];
                            count++;
                        }
                    }
                }
                if (count == 0) continue;
                glm::vec3 center(sx / count, sy / count, sz / count);
                QPointF sp = project(center);
                if (sp.x() < 0) continue;

                QString text = QString("Part %1").arg(pi + 1);
                int tx = static_cast<int>(sp.x()) - fm.horizontalAdvance(text) / 2;
                int ty = static_cast<int>(sp.y()) + offsetY;
                drawOutlinedText(tx, ty, text);
            }
        }

    } else {
        // ── 单元标签（在单元重心位置显示） ──
        if (!selection_.selectedElements.empty() && !triToElem_.empty()) {
            struct ElemAccum { float sx = 0, sy = 0, sz = 0; int count = 0; };
            std::unordered_map<int, ElemAccum> elemCentroids;

            int triCount = static_cast<int>(triToElem_.size());
            int idxCount = static_cast<int>(mesh_.indices.size());
            for (int t = 0; t < triCount; ++t) {
                if (t * 3 + 2 >= idxCount) break;
                int eid = triToElem_[t];
                if (selection_.selectedElements.count(eid) == 0) continue;
                auto& acc = elemCentroids[eid];
                for (int k = 0; k < 3; ++k) {
                    unsigned int vi = mesh_.indices[t * 3 + k];
                    if (vi * 6 + 2 < mesh_.vertices.size()) {
                        acc.sx += mesh_.vertices[vi * 6];
                        acc.sy += mesh_.vertices[vi * 6 + 1];
                        acc.sz += mesh_.vertices[vi * 6 + 2];
                        acc.count++;
                    }
                }
            }

            for (const auto& [eid, acc] : elemCentroids) {
                if (acc.count == 0) continue;
                glm::vec3 center(acc.sx / acc.count, acc.sy / acc.count, acc.sz / acc.count);
                QPointF sp = project(center);
                if (sp.x() < 0) continue;

                QString text = QString::number(eid);
                int tx = static_cast<int>(sp.x()) - fm.horizontalAdvance(text) / 2;
                int ty = static_cast<int>(sp.y()) + offsetY;
                drawOutlinedText(tx, ty, text);
            }
        }
    }
}

void GLWidget::setColorBarVisible(bool visible) {
    colorBarVisible_ = visible;
    if (colorBarOverlay_) {
        colorBarOverlay_->setVisible(visible);
        colorBarOverlay_->resize(size());
    }
    update();
}

void GLWidget::setColorBarRange(float min, float max) {
    colorBarMin_ = min;
    colorBarMax_ = max;
    if (colorBarOverlay_) colorBarOverlay_->setRange(min, max);
    update();
}

void GLWidget::setColorBarTitle(const QString& title) {
    colorBarTitle_ = title;
    if (colorBarOverlay_) colorBarOverlay_->setTitle(title);
    update();
}

void GLWidget::setColorBarExtremes(int minId, float minVal, int maxId, float maxVal) {
    if (colorBarOverlay_) colorBarOverlay_->setExtremes(minId, minVal, maxId, maxVal);
    update();
}

void GLWidget::setColorBarIdLabel(const QString& label) {
    if (colorBarOverlay_) colorBarOverlay_->setIdLabel(label);
    update();
}

void GLWidget::applyTheme(const Theme& theme) {
    // 更新色标文字颜色
    barTextColor_ = QColor(theme.barTextR, theme.barTextG, theme.barTextB);
    if (colorBarOverlay_) colorBarOverlay_->setTextColor(barTextColor_);

    // 存储背景颜色（initializeGL 会使用）
    bgTopColor_[0] = theme.bgTopR; bgTopColor_[1] = theme.bgTopG; bgTopColor_[2] = theme.bgTopB;
    bgBotColor_[0] = theme.bgBotR; bgBotColor_[1] = theme.bgBotG; bgBotColor_[2] = theme.bgBotB;

    // 更新渐变背景 VBO（仅在 GL 已初始化时）
    if (backgroundGeometry_) {
        float bgData[] = {
            -1, -1,  theme.bgBotR, theme.bgBotG, theme.bgBotB,
             1, -1,  theme.bgBotR, theme.bgBotG, theme.bgBotB,
             1,  1,  theme.bgTopR, theme.bgTopG, theme.bgTopB,
            -1, -1,  theme.bgBotR, theme.bgBotG, theme.bgBotB,
             1,  1,  theme.bgTopR, theme.bgTopG, theme.bgTopB,
            -1,  1,  theme.bgTopR, theme.bgTopG, theme.bgTopB,
        };
        makeCurrent();
        openGLBackend()->updatePositionColorGeometry(*backgroundGeometry_,
                                                     bgData,
                                                     sizeof(bgData));
        doneCurrent();
    }
    update();
}

void GLWidget::setTriangleToPartMap(const std::vector<int>& map) {
    triToPart_ = map;
    int triCount = static_cast<int>(mesh_.indices.size() / 3);
    alignSize(triToPart_, triCount, -1);
    rebuildPartLookup();

    // 上传 triToPart 到 texture buffer（供片段着色器用 gl_PrimitiveID 查表）
    triPartDirty_ = true;
    needsColorUpload_ = true;
    update();
}

void GLWidget::setEdgeToPartMap(const std::vector<int>& map) {
    edgeToPart_ = map;
    int edgeCount = static_cast<int>(mesh_.edgeIndices.size() / 2);
    alignSize(edgeToPart_, edgeCount, -1);
    rebuildPartLookup();
    edgeVisibilityDirty_ = true;
    update();
}

void GLWidget::setPartVisibility(int partIndex, bool visible) {
    partVisibility_[partIndex] = visible;
    partVisibilityDirty_ = true;
    edgeVisibilityDirty_ = true;
    bool selectionChangedNow = false;
    for (auto it = selection_.selectedElements.begin(); it != selection_.selectedElements.end();) {
        if (!isElementVisibleForSelection(*it)) {
            it = selection_.selectedElements.erase(it);
            selectionChangedNow = true;
        } else {
            ++it;
        }
    }
    for (auto it = selection_.selectedNodes.begin(); it != selection_.selectedNodes.end();) {
        if (!isNodeVisibleForSelection(*it)) {
            it = selection_.selectedNodes.erase(it);
            selectionChangedNow = true;
        } else {
            ++it;
        }
    }
    // 可见性变化影响选中高亮（隐藏部件不显示高亮）
    if (selection_.hasSelection() || selectionChangedNow) {
        partEdgeCacheValid_ = false;
        selectionDirty_ = true;
    }
    if (selectionChangedNow) {
        std::vector<int> ids;
        if (pickMode_ == PickMode::Node)
            ids.assign(selection_.selectedNodes.begin(), selection_.selectedNodes.end());
        else
            ids.assign(selection_.selectedElements.begin(), selection_.selectedElements.end());
        std::sort(ids.begin(), ids.end());
        emit selectionChanged(pickMode_, static_cast<int>(ids.size()), ids);
        if (pickMode_ == PickMode::Part) {
            std::vector<int> pickedParts;
            for (int pi = 0; pi < static_cast<int>(partElementIds_.size()); ++pi)
                if (isPartFullySelected(pi)) pickedParts.push_back(pi);
            emit partsPicked(pickedParts);
        }
    }
    update();
}

void GLWidget::highlightParts(const std::vector<int>& partIndices) {
    // 清除当前选中
    selection_.selectedElements.clear();
    selection_.selectedNodes.clear();

    // 将指定部件的所有单元加入选中
    for (int pi : partIndices)
        selectPart(pi);

    // 触发高亮重建
    partEdgeCacheValid_ = false;
    selectionDirty_ = true;

    // 切换到部件拾取模式以使用轮廓边高亮
    pickMode_ = PickMode::Part;

    emit selectionChanged(pickMode_,
                          static_cast<int>(selection_.selectedElements.size()),
                          std::vector<int>(selection_.selectedElements.begin(),
                                           selection_.selectedElements.end()));
    update();
}

void GLWidget::uploadColors() {
    needsColorUpload_ = false;

    // 云图模式下颜色由片段着色器从标量值生成，不需要更新
    if (useVertexColor_) return;

    if (triToPart_.empty()) return;

    // 上传 triToPart 到 texture buffer（供片段着色器用 gl_PrimitiveID 查表）
    if (triPartDirty_) {
        triPartDirty_ = false;
        std::vector<float> triPartData(triToPart_.size());
        for (size_t i = 0; i < triToPart_.size(); ++i)
            triPartData[i] = static_cast<float>(triToPart_[i]);
        auto* glBackend = openGLBackend();
        if (triPartTextureBuffer_) {
            glBackend->uploadTextureBuffer(
                *triPartTextureBuffer_,
                triPartData.data(),
                static_cast<int>(triPartData.size() * sizeof(float)));
        }
    }
}

void GLWidget::rebuildEdgeIbo() {
    edgeVisibilityDirty_ = false;
    if (allEdgeIndices_.empty()) return;

    std::vector<unsigned int> filtered;
    filtered.reserve(allEdgeIndices_.size());
    int edgeCount = static_cast<int>(allEdgeIndices_.size() / 2);
    for (int e = 0; e < edgeCount; ++e) {
        int part = (e < static_cast<int>(edgeToPart_.size())) ? edgeToPart_[e] : -1;
        if (part >= 0) {
            auto it = partVisibility_.find(part);
            if (it != partVisibility_.end() && !it->second)
                continue;   // 该部件不可见，跳过此边
        }
        filtered.push_back(allEdgeIndices_[e * 2]);
        filtered.push_back(allEdgeIndices_[e * 2 + 1]);
    }
    activeEdgeIndexCount_ = static_cast<int>(filtered.size());

    if (edgeResource_) {
        auto* glBackend = openGLBackend();
        glBackend->uploadEdgeIndexBuffer(
            *edgeResource_,
            filtered.data(),
            static_cast<int>(filtered.size() * sizeof(unsigned int)));
    }
}

void GLWidget::uploadMesh() {
    needsUpload_ = false;
    if (!meshResource_) return;

    auto* glBackend = openGLBackend();

    activeIndexCount_ = static_cast<int>(mesh_.indices.size());
    glBackend->uploadMeshBuffers(*meshResource_,
                                 mesh_.vertices.data(),
                                 static_cast<int>(mesh_.vertices.size() * sizeof(float)),
                                 mesh_.indices.data(),
                                 static_cast<int>(mesh_.indices.size() * sizeof(unsigned int)));

    // ── 颜色缓冲（per-vertex，默认为 color_） ──
    {
        int vertCount = static_cast<int>(mesh_.vertices.size() / 6);
        std::vector<float> defaultColors(vertCount * 3);
        for (int v = 0; v < vertCount; ++v) {
            defaultColors[v * 3 + 0] = color_.x;
            defaultColors[v * 3 + 1] = color_.y;
            defaultColors[v * 3 + 2] = color_.z;
        }
        glBackend->uploadMeshColorBuffer(*meshResource_,
                                         defaultColors.data(),
                                         static_cast<int>(defaultColors.size() * sizeof(float)));
    }

    // ── 标量值缓冲（per-vertex，默认全 0） ──
    {
        int vertCount = static_cast<int>(mesh_.vertices.size() / 6);
        std::vector<float> defaultScalars(vertCount, 0.0f);
        glBackend->uploadMeshScalarBuffer(*meshResource_,
                                          defaultScalars.data(),
                                          static_cast<int>(defaultScalars.size() * sizeof(float)));
    }

    // ── 上传边线数据（如果有） ──
    edgeIndexCount_ = static_cast<int>(mesh_.edgeIndices.size());
    activeEdgeIndexCount_ = edgeIndexCount_;
    if (edgeIndexCount_ > 0 && edgeResource_) {
        glBackend->uploadEdgeBuffers(
            *edgeResource_,
            mesh_.edgeVertices.data(),
            static_cast<int>(mesh_.edgeVertices.size() * sizeof(float)),
            mesh_.edgeIndices.data(),
            static_cast<int>(mesh_.edgeIndices.size() * sizeof(unsigned int)));
        const int edgeVertCount = static_cast<int>(mesh_.edgeVertices.size() / 3);
        const std::vector<float> defaultEdgeScalars(edgeVertCount, 0.0f);
        glBackend->uploadEdgeScalarBuffer(
            *edgeResource_,
            defaultEdgeScalars.data(),
            static_cast<int>(defaultEdgeScalars.size() * sizeof(float)));
    }
}
