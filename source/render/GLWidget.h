/**
 * @file GLWidget.h
 * @brief OpenGL 渲染窗口组件声明
 *
 * GLWidget 继承自 QOpenGLWidget，负责：
 *   - 初始化 OpenGL 上下文、编译着色器
 *   - 上传网格数据到 GPU（VAO/VBO/IBO）
 *   - 每帧渲染场景（FE 平光 + 网格线框）
 *   - 处理鼠标/键盘事件（轨道交互 + 拾取）
 *   - GPU Color Picking（点选 + 框选）
 *   - 统计 FPS 和查询硬件信息
 */

#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QElapsedTimer>
#include <QRubberBand>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_set>
#include <unordered_map>
#include <set>
#include <vector>
#include <memory>

#include <array>

#include "Camera.h"
#include "Geometry.h"
#include "FEPickResult.h"
#include "RenderBackend.h"
#include "ferender_export.h"

struct Theme;
class ColorBarOverlay;   // 前向声明：色标覆盖层控件
class IRenderBackend;
class OpenGLRenderBackend;
struct OpenGLEdgeResource;
struct OpenGLFramebuffer;
struct OpenGLLineResource;
struct OpenGLMeshResource;
struct OpenGLPositionColorGeometry;
struct OpenGLTextureBufferResource;
struct OpenGLVertexArrayResource;

class FERENDER_EXPORT GLWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit GLWidget(QWidget* parent = nullptr);
    ~GLWidget() override;

    /** @brief 设置要渲染的网格数据 */
    void setMesh(const Mesh& mesh);

    /** @brief 直接设置 per-vertex 颜色到主网格颜色缓冲（用于云图） */
    void setVertexColors(const std::vector<float>& colors);

    /** @brief 设置物体颜色 */
    void setObjectColor(const glm::vec3& c);

    /** @brief 设置主模型显示模式。 */
    void setModelDisplayMode(ModelDisplayMode mode);

    /** @brief 显示/隐藏视口背景辅助网格。 */
    void setViewportGridVisible(bool visible);

    /** @brief 自适应缩放 */
    void fitToModel(const glm::vec3& center, float size);

    /** @brief 切换到标准正交观察方向（前/后/左/右/上/下）。 */
    void setStandardView(StandardView view);

    /** @brief 应用主题（更新背景渐变和色标文字颜色） */
    void applyTheme(const Theme& theme);

    /** @brief 色标控制 */
    void setColorBarVisible(bool visible);
    void setColorBarRange(float min, float max);
    void setColorBarTitle(const QString& title);
    void setColorBarExtremes(int minId, float minVal, int maxId, float maxVal);
    void setColorBarIdLabel(const QString& label);

    /** @brief 设置三角形→单元映射表（用于拾取） */
    void setTriangleToElementMap(const std::vector<int>& map);

    /** @brief 设置三角形→面映射表（保留接口兼容性） */
    void setTriangleToFaceMap(const std::vector<int>& map) { (void)map; }

    /** @brief 设置渲染顶点→FEM节点ID映射表 */
    void setVertexToNodeMap(const std::vector<int>& map);

    /** @brief 设置拾取模式 */
    void setPickMode(PickMode mode);

    /** @brief 设置当前视口鼠标左键工具 */
    void setInteractionMode(ViewportInteractionMode mode);

    /** @brief 设置是否显示选中项的 ID 标签 */
    void setShowLabels(bool show);

    /** @brief 按 ID 列表选中节点或单元（供搜索框调用） */
    void selectByIds(PickMode mode, const std::vector<int>& ids);

    /** @brief 设置未变形叠加网格（半透明线框显示原始形状） */
    void setOverlayMesh(const Mesh& mesh);

    /** @brief 控制叠加网格显隐 */
    void setOverlayVisible(bool visible);

    /** @brief 设置首选 RHI；当前 QOpenGLWidget 视口仅 OpenGL 会立即生效。 */
    void setPreferredRenderBackend(RenderBackendKind kind);

    /** @brief 用户请求的 RHI。 */
    RenderBackendKind requestedRenderBackendKind() const { return requestedBackendKind_; }

    /** @brief 当前视口实际使用的 RHI。 */
    RenderBackendKind activeRenderBackendKind() const { return activeBackendKind_; }

    /** @brief 设置是否使用顶点颜色（云图模式） */
    void setUseVertexColor(bool use);

    /** @brief 设置切片交线（GL_LINES 显示） */
    void setSliceLines(const std::vector<float>& lineVertices);

    /** @brief 清除切片交线 */
    void clearSliceLines();

    /** @brief 设置等值面网格（半透明叠加显示） */
    void setIsoSurfaceMesh(const Mesh& mesh);

    /** @brief 清除等值面网格 */
    void clearIsoSurface();

    /** @brief 设置裁剪/切片平面预览（半透明平面） */
    void setClipPlanePreview(const glm::vec3& bbMin,
                             const glm::vec3& bbMax,
                             const glm::vec3& origin,
                             const glm::vec3& normal);

    /** @brief 清除裁剪/切片平面预览 */
    void clearClipPlanePreview();

    /** @brief 上传 per-vertex 标量值到 GPU，由片段着色器做量化 + 颜色映射 */
    void setVertexScalars(const std::vector<float>& scalars, float minVal, float maxVal, int numBands);

    /** @brief 上传边线 per-vertex 标量值到 GPU，用于线/梁单元云图。 */
    void setEdgeScalars(const std::vector<float>& scalars, float minVal, float maxVal, int numBands);

    /** @brief 设置三角形 → 部件索引映射表 */
    void setTriangleToPartMap(const std::vector<int>& map);

    /** @brief 设置边线 → 部件索引映射表（用于边线可见性控制） */
    void setEdgeToPartMap(const std::vector<int>& map);

    /** @brief 获取各部件的渲染颜色（供 PartsPanel 显示色块使用） */
    const std::vector<glm::vec3>& partColors() const { return partColors_; }

