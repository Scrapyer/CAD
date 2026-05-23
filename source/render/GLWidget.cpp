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

namespace {
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
    hiddenElementIds_.clear();
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
    selectionEdgeCacheUsesSilhouette_ = false;
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
    if (selection_.hasSelection() &&
        (pickMode_ == PickMode::Part || selectionEdgeCacheUsesSilhouette_)) {
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
