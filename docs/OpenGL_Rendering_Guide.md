# OpenGL 渲染引擎详解 — 从零理解 GLWidget

本文档面向**不了解 OpenGL 的读者**，从最基础的 OpenGL 概念讲起，逐步解析本项目渲染引擎的每一个细节。

---

## 目录

1. [OpenGL 基础概念速览](#1-opengl-基础概念速览)
2. [Qt 对 OpenGL 的封装](#2-qt-对-opengl-的封装)
3. [GLWidget 初始化流程详解 (initializeGL)](#3-glwidget-初始化流程详解-initializegl)
4. [数据上传到 GPU (uploadMesh)](#4-数据上传到-gpu-uploadmesh)
5. [每帧渲染流程 (paintGL)](#5-每帧渲染流程-paintgl)
6. [着色器系统详解](#6-着色器系统详解)
7. [变换矩阵与坐标系](#7-变换矩阵与坐标系)
8. [光照模型](#8-光照模型)
9. [GPU Color Picking（拾取系统）](#9-gpu-color-picking拾取系统)
10. [部件可见性与 Texture Buffer](#10-部件可见性与-texture-buffer)
11. [选中高亮与轮廓边算法](#11-选中高亮与轮廓边算法)
12. [渐变背景与坐标轴指示器](#12-渐变背景与坐标轴指示器)
13. [云图渲染（标量场可视化）](#13-云图渲染标量场可视化)
14. [QPainter 与 OpenGL 混合绘制](#14-qpainter-与-opengl-混合绘制)

---

## 1. OpenGL 基础概念速览

### 1.1 OpenGL 是什么

OpenGL（Open Graphics Library）是一套**跨平台的图形渲染 API**。它不是引擎、不是框架，而是一组函数接口规范。你调用这些函数，GPU 硬件帮你画出东西。

打个比方：OpenGL 就像是你和 GPU 之间的"通话协议"。你告诉 GPU"这里有一堆三角形，用这个颜色画"，GPU 就帮你画到屏幕上。

### 1.2 渲染管线（Rendering Pipeline）

OpenGL 按照一个固定的流水线处理数据：

```
CPU 端数据 (顶点坐标、法线、颜色...)
    │
    ▼
┌──────────────────┐
│  顶点着色器       │  ← 你写的 GLSL 代码，处理每个顶点
│  (Vertex Shader)  │     输入：模型空间坐标
│                   │     输出：屏幕空间坐标 (gl_Position)
└──────────────────┘
    │
    ▼  图元装配（把顶点组装成三角形）
    ▼  光栅化（把三角形拆成像素碎片）
    │
┌──────────────────┐
│  片段着色器       │  ← 你写的 GLSL 代码，处理每个像素
│  (Fragment Shader)│     输入：插值后的法线、颜色等
│                   │     输出：最终像素颜色 (outColor)
└──────────────────┘
    │
    ▼  深度测试、混合、写入帧缓冲
    │
  屏幕上看到图像
```

**你能自定义的部分**就是顶点着色器和片段着色器（合称 Shader）。其余环节由 GPU 硬件自动完成。

### 1.3 核心 OpenGL 对象

| 对象 | 缩写 | 类比 | 作用 |
|------|------|------|------|
| **VAO** | Vertex Array Object | "连线说明书" | 记录"哪个缓冲的哪些数据对应着色器的哪个输入" |
| **VBO** | Vertex Buffer Object | "数据仓库" | 在 GPU 显存中存储顶点数据（坐标、法线、颜色等） |
| **IBO/EBO** | Index Buffer Object | "索引表" | 存储顶点索引，避免重复存储共享顶点 |
| **Shader Program** | - | "GPU 程序" | 编译好的着色器代码，告诉 GPU 如何处理每个顶点/像素 |
| **FBO** | Frame Buffer Object | "离屏画布" | 不画到屏幕上，而是画到一张纹理/图片上（用于拾取等） |
| **Texture** | - | "贴图" | 在 GPU 显存中存储图片或数据表 |

### 1.4 VAO / VBO / IBO 的关系

这是 OpenGL 初学者最容易困惑的点，用一个具体例子说明：

假设你要画一个四边形（由 2 个三角形组成，4 个顶点）：

```
顶点数据（VBO 中的内容）:
  顶点0: [x0,y0,z0, nx0,ny0,nz0]   ← 位置 + 法线
  顶点1: [x1,y1,z1, nx1,ny1,nz1]
  顶点2: [x2,y2,z2, nx2,ny2,nz2]
  顶点3: [x3,y3,z3, nx3,ny3,nz3]

索引数据（IBO 中的内容）:
  [0, 1, 2,     ← 第1个三角形：顶点0→1→2
   0, 2, 3]     ← 第2个三角形：顶点0→2→3

VAO 记录的配置:
  "location 0 (aPos)   → VBO 偏移 0 字节，每隔 24 字节取 3 个 float"
  "location 1 (aNormal) → VBO 偏移 12 字节，每隔 24 字节取 3 个 float"
```

**为什么需要 IBO**？因为顶点 0 和顶点 2 被两个三角形共享。没有 IBO 的话，你需要把这两个顶点重复存两次，浪费显存。

### 1.5 OpenGL 状态机

OpenGL 是一个**全局状态机**。你调用的每个函数都在修改某个全局状态：

```cpp
glEnable(GL_DEPTH_TEST);      // 开启深度测试（之后所有绘制都会测试深度）
glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);  // 之后所有绘制都只画线框
glLineWidth(3.0f);             // 之后所有线段宽度为 3 像素
```

这就像你在画画前设置画笔颜色——设置一次，之后画的所有东西都用这个颜色，直到你再次修改。

所以**必须在绘制前设好状态，绘制后恢复状态**，否则后续绘制会被错误的状态影响。

### 1.6 常用 OpenGL 函数速查

```
── 状态设置 ──
glEnable(X) / glDisable(X)      开启/关闭某个功能
  GL_DEPTH_TEST                  深度测试（近处遮挡远处）
  GL_BLEND                       透明混合
  GL_MULTISAMPLE                 多重采样抗锯齿
  GL_LINE_SMOOTH                 线段平滑
  GL_POLYGON_OFFSET_FILL         多边形偏移（防止 Z-fighting）

── 清屏 ──
glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)   清除颜色和深度缓冲
glClearColor(r, g, b, a)        设置清屏颜色

── 绘制 ──
glDrawArrays(mode, first, count)           按顺序画
glDrawElements(mode, count, type, offset)  按索引画
  mode: GL_TRIANGLES（三角形）/ GL_LINES（线段）/ GL_POINTS（点）

── 缓冲操作 ──
glGenBuffers(n, &id)            生成缓冲对象
glBindBuffer(target, id)        绑定缓冲（之后操作都针对这个缓冲）
glBufferData(target, size, data, usage)  上传数据到缓冲

── 顶点属性 ──
glVertexAttribPointer(location, size, type, normalized, stride, offset)
  告诉 GPU："着色器的第 location 个输入，
  从当前绑定的 VBO 中，每隔 stride 字节，
  从偏移 offset 处开始，取 size 个 type 类型的值"
glEnableVertexAttribArray(location)   启用这个属性

── 纹理 ──
glActiveTexture(GL_TEXTURE0)    激活纹理单元 0
glBindTexture(target, id)       绑定纹理

── 帧缓冲 ──
glBindFramebuffer(GL_FRAMEBUFFER, id)   绑定 FBO（0 = 默认屏幕）
glReadPixels(x, y, w, h, format, type, data)   从帧缓冲读取像素

── 查询 ──
glGetString(GL_RENDERER)        GPU 型号
glGetString(GL_VERSION)         OpenGL 版本
glGetIntegerv(pname, &value)    查询当前状态值
```

---

## 2. Qt 对 OpenGL 的封装

本项目使用 Qt 的 OpenGL 封装类，比直接调用原始 GL 更安全方便：

### 2.1 QOpenGLWidget

Qt 提供的 OpenGL 绘图控件，你只需要覆写 3 个虚函数：

```cpp
class GLWidget : public QOpenGLWidget, protected QOpenGLFunctions {
protected:
    void initializeGL() override;   // OpenGL 上下文创建后调用一次
    void paintGL() override;        // 每帧绘制时调用（调用 update() 触发）
    void resizeGL(int w, int h) override;  // 窗口大小改变时调用
};
```

`QOpenGLFunctions` 提供了所有 OpenGL 函数的成员函数版本（如 `glEnable()`、`glDrawElements()` 等），调用 `initializeOpenGLFunctions()` 后即可使用。

### 2.2 Qt 封装类 vs 原始 GL

| Qt 封装 | 原始 GL 等价 | 说明 |
|---------|-------------|------|
| `QOpenGLShaderProgram` | `glCreateProgram` + `glCompileShader` + `glLinkProgram` | 编译链接着色器 |
| `QOpenGLVertexArrayObject` | `glGenVertexArrays` + `glBindVertexArray` | VAO 管理 |
| `QOpenGLBuffer` | `glGenBuffers` + `glBindBuffer` + `glBufferData` | VBO/IBO 管理 |
| `QOpenGLFramebufferObject` | `glGenFramebuffers` + `glFramebufferTexture2D` | FBO 管理 |

**Qt 封装的好处**：自动管理生命周期（析构时自动释放 GPU 资源），避免忘记 `glDelete*`。

### 2.3 本项目中 Qt 封装与原始 GL 混用

项目中大部分地方使用 Qt 封装，但**拾取渲染** (`renderPickBuffer`) 中刻意使用了原始 GL 函数指针：

```cpp
auto glBindVAO_ = reinterpret_cast<void(APIENTRY*)(GLuint)>(
    context()->getProcAddress("glBindVertexArray"));
```

原因：Qt 的 `QOpenGLShaderProgram::bind()` 会内部记录状态，与 `QPainter` 冲突。用原始 GL 调用可以完全绕过 Qt 的状态追踪，避免 QPainter 在后续绘制 2D 文字时崩溃。

---

## 3. GLWidget 初始化流程详解 (initializeGL)

`initializeGL()` 在 OpenGL 上下文创建后被 Qt 自动调用**一次**，负责所有 GPU 资源的初始创建。

### 3.1 初始化 OpenGL 函数

```cpp
initializeOpenGLFunctions();
```
加载当前上下文支持的所有 OpenGL 函数指针。之后才能调用 `glEnable()` 等函数。

### 3.2 查询 GPU 硬件信息

```cpp
glRenderer_  = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
glVersion_   = reinterpret_cast<const char*>(glGetString(GL_VERSION));
```
`glGetString` 返回 GPU 的描述字符串，用于 MonitorPanel 显示。

### 3.3 编译着色器

```cpp
shader_ = new QOpenGLShaderProgram(this);
shader_->addShaderFromSourceCode(QOpenGLShader::Vertex,
    loadShaderSource(":/shaders/scene.vert"));
shader_->addShaderFromSourceCode(QOpenGLShader::Fragment,
    loadShaderSource(":/shaders/scene.frag"));
shader_->link();
```

流程：
1. `loadShaderSource` 从 Qt 资源文件 (`shaders.qrc`) 加载 GLSL 源码
2. `addShaderFromSourceCode` 编译单个着色器（GPU 端编译）
3. `link()` 链接顶点 + 片段着色器，生成可执行的 Shader Program

如果 GLSL 代码有语法错误，`link()` 会失败并输出编译日志。

### 3.4 创建 GPU 缓冲对象

```cpp
meshResource_ = std::make_unique<OpenGLMeshResource>();
glBackend->createMeshResource(*meshResource_);
```

后端托管主网格 VAO、顶点 VBO、索引 IBO、每顶点颜色 VBO 和标量 VBO。底层 `.create()` 相当于 `glGenBuffers(1, &id)`，此时还没有上传任何数据，实际数据在 `uploadMesh()` 时上传。

### 3.5 创建 Texture Buffer（部件索引查找表）

```cpp
triPartTextureBuffer_ = std::make_unique<OpenGLTextureBufferResource>();
glBackend->createTextureBufferResource(*triPartTextureBuffer_);
```

这是一种特殊纹理：不是图片，而是一维数据数组。片段着色器中通过 `texelFetch(uTriPartMap, gl_PrimitiveID)` 查表，用三角形序号获取其所属部件索引。

为什么不用 uniform 数组？因为 uniform 有大小限制（通常几千个元素），而一个模型可能有几十万个三角形。Texture Buffer 没有这个限制。

### 3.6 创建渐变背景

```cpp
float bgData[] = {
    -1, -1,  bgBotColor_[0], bgBotColor_[1], bgBotColor_[2],  // 左下角
     1, -1,  bgBotColor_[0], bgBotColor_[1], bgBotColor_[2],  // 右下角
     1,  1,  bgTopColor_[0], bgTopColor_[1], bgTopColor_[2],  // 右上角
    // ... 共 6 个顶点 = 2 个三角形 = 1 个全屏四边形
};
backgroundGeometry_ = std::make_unique<OpenGLPositionColorGeometry>();
glBackend->uploadPositionColorGeometry(*backgroundGeometry_,
                                       bgData,
                                       sizeof(bgData),
                                       2);
```

逐行解读：
- **坐标 (-1,-1) 到 (1,1)**：这是 NDC（归一化设备坐标），覆盖整个屏幕
- **每顶点 5 个 float**：2 个位置 + 3 个颜色
- `glVertexAttribPointer(0, 2, ...)`: location 0 (aPos) → 从偏移 0 开始，取 2 个 float
- `glVertexAttribPointer(1, 3, ...)`: location 1 (aColor) → 从偏移 8 字节开始，取 3 个 float
- `stride = 5*sizeof(float) = 20`：相邻顶点之间间隔 20 字节
- GPU 自动插值：底部顶点是底色，顶部顶点是顶色 → 中间平滑渐变

### 3.7 创建坐标轴指示器

坐标轴是纯几何体（3 个圆柱 + 3 个圆锥 + 1 个球），用参数方程生成：

```
X 轴：红色圆柱（0→0.7）+ 红色圆锥（0.7→1.0）
Y 轴：绿色圆柱 + 圆锥
Z 轴：蓝色圆柱 + 圆锥
中心：灰色小球
```

每个圆柱由 24 个分段组成（`segs = 24`），每段一个四边形 = 2 个三角形。所有几何体合并到一个 VBO 中。

### 3.8 全局 GL 状态

```cpp
glEnable(GL_DEPTH_TEST);    // 近处物体遮挡远处物体
glEnable(GL_MULTISAMPLE);   // 8x MSAA 抗锯齿
glEnable(GL_LINE_SMOOTH);   // 线段边缘平滑
```

---

## 4. 数据上传到 GPU (uploadMesh)

当 `setMesh()` 被调用时，设置 `needsUpload_ = true`，在下一帧 `paintGL()` 开头执行 `uploadMesh()`。

### 顶点数据格式

```
VBO 中每个顶点占 6 个 float = 24 字节：
  [px, py, pz, nx, ny, nz]
   位置x 位置y 位置z 法线x 法线y 法线z
```

上传过程：

```cpp
vao_.bind();               // ① 绑定 VAO（之后的配置都记录在这个 VAO 上）

vbo_.bind();               // ② 绑定 VBO
vbo_.allocate(             // ③ 上传顶点数据到 GPU 显存
    mesh_.vertices.data(),
    mesh_.vertices.size() * sizeof(float)
);

// ④ 告诉 GPU 数据格式
glVertexAttribPointer(
    0,                     // location 0 = aPos（顶点着色器中的 layout(location=0)）
    3,                     // 每个属性 3 个分量 (x,y,z)
    GL_FLOAT,              // 数据类型：float
    GL_FALSE,              // 不归一化
    6 * sizeof(float),     // stride：相邻顶点间隔 24 字节
    nullptr                // offset：从缓冲起始处开始
);
glEnableVertexAttribArray(0);   // 启用 location 0

glVertexAttribPointer(
    1,                     // location 1 = aNormal
    3,                     // 3 个分量 (nx,ny,nz)
    GL_FLOAT,
    GL_FALSE,
    6 * sizeof(float),     // 同样的 stride
    (void*)(3*sizeof(float))  // offset = 12 字节（跳过前 3 个 float）
);
glEnableVertexAttribArray(1);

ibo_->bind();              // ⑤ 绑定 IBO
ibo_->allocate(            // ⑥ 上传索引数据
    mesh_.indices.data(),
    mesh_.indices.size() * sizeof(unsigned int)
);

vao_.release();            // ⑦ 解绑 VAO
```

**内存布局图**：

```
VBO (GPU 显存):
偏移:  0    4    8   12   16   20   24   28  ...
     ┌────┬────┬────┬────┬────┬────┬────┬────┬─...
     │ px │ py │ pz │ nx │ ny │ nz │ px │ py │
     └────┴────┴────┴────┴────┴────┴────┴────┴─...
     ├── 顶点 0 (24字节) ──┤├── 顶点 1 ──┤

     ← stride = 24 bytes →
     aPos: offset=0, 取 3 float ──┐
     aNormal: offset=12, 取 3 float ──┘

IBO (GPU 显存):
     ┌───┬───┬───┬───┬───┬───┬─...
     │ 0 │ 1 │ 2 │ 0 │ 2 │ 3 │
     └───┴───┴───┴───┴───┴───┴─...
     ├─ 三角形0 ─┤├─ 三角形1 ─┤
```

---

## 5. 每帧渲染流程 (paintGL)

`paintGL()` 是整个渲染引擎的核心函数，每帧被 Qt 调用一次。

### 完整流程

```
paintGL()
  │
  ├── ① 恢复 GL 状态（QPainter 可能改过）
  │     glViewport / glEnable(GL_DEPTH_TEST) / glDisable(GL_BLEND)
  │
  ├── ② 处理延迟拾取请求
  │     if (pickPointPending_) → pickAtPoint()
  │     if (pickRectPending_)  → pickInRect()
  │
  ├── ③ 上传新数据（如有）
  │     if (needsUpload_)           → uploadMesh()
  │     if (partVisibilityDirty_)   → 重建过滤后的 IBO
  │     if (needsColorUpload_)      → uploadColors()
  │
  ├── ④ 绘制渐变背景
  │     关闭深度测试 → bgShader → 全屏四边形 → 恢复深度测试
  │
  ├── ⑤ 计算变换矩阵
  │     projection × view × model = MVP
  │
  ├── ⑥ 绘制实体面
  │     shader->bind() → 设置 uniform → glDrawElements(GL_TRIANGLES)
  │     使用 glPolygonOffset 避免 Z-fighting
  │
  ├── ⑦ 绘制网格边线
  │     wireframe 模式 → glDrawElements(GL_LINES)
  │     透明度自适应（密集网格淡化边线）
  │
  ├── ⑧ 绘制选中高亮
  │     金色粗线/点 → glDrawArrays(GL_LINES/GL_POINTS)
  │     关闭深度测试（始终显示在最前）
  │
  ├── ⑨ 绘制坐标轴（GL 部分）
  │     drawAxesIndicator() → 独立小视口
  │
  ├── ⑩ QPainter 绘制 2D 叠加层
  │     坐标轴 X/Y/Z 标签 + 选中项 ID 标签
  │
  └── ⑪ FPS 统计
```

### ⑥ 绘制实体面详解

```cpp
// 设置 uniform（传给着色器的参数）
shader_->setUniformValue("uMVP", ...);         // 模型-视图-投影矩阵
shader_->setUniformValue("uLightDir", ...);     // 光照方向
shader_->setUniformValue("uViewPos", ...);      // 相机位置（用于高光计算）
shader_->setUniformValue("uContourMode", ...);  // 是否云图模式
shader_->setUniformValue("uUseVertexColor", ...); // 是否使用部件颜色

// 防止 Z-fighting：实体面稍微往后偏移，让线框显示在面上方
glEnable(GL_POLYGON_OFFSET_FILL);
glPolygonOffset(1.0f, 1.0f);  // 深度值 +1 个单位

// 以填充模式绘制三角形
glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);

glDisable(GL_POLYGON_OFFSET_FILL);
```

**Z-fighting**：当两个面在同一深度时，由于浮点精度问题，像素会随机闪烁。`glPolygonOffset` 给实体面加一点深度偏移，让线框总是画在面前面。

### ⑦ 网格边线自适应透明度

```cpp
// 估算平均边长在屏幕上的像素大小
float avgEdgeLen = modelSize * 2.0f / sqrt(numTriangles);
float screenEdgePx = avgEdgeLen / cam_.distance * fovFactor;

// < 3px：全透明（太密了不画）  > 10px：全不透明
wireAlpha = clamp((screenEdgePx - 3.0f) / 7.0f, 0.0f, 1.0f);
```

这是为了避免密集网格时边线过于拥挤，变成一片黑色。

---

## 6. 着色器系统详解

### 6.1 Scene Shader（场景着色器）

**顶点着色器** `scene.vert`：

```glsl
#version 410 core
layout (location = 0) in vec3 aPos;      // 顶点位置
layout (location = 1) in vec3 aNormal;   // 法线
layout (location = 2) in vec3 aColor;    // 每顶点颜色（部件色/云图色）
layout (location = 3) in float aScalar;  // 每顶点标量值（云图用）

uniform mat4 uMVP;        // 模型-视图-投影 合并矩阵
uniform mat4 uModel;      // 模型矩阵（本项目中恒为单位阵）
uniform mat3 uNormalMat;  // 法线变换矩阵

out vec3 vWorldPos;   // 传给片段着色器的世界坐标
out vec3 vNormal;     // 传给片段着色器的法线
out vec3 vColor;      // 传给片段着色器的颜色
out float vScalar;    // 传给片段着色器的标量值

void main() {
    vWorldPos = (uModel * vec4(aPos, 1.0)).xyz;
    vNormal   = normalize(uNormalMat * aNormal);
    vColor    = aColor;
    vScalar   = aScalar;
    gl_Position = uMVP * vec4(aPos, 1.0);   // 最终屏幕坐标
}
```

`layout (location = N)` 对应 C++ 端 `glVertexAttribPointer(N, ...)` 中的 N。

**片段着色器** `scene.frag`：

```glsl
// 部件调色板（8 种颜色循环使用）
const vec3 kPalette[8] = vec3[8](
    vec3(0.61, 0.86, 0.63),  // 绿
    vec3(0.54, 0.71, 0.98),  // 蓝
    vec3(0.98, 0.70, 0.53),  // 橙
    // ...
);

void main() {
    // ── 确定表面颜色 ──
    if (uContourMode) {
        // 云图模式：标量值 → 量化 → Jet 色谱
        float t = (vScalar - uScalarMin) / (uScalarMax - uScalarMin);
        int band = int(t * numBands);
        surfaceColor = jetColor(quantized_t);
    } else if (uUseVertexColor) {
        // 部件颜色：用 gl_PrimitiveID 查表
        int partIdx = int(texelFetch(uTriPartMap, gl_PrimitiveID).r);
        surfaceColor = kPalette[partIdx % 8];
    }

    // ── 光照计算 ──
    // Blinn-Phong = 环境光 + 漫反射 + 高光
    vec3 color = surfaceColor * (ambient + diffuse) + specular;
    outColor = vec4(color, 1.0);
}
```

**gl_PrimitiveID**：GPU 自动提供的内置变量，当前正在绘制的第几个图元（三角形）。利用它查 texture buffer 获取部件索引，无需额外的 per-vertex 属性。

### 6.2 Pick Shader（拾取着色器）

极简着色器，用于离屏拾取渲染：

```glsl
// pick.vert
layout (location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}

// pick.frag
uniform vec3 uPickColor;    // 由 C++ 端设置为 ID 编码的颜色
out vec4 outColor;
void main() {
    outColor = vec4(uPickColor, 1.0);
}
```

没有法线、没有光照——只输出纯色。每个单元用不同的颜色绘制，读取像素颜色就知道点击了哪个单元。

### 6.3 Background Shader（背景着色器）

```glsl
// background.vert
layout (location = 0) in vec2 aPos;    // 屏幕坐标 (-1,-1)~(1,1)
layout (location = 1) in vec3 aColor;  // 顶点颜色
out vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = vec4(aPos, 0.0, 1.0);  // 直接用 NDC 坐标，不需要变换
}

// background.frag
in vec3 vColor;
out vec4 outColor;
void main() {
    outColor = vec4(vColor, 1.0);  // GPU 自动插值颜色
}
```

底部顶点颜色 = 浅色，顶部 = 深色 → GPU 光栅化时自动插值 → 渐变背景。

---

## 7. 变换矩阵与坐标系

### 坐标空间变换链

```
模型空间 (Model Space)
  │  × Model 矩阵（本项目中 = 单位阵，不做变换）
  ▼
世界空间 (World Space)
  │  × View 矩阵（相机位置/朝向）
  ▼
相机空间 (View Space)
  │  × Projection 矩阵（透视投影）
  ▼
裁剪空间 (Clip Space)
  │  ÷ w（GPU 自动执行）
  ▼
NDC 空间 (Normalized Device Coordinates)
  │  × Viewport 变换（GPU 自动执行）
  ▼
屏幕空间 (Screen Space) → 最终像素位置
```

### 本项目中的矩阵

```cpp
// 透视投影矩阵
glm::mat4 projection = glm::perspective(
    glm::radians(45.0f),           // 视角 45°
    aspect,                         // 宽高比
    cam_.distance * 0.01f,          // 近裁剪面（动态调整）
    cam_.distance * 10.0f           // 远裁剪面
);

// 模型矩阵（无变换）
glm::mat4 model = glm::mat4(1.0f);

// 视图矩阵（由 Camera 计算）
glm::mat4 view = cam_.viewMatrix();
//   = glm::lookAt(eye, target, up)

// 合并：MVP = Projection × View × Model
glm::mat4 mvp = projection * view * model;

// 法线变换矩阵（model 矩阵的逆转置的 3x3 部分）
glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(model)));
```

**为什么近裁剪面用 `distance * 0.01`**：近裁剪面越靠近 0，深度精度越差（Z-fighting 越严重）。动态调整让近裁剪面始终在合理位置。

### 法线变换矩阵

法线不能直接用 Model 矩阵变换（如果模型有非均匀缩放，法线会歪）。正确的变换是 **Model 矩阵的逆转置**。本项目 Model = 单位阵，所以 normalMat 也是单位阵，但代码保留了通用计算。

---

## 8. 光照模型

### Blinn-Phong 光照

片段着色器中的光照计算：

```glsl
// 1. 环境光（全局均匀亮度，没有它物体背光面会全黑）
float ambient = 0.65;

// 2. 漫反射（物体面对光源的程度）
//    dot(N, L) = cos(入射角)，面对光源时最亮，垂直时为0
float diff1 = max(dot(N, L1), 0.0);   // 主光源
float diff2 = max(dot(N, L2), 0.0);   // 补光

// 3. 高光（Blinn-Phong：用半程向量代替反射向量，更高效）
//    H = normalize(L + V)   半程向量
//    spec = pow(dot(N, H), shininess)
//    shininess 越大，高光越小越集中
vec3 H = normalize(L1 + V);
float spec = pow(max(dot(N, H), 0.0), 64.0) * 0.10;

// 4. 合成
color = surfaceColor * (ambient + diffuse) + specular;
```

### 双面渲染

```glsl
if (!gl_FrontFacing) N = -N;              // 背面法线翻转
float sideFactor = gl_FrontFacing ? 1.0 : 0.8;  // 背面稍暗
```

`gl_FrontFacing` 是 GPU 内置变量，告诉你当前像素是正面还是背面。这样从背后看壳体时也能正确显示。

### 云图模式 vs 几何模式的光照差异

| 参数 | 几何模式 | 云图模式 |
|------|----------|----------|
| ambient | 0.65 | 0.55 |
| diffuse1 | 0.35 | 0.35 |
| diffuse2 | 0.20 | 0.10 |
| specular | 0.10 | 0.0 |
| shininess | 64 | 32 |

云图模式**提高环境光、去除高光**，避免光照改变色谱颜色，保证颜色准确反映数据值。

---

## 9. GPU Color Picking（拾取系统）

### 9.1 原理

GPU Color Picking 的核心思想：**不用射线求交，让 GPU 告诉你点击了什么**。

```
正常渲染：每个三角形用物理颜色绘制 → 人眼看到
拾取渲染：每个单元用唯一颜色 ID 绘制 → 读像素颜色 → 解码得到单元 ID
```

### 9.2 ID 编码/解码

```cpp
// 编码：单元 ID → RGB 颜色
glm::vec3 GLWidget::idToColor(int id) {
    id += 1;                    // +1 是因为 (0,0,0) 保留给"背景/未命中"
    int r = (id      ) & 0xFF; // 取低 8 位
    int g = (id >>  8) & 0xFF; // 取中 8 位
    int b = (id >> 16) & 0xFF; // 取高 8 位
    return glm::vec3(r/255.0f, g/255.0f, b/255.0f);
}

// 解码：RGB 颜色 → 单元 ID
int GLWidget::colorToId(unsigned char r, unsigned char g, unsigned char b) {
    if (r == 0 && g == 0 && b == 0) return -1;  // 背景
    return (r | (g << 8) | (b << 16)) - 1;
}
```

用 3 个字节编码 ID，最多支持 **16,777,215** 个单元（1600 万），足够了。

### 9.3 拾取渲染流程 (renderPickBuffer)

```
① 绑定离屏 FBO（画到隐藏画布上）
  glBindFramebuffer(GL_FRAMEBUFFER, pickFramebuffer_ 的后端 FBO handle)

② 清屏为黑色 (0,0,0) = 背景/未命中
  glClearColor(0, 0, 0, 1)

③ 使用 Pick Shader（无光照、纯色输出）
  glUseProgram(pickShader)

④ 逐单元绘制
  for 每组连续的同单元三角形:
      跳过隐藏部件
      设置该单元的颜色 ID: glUniform3f(pickColorLoc, r, g, b)
      绘制: glDrawElements(GL_TRIANGLES, count, ...)

⑤ 恢复所有 GL 状态
```

### 9.4 读取像素

```cpp
// 绑定 FBO 并读取点击位置的像素
glBindFramebuffer(GL_FRAMEBUFFER, pickFramebuffer_ 的后端 FBO handle);
int px = pos.x() * dpr;                    // 处理高 DPI
int py = (height() - pos.y()) * dpr;       // Y 轴翻转（OpenGL 坐标系 Y 朝上）
glReadPixels(px, py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
int elemId = colorToId(pixel[0], pixel[1], pixel[2]);
```

### 9.5 节点拾取的特殊处理

Color Picking 得到的是**单元 ID**（因为整个单元用同一颜色画）。在节点拾取模式下，还需要找到该单元中**最近的节点**：

```cpp
// 遍历命中单元的所有三角形的所有顶点
// 将每个顶点投影到 NDC 空间
// 计算与鼠标位置的 2D 距离
// 取距离最小的顶点 → 对应的 FEM 节点 ID
```

### 9.6 框选

框选不用 FBO，直接在 CPU 端遍历：

```cpp
// 将框选矩形转换为 NDC 坐标
float ndcL = (2.0f * rect.left() / width()) - 1.0f;
// ...

// 遍历所有顶点/三角形
for 每个顶点/三角形:
    投影到 NDC: clip = MVP * worldPos; ndc = clip.xy / clip.w
    if (ndc 在框选矩形内):
        加入选中集合
```

### 9.7 延迟拾取

```cpp
// mouseReleaseEvent 中：
pickPointPending_ = true;    // 标记"下一帧需要拾取"
pendingPickPos_ = pos;
update();                    // 请求重绘

// paintGL() 开头：
if (pickPointPending_) {
    pickPointPending_ = false;
    pickAtPoint(pendingPickPos_, pendingPickCtrl_);
}
```

为什么不在 mouseReleaseEvent 中直接拾取？因为拾取需要操作 OpenGL（渲染 FBO、读像素），而在 Qt 事件处理函数中 GL 上下文可能不是当前的。在 `paintGL()` 中 Qt 保证 GL 上下文可用。

---

## 10. 部件可见性与 Texture Buffer

### 10.1 部件可见性过滤

当用户在 PartsPanel 中隐藏某个部件时：

```cpp
void GLWidget::setPartVisibility(int partIndex, bool visible) {
    partVisibility_[partIndex] = visible;
    partVisibilityDirty_ = true;     // 标记需要重建 IBO
    edgeVisibilityDirty_ = true;     // 边线也需要重建
    update();
}
```

在 `paintGL()` 中重建 IBO：

```cpp
if (partVisibilityDirty_) {
    vector<unsigned int> filtered;      // 过滤后的索引
    vector<float> filteredTriPart;      // 过滤后的部件索引

    for 每个三角形 t:
        int part = triToPart_[t];
        if (部件不可见): continue;      // 跳过
        filtered.push_back(索引...);    // 保留
        filteredTriPart.push_back(part);

    // 重新上传 IBO
    ibo_->allocate(filtered.data(), ...);

    // 同步更新 texture buffer
    // （因为 gl_PrimitiveID 是从 0 重新编号的，需要重建映射）
    glBackend->uploadTextureBuffer(*triPartTextureBuffer_,
                                   filteredTriPart.data(),
                                   ...);
}
```

### 10.2 Texture Buffer 详解

Texture Buffer 是一种特殊的一维纹理，用于在着色器中按索引查找数据。

**C++ 端**：
```cpp
// 创建
triPartTextureBuffer_ = std::make_unique<OpenGLTextureBufferResource>();
glBackend->createTextureBufferResource(*triPartTextureBuffer_);

// 上传数据
glBackend->uploadTextureBuffer(*triPartTextureBuffer_, data, size);

// 绑定到纹理单元 0
glBackend->bindTextureBufferToUnit(*triPartTextureBuffer_, 0);
shader_->setUniformValue("uTriPartMap", 0);  // 告诉着色器用纹理单元 0
```

**着色器端**：
```glsl
uniform samplerBuffer uTriPartMap;

void main() {
    // gl_PrimitiveID = 当前三角形的序号（从 0 开始）
    int partIdx = int(texelFetch(uTriPartMap, gl_PrimitiveID).r);
    vec3 color = kPalette[partIdx % 8];
}
```

`texelFetch(sampler, index)` 就是按索引取数据，类似数组的 `array[index]`。

---

## 11. 选中高亮与轮廓边算法

### 11.1 三种高亮方式

| 拾取模式 | 高亮方式 | 绘制命令 |
|----------|----------|----------|
| Node | 金色圆点 | `glDrawArrays(GL_POINTS, ...)` + `glPointSize(8.0)` |
| Element | 金色边线 | `glDrawArrays(GL_LINES, ...)` + `glLineWidth(2.5)` |
| Part | 轮廓边线 | 同上，但只画轮廓边 |

### 11.2 部件轮廓边算法

部件模式使用了优化的**两级缓存**架构：

**第一级：选中变化时构建** (`buildPartEdgeCache`)

遍历选中部件的所有三角形的所有边，利用预建的全局边邻接表 `edgeAdjMap_` 分类每条边：

```
每条边的邻接三角形分析:
  ├── 边界边（一侧选中，一侧未选中）→ 缓存为静态边
  ├── 开放边（只有一个三角形）→ 缓存为静态边
  ├── 特征边（两侧法线夹角 > 60°）→ 缓存为静态边
  └── 平滑内部边 → 缓存为轮廓边候选（记录两侧法线）
```

**第二级：相机变化时刷新** (`updateSilhouetteFromCache`)

```cpp
for 每条轮廓边候选:
    计算视线方向 viewDir = eyePos - edgeMidpoint
    float d0 = dot(法线0, viewDir)   // 一侧朝向相机？
    float d1 = dot(法线1, viewDir)   // 另一侧朝向相机？
    if (d0 * d1 <= 0)                // 符号相反 = 一侧朝向一侧背离
        → 这是轮廓边，加入渲染列表
```

**为什么分两级**：
- 选中变化时需要遍历所有边（O(边数)），较慢
- 相机旋转时只需要检查候选边（O(候选边数)），很快
- 大部分边是边界/特征/开放边，不会随视角变化，所以缓存为"静态边"

### 11.3 全局边邻接表 (buildEdgeAdjacency)

```cpp
// 预建：每条边知道自己相邻哪些三角形
for 每个三角形 t:
    for 每条边 (va, vb):
        // 用 FEM 节点 ID 排序后作为 key（确保同一条边只有一个 key）
        int64_t key = (min(na,nb) << 32) | max(na,nb);
        edgeAdjMap_[key].adjTris.push_back(t);
```

这个结构在网格加载后构建一次，之后所有边的邻接查询都是 O(1)。

---

## 12. 渐变背景与坐标轴指示器

### 12.1 渐变背景

```
绘制顺序：
  ① 关闭深度测试（背景不参与深度比较）
  ② 绑定 bgShader + bgVao
  ③ 绘制全屏四边形（6 顶点 = 2 三角形）
  ④ GPU 自动插值顶点颜色 → 渐变
  ⑤ 恢复深度测试
```

背景颜色由当前主题决定（`bgTopColor_` / `bgBotColor_`），切换主题时重新上传 VBO 数据。

### 12.2 坐标轴指示器

坐标轴在**独立的小视口**中绘制：

```cpp
// 设置左下角 120×120 像素的小视口
glViewport(margin * dpr, margin * dpr, axesSize * dpr, axesSize * dpr);

// 只清除深度（不清除颜色，保留已绘制的背景/场景）
glClear(GL_DEPTH_BUFFER_BIT);

// 使用正交投影（不需要透视效果）
glm::mat4 axesProj = glm::ortho(-1.3f, 1.3f, -1.3f, 1.3f, 0.01f, 10.0f);

// 视图矩阵只保留旋转（跟随相机朝向，但不跟随平移/缩放）
glm::mat3 rot = glm::mat3(cam_.viewMatrix());  // 提取 3x3 旋转部分

// 绘制
axesShader_->bind();
glDrawArrays(GL_TRIANGLES, offset, count);

// 恢复全屏视口
glViewport(0, 0, width() * dpr, height() * dpr);
```

**X/Y/Z 文字标签**在 QPainter 阶段用 2D 文字绘制（见下节）。

---

## 13. 云图渲染（标量场可视化）

### 13.1 数据流

```
ResultPanel 选择分量 → applyResult(field, title) 信号
    │
    ▼
MainWindow 接收信号
    │
    ├── 遍历渲染顶点，通过 vertexToNode 映射查标量值
    │   for (int vi = 0; vi < vertexCount; vi++):
    │       int nodeId = vertexToNode[vi]
    │       float val = field.values[nodeId]
    │       scalars.push_back(val)
    │
    └── GLWidget::setVertexScalars(scalars, min, max, numBands)
        │
        ├── 上传标量到 meshResource_ 的标量缓冲（GPU 端 VBO）
        │   glBackend->uploadMeshScalarBuffer(*meshResource_, ...)
        │   glVertexAttribPointer(3, 1, GL_FLOAT, ...)  // location 3
        │
        └── 设置 uniform 参数
            scalarMin_, scalarMax_, numBands_
```

### 13.2 GPU 端色谱映射

在**片段着色器**中执行，而非 CPU 端：

```glsl
if (uContourMode) {
    // ① 归一化：值 → [0, 1]
    float range = uScalarMax - uScalarMin;
    float t = clamp((vScalar - uScalarMin) / range, 0.0, 1.0);

    // ② 量化：分成 N 个色阶
    int band = int(t * float(uNumBands));
    if (band >= uNumBands) band = uNumBands - 1;
    float qt = (float(band) + 0.5) / float(uNumBands);  // 取色阶中点

    // ③ Jet 色谱映射
    surfaceColor = jetColor(qt);
}
```

**为什么在 GPU 做**：CPU 端遍历几十万个顶点做色谱映射很慢，而 GPU 可以对每个像素并行计算，快几个数量级。

### 13.3 Jet 色谱函数

```glsl
vec3 jetColor(float t) {   // t ∈ [0, 1]
    if (t < 0.125)      → 深蓝
    else if (t < 0.375) → 蓝→青
    else if (t < 0.625) → 青→绿→黄
    else if (t < 0.875) → 黄→红
    else                 → 深红
}
```

这是经典的工程可视化色谱，类似 MATLAB 默认配色。

---

## 14. QPainter 与 OpenGL 混合绘制

### 14.1 问题

QOpenGLWidget 支持用 `QPainter` 在 OpenGL 渲染结果上叠加 2D 内容（文字、线条等）。但 QPainter 会修改 OpenGL 状态（viewport、深度测试、混合等），导致后续 GL 操作出错。

### 14.2 解决方案

本项目使用两种策略：

**策略一：在 paintGL() 末尾使用 QPainter**

```cpp
// 所有 OpenGL 绘制完成后...
{
    QPainter painter(this);
    painter.beginNativePainting();   // 告诉 QPainter "我刚用过 OpenGL"
    painter.endNativePainting();     // QPainter 恢复自己的状态
    painter.setRenderHint(QPainter::Antialiasing);
    drawAxesLabels(painter);         // 绘制 XYZ 标签
    drawIdLabels(painter, mvp);      // 绘制选中项 ID
    painter.end();
}
```

**策略二：独立覆盖层控件 (ColorBarOverlay)**

色标不在 QPainter(QOpenGLWidget) 中画，而是用一个独立的 QWidget 子控件覆盖在 GLWidget 上方：

```cpp
class ColorBarOverlay : public QWidget {
    // 使用 Qt 的 raster 绘图引擎，完全不涉及 OpenGL
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);   // 这里 this 是普通 QWidget，不是 QOpenGLWidget
        // 绘制色标...
    }
};
```

**为什么要这样**：色标的 `paintEvent` 可能在 FBO 拾取操作中途被触发（Qt 事件驱动），此时 GL 状态不可预测。独立控件完全隔离了 GL 状态。

### 14.3 paintGL() 开头恢复状态

```cpp
void GLWidget::paintGL() {
    // QPainter 可能在上一帧末尾修改了这些状态
    int dpr_ = devicePixelRatio();
    glViewport(0, 0, width() * dpr_, height() * dpr_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    // 现在 GL 状态是干净的，可以开始渲染...
}
```

---

## 附录：GLWidget 中所有 OpenGL 资源一览

```
场景渲染:
  shader_        → scene.vert + scene.frag
  meshResource_  → 后端托管主 VAO/VBO/IBO/颜色 VBO/标量 VBO
  triPartTextureBuffer_ → 后端托管三角形→部件索引 texture buffer

边线渲染:
  edgeResource_  → 后端托管普通边线 VAO/VBO/IBO（按部件可见性过滤）

选中高亮:
  selectionEdgeResource_ → 后端托管高亮边线/轮廓点 VAO/VBO

后处理叠加:
  overlayResource_ → 后端托管未变形叠加线框 VAO/VBO
  sliceResource_   → 后端托管切片交线 VAO/VBO
  isoResource_     → 后端托管等值面 VAO/VBO/IBO
  clipPreviewResource_     → 后端托管裁剪/切片预览平面 VAO/VBO/IBO
  clipPreviewEdgeResource_ → 后端托管裁剪/切片预览轮廓线 VAO/VBO

渐变背景:
  bgShader_      → background.vert + background.frag
  backgroundGeometry_ → 后端托管背景 VAO/VBO（全屏四边形 6 顶点）

坐标轴:
  axesShader_    → axes.vert + axes.frag
  axesGeometry_  → 后端托管坐标轴 VAO/VBO（圆柱+圆锥+球体）

拾取:
  pickShader_    → pick.vert + pick.frag
  pickFramebuffer_ → 后端托管离屏 FBO（与窗口同尺寸）
  pickVertexArray_ → 后端托管拾取专用 VAO（避免污染主 VAO）
```