public slots:
    /** @brief 设置指定部件的可见性，触发重绘 */
    void setPartVisibility(int partIndex, bool visible);

    /** @brief 设置指定单元的可见性，触发重绘 */
    void setElementVisibility(int elementId, bool visible);

    /** @brief 批量设置单元可见性，触发重绘 */
    void setElementsVisibility(const std::vector<int>& elementIds, bool visible);

    /** @brief 清除所有单元级隐藏状态 */
    void setAllElementsVisible();

    /** @brief 高亮显示指定部件（来自模型树多选） */
    void highlightParts(const std::vector<int>& partIndices);

    /** @brief 获取当前选中状态 */
    const FESelection& selection() const { return selection_; }

    // ── 硬件信息查询 ──
    QString glRenderer()  const { return glRenderer_; }
    QString glVersion()   const { return glVersion_; }
    QString glslVersion() const { return glslVersion_; }
    QString gpuVendor()   const { return gpuVendor_; }

    // ── 网格统计 ──
    int vertexCount()     const;
    int triangleCount()   const;

    // ── 性能统计 ──
    float currentFps()    const { return fps_; }
    float frameTimeMs()   const { return frameTime_; }

signals:
    void glInitialized();
    void selectionChanged(PickMode mode, int count, const std::vector<int>& ids);
    /** @brief 部件拾取后发射选中的部件索引列表（用于同步模型树选中状态） */
    void partsPicked(const std::vector<int>& partIndices);
    /** @brief 请求显示视口右键菜单。 */
    void contextMenuRequested(const QPoint& globalPos);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    OpenGLRenderBackend* openGLBackend() const;
    RenderBackendKind resolveRenderBackendForWidget(RenderBackendKind requested) const;
    glm::mat4 projectionMatrix(float aspect) const;

    void uploadMesh();
    void drawAxesIndicator();
    void drawAxesLabels(QPainter& painter);  // 坐标轴标签（需外部提供 QPainter）
    void drawIdLabels(QPainter& painter, const glm::mat4& mvp);  // 选中项 ID 标签
    glm::mat4 axesIndicatorMvp() const;
    bool standardViewFromAxesClick(const QPoint& pos, StandardView* view) const;
    void uploadColors();      // 将部件颜色上传到主网格颜色缓冲
    void rebuildEdgeIbo();    // 根据部件可见性重建边线 IBO

    // ── paintGL 渲染子步骤 ──
    void paintOpenGLFrame();
    void processDeferredPicks();
    void rebuildPartVisibilityIbo();
    void renderBackground();
    void renderMainMesh();
    void renderMeshEdges();
    void renderMeshPoints();
    void updateSelectionHighlight();
    void renderOverlayMesh();
    void renderClipPreview();
    void renderSliceLines();
    void renderIsoSurface();
    void renderSelectionHighlight();
    void render2DOverlays(const glm::mat4& mvp);
    void updateFpsStats();

    // ── 拾取相关 ──
    void renderPickBuffer(const glm::mat4& mvp);
    void pickAtPoint(const QPoint& pos, bool ctrlHeld);
    void pickInRect(const QRect& rect);
    void deselectAtPoint(const QPoint& pos);
    void deselectInRect(const QRect& rect);
    int edgeElementAtPoint(const QPointF& pos, const glm::mat4& mvp, float thresholdPx) const;
    int closestNodeForElement(int elementId, const QPointF& pos, const glm::mat4& mvp) const;
    glm::vec3 idToColor(int id);
    int colorToId(unsigned char r, unsigned char g, unsigned char b);
    void rebuildSelectionEdges();
    void buildPartEdgeCache();       // 选中变化时构建边缓存（重操作）
    void updateSilhouetteFromCache(); // 相机变化时从缓存刷新轮廓边（轻操作）
    void buildEdgeAdjacency();       // 预建全局边邻接表（网格加载后一次性构建）
    void rebuildPartLookup();        // 根据三角形/边线映射重建部件反查表
    void selectPart(int partIndex);  // 将 partIndex 对应的所有单元加入 selection_.selectedElements
    void deselectPart(int partIndex);  // 从 selection_ 中移除该部件所有单元
    bool isPartFullySelected(int partIndex) const;  // 检查部件是否全部已选中
    bool isPartVisible(int partIndex) const;
    bool isTriangleVisible(int triangleIndex) const;
    bool isElementVisibleForSelection(int elementId) const;
    bool isNodeVisibleForSelection(int nodeId) const;

    // ── 场景对象 ──
    Camera cam_;
    Mesh mesh_;
    bool needsUpload_ = true;
    std::unique_ptr<IRenderBackend> renderBackend_;
    RenderBackendKind requestedBackendKind_ = RenderBackendKind::OpenGL;
    RenderBackendKind activeBackendKind_ = RenderBackendKind::OpenGL;
    ModelDisplayMode displayMode_ = ModelDisplayMode::SolidWireframe;
    float modelSize_ = 1.0f;

    // ── OpenGL 对象 ──
    QOpenGLShaderProgram* shader_ = nullptr;
    std::unique_ptr<OpenGLMeshResource> meshResource_;

    // ── 渐变背景 ──
    QOpenGLShaderProgram* bgShader_ = nullptr;
    std::unique_ptr<OpenGLPositionColorGeometry> backgroundGeometry_;

    // ── 边线 ──
    std::unique_ptr<OpenGLEdgeResource> edgeResource_;
    int edgeIndexCount_ = 0;

    // ── 拾取 ──
    QOpenGLShaderProgram* pickShader_ = nullptr;
    std::unique_ptr<OpenGLFramebuffer> pickFramebuffer_;
    std::unique_ptr<OpenGLVertexArrayResource> pickVertexArray_; // 拾取专用 VAO，避免污染主 VAO 的顶点属性状态
    std::vector<int> triToElem_;        // 三角形索引 → 单元 ID
    std::vector<int> vertexToNode_;     // 渲染顶点索引 → FEM 节点 ID
    PickMode pickMode_ = PickMode::Node;  // 当前拾取模式（与下拉框默认值同步）
    FESelection selection_;             // 当前选中状态
    bool showLabels_ = false;           // 是否显示选中项的 ID 标签

    // ── 部件可见性 ──
    std::vector<int> triToPart_;                        // 三角形 → 部件索引
    std::unordered_map<int, bool> partVisibility_;      // 部件索引 → 是否可见
    std::unordered_set<int> hiddenElementIds_;          // 被单独隐藏的 FEM 单元 ID
    std::vector<unsigned int> allTriIndices_;            // 完整三角形索引（不过滤）
    int activeIndexCount_ = 0;                          // 当前实际绘制的索引数量
    bool partVisibilityDirty_ = false;                  // 是否需要重建 IBO

    // ── 部件颜色 ──
    std::vector<glm::vec3> partColors_;      // 每个部件的渲染颜色

    // ── 顶点颜色缓冲（per-vertex 部件色，存放于 meshResource_） ──
    bool needsColorUpload_ = false;          // 需要上传颜色数据到 GPU

    // ── 标量值缓冲（per-vertex，片段着色器量化，存放于 meshResource_） ──
    std::vector<float> edgeScalars_;
    float scalarMin_ = 0.0f;
    float scalarMax_ = 1.0f;
    int numBands_ = 10;

    // ── 部件索引 texture buffer（per-triangle，用 gl_PrimitiveID 查表） ──
    std::unique_ptr<OpenGLTextureBufferResource> triPartTextureBuffer_;
    bool triPartDirty_ = false;   // 需要重新上传

    // ── 边线可见性 ──
    std::vector<int> edgeToPart_;            // 边线 → 部件索引
    std::vector<unsigned int> allEdgeIndices_;  // 完整边线索引（不过滤）
    int activeEdgeIndexCount_ = 0;           // 当前可见边线索引数量
    bool edgeVisibilityDirty_ = false;       // 需要重建边线 IBO

    // ── 选中高亮边线 ──
    std::unique_ptr<OpenGLLineResource> selectionEdgeResource_;
    int selEdgeVertCount_ = 0;
    bool selectionDirty_ = false;
    bool silhouetteDirty_ = false;    // 仅视角变化，需刷新轮廓边
    int selHlMode_ = 0;   // 0=lines, 1=points

    // ── 部件轮廓边缓存（选中变化时构建，相机变化时复用） ──
    struct SilhouetteCandidate {
        float ax, ay, az, bx, by, bz;  // 边两端顶点坐标
        glm::vec3 n0, n1;               // 两侧三角形法线
    };
    std::vector<float> cachedStaticEdgeVerts_;              // 不随视角变化的边（边界/特征/开放）
    std::vector<SilhouetteCandidate> cachedSilhouettes_;    // 需逐帧判定的轮廓边候选
    bool partEdgeCacheValid_ = false;                       // 缓存是否有效

    // ── 预计算的每部件数据（在 setTriangleToPartMap 中构建） ──
    std::vector<std::vector<int>> partTriangles_;    // partIndex → 三角形索引列表
    std::vector<std::vector<int>> partElementIds_;   // partIndex → 去重单元 ID 列表
    std::unordered_map<int, int> elemToPart_;        // 单元 ID → 部件索引（快速反查）

    // ── 预计算的全局边邻接表（网格+节点映射就绪后一次性构建） ──
    struct PreEdge {
        unsigned int va, vb;       // 代表性顶点索引
        std::vector<int> adjTris;  // 相邻三角形索引
    };
    std::unordered_map<int64_t, PreEdge> edgeAdjMap_;
    bool edgeAdjDirty_ = true;     // 网格或节点映射变化时置 true

    // ── 色标（Colorbar） ──
    bool colorBarVisible_ = false;
    float colorBarMin_ = 0.0f;
    float colorBarMax_ = 1.0f;
    QString colorBarTitle_ = "Result";
    bool useVertexColor_ = false;       // 是否使用云图颜色（通过主网格颜色/标量缓冲）
    ColorBarOverlay* colorBarOverlay_ = nullptr;  // 独立覆盖层控件（raster 绘制，不受 GL 状态影响）
    QColor barTextColor_{255, 255, 255};            // 色标数值文字颜色（随主题变化）
    // 背景渐变颜色（initializeGL 使用，applyTheme 更新）
    float bgTopColor_[3] = {0.38f, 0.45f, 0.58f};
    float bgBotColor_[3] = {0.68f, 0.74f, 0.82f};
    bool viewportGridVisible_ = true;
    glm::mat4 axesMVP_{1.0f};          // drawAxesIndicator() 计算后传给 drawAxesLabels()

    // ── 交互状态 ──
    QPoint lastPos_;
    QPoint pressPos_;                   // 鼠标按下位置（区分点击和拖拽）
    ViewportInteractionMode interactionMode_ = ViewportInteractionMode::Pick; // 当前鼠标左键工具
    bool isDragging_ = false;           // 是否正在拖拽旋转/平移
    bool isBoxSelecting_ = false;       // 是否正在框选（添加）
    bool isBoxDeselecting_ = false;     // 是否正在框选（取消）
    QRubberBand* rubberBand_ = nullptr; // 框选矩形
    QPoint boxOrigin_;                  // 框选起始点

    // ── 延迟拾取（避免在 paintGL 外调用 makeCurrent 导致 GL 状态污染） ──
    bool pickPointPending_ = false;     // 点选待处理
    QPoint pendingPickPos_;             // 点选位置
    bool pendingPickCtrl_ = false;      // 是否按住 Ctrl/Shift（累加模式）
    bool pickRectPending_ = false;      // 框选待处理
    QRect pendingPickRect_;             // 框选矩形
    bool deselectPointPending_ = false; // 点选取消待处理
    QPoint pendingDeselectPos_;         // 点选取消位置
    bool deselectRectPending_ = false;  // 框选取消待处理
    QRect pendingDeselectRect_;         // 框选取消矩形
    glm::vec3 color_{0.55f, 0.75f, 0.73f};

    // ── 叠加网格（未变形线框） ──
    Mesh overlayMesh_;
    std::unique_ptr<OpenGLLineResource> overlayResource_;
    int overlayVertCount_ = 0;
    bool overlayVisible_ = false;
    bool overlayNeedsUpload_ = false;

    // ── 切片交线 ──
    std::unique_ptr<OpenGLLineResource> sliceResource_;
    int sliceVertCount_ = 0;
    bool sliceNeedsUpload_ = false;

    // ── 等值面 ──
    Mesh isoMesh_;
    std::unique_ptr<OpenGLMeshResource> isoResource_;
    int isoIndexCount_ = 0;
    bool isoNeedsUpload_ = false;

    // ── 裁剪/切片平面预览 ──
    Mesh clipPreviewMesh_;
    std::unique_ptr<OpenGLMeshResource> clipPreviewResource_;
    std::unique_ptr<OpenGLLineResource> clipPreviewEdgeResource_;
    int clipPreviewIndexCount_ = 0;
    int clipPreviewEdgeVertCount_ = 0;
    bool clipPreviewVisible_ = false;
    bool clipPreviewNeedsUpload_ = false;

    // ── 坐标轴指示器 ──
    QOpenGLShaderProgram* axesShader_ = nullptr;
    std::unique_ptr<OpenGLPositionColorGeometry> axesGeometry_;
    int axesLineCount_ = 0;
    int axesTriCount_ = 0;

    // ── 硬件信息 ──
    QString glRenderer_, glVersion_, glslVersion_, gpuVendor_;

    // ── FPS 统计 ──
    QElapsedTimer fpsTimer_;
    int frameCount_ = 0;
    float fps_ = 0.0f;
    float frameTime_ = 0.0f;
};
