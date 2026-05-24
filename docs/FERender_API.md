# FERender 渲染库 API 参考文档

> **版本**：1.0
> **依赖**：Qt 6.8+ (Core, Gui, Widgets, OpenGL, OpenGLWidgets) · OpenGL 4.1+ · GLM 0.9.9+ · Vulkan SDK 1.4+（可选 RHI 后端，老 SDK 可通过 CMake cache 回退 shader target env）· Metal.framework（macOS 可选 RHI 后端）
> **语言标准**：C++17
> **构建产物**：共享库 `FERender.dll` / `libFERender.so` / `libFERender.dylib`

---

## 目录

- [1. 快速开始](#1-快速开始)
  - [1.1 CMake 集成](#11-cmake-集成)
  - [1.2 最小示例](#12-最小示例)
  - [1.3 典型工作流](#13-典型工作流)
- [2. 数据层 API](#2-数据层-api)
  - [2.1 FENode — 有限元节点](#21-fenode--有限元节点)
  - [2.2 FEElement — 有限元单元](#22-feelement--有限元单元)
  - [2.3 ElementType — 单元类型枚举](#23-elementtype--单元类型枚举)
  - [2.4 FEGroup — 分组结构](#24-fegroup--分组结构)
  - [2.5 FEModel — 有限元模型容器](#25-femodel--有限元模型容器)
  - [2.6 FEField — 结果场与色谱](#26-ffield--结果场与色谱)
  - [2.7 FEResultData — 多工况结果层级](#27-feresultdata--多工况结果层级)
  - [2.8 FEParser — 有限元文件解析器](#28-feparser--有限元文件解析器)
- [3. 转换层 API](#3-转换层-api)
  - [3.1 Mesh — 三角网格数据结构](#31-mesh--三角网格数据结构)
  - [3.2 Geometry — 基础几何体生成器](#32-geometry--基础几何体生成器)
  - [3.3 FERenderData — 渲染数据包](#33-ferenderdata--渲染数据包)
  - [3.4 FEMeshConverter — 网格转换器](#34-femeshconverter--网格转换器)
- [4. 渲染层 API](#4-渲染层-api)
  - [4.1 Camera — 轨道相机](#41-camera--轨道相机)
  - [4.2 GLWidget — OpenGL 渲染窗口](#42-glwidget--opengl-渲染窗口)
  - [4.3 RenderViewport — 渲染视口宿主](#43-renderviewport--渲染视口宿主)
  - [4.4 RHI 公共类型与设置](#44-rhi-公共类型与设置)
- [5. 交互层 API](#5-交互层-api)
  - [5.1 PickMode — 拾取模式](#51-pickmode--拾取模式)
  - [5.2 ViewportInteractionMode — 视口交互工具](#52-viewportinteractionmode--视口交互工具)
  - [5.3 FEPickResult — 拾取结果](#53-fpickresult--拾取结果)
  - [5.4 FESelection — 选中状态](#54-feselection--选中状态)
- [6. 变形与动画 API](#6-变形与动画-api)
  - [6.1 FEDeformation — 变形显示工具类](#61-fedeformation--变形显示工具类)
  - [6.2 FEAnimationController — 帧动画控制器](#62-feanimationcontroller--帧动画控制器)
- [7. 探针与导出 API](#7-探针与导出-api)
  - [7.1 FEProbe — 结果探针](#71-feprobe--结果探针)
- [8. 过滤与等值面 API](#8-过滤与等值面-api)
  - [8.1 FEPostFilter — 后处理过滤器](#81-fepostfilter--后处理过滤器)
  - [8.2 FEIsoSurface — 等值面提取](#82-feisosurface--等值面提取)
- [9. 完整使用示例](#9-完整使用示例)
  - [9.1 加载模型并渲染](#91-加载模型并渲染)
  - [9.2 显示标量云图](#92-显示标量云图)
  - [9.3 部件可见性控制](#93-部件可见性控制)
  - [9.4 拾取交互](#94-拾取交互)
  - [9.5 变形显示](#95-变形显示)
  - [9.6 变形动画](#96-变形动画)
  - [9.7 探针查值与热点导出](#97-探针查值与热点导出)
  - [9.8 阈值过滤与裁剪](#98-阈值过滤与裁剪)
- [附录 A：头文件清单](#附录-a头文件清单)
- [附录 B：单元类型速查表](#附录-b单元类型速查表)

---

## 1. 快速开始

### 1.1 CMake 集成

将 FERender 安装到某个前缀路径后，在消费项目的 `CMakeLists.txt` 中：

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# 指向 FERender 的安装路径
list(APPEND CMAKE_PREFIX_PATH "/path/to/ferender/install")

find_package(FERender REQUIRED)

add_executable(MyApp main.cpp)
target_link_libraries(MyApp PRIVATE FERender::FERender)
```

`FERender::FERender` 会自动传递以下依赖：
- Qt6::Core, Qt6::Gui, Qt6::Widgets, Qt6::OpenGL, Qt6::OpenGLWidgets
- OpenGL::GL
- GLM 头文件路径
- 可选 Vulkan SDK 1.4+（仅在构建 FERender 时启用 Vulkan RHI 后端；shader target env 默认 `vulkan1.4`）
- macOS 可选 Metal.framework / Foundation.framework（仅在构建 FERender 时启用 Metal RHI 后端）

仓库内的 `examples/simple_viewer` 是完整的外部调用示例：它通过
`find_package(FERender REQUIRED CONFIG)` 链接安装后的 `FERender::FERender`，
并用一个简单 Qt 界面演示 `FEModel → FEMeshConverter → GLWidget` 的调用链，
包含云图显示（`FEResultMapper`）和变形显示（`FEDeformation` + overlay）两个演示按钮。

### 1.2 最小示例

```cpp
#include <QApplication>
#include <QSurfaceFormat>
#include "GLWidget.h"
#include "FEModel.h"
#include "FEMeshConverter.h"
#include "FEResultMapper.h"

int main(int argc, char* argv[]) {
    // 配置 OpenGL
    QSurfaceFormat fmt;
    fmt.setVersion(4, 1);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(32);
    fmt.setSamples(8);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);

    // 构建一个简单模型
    FEModel model;
    model.addNode(1, {0.0f, 0.0f, 0.0f});
    model.addNode(2, {1.0f, 0.0f, 0.0f});
    model.addNode(3, {0.5f, 1.0f, 0.0f});
    model.addElement(1, ElementType::TRI3, {1, 2, 3});

    // 转换为渲染数据
    FERenderData rd = FEMeshConverter::toRenderData(model);

    // 创建渲染窗口并显示
    GLWidget viewer;
    viewer.setMesh(rd.mesh);
    viewer.setTriangleToElementMap(rd.triangleToElement);
    viewer.setVertexToNodeMap(rd.vertexToNode);
    viewer.fitToModel(model.computeCenter(), model.computeSize());
    viewer.resize(800, 600);
    viewer.show();

    return app.exec();
}
```

### 1.3 典型工作流

```
                ┌─────────────┐
                │  数据加载    │  用户自行解析 FEM 文件
                │  填充 FEModel│  (Nastran/Abaqus/OP2...)
                └──────┬──────┘
                       ↓
              ┌────────────────┐
              │ FEMeshConverter │  模型 → 渲染数据
              │ ::toRenderData()│
              └───────┬────────┘
                      ↓
           ┌──────────────────┐
           │   FERenderData   │  Mesh + 映射表
           └────────┬─────────┘
                    ↓
         ┌─────────────────────┐
         │     GLWidget        │  设置 Mesh → 渲染
         │  setMesh / 映射表   │  鼠标交互 → 拾取
         └─────────────────────┘
```

---

## 2. 数据层 API

### 2.1 FENode — 有限元节点

**头文件**：`FENode.h`

```cpp
struct FENode {
    int       id;      // 节点全局编号（不要求连续）
    glm::vec3 coords;  // 节点坐标 (x, y, z)，单精度
};
```

| 字段     | 类型        | 说明 |
|----------|-------------|------|
| `id`     | `int`       | 节点全局唯一 ID，支持不连续编号 |
| `coords` | `glm::vec3` | 三维坐标，默认 `(0, 0, 0)` |

> **注意**：使用单精度 `float`，满足可视化精度需求。如需双精度计算，需在外部使用 `glm::dvec3`。

---

### 2.2 FEElement — 有限元单元

**头文件**：`FEElement.h`

```cpp
struct FEElement {
    int              id;       // 单元全局编号
    ElementType      type;     // 单元类型
    std::vector<int> nodeIds;  // 节点 ID 列表（顺序遵循标准约定）
};
```

| 字段      | 类型               | 说明 |
|-----------|--------------------|------|
| `id`      | `int`              | 单元全局唯一 ID |
| `type`    | `ElementType`      | 单元类型枚举 |
| `nodeIds` | `std::vector<int>` | 构成该单元的节点 ID，顺序遵循 Abaqus/Nastran 约定 |

**辅助函数**：

```cpp
// 获取单元维度：1(线) / 2(壳) / 3(实体)
int elementDimension(ElementType type);

// 获取角节点数（不含高阶中间节点）
int elementCornerNodeCount(ElementType type);
```

---

### 2.3 ElementType — 单元类型枚举

**头文件**：`FEElement.h`

```cpp
enum class ElementType {
    // 1D 线单元
    BAR2,        // 2 节点杆单元
    BAR3,        // 3 节点二次杆单元

    // 2D 壳/板单元
    TRI3,        // 3 节点三角形
    TRI6,        // 6 节点二次三角形
    QUAD4,       // 4 节点四边形
    QUAD8,       // 8 节点二次四边形

    // 3D 实体单元
    TET4,        // 4 节点四面体
    TET10,       // 10 节点二次四面体
    HEX8,        // 8 节点六面体
    HEX20,       // 20 节点二次六面体
    WEDGE6,      // 6 节点三棱柱
    PYRAMID5,    // 5 节点四棱锥
};
```

完整速查表见 [附录 B](#附录-b单元类型速查表)。

---

### 2.4 FEGroup — 分组结构

**头文件**：`FEGroup.h`

#### FEPart（部件）

```cpp
struct FEPart {
    std::string      name;        // 部件名称
    std::vector<int> nodeIds;     // 属于该部件的节点 ID
    std::vector<int> elementIds;  // 属于该部件的单元 ID
    bool             visible;     // 是否可见，默认 true
};
```

#### FENodeSet（节点集）

```cpp
struct FENodeSet {
    std::string      name;     // 集合名称（如 "FixedSupport"）
    std::vector<int> nodeIds;  // 节点 ID 列表
};
```

#### FEElementSet（单元集）

```cpp
struct FEElementSet {
    std::string      name;        // 集合名称（如 "Steel_Part"）
    std::vector<int> elementIds;  // 单元 ID 列表
};
```

---

### 2.5 FEModel — 有限元模型容器

**头文件**：`FEModel.h`

FEModel 是整个有限元数据的统一入口，持有模型的全部信息。**纯数据层，不包含任何渲染逻辑。**

#### 公开数据成员

| 成员 | 类型 | 说明 |
|------|------|------|
| `name` | `std::string` | 模型名称（通常来自文件名） |
| `filePath` | `std::string` | 源文件路径 |
| `nodes` | `std::unordered_map<int, FENode>` | 节点表：ID → 节点 |
| `elements` | `std::unordered_map<int, FEElement>` | 单元表：ID → 单元 |
| `parts` | `std::vector<FEPart>` | 部件列表 |
| `nodeSets` | `std::vector<FENodeSet>` | 节点集列表 |
| `elementSets` | `std::vector<FEElementSet>` | 单元集列表 |
| `scalarFields` | `std::vector<FEScalarField>` | 标量结果场列表 |
| `vectorFields` | `std::vector<FEVectorField>` | 矢量结果场列表 |

#### 公开方法

```cpp
// ── 添加数据 ──

// 添加节点，若 ID 已存在则覆盖
void addNode(int id, const glm::vec3& coords);

// 添加单元
void addElement(int id, ElementType type, const std::vector<int>& nodeIds);

// ── 查询 ──

// 按 ID 查找节点坐标，未找到返回 nullptr
const glm::vec3* nodeCoords(int id) const;

// 节点总数
int nodeCount() const;

// 单元总数
int elementCount() const;

// 模型是否为空
bool isEmpty() const;

// ── 空间信息 ──

// 计算轴对齐包围盒 (AABB)
void computeBoundingBox(glm::vec3& bbMin, glm::vec3& bbMax) const;

// 计算几何中心（包围盒中点）
glm::vec3 computeCenter() const;

// 计算最大尺寸（包围盒对角线长度）
float computeSize() const;

// ── 管理 ──

// 清空所有数据（节点、单元、分组、结果场）
void clear();
```

#### 使用示例

```cpp
FEModel model;
model.name = "BracketAssembly";

// 添加节点
model.addNode(1, {0.0f, 0.0f, 0.0f});
model.addNode(2, {1.0f, 0.0f, 0.0f});
model.addNode(3, {1.0f, 1.0f, 0.0f});
model.addNode(4, {0.0f, 1.0f, 0.0f});

// 添加四边形单元
model.addElement(1, ElementType::QUAD4, {1, 2, 3, 4});

// 添加部件
FEPart part;
part.name = "Bracket";
part.elementIds = {1};
part.nodeIds = {1, 2, 3, 4};
model.parts.push_back(part);

// 查询
auto* pos = model.nodeCoords(2);   // → (1, 0, 0)
float size = model.computeSize();  // 包围盒对角线长度
```

---

### 2.6 FEField — 结果场与色谱

**头文件**：`FEField.h`

#### FieldLocation（场数据位置）

```cpp
enum class FieldLocation {
    Node,      // 定义在节点上（位移、温度等）
    Element    // 定义在单元上（应力、应变等）
};
```

#### FEScalarField（标量场）

```cpp
struct FEScalarField {
    std::string                    name;      // 场名称（如 "Von Mises Stress"）
    std::string                    unit;      // 单位（如 "MPa"）
    FieldLocation                  location;  // 数据位置，默认 Node
    std::unordered_map<int, float> values;    // ID → 标量值

    // 计算值域范围
    void computeRange(float& minVal, float& maxVal) const;

    // 计算值域范围，同时返回极值对应的 ID
    void computeRangeWithIds(float& minVal, float& maxVal, int& minId, int& maxId) const;
};
```

#### FEVectorField（矢量场）

```cpp
struct FEVectorField {
    std::string                        name;      // 场名称（如 "Displacement"）
    std::string                        unit;      // 单位（如 "mm"）
    FieldLocation                      location;  // 数据位置，默认 Node
    std::unordered_map<int, glm::vec3> values;    // ID → 矢量值

    // 计算矢量幅值（模长）范围
    void computeMagnitudeRange(float& minMag, float& maxMag) const;
};
```

#### ColorMapType（色谱类型）

```cpp
enum class ColorMapType {
    Rainbow,    // 经典彩虹：蓝 → 青 → 绿 → 黄 → 红
    Jet,        // Jet 色谱（类 MATLAB 默认）
    CoolWarm,   // 冷暖色谱：蓝 → 白 → 红（适合正负值对比）
    Grayscale,  // 灰度：黑 → 白
    Viridis     // Viridis（感知均匀，色盲友好）
};
```

#### ColorMap（色谱映射器）

```cpp
struct ColorMap {
    ColorMapType type;            // 色谱类型，默认 Rainbow
    int          discreteLevels;  // 分段色阶数，默认 10（0 = 平滑渐变）

    // 归一化值 [0,1] → RGB 颜色 [0,1]
    glm::vec3 map(float t) const;

    // 原始值 → RGB（自动归一化）
    glm::vec3 map(float value, float minVal, float maxVal) const;
};
```

#### 使用示例

```cpp
// 创建温度标量场
FEScalarField tempField;
tempField.name = "Temperature";
tempField.unit = "°C";
tempField.location = FieldLocation::Node;
tempField.values[1] = 25.0f;
tempField.values[2] = 100.0f;
tempField.values[3] = 75.0f;

// 查询值域
float minT, maxT;
tempField.computeRange(minT, maxT);  // minT=25, maxT=100

// 色谱映射
ColorMap cmap;
cmap.type = ColorMapType::Jet;
cmap.discreteLevels = 12;            // 12 级色阶

glm::vec3 color = cmap.map(75.0f, minT, maxT);  // → 对应的 RGB
```

---

### 2.7 FEResultData — 多工况结果层级

**头文件**：`FEResultData.h`

用于组织 OP2 等求解结果文件中的层级数据。

```
FEResultData                    顶层容器
  └─ FESubcase                  一个工况
       └─ FEResultType          一种结果类型（位移/应力）
            └─ FEResultComponent 单个分量（X/Y/Z/Magnitude...）
```

#### FEResultComponent

```cpp
struct FEResultComponent {
    std::string   name;    // 分量名称（"X", "Y", "Z", "Magnitude", "Von Mises"）
    FEScalarField field;   // 标量场数据
};
```

#### FEResultType

```cpp
struct FEResultType {
    std::string                    name;         // 类型名称（"Displacement", "Stress"）
    std::vector<FEResultComponent> components;   // 分量列表
    FEVectorField                  vectorField;  // 可选矢量场
    bool                           hasVector;    // 是否有矢量场，默认 false
};
```

#### FESubcase

```cpp
struct FESubcase {
    int                        id;           // 工况 ID
    std::string                name;         // 工况名称
    std::vector<FEResultType>  resultTypes;  // 结果类型列表
};
```

#### FEResultData

```cpp
struct FEResultData {
    std::vector<FESubcase> subcases;   // 工况列表

    bool empty() const;   // 是否有数据
    void clear();         // 清空所有结果
};
```

#### 使用示例

```cpp
FEResultData results;

FESubcase sc;
sc.id = 1;
sc.name = "Static Load Case 1";

FEResultType dispType;
dispType.name = "Displacement";
dispType.hasVector = true;
dispType.vectorField.name = "Displacement";
dispType.vectorField.unit = "mm";
dispType.vectorField.values[1] = {0.1f, 0.0f, -0.05f};
dispType.vectorField.values[2] = {0.3f, 0.0f, -0.12f};

// 添加幅值分量
FEResultComponent magComp;
magComp.name = "Magnitude";
magComp.field.name = "Displacement Magnitude";
magComp.field.unit = "mm";
magComp.field.values[1] = 0.112f;
magComp.field.values[2] = 0.323f;
dispType.components.push_back(magComp);

sc.resultTypes.push_back(dispType);
results.subcases.push_back(sc);
```

### 2.8 FEResultRepository — 结果仓库与帧模型

**头文件**：`FEResultRepository.h`

在 `FEResultData` 的 subcase/type/component 层级基础上，新增 step/frame/time/frequency/mode 帧模型，
为动画、工况比较和结果曲线打底。

#### FEResultDomain（分析域）

```cpp
enum class FEResultDomain {
    Static,       // 静力分析
    Time,         // 瞬态/时间域
    Frequency,    // 频率域
    Mode          // 模态分析
};
```

#### FEResultFrameInfo（帧元数据）

```cpp
struct FEResultFrameInfo {
    int subcaseId = 0;                              // 原始工况 ID
    int stepIndex = 0;                              // 步序号
    int frameIndex = 0;                             // 帧序号
    double value = 0.0;                             // 轴值（时间/频率/特征值）
    std::string valueLabel;                         // 轴值标签（如 "t=0.5s"）
    FEResultDomain domain = FEResultDomain::Static; // 分析域
};
```

#### FEResultFrame（结果帧）

```cpp
struct FEResultFrame {
    FEResultFrameInfo info;                  // 帧元数据
    std::vector<FEResultType> resultTypes;   // 该帧包含的结果类型列表
};
```

#### FEResultRepository（结果仓库）

```cpp
class FEResultRepository {
public:
    void clear();                                              // 清空
    void addFrame(const FEResultFrame& frame);                 // 添加帧
    int frameCount() const;                                    // 帧总数
    const FEResultFrame* frame(int index) const;               // 按索引取帧（越界返回 nullptr）
    std::vector<std::string> resultTypeNames(int frameIndex) const;  // 帧内结果类型名称
    bool empty() const;                                        // 是否为空

    // 从旧 FEResultData 转换（每个 subcase 变为一帧，domain=Static）
    static FEResultRepository fromResultData(const FEResultData& data);
};
```

#### 与 FEResultData 的关系

| 旧结构 | 新结构 | 说明 |
|--------|--------|------|
| `FESubcase` | `FEResultFrame` | 一个工况对应一帧，`subcaseId` 保持不变 |
| `FESubcase::name` | `FEResultFrameInfo::valueLabel` | 帧标签，可含域轴值 |
| —（缺失） | `FEResultFrameInfo::domain/value` | 新增分析域和轴值 |
| `FESubcase::resultTypes` | `FEResultFrame::resultTypes` | 结果类型列表不变 |

`FEResultData` 保持不变，旧代码可继续使用。新代码推荐使用 `FEResultRepository`。
调用 `FEResultRepository::fromResultData()` 可一行完成转换。

#### 使用示例

```cpp
// 方式 A：从旧 FEResultData 转换
FEResultData data;
FEParser::parseNastranOp2Results("model.op2", data);
FEResultRepository repo = FEResultRepository::fromResultData(data);

// 方式 B：直接解析到 repository（推荐，保留帧域信息）
FEResultRepository repo;
FEParser::parseNastranOp2Results("model.op2", repo);
// 或
FEParser::parseUnvResults("result.unv", repo);

// 查询
for (int i = 0; i < repo.frameCount(); ++i) {
    const FEResultFrame* f = repo.frame(i);
    qDebug() << "Frame" << i << ":" << f->info.valueLabel.c_str()
             << "domain=" << static_cast<int>(f->info.domain);
}
```

---

### 2.9 FEParser — 有限元文件解析器

**头文件**：`FEParser.h`

无状态静态工具类，负责解析各种有限元文件格式并填充 `FEModel` / `FEResultData`。

```cpp
class FERENDER_EXPORT FEParser {
public:
    /** 解析 ABAQUS INP 文件（含 *INCLUDE 递归展开） */
    static bool parseAbaqusInp(const QString& filePath, FEModel& model,
                                const std::function<void(int)>& progress = nullptr);

    /** 解析 Nastran BDF/FEM 文件（固定/自由格式，CORD2R 坐标系变换） */
    static bool parseNastranBdf(const QString& filePath, FEModel& model,
                                 const std::function<void(int)>& progress = nullptr);

    /** 解析 Nastran OP2 二进制几何数据（GEOM1/GEOM2/BGPDT/EQEXIN） */
    static bool parseNastranOp2(const QString& filePath, FEModel& model,
                                 const std::function<void(int)>& progress = nullptr);

    /** 解析 STL 三角面几何（ASCII / Binary，生成 TRI3 壳单元） */
    static bool parseStlGeometry(const QString& filePath, FEModel& model,
                                  const std::function<void(int)>& progress = nullptr);

    /** 解析 CAD 交换几何（STEP / IGES，需要构建时启用 OpenCASCADE） */
    static bool parseCadGeometry(const QString& filePath, FEModel& model,
                                  const std::function<void(int)>& progress = nullptr);

    /** 解析 Nastran OP2 结果数据（位移 OUG + 应力 OES） */
    static bool parseNastranOp2Results(const QString& filePath, FEResultData& results);

    /** 解析 UNV 结果数据（Dataset 2414 / 55） */
    static bool parseUnvResults(const QString& filePath, FEResultData& results);

    /** 解析 OP2 结果到 FEResultRepository（兼容包装） */
    static bool parseNastranOp2Results(const QString& filePath, FEResultRepository& repo);

    /** 解析 UNV 结果到 FEResultRepository（保留帧域信息） */
    static bool parseUnvResults(const QString& filePath, FEResultRepository& repo);
};
```

**参数说明**：

| 参数 | 说明 |
|------|------|
| `filePath` | 文件绝对路径 |
| `model` | 输出：填充节点、单元、部件数据 |
| `results` | 输出：填充多工况位移/应力结果 |
| `progress` | 可选回调，参数为 0-100 的进度百分比 |

**支持的 OP2 应力单元类型**：

| 类别 | 类型码 |
|------|--------|
| Rod/Bar | CROD(1), CBEAM(2), CTUBE(3), CONROD(10), CBAR(34/100) |
| Shell | CQUAD4(33/144), CTRIA3(74), CTRIA6(75), composite(95/97), nonlinear(88), CQUAD8(64) |
| Solid | CTETRA(39/85), CHEXA(67/86/93), CPENTA(68/91) |
| Spring/Bush | CELAS(11/12), CBUSH(102) |

**使用示例**：

```cpp
#include "FEParser.h"

FEModel model;
bool ok = FEParser::parseNastranOp2("model.op2", model, [](int pct) {
    qDebug("进度: %d%%", pct);
});

// STL 会按每个三角面生成 TRI3 单元，适合导入几何外形做查看。
FEModel stlModel;
FEParser::parseStlGeometry("geometry.stl", stlModel);

// STEP / IGES 通过 OpenCASCADE 三角化为 TRI3 壳单元。
FEModel cadModel;
FEParser::parseCadGeometry("assembly.step", cadModel);


if (ok) {
    FEResultData results;
    FEParser::parseNastranOp2Results("model.op2", results);
}
```

---

## 3. 转换层 API

### 3.1 Mesh — 三角网格数据结构

**头文件**：`Geometry.h`

```cpp
struct Mesh {
    // ── 主要渲染数据 ──
    std::vector<float>        vertices;     // 顶点数据（交错存储）
    std::vector<unsigned int> indices;      // 三角形索引（每 3 个一组）

    // ── 边线数据 ──
    std::vector<float>        edgeVertices; // 边线顶点（仅位置，GL_LINES 用）
    std::vector<unsigned int> edgeIndices;  // 边线索引（每 2 个一组）
    std::vector<int>          edgeToElement; // 边线 → FEM 单元 ID
    std::vector<std::pair<int,int>> edgeNodeIds; // 边线两端 FEM 节点 ID

    // ── 单元完整边线（用于选中高亮）──
    std::vector<float>              elemEdgeVertices;   // 边顶点坐标
    std::vector<int>                elemEdgeToElement;  // 边 → 单元 ID
    std::vector<std::pair<int,int>> elemEdgeNodeIds;    // 边的节点 ID 对（已排序）

    // ── 便捷方法 ──
    void addVertex(glm::vec3 pos, glm::vec3 normal);
    void addTriangle(unsigned int a, unsigned int b, unsigned int c);
    void addFlatTriangle(glm::vec3 a, glm::vec3 b, glm::vec3 c);   // 自动计算面法线
    void addFlatQuad(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d);
};
```

**顶点内存布局**（每顶点 6 个 float）：

```
偏移  0: px, py, pz   ← 位置
偏移 12: nx, ny, nz   ← 法线
──────────────────────
步长 = 6 × sizeof(float) = 24 bytes
```

| 方法 | 说明 |
|------|------|
| `addVertex(pos, normal)` | 追加一个顶点（位置 + 法线） |
| `addTriangle(a, b, c)` | 追加一个三角形索引 |
| `addFlatTriangle(a, b, c)` | 追加三角形并自动计算面法线（flat shading） |
| `addFlatQuad(a, b, c, d)` | 追加四边形，拆分为 2 个三角形 |

普通边线的 `edgeToElement`、`edgeNodeIds` 与 `edgeIndices` 按边一一对应：`edgeToElement.size() == edgeNodeIds.size() == edgeIndices.size() / 2`。它们用于没有三角面的线/梁单元显隐、拾取过滤、节点/单元结果映射和部件归属反查；裁剪生成的新交点节点 ID 记为 `-1`。`elemEdgeToElement` / `elemEdgeNodeIds` 则用于选中高亮时绘制单元完整边线。

---

### 3.2 Geometry — 基础几何体生成器

**头文件**：`Geometry.h`

命名空间 `Geometry` 提供 7 种基础几何体生成函数，均返回以原点为中心的标准大小 `Mesh`。

```cpp
namespace Geometry {
    Mesh cube();                                          // 正方体
    Mesh tetrahedron();                                   // 正四面体
    Mesh triangularPrism();                               // 三棱柱
    Mesh cylinder(int segments = 36);                     // 圆柱
    Mesh cone(int segments = 36);                         // 圆锥
    Mesh sphere(int rings = 24, int sectors = 36);        // 球体
    Mesh torus(int ringSegs = 36, int tubeSegs = 24);     // 圆环
}
```

| 函数 | 参数 | 默认值 | 说明 |
|------|------|--------|------|
| `cube()` | 无 | — | 单位正方体 |
| `tetrahedron()` | 无 | — | 正四面体 |
| `triangularPrism()` | 无 | — | 三棱柱 |
| `cylinder(segments)` | 侧面分段数 | 36 | 圆柱体 |
| `cone(segments)` | 侧面分段数 | 36 | 圆锥体 |
| `sphere(rings, sectors)` | 纬线数、经线数 | 24, 36 | 球体 |
| `torus(ringSegs, tubeSegs)` | 环分段、管分段 | 36, 24 | 圆环体 |

#### 使用示例

```cpp
GLWidget viewer;
Mesh sphereMesh = Geometry::sphere(32, 64);  // 更精细的球体
viewer.setMesh(sphereMesh);
```

---

### 3.3 FERenderData — 渲染数据包

**头文件**：`FERenderData.h`

`FERenderData` 是 `FEMeshConverter` 的输出结构，捆绑了可渲染的三角网格和反向映射表。

```cpp
struct FERenderData {
    Mesh             mesh;                // 三角网格
    std::vector<int> triangleToElement;   // 三角形索引 → FEM 单元 ID
    std::vector<int> triangleToFace;      // 三角形索引 → 单元内面序号
    std::vector<int> vertexToNode;        // 渲染顶点索引 → FEM 节点 ID
    std::vector<int> triangleToPart;      // 三角形索引 → 部件索引（-1=无部件）
    std::vector<int> edgeToPart;          // 边线索引 → 部件索引（-1=无部件）

    // 便捷查询方法
    int elementAtTriangle(int triIndex) const;  // 三角形 → 单元 ID
    int faceAtTriangle(int triIndex) const;     // 三角形 → 面序号
    int nodeAtVertex(int vertIndex) const;      // 顶点 → 节点 ID
    int triangleCount() const;                   // 三角形总数
    int vertexCount() const;                     // 顶点总数
    void clear();                                // 清空所有数据
};
```

**映射关系示意**：

```
渲染三角形 #5  ─── triangleToElement[5] ───→  FEM 单元 #102
                ─── triangleToFace[5]    ───→  面序号 2（单元的第 3 个面）
                ─── triangleToPart[5]    ───→  部件索引 0

渲染顶点 #10   ─── vertexToNode[10]     ───→  FEM 节点 #57
```

> **注意**：flat shading 下同一个 FEM 节点可能对应多个渲染顶点（因不同面法线不同而复制），所以 `vertexToNode` 是多对一映射。

---

### 3.4 FEMeshConverter — 网格转换器

**头文件**：`FEMeshConverter.h`

纯静态工具类，将 FEModel 转换为 GLWidget 可渲染的数据。**无状态，所有方法为 `static`**。

#### 进度回调

```cpp
using ProgressCallback = std::function<void(int percent)>;  // percent: 0~100
```

#### 主要接口（返回 FERenderData = Mesh + 映射表）

```cpp
// 整个模型 → 渲染数据包
// 自动处理 2D 三角化 + 3D 外表面提取
static FERenderData toRenderData(
    const FEModel& model,
    const ProgressCallback& progress = nullptr
);

// 指定单元子集 → 渲染数据包
// 用于分部件显示、选中高亮等
static FERenderData toRenderData(
    const FEModel& model,
    const std::vector<int>& elementIds,
    const ProgressCallback& progress = nullptr
);

// 带云图颜色的渲染数据包
// Mesh 顶点格式变为 [pos(3) + normal(3) + color(3)]
static FERenderData toColoredRenderData(
    const FEModel& model,
    const FEScalarField& field,
    const ColorMap& colorMap,
    float minVal, float maxVal
);
```

#### 辅助接口（仅返回 Mesh，无映射表）

```cpp
// 变形后的网格：新坐标 = 原坐标 + displacement × scale
static Mesh toDeformedMesh(
    const FEModel& model,
    const FEVectorField& displacement,
    float scale = 1.0f
);

// 线框网格（仅边，GL_LINES 渲染）
static Mesh toWireframeMesh(const FEModel& model);
```

#### 内部转换逻辑

| 单元维度 | 处理方式 |
|----------|----------|
| 1D (BAR2/BAR3) | 生成边线数据，并填充 `Mesh::edgeToElement` / `FERenderData::edgeToPart` |
| 2D (TRI3/QUAD4...) | 直接三角化 |
| 3D (TET4/HEX8...) | 提取外表面 → 三角化 |

**外表面提取算法**：遍历所有单元的所有面，将面的节点 ID 排序后作为 key，只保留被单个单元引用的面（即外表面）。

#### 使用示例

```cpp
FEModel model;
// ... 填充模型数据 ...

// 带进度回调的转换
FERenderData rd = FEMeshConverter::toRenderData(model, [](int pct) {
    qDebug() << "Converting:" << pct << "%";
});

// 仅转换某些单元
std::vector<int> subset = {1, 2, 5, 8};
FERenderData partRd = FEMeshConverter::toRenderData(model, subset);

// 变形显示
FEVectorField disp;
disp.values[1] = {0.1f, 0.0f, -0.05f};
// ...
Mesh deformed = FEMeshConverter::toDeformedMesh(model, disp, 10.0f);  // 10 倍放大
```

---

## 4. 渲染层 API

### 4.1 Camera — 轨道相机

**头文件**：`Camera.h`

围绕目标点旋转的轨道相机，使用球坐标系（yaw / pitch / distance）。

#### 公开数据成员

| 成员 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `yaw` | `float` | `0.0` | 水平旋转角（绕 Y 轴，度） |
| `pitch` | `float` | `0.0` | 垂直旋转角（仰俯角，度） |
| `distance` | `float` | `3.0` | 相机到目标点距离 |
| `target` | `glm::vec3` | `(0,0,0)` | 注视目标点（世界坐标） |
| `rotateSensitivity` | `float` | `0.15` | 旋转灵敏度（像素→角度） |
| `panSensitivity` | `float` | `0.001` | 平移灵敏度（相对距离比例） |
| `zoomSensitivity` | `float` | `0.3` | 缩放灵敏度（滚轮→距离） |
| `minDist` | `float` | `0.1` | 最小缩放距离 |
| `maxDist` | `float` | `50.0` | 最大缩放距离 |

#### 公开方法

```cpp
// 计算相机在世界空间的位置（由球坐标转换）
glm::vec3 eye() const;

// 生成观察矩阵（View Matrix）
glm::mat4 viewMatrix() const;

// 旋转：dx/dy 为鼠标移动像素量
// pitch 限制在 [-89°, 89°] 防止万向锁
void rotate(float dx, float dy);

// 平移：移动 target，速度与 distance 成正比
void pan(float dx, float dy);

// 缩放：delta 为滚轮增量（正值放大），限制在 [minDist, maxDist]
void zoom(float delta);
```

#### 使用示例

```cpp
Camera cam;
cam.target = model.computeCenter();
cam.distance = model.computeSize() * 2.0f;
cam.yaw = 45.0f;
cam.pitch = 30.0f;

glm::mat4 view = cam.viewMatrix();
glm::vec3 eyePos = cam.eye();
```

---

### 4.2 GLWidget — OpenGL 渲染窗口

**头文件**：`GLWidget.h`

继承自 `QOpenGLWidget`，是渲染和交互的核心组件。

#### 构造函数

```cpp
explicit GLWidget(QWidget* parent = nullptr);
~GLWidget() override;
```

#### 网格与颜色

| 方法 | 说明 |
|------|------|
| `void setMesh(const Mesh& mesh)` | 设置要渲染的三角网格，触发 GPU 上传 |
| `void setVertexColors(const std::vector<float>& colors)` | 设置 per-vertex RGB 颜色（用于云图） |
| `void setObjectColor(const glm::vec3& c)` | 设置统一物体颜色 |
| `void setUseVertexColor(bool use)` | 切换云图模式 (`true`) / 纯色模式 (`false`) |
| `void setModelDisplayMode(ModelDisplayMode mode)` | 切换模型显示方式：实体、线框、实体+线框或点显示 |
| `void setViewportGridVisible(bool visible)` | 显示/隐藏视口背景辅助网格；网格线距会随相机缩放自适应分级 |
| `void setVertexScalars(const std::vector<float>& scalars, float minVal, float maxVal, int numBands)` | 上传 per-vertex 标量值，由 GPU 着色器做量化 + 颜色映射 |
| `void setEdgeScalars(const std::vector<float>& scalars, float minVal, float maxVal, int numBands)` | 上传边线 per-vertex 标量值，用于线/梁单元云图；OpenGL、Vulkan 和 Metal 路径均接入线段着色 |

#### 相机与视图

| 方法 | 说明 |
|------|------|
| `void fitToModel(const glm::vec3& center, float size)` | 自适应缩放，将模型居中并适配视口 |
| `void setStandardView(StandardView view)` | 切换标准视图方向：前、后、左、右、上、下 |
| `void setShowLabels(bool show)` | 显示/隐藏当前选中节点、单元或部件的 ID 标签；OpenGL、Vulkan 和 Metal 视口均支持 |

#### 色标控制

| 方法 | 说明 |
|------|------|
| `void setColorBarVisible(bool visible)` | 显示/隐藏色标 |
| `void setColorBarRange(float min, float max)` | 设置色标值域范围 |
| `void setColorBarTitle(const QString& title)` | 设置色标标题 |
| `void setColorBarExtremes(int minId, float minVal, int maxId, float maxVal)` | 设置色标极值信息（最大/最小值及对应 ID，显示在色标下方） |
| `void setColorBarIdLabel(const QString& label)` | 设置色标极值 ID 的标签前缀（如 `"Node ID"` 或 `"Ele ID"`） |

#### 叠加网格（未变形线框）

| 方法 | 说明 |
|------|------|
| `void setOverlayMesh(const Mesh& mesh)` | 设置未变形叠加网格（半透明线框） |
| `void setOverlayVisible(bool visible)` | 控制叠加网格显隐 |

用于变形显示时叠加原始形状，以便对比观察变形量。叠加网格以半透明灰色线框绘制。

#### RHI 选择状态

| 方法 | 返回类型 | 说明 |
|------|----------|------|
| `setPreferredRenderBackend(RenderBackendKind kind)` | `void` | 设置用户首选 RHI，并写入全局 `RenderSettings` |
| `requestedRenderBackendKind()` | `RenderBackendKind` | 用户请求的 RHI |
| `activeRenderBackendKind()` | `RenderBackendKind` | 当前视口实际使用的 RHI |

`GLWidget` 只表示 OpenGL 子视口自身的状态；主界面应优先使用 `RenderViewport`，由它根据全局设置在 OpenGL `GLWidget`、macOS Vulkan 宿主视口和 macOS Metal 宿主视口之间切换。Vulkan/Metal 不可用时会回退到 OpenGL。

#### 过滤预览与后处理叠加

| 方法 | 说明 |
|------|------|
| `void setClipPlanePreview(const glm::vec3& bbMin, const glm::vec3& bbMax, const glm::vec3& origin, const glm::vec3& normal)` | 显示裁剪/切片平面的半透明预览，预览范围由模型包围盒决定 |
| `void clearClipPlanePreview()` | 清除裁剪/切片平面预览 |
| `void setSliceLines(const std::vector<float>& lineVertices)` | 设置切片交线（GL_LINES） |
| `void clearSliceLines()` | 清除切片交线 |
| `void setIsoSurfaceMesh(const Mesh& mesh)` | 设置等值面半透明叠加网格 |
| `void clearIsoSurface()` | 清除等值面 |

#### 拾取映射表

在设置 Mesh 后，需传入映射表以启用拾取功能：

| 方法 | 说明 |
|------|------|
| `void setTriangleToElementMap(const std::vector<int>& map)` | 三角形→单元 ID 映射 |
| `void setVertexToNodeMap(const std::vector<int>& map)` | 顶点→节点 ID 映射 |
| `void setTriangleToPartMap(const std::vector<int>& map)` | 三角形→部件索引映射 |
| `void setEdgeToPartMap(const std::vector<int>& map)` | 边线→部件索引映射 |

#### 拾取模式

```cpp
void setPickMode(PickMode mode);  // Node / Element / Part — 切换时自动清除之前的选中状态
void setInteractionMode(ViewportInteractionMode mode);  // Pick / Rotate / Pan / Zoom — 控制左键工具
```

#### 按 ID 选中

```cpp
void selectByIds(PickMode mode, const std::vector<int>& ids);
```

按指定模式和 ID 列表选中节点/单元/部件，自动切换拾取模式、高亮选中项并显示 ID 标签。供搜索框等外部调用。

#### 部件可见性（Slot）

```cpp
public slots:
    void setPartVisibility(int partIndex, bool visible);
    void setElementVisibility(int elementId, bool visible);
    void setElementsVisibility(const std::vector<int>& elementIds, bool visible);
    void setAllElementsVisible();
    void highlightParts(const std::vector<int>& partIndices);
```

部件显隐按 `partIndex` 控制整组单元；单元显隐按 FEM element id 控制局部单元，不会反向隐藏所属部件。

#### 查询方法

| 方法 | 返回类型 | 说明 |
|------|----------|------|
| `selection()` | `const FESelection&` | 当前选中状态 |
| `partColors()` | `const std::vector<glm::vec3>&` | 各部件渲染颜色 |
| `glRenderer()` | `QString` | GPU 渲染器名称 |
| `glVersion()` | `QString` | OpenGL 版本 |
| `glslVersion()` | `QString` | GLSL 版本 |
| `gpuVendor()` | `QString` | GPU 厂商 |
| `renderDiagnostics()` | `QString` | 当前视口诊断文本；Metal 路径包含 layer 状态、drawable size、DPR 和最近错误，Vulkan 路径包含设备、queue compute 能力、GPU-driven actual/fallback 状态、V1/V2 资源计数、V2 surface 启用状态、CPU surface/point 保留数量、fallback/dispatch 次数、frustum culling 状态、可见三角/点/边数量、visibility compute GPU timestamp 耗时和 CPU 侧耗时 |
| `vertexCount()` | `int` | 当前渲染顶点数 |
| `triangleCount()` | `int` | 当前渲染三角形数 |
| `currentFps()` | `float` | 当前帧率 |
| `frameTimeMs()` | `float` | 单帧渲染耗时 (ms) |

#### 信号 (Signals)

```cpp
signals:
    // OpenGL 上下文初始化完成
    void glInitialized();

    // 选中状态变化
    // mode: 拾取模式, count: 选中数量, ids: 选中的 ID 列表
    void selectionChanged(PickMode mode, int count, const std::vector<int>& ids);

    // 部件拾取（用于同步 UI 模型树的选中状态）
    void partsPicked(const std::vector<int>& partIndices);

    // 普通右键单击且未拖动时请求应用层显示视口上下文菜单
    void contextMenuRequested(const QPoint& globalPos);
```

#### 鼠标交互行为

| 操作 | 行为 |
|------|------|
| `ViewportInteractionMode::Pick` + 左键单击/拖拽 | 按当前 `PickMode` 点选/框选节点、单元或部件 |
| `ViewportInteractionMode::Rotate` + 左键拖拽 | 旋转（轨道相机） |
| `ViewportInteractionMode::Pan` + 左键拖拽 | 平移 |
| `ViewportInteractionMode::Zoom` + 左键上下拖拽 | 缩放 |
| 中键拖拽 / 右键拖拽 | 平移 |
| 右键单击且未拖动 | 发射 `contextMenuRequested`，由应用层显示上下文菜单 |
| 滚轮 | 缩放 |
| Ctrl + 左键单击 | 追加/取消选中（多选） |
| Ctrl/Shift + 左键拖拽 | 框选添加 |
| Ctrl/Shift + 右键单击/拖拽 | 点选/框选取消 |

---

### 4.3 RenderViewport — 渲染视口宿主

**头文件**：`RenderViewport.h`

`RenderViewport` 是主界面使用的渲染视口宿主层，负责按全局 RHI 设置分发到具体视口实现。OpenGL 路径承载现有 `GLWidget`，可通过 `setVertexScalars()` 渲染面云图，并通过 `setEdgeScalars()` 渲染线/梁边线云图；Metal 路径在 macOS 上承载 `QWindow + CAMetalLayer` 宿主视口，当前已可通过 `MetalRenderBackend` 创建 `MTLDevice` / command queue，上传 `Mesh.vertices / Mesh.indices` 和 `Mesh.edgeVertices / Mesh.edgeIndices`，并用运行时 MSL pipeline 结合 depth attachment 绘制渐变背景、主网格三角面、普通边线、点显示、云图标量映射、线/梁边线云图、左下角坐标轴、选中高亮线、未变形叠加线框、切片交线、半透明等值面和裁剪/切片平面预览；Metal 上传阶段会按 `setTriangleToElementMap()`、`setVertexToNodeMap()`、`setTriangleToPartMap()`、`setEdgeToPartMap()` 和 `setPartVisibility()` 过滤三角形/边线并写入基础部件颜色、per-vertex scalar 和 edge scalar，Element 离屏拾取 pass 会把可见三角形的 element id 编码到 RGBA8 pick texture，再通过 blit readback 支持 Element / Part 单点拾取、`selectionChanged` 和 `partsPicked` 信号；Node 模式会在命中单元后用 `vertexToNode` 选取屏幕最近节点，Ctrl/Shift 左键框选添加和 Ctrl/Shift 右键点选/框选取消会按当前 PickMode 更新选择状态，选中 Node 会绘制小型三轴标记，选中 Element 会绘制完整单元边，选中 Part 会绘制边界/开放/特征/视角轮廓边。Metal 视口支持左下角 X/Y/Z 坐标轴标签、选中 ID 标签、左键旋转、中键/右键平移和滚轮缩放。Vulkan 路径在 macOS 上承载 `QWindow + VkSurfaceKHR` 宿主视口，可上传 `Mesh.vertices / Mesh.indices` 和 `Mesh.edgeVertices / Mesh.edgeIndices`，通过 `fitToModel()` 同步相机适配，通过 `setObjectColor()` 同步基础对象色，通过 `setTriangleToPartMap()`、`setEdgeToPartMap()` 和 `setPartVisibility()` 支持基础部件颜色与显隐，支持基础轨道相机交互，并绘制渐变背景、主表面、普通边线、线/梁边线云图、左下角坐标轴、X/Y/Z 标签和选中 ID 标签。Vulkan 视口会在窗口 resize 后重建 swapchain，也会在 acquire/present 返回 out-of-date 或 suboptimal 时标记下一帧重建。当前 Vulkan 路径已有离屏 pick render pass，可按 `triangleToElement` 编码可见三角形颜色，并通过 staging buffer 读回点击像素；Node / Element / Part 单点点选、Ctrl/Shift 左键框选添加、Ctrl/Shift 右键点选/框选取消会更新 Vulkan 视口选择状态并转发 `selectionChanged`，Part 模式还会转发 `partsPicked`。Vulkan 会为选中单元绘制完整单元边，为选中部件绘制边界/开放/特征/视角轮廓边，为选中节点绘制小型三轴标记；Vulkan 的 `setVertexScalars()` 会把 per-vertex scalar 上传为 storage buffer 并通过 descriptor set 绑定到 mesh pipeline，由 shader 通过 `gl_VertexIndex` 和 push constant 中的 min/max/bands 做 Jet 分段映射；Metal 的 `setVertexScalars()` 会更新 shared vertex buffer 中的 scalar 字段，由 MSL shader 做同样的 Jet 分段映射；`setEdgeScalars()` 会在 Vulkan/Metal 的 line pipeline 中启用 edge scalar Jet 分段映射。mesh 已上传后再次调用 `setVertexScalars()` 只更新 scalar 数据和 contour 参数，不重传 mesh geometry；`setEdgeScalars()` 会重传普通边线顶点以更新 line vertex scalar。主网格、普通边线、等值面和裁剪/切片平面预览三角面几何已通过 staging buffer 上传到 device-local vertex/index buffer，staging copy 使用单次 fence 等待。`setColorBar*()` 接口会通过宿主层 Qt overlay 在 Vulkan 和 Metal 视口上显示色标；`setOverlayMesh()` / `setOverlayVisible()` 已可在 Vulkan 和 Metal 路径绘制变形显示使用的未变形线框，`setSliceLines()` / `clearSliceLines()` 已可在 Vulkan 和 Metal 路径绘制和清除基础切片交线，`setIsoSurfaceMesh()` / `clearIsoSurface()` 已可在 Vulkan 和 Metal 路径绘制和清除半透明等值面叠加；`setClipPlanePreview()` / `clearClipPlanePreview()` 已可在 Vulkan 和 Metal 路径绘制和清除裁剪/切片平面预览。

常用接口与 `GLWidget` 保持一致，包括：

```cpp
void setMesh(const Mesh& mesh);
void setPickMode(PickMode mode);
void setInteractionMode(ViewportInteractionMode mode);
void setModelDisplayMode(ModelDisplayMode mode);
void setViewportGridVisible(bool visible);
void setPartVisibility(int partIndex, bool visible);
void setElementVisibility(int elementId, bool visible);
void setElementsVisibility(const std::vector<int>& elementIds, bool visible);
void setAllElementsVisible();
void setStandardView(StandardView view);
void setPreferredRenderBackend(RenderBackendKind kind);
RenderBackendKind requestedRenderBackendKind() const;
RenderBackendKind activeRenderBackendKind() const;
QString renderDiagnostics() const;
void setVertexScalars(const std::vector<float>& scalars, float minVal, float maxVal, int numBands);
void setEdgeScalars(const std::vector<float>& scalars, float minVal, float maxVal, int numBands);
```

Vulkan 路径当前实现 `PickMode::Node` / `PickMode::Element` / `PickMode::Part` 的单点拾取、框选添加和点选/框选取消。Metal 路径当前提供基础主网格三角面、普通边线、点显示绘制、云图标量映射、部件颜色/显隐、Node / Element / Part 点选/框选、选中高亮、未变形叠加线框、切片交线、等值面、裁剪/切片平面预览和轨道相机交互。`RenderViewport` 启动时读取全局 RHI 配置并激活对应视口；运行时调用 `setPreferredRenderBackend()` 只写入下次启动的首选 RHI，不再立即销毁/重建当前视口。
运行时 `VulkanContext` 会优先请求 SDK 和 loader 共同支持的 Vulkan 1.4 API；shader 编译默认使用
`FERENDER_VULKAN_SHADER_TARGET_ENV=vulkan1.4`，可在老工具链上通过 CMake cache 改为较低 target env。
Vulkan 内部资源模型已开始把 device-local/staging buffer、动态/readback buffer、scalar descriptor/set layout、主视口/拾取 render pass、framebuffer、graphics pipeline / pipeline layout 和 command pool / command buffer 从裸 handle 收敛到独立资源对象；主网格帧录制已收敛到 `VulkanMeshFramePass`，拾取绘制和 readback barrier/copy 录制已收敛到 `VulkanPickPass`。Vulkan GPU-driven 基础路径已新增内部 `VulkanGpuDrivenMeshResources`、`VulkanGpuDrivenUploadBuilder`、`VulkanGpuDrivenMeshUploader`、`VulkanVisibilityComputePass` 和 `VulkanGpuDrivenRuntimeStats`，默认 Vulkan 绘制策略会由 compute pass 生成可见三角 index buffer、可见 edge index buffer 与 indirect command，并通过 `vkCmdDrawIndexedIndirect` 绘制主三角面、点模式和具备 edge metadata 的 Wireframe/SolidWireframe 边线；pick pass 也可复用同一份 GPU-driven visible index buffer 和 indirect command 绘制到离屏 pick framebuffer；GPU-driven surface 已支持 V2 source-vertex sidecar、V2 visibility compute variant、无 vertex input 的 V2 mesh/point/pick pipeline，启用时 visible index buffer 输出 draw-corner id，vertex shader 再从 triangle metadata 反查 source vertex，从而避免 surface 顶点按三角形展开；V2 点模式会在 Points 模式下由 compute pass 通过 source-vertex flag 去重，输出独立 visible point index buffer 和 point indirect command，用 unique visible source vertex 绘制点，非 Points 模式不生成 visible point list；V2 默认上传路径会跳过 V1 展开 surface 资源、传统 mesh surface 资源和 CPU point buffer，并支持通过 V2 source vertex cache 更新云图 scalar；若 V2 sidecar、descriptor 或 pipeline 不完整会保留 V1 GPU-driven 路径。part state、hidden element 和 visibility uniform 已支持内部小 buffer 更新，每帧会从 MVP 写入 frustum planes 供 compute pass 做 AABB 剔除，不需要重传完整 vertex/triangle/edge metadata；compute 后会把 indirect command 复制到 host-visible readback buffer 以记录可见三角/点/边数量，并在设备支持时用 timestamp query 记录 visibility compute GPU 耗时；`VulkanViewport` 在 `Solid` / `Points` 以及具备 surface+edge 数据的 `Wireframe` / `SolidWireframe` 显示模式下可走该热路径，线框-only、缺少 surface metadata、graphics queue 不支持 compute 或 shader/pipeline/descriptor 创建失败时会回退传统过滤资源；诊断字符串会显示 requested/effective/actual 策略、GPU-driven 计数、sourceV2/v2 状态、CPU surface/point 保留数量、fallback 原因、fallback 次数、visibility dispatch 次数、frustum culling 状态、可见三角/点/边数量、visibility compute GPU timestamp 耗时和 CPU 侧 upload/update/render/pick 耗时；该路径仍不作为公开 API 暴露，用户界面默认选择 GPU-driven，并保留 Traditional 作为手动兼容回退。Metal 内部资源模型已开始用 `MetalBufferResource` 管理主网格、普通边线、点显示、overlay、slice、selection、坐标轴、等值面、裁剪预览和 pick readback buffer 的 `MTLBuffer` 生命周期，用 `MetalTextureResource` 管理主 depth 与 pick color/depth texture 生命周期，用 `MetalStateResource` 管理 pipeline 和 depth-stencil state 生命周期，用 `MetalDeviceFactory` 管理系统默认 device、command queue 和 backend info 创建，用 `MacOSMetalLayerHost` 管理 `QWindow` 到 `NSView/CAMetalLayer` 的宿主配置，`MetalRenderBackend` 只接收已准备好的 layer 和 drawable size，用 `MetalObjectResource` 管理 device、command queue 和 layer 生命周期，用 `MetalShaderSources` 管理运行时 MSL 源码，用 `MetalShaderTypes` 管理 C++/MSL 共享布局，用 `MetalPipelineFactory` 管理 shader 编译和 render pipeline 创建与资源确保，用 `MetalRenderPassFactory` 管理 render pass descriptor 创建，用 `MetalUniformUtils` 管理 uniform 构建，用 `MetalPickUtils` 管理 pick 颜色编解码和 readback 读取，用 `MetalMeshUploadBuilder` 管理 mesh 上传数据构建，用 `MetalMeshUploader` 管理主 mesh/point/edge buffer 上传和计数同步，用 `MetalMeshScalarUpdater` 管理 mesh scalar 局部更新，用 `MetalSurfaceUploadBuilder` 管理等值面/裁剪预览上传数据构建，用 `MetalSurfaceUploader` 管理等值面/裁剪预览 buffer 上传和计数同步，用 `MetalLineUpload` 管理动态线段上传，用 `MetalClearFramePass` 管理 clear/present 提交，用 `MetalDrawableFrameSubmitter` 管理主 drawable frame 提交，用 `MetalMeshFramePassBuilder` 管理主视口 draw pass 输入组装，用 `MetalMeshFramePass` 管理主视口 draw 录制，用 `MetalPickPassBuilder` 管理离屏拾取 pass 输入组装，用 `MetalPickPass` 管理离屏拾取 draw/readback 录制，并用 `MetalDepthStencilFactory` 管理 depth-stencil state 创建。公开 `RenderViewport` API 不暴露这些实现细节。

信号：

```cpp
void renderInitialized();
void selectionChanged(PickMode mode, int count, const std::vector<int>& ids);
void partsPicked(const std::vector<int>& partIndices);
void contextMenuRequested(const QPoint& globalPos);
```

---

### 4.4 RHI 公共类型与设置

**头文件**：`RenderBackend.h`、`RenderBackendFactory.h`、`RenderSettings.h`

使用应用目录下的 `config/settings.ini` 持久化用户首选 RHI 与 Vulkan 绘制策略；启动时读取，运行时写入后下次启动生效。测试或特殊部署可通过环境变量 `FEMODELVIEWER_CONFIG_DIR` 指定配置目录。`RenderBackendKind` 当前包含 `OpenGL`、`Vulkan`、`Metal`；`VulkanDrawStrategy` 当前公开 `Traditional`、`GpuDrivenIndirect`、`MeshShader`，其中 `GpuDrivenIndirect` 为默认 Vulkan 绘制策略并支持运行时 fallback，`Traditional` 保留为兼容回退路径，`MeshShader` 仍作为 UI/配置预留。旧配置中没有 `vulkanDrawStrategyVersion` 的 `traditional` 会迁移为 `gpu_driven_indirect`；迁移后用户仍可手动选择 Traditional。Metal 后端在 macOS 构建中会探测默认 `MTLDevice`，创建 command queue，并通过 `CAMetalLayer` 执行基础 clear/present、渐变背景、深度测试、主网格三角面、普通边线、点绘制、云图标量映射、部件颜色/显隐、Node/Element/Part 点选/框选、选中高亮、叠加线框、切片交线、等值面、裁剪预览、左下角坐标轴和轨道相机交互。

`RenderBackend.h` 公开 `RenderBackendInfo`、`RenderBackendKind`、`ModelDisplayMode`、`StandardView`、`IRenderBackend` 和 scene pass 描述结构。`IRenderBackend` 是后端最小生命周期边界，目前暴露 `initialize()` 和 `info()`；OpenGL 完整模型绘制由 `OpenGLRenderBackend` 实现，Vulkan/Metal 可沿同一边界接入。`ModelDisplayMode` 取值为 `Solid`、`Wireframe`、`SolidWireframe`、`Points`，由 `GLWidget` 和 `RenderViewport` 统一接收并转发到当前 OpenGL、Vulkan 或 Metal 视口。`StandardView` 取值为 `Front`、`Back`、`Left`、`Right`、`Top`、`Bottom`，用于标准方向视图切换。

```cpp
std::unique_ptr<IRenderBackend> createRenderBackend(
    RenderBackendKind kind = RenderBackendKind::OpenGL);
bool isRenderBackendAvailable(RenderBackendKind kind);
const char* renderBackendName(RenderBackendKind kind);
```

| 函数 | 说明 |
|------|------|
| `createRenderBackend(kind)` | 创建指定 RHI 后端实例；macOS Metal 构建会返回 Metal 后端骨架 |
| `isRenderBackendAvailable(kind)` | 查询后端是否已编译并可用；Metal 会尝试创建系统默认 `MTLDevice` |
| `renderBackendName(kind)` | 返回用于界面和日志的稳定后端名称 |

```cpp
class RenderSettings {
public:
    static RenderBackendKind preferredBackend();
    static void setPreferredBackend(RenderBackendKind kind);
    static RenderBackendKind effectiveBackend();
    static QString backendKey(RenderBackendKind kind);
    static RenderBackendKind backendFromKey(const QString& key,
                                            RenderBackendKind fallback = RenderBackendKind::OpenGL);

    static VulkanDrawStrategy preferredVulkanDrawStrategy();
    static void setPreferredVulkanDrawStrategy(VulkanDrawStrategy strategy);
    static VulkanDrawStrategy effectiveVulkanDrawStrategy();
    static bool isVulkanDrawStrategyAvailable(VulkanDrawStrategy strategy);
    static QString vulkanDrawStrategyKey(VulkanDrawStrategy strategy);
    static VulkanDrawStrategy vulkanDrawStrategyFromKey(
        const QString& key,
        VulkanDrawStrategy fallback = VulkanDrawStrategy::Traditional);
    static QString vulkanDrawStrategyName(VulkanDrawStrategy strategy);
};
```

| Vulkan 绘制策略 | 配置值 | 当前状态 |
|-----------------|--------|----------|
| `Traditional` | `traditional` | 可用，兼容 Vulkan 路径，可手动选择用于回退验证 |
| `GpuDrivenIndirect` | `gpu_driven_indirect` | 默认 Vulkan 路径；内部基础路径已覆盖三角面、点、边线和拾取，运行时能力不足会回退 |
| `MeshShader` | `mesh_shader` | UI 中显示为实验预留，暂不可启用 |

| 方法 | 说明 |
|------|------|
| `preferredBackend()` | 从 `config/settings.ini` 读取用户首选 RHI，默认 OpenGL |
| `setPreferredBackend(kind)` | 写入用户首选 RHI，下次启动生效 |
| `effectiveBackend()` | 首选 RHI 已编译可用时返回首选，否则回退 OpenGL |
| `backendKey(kind)` | 转换为稳定配置字符串：`"opengl"` / `"vulkan"` / `"metal"` |
| `backendFromKey(key, fallback)` | 从配置字符串解析 RHI，支持 `gl` / `vk` / `mtl` 简写 |
| `preferredVulkanDrawStrategy()` | 从 `config/settings.ini` 读取 Vulkan 绘制策略；未配置或旧版默认 `traditional` 时迁移为 `GpuDrivenIndirect` |
| `effectiveVulkanDrawStrategy()` | 配置层返回当前可选择的 Vulkan 绘制策略；运行时资源或能力不足仍会回退传统路径 |

---

## 5. 交互层 API

### 5.1 PickMode — 拾取模式

**头文件**：`FEPickResult.h`

```cpp
enum class PickMode {
    Node,      // 选中最近的节点
    Element,   // 选中点击处的单元
    Part       // 选中整个部件
};
```

### 5.2 ViewportInteractionMode — 视口交互工具

**头文件**：`FEPickResult.h`

```cpp
enum class ViewportInteractionMode {
    Pick,    // 左键按当前 PickMode 点选/框选
    Rotate,  // 左键拖拽旋转视图
    Pan,     // 左键拖拽平移视图
    Zoom     // 左键上下拖拽缩放视图
};
```

`PickMode` 决定“拾取什么对象”，`ViewportInteractionMode` 决定“鼠标左键当前做什么”。例如先调用 `setPickMode(PickMode::Element)`，再调用 `setInteractionMode(ViewportInteractionMode::Pick)`，左键就会按单元模式拾取。

### 5.3 FEPickResult — 拾取结果

**头文件**：`FEPickResult.h`

描述单次鼠标拾取的结果。

```cpp
struct FEPickResult {
    bool      hit;             // 是否命中有效实体
    int       nodeId;          // 命中的节点 ID（Node 模式，默认 -1）
    int       elementId;       // 命中的单元 ID（Element/Part 模式，默认 -1）
    int       faceIndex;       // 命中的面索引（默认 -1）
    glm::vec3 worldPos;        // 命中点世界坐标
    float     depth;           // 命中点深度值
    int       triangleIndex;   // 命中的渲染三角形索引
};
```

### 5.4 FESelection — 选中状态

**头文件**：`FEPickResult.h`

维护当前被选中的节点和单元集合。

```cpp
struct FESelection {
    std::unordered_set<int> selectedNodes;      // 选中的节点 ID
    std::unordered_set<int> selectedElements;   // 选中的单元 ID

    void clear();                               // 清空所有选中
    bool isNodeSelected(int nodeId) const;      // 节点是否被选中
    bool isElementSelected(int elemId) const;   // 单元是否被选中
    void toggleNode(int nodeId);                // 切换节点选中状态
    void toggleElement(int elemId);             // 切换单元选中状态
    int  selectedNodeCount() const;             // 选中节点数
    int  selectedElementCount() const;          // 选中单元数
    bool hasSelection() const;                  // 是否有任何选中
};
```

---

## 6. 变形与动画 API

### 6.1 FEDeformation — 变形显示工具类

**头文件**：`FEDeformation.h`

无状态静态工具类，将位移矢量场叠加到 FEModel 坐标，生成变形后的模型副本。

#### FEDeformationOptions（变形选项）

```cpp
struct FEDeformationOptions {
    float scale = 1.0f;           // 变形缩放比例
    bool overlayUndeformed = false; // 是否叠加显示原始模型
};
```

#### FEDeformation

```cpp
class FEDeformation {
public:
    // 生成变形后的 FEModel（新坐标 = 原始坐标 + displacement × scale）
    // 缺失位移的节点保持原坐标不变，原始模型不被修改
    static FEModel apply(const FEModel& model,
                         const FEVectorField& displacement,
                         const FEDeformationOptions& options);

    // 计算自动缩放比例，使最大变形约为模型尺寸的 targetRatio 倍
    // 若位移为零返回 1.0
    static float autoScale(const FEModel& model,
                           const FEVectorField& displacement,
                           float targetRatio = 0.1f);
};
```

#### 使用示例

```cpp
FEVectorField disp;
disp.values[1] = {0.001f, 0.0f, 0.0f};
disp.values[2] = {0.002f, 0.0f, -0.001f};

// 自动计算比例（微小变形放大到可视范围）
float scale = FEDeformation::autoScale(model, disp);

// 生成变形模型
FEDeformationOptions opts;
opts.scale = scale;
opts.overlayUndeformed = true;
FEModel deformed = FEDeformation::apply(model, disp, opts);

// 转换渲染并显示
FERenderData rd = FEMeshConverter::toRenderData(deformed);
viewer.setMesh(rd.mesh);

// 叠加原始模型线框
if (opts.overlayUndeformed) {
    FERenderData origRd = FEMeshConverter::toRenderData(model);
    viewer.setOverlayMesh(origRd.mesh);
    viewer.setOverlayVisible(true);
}
```

---

### 6.2 FEAnimationController — 帧动画控制器

**头文件**：`FEAnimationController.h`

基于 QTimer 驱动帧索引循环播放，不直接操作渲染或结果数据。
外部通过 `frameChanged` 信号获取当前帧索引，自行更新模型和渲染。

```cpp
class FEAnimationController : public QObject {
    Q_OBJECT
public:
    explicit FEAnimationController(QObject* parent = nullptr);

    void setFrameCount(int count);        // 设置总帧数
    int frameCount() const;

    void setFps(double fps);              // 设置播放帧率
    double fps() const;

    int currentFrame() const;             // 当前帧索引
    bool isPlaying() const;               // 是否正在播放

public slots:
    void play();                          // 开始播放
    void pause();                         // 暂停
    void stop();                          // 停止并回到第 0 帧
    void setCurrentFrame(int frame);      // 跳转到指定帧

signals:
    void frameChanged(int frameIndex);    // 帧索引变化
    void playStateChanged(bool playing);  // 播放状态变化
};
```

#### 使用示例

```cpp
FEAnimationController anim;
anim.setFrameCount(repo.frameCount());
anim.setFps(12.0);

// 监听帧变化，更新渲染
connect(&anim, &FEAnimationController::frameChanged, [&](int frame) {
    const FEResultFrame* f = repo.frame(frame);
    // 根据帧数据更新变形/云图...
});

anim.play();    // 开始循环播放
anim.pause();   // 暂停
anim.stop();    // 回到第 0 帧
```

---

## 7. 探针与导出 API

### 7.1 FEProbe — 结果探针

**头文件**：`FEProbe.h`

无状态静态工具类，提供结果场的点值查询、热点排序、路径采样和 CSV 导出。

#### FEProbeValue（探针值）

```cpp
struct FEProbeValue {
    bool valid = false;                        // 是否有效
    int entityId = -1;                         // 节点/单元 ID
    FieldLocation location = FieldLocation::Node;  // 数据位置
    float value = 0.0f;                        // 标量值
};
```

#### FEPathSample（路径采样点）

```cpp
struct FEPathSample {
    float distance = 0.0f;    // 沿路径的累计弧长
    glm::vec3 position{0.0f}; // 三维坐标
    FEProbeValue value;        // 该点的探针值
};
```

#### FEProbe

```cpp
class FEProbe {
public:
    // 查询指定 ID 处的标量值（未找到时 valid=false）
    static FEProbeValue valueAtEntity(const FEScalarField& field, int entityId);

    // 返回最大（descending=true）或最小的 N 个热点
    static std::vector<FEProbeValue> topHotspots(const FEScalarField& field,
                                                  int count,
                                                  bool descending = true);

    // 沿节点路径采样标量值和累计弧长
    // 不存在的节点 ID 被跳过，距离单调递增
    static std::vector<FEPathSample> sampleNodePath(const FEModel& model,
                                                     const FEScalarField& field,
                                                     const std::vector<int>& nodeIds);

    // 路径采样数据导出 CSV（Distance,X,Y,Z,NodeID,Value,Valid）
    static bool writePathSamplesCsv(const std::string& filePath,
                                     const std::vector<FEPathSample>& samples);

    // 标量场导出 CSV（NodeID/ElementID,Value），按 ID 升序
    static bool writeScalarFieldCsv(const std::string& filePath,
                                     const FEScalarField& field);
};
```

#### 使用示例

```cpp
#include "FEProbe.h"

// 1. 点值查询
auto pv = FEProbe::valueAtEntity(stressField, 42);
if (pv.valid)
    qDebug() << "Node 42 stress:" << pv.value;

// 2. 热点排序（前 5 大）
auto hotspots = FEProbe::topHotspots(stressField, 5, true);
for (const auto& h : hotspots)
    qDebug() << "ID:" << h.entityId << "Value:" << h.value;

// 3. 路径采样
std::vector<int> pathNodes = {1, 5, 10, 15, 20};
auto samples = FEProbe::sampleNodePath(model, stressField, pathNodes);
for (const auto& s : samples)
    qDebug() << "dist:" << s.distance << "val:" << s.value.value;

// 4. 导出 CSV
FEProbe::writePathSamplesCsv("path_data.csv", samples);
FEProbe::writeScalarFieldCsv("stress_field.csv", stressField);
```

---

## 8. 过滤与等值面 API

### 8.1 FEPostFilter — 后处理过滤器

**头文件**：`FEPostFilter.h`

提供 CPU 端的网格过滤操作：阈值、裁剪平面、切片平面。

#### FEPlane（平面定义）

```cpp
struct FEPlane {
    glm::vec3 origin{0.0f};           // 平面上一点
    glm::vec3 normal{0.0f, 0.0f, 1.0f}; // 平面法线
};
```

#### FESliceResult（切片结果）

```cpp
struct FESliceResult {
    std::vector<float> lineVertices;  // 交线顶点 [x,y,z, ...]
    int lineCount = 0;                // 线段数量
};
```

#### FEPostFilter

```cpp
class FEPostFilter {
public:
    // 按单元标量值阈值过滤（保留完整三角形）
    static FERenderData thresholdByElementValue(const FERenderData& input,
                                                const FEScalarField& field,
                                                float minValue, float maxValue);

    // 按裁剪平面过滤（三角形半空间几何裁剪）
    static FERenderData clipByPlane(const FERenderData& input,
                                    const FEPlane& plane,
                                    bool keepPositiveSide);

    // 生成切片平面与网格的交线
    static FESliceResult sliceByPlane(const FERenderData& input,
                                      const FEPlane& plane);
};
```

`sliceByPlane()` 会对交点去重：平面只接触三角形单个顶点时不生成零长度线段；三角形整体落在切片平面上时返回该三角形轮廓线。

`clipByPlane()` 会对跨越平面的三角形做几何裁切，而不是按质心整片保留，因此裁剪边界不会留下跨过平面的尖三角。

**使用示例**：

```cpp
#include "FEPostFilter.h"

// 阈值过滤：保留应力 > 100 的单元
auto filtered = FEPostFilter::thresholdByElementValue(renderData, stressField, 100.0f, 1e10f);
glWidget->setMesh(filtered.mesh);
glWidget->setTriangleToElementMap(filtered.triangleToElement);

// 裁剪平面：保留 Y > 0 的部分
FEPlane plane;
plane.origin = glm::vec3(0.0f);
plane.normal = glm::vec3(0.0f, 1.0f, 0.0f);
auto clipped = FEPostFilter::clipByPlane(renderData, plane, true);

// 切片：获取 Z=5.0 处的交线
FEPlane slicePlane;
slicePlane.origin = glm::vec3(0.0f, 0.0f, 5.0f);
slicePlane.normal = glm::vec3(0.0f, 0.0f, 1.0f);
auto slice = FEPostFilter::sliceByPlane(renderData, slicePlane);
glWidget->setSliceLines(slice.lineVertices);
```

### 8.2 FEIsoSurface — 等值面提取

**头文件**：`FEIsoSurface.h`

从体网格中提取标量等值面（Marching Tetrahedra），仅支持 TET4 和 HEX8。

```cpp
class FEIsoSurface {
public:
    static Mesh extract(const FEModel& model,
                        const FEScalarField& field,   // 必须是节点场
                        float isoValue);
};
```

**使用示例**：

```cpp
#include "FEIsoSurface.h"

Mesh isoMesh = FEIsoSurface::extract(model, temperatureField, 50.0f);
glWidget->setIsoSurfaceMesh(isoMesh);

// 清除等值面
glWidget->clearIsoSurface();
```

---

## 9. 完整使用示例

### 9.1 加载模型并渲染

```cpp
#include <QApplication>
#include <QSurfaceFormat>
#include "FEModel.h"
#include "FEMeshConverter.h"
#include "GLWidget.h"

int main(int argc, char* argv[]) {
    QSurfaceFormat fmt;
    fmt.setVersion(4, 1);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(32);
    fmt.setSamples(8);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);

    // ── 1. 构建模型（实际项目中从文件解析） ──
    FEModel model;
    model.name = "SimpleBox";

    // 8 个节点构成一个六面体
    model.addNode(1, {0,0,0}); model.addNode(2, {1,0,0});
    model.addNode(3, {1,1,0}); model.addNode(4, {0,1,0});
    model.addNode(5, {0,0,1}); model.addNode(6, {1,0,1});
    model.addNode(7, {1,1,1}); model.addNode(8, {0,1,1});

    model.addElement(1, ElementType::HEX8, {1,2,3,4,5,6,7,8});

    // ── 2. 转换为渲染数据 ──
    FERenderData rd = FEMeshConverter::toRenderData(model);

    // ── 3. 创建 GLWidget 并传入数据 ──
    GLWidget viewer;
    viewer.setMesh(rd.mesh);
    viewer.setTriangleToElementMap(rd.triangleToElement);
    viewer.setVertexToNodeMap(rd.vertexToNode);
    viewer.setTriangleToPartMap(rd.triangleToPart);
    viewer.setEdgeToPartMap(rd.edgeToPart);
    viewer.fitToModel(model.computeCenter(), model.computeSize());

    viewer.resize(1024, 768);
    viewer.setWindowTitle("FERender Example");
    viewer.show();

    return app.exec();
}
```

### 9.2 显示标量云图

```cpp
// 假设已有 model 和 viewer

// 创建 Von Mises 应力场
FEScalarField stress;
stress.name = "Von Mises";
stress.unit = "MPa";
stress.location = FieldLocation::Node;
for (auto& [id, node] : model.nodes) {
    stress.values[id] = /* 从求解器获取 */ ;
}

// 方式 A：CPU 颜色映射（通过 FEMeshConverter）
float sMin, sMax;
stress.computeRange(sMin, sMax);
ColorMap cmap;
cmap.type = ColorMapType::Jet;
cmap.discreteLevels = 12;
FERenderData coloredRd = FEMeshConverter::toColoredRenderData(model, stress, cmap, sMin, sMax);
viewer.setMesh(coloredRd.mesh);
viewer.setUseVertexColor(true);

// 方式 B：GPU 着色器量化（推荐，性能更好）
FERenderData rd = FEMeshConverter::toRenderData(model);
viewer.setMesh(rd.mesh);
viewer.setVertexToNodeMap(rd.vertexToNode);

// 使用 FERender 引擎 API 构建 per-vertex 标量数组
FEMappedScalars mapped = FEResultMapper::mapScalarToVertices(stress, rd, model);
viewer.setVertexScalars(mapped.scalars, mapped.minValue, mapped.maxValue, 12);

// 显示色标
viewer.setColorBarVisible(true);
viewer.setColorBarRange(mapped.minValue, mapped.maxValue);
viewer.setColorBarTitle("Von Mises Stress [MPa]");
viewer.setColorBarIdLabel("Node ID");
viewer.setColorBarExtremes(mapped.minId, mapped.minValue, mapped.maxId, mapped.maxValue);
```

当前 `FEResultMapper::mapScalarToVertices()` 会把 1D 线/梁单元纳入结果映射：面网格标量写入 `mapped.scalars`，普通边线标量写入 `mapped.edgeScalars`，纯线模型没有三角面顶点时 `mapped.scalars` 可以为空但 `edgeScalars`、`minValue/maxValue/minId/maxId` 仍可用于色标和查询。OpenGL、Vulkan 和 Metal 路径均通过 `setEdgeScalars()` 让线/梁按结果值着色。

### 9.3 部件可见性控制

```cpp
// 假设模型有 3 个部件
FEPart p1, p2, p3;
p1.name = "Housing"; p1.elementIds = {1,2,3};
p2.name = "Shaft";   p2.elementIds = {4,5};
p3.name = "Bearing"; p3.elementIds = {6,7,8,9};
model.parts = {p1, p2, p3};

FERenderData rd = FEMeshConverter::toRenderData(model);
viewer.setMesh(rd.mesh);
viewer.setTriangleToPartMap(rd.triangleToPart);
viewer.setEdgeToPartMap(rd.edgeToPart);

// 隐藏 Housing 部件
viewer.setPartVisibility(0, false);

// 高亮 Shaft 和 Bearing
viewer.highlightParts({1, 2});

// 读取部件颜色（用于 UI 显示色块）
const auto& colors = viewer.partColors();
// colors[0] → Housing 的 RGB
// colors[1] → Shaft 的 RGB
// colors[2] → Bearing 的 RGB
```

### 9.4 拾取交互

```cpp
// 设置拾取模式
viewer.setPickMode(PickMode::Element);

// 连接选中信号
QObject::connect(&viewer, &GLWidget::selectionChanged,
    [](PickMode mode, int count, const std::vector<int>& ids) {
        if (mode == PickMode::Element) {
            qDebug() << "Selected" << count << "elements:";
            for (int id : ids) qDebug() << "  Element" << id;
        }
    });

// 按 ID 搜索并高亮（支持节点/单元/部件）
viewer.selectByIds(PickMode::Node, {1, 3, 5, 10});

// 连接部件拾取信号（Part 模式下）
QObject::connect(&viewer, &GLWidget::partsPicked,
    [](const std::vector<int>& partIndices) {
        for (int idx : partIndices)
            qDebug() << "Part" << idx << "picked";
    });

// 程序化查询选中状态
const FESelection& sel = viewer.selection();
if (sel.hasSelection()) {
    qDebug() << "Nodes:" << sel.selectedNodeCount()
             << "Elements:" << sel.selectedElementCount();

    // 检查特定节点是否被选中
    if (sel.isNodeSelected(42)) {
        qDebug() << "Node 42 is selected";
    }
}
```

### 9.5 变形显示

```cpp
// 假设有位移矢量场
FEVectorField disp;
disp.name = "Displacement";
disp.unit = "mm";
disp.values[1] = {0.0f,  0.0f,  0.0f};
disp.values[2] = {0.5f,  0.0f, -0.1f};
disp.values[3] = {0.8f,  0.0f, -0.3f};
// ...

// 生成变形网格（放大 20 倍以便观察微小变形）
Mesh deformed = FEMeshConverter::toDeformedMesh(model, disp, 20.0f);

// 渲染变形后的网格
viewer.setMesh(deformed);
viewer.fitToModel(model.computeCenter(), model.computeSize());
```

### 9.6 变形动画

```cpp
#include "FEDeformation.h"
#include "FEAnimationController.h"
#include "FEResultRepository.h"

// 假设已有 model, viewer, repo（多帧结果仓库）

FEAnimationController anim;
anim.setFrameCount(repo.frameCount());
anim.setFps(12.0);

// 帧变化时更新变形显示
QObject::connect(&anim, &FEAnimationController::frameChanged, [&](int frame) {
    const FEResultFrame* f = repo.frame(frame);
    if (!f) return;

    // 查找位移结果类型
    for (const auto& rt : f->resultTypes) {
        if (rt.hasVector && rt.name == "Displacement") {
            float scale = FEDeformation::autoScale(model, rt.vectorField);
            FEDeformationOptions opts;
            opts.scale = scale;
            FEModel deformed = FEDeformation::apply(model, rt.vectorField, opts);

            FERenderData rd = FEMeshConverter::toRenderData(deformed);
            viewer.setMesh(rd.mesh);
            break;
        }
    }
});

anim.play();
```

---

### 9.7 探针查值与热点导出

```cpp
#include "FEProbe.h"
#include "FEResultMapper.h"

// 假设已有 model, stressField（标量场）

// 拾取回调中查询结果值
connect(&viewer, &GLWidget::selectionChanged,
    [&](PickMode mode, int count, const std::vector<int>& ids) {
        if (count == 1 && mode == PickMode::Node) {
            auto pv = FEProbe::valueAtEntity(stressField, ids[0]);
            if (pv.valid)
                qDebug() << "Node" << ids[0] << "=" << pv.value << "MPa";
        }
    });

// 列出前 10 个热点
auto hotspots = FEProbe::topHotspots(stressField, 10);
for (const auto& h : hotspots)
    qDebug() << "Hotspot:" << h.entityId << h.value;

// 沿一组节点采样
auto samples = FEProbe::sampleNodePath(model, stressField, {1, 5, 10, 20});

// 导出 CSV
FEProbe::writePathSamplesCsv("path.csv", samples);
FEProbe::writeScalarFieldCsv("field.csv", stressField);
```

### 9.8 阈值过滤与裁剪

```cpp
#include "FEPostFilter.h"
#include "FEIsoSurface.h"

// 阈值过滤：仅显示应力在 [100, 500] 范围内的单元
auto filtered = FEPostFilter::thresholdByElementValue(renderData, stressField, 100.0f, 500.0f);
glWidget->setMesh(filtered.mesh);
glWidget->setTriangleToElementMap(filtered.triangleToElement);
glWidget->setVertexToNodeMap(filtered.vertexToNode);

// 裁剪平面：切掉 X < 0 的部分
FEPlane plane;
plane.origin = glm::vec3(0.0f);
plane.normal = glm::vec3(1.0f, 0.0f, 0.0f);
auto clipped = FEPostFilter::clipByPlane(renderData, plane, true);

// 切片：在 Z=10 处显示截面交线
FEPlane slicePlane;
slicePlane.origin = glm::vec3(0.0f, 0.0f, 10.0f);
slicePlane.normal = glm::vec3(0.0f, 0.0f, 1.0f);
auto slice = FEPostFilter::sliceByPlane(renderData, slicePlane);
glWidget->setSliceLines(slice.lineVertices);

// 等值面：提取温度 = 50 度的等值面
Mesh iso = FEIsoSurface::extract(model, tempField, 50.0f);
glWidget->setIsoSurfaceMesh(iso);
```

---

## 附录 A：头文件清单

源码已按模块放入 `source/`；安装后的公开头文件仍平铺到 `include/FERender`，因此外部项目继续使用 `#include "FEModel.h"`、`#include "GLWidget.h"` 等写法。

| 头文件 | 源码位置 | 主要类型 | 说明 |
|--------|----------|----------|------|
| `FENode.h` | `source/data/FENode.h` | `FENode` | 节点数据结构 |
| `FEElement.h` | `source/data/FEElement.h` | `FEElement`, `ElementType` | 单元数据结构与类型枚举 |
| `FEGroup.h` | `source/data/FEGroup.h` | `FEPart`, `FENodeSet`, `FEElementSet` | 分组结构 |
| `FEModel.h` | `source/data/FEModel.h` | `FEModel` | 模型顶层容器 |
| `FEField.h` | `source/data/FEField.h` | `FEScalarField`, `FEVectorField`, `ColorMap`, `ColorMapType` | 结果场与色谱 |
| `FEResultData.h` | `source/data/FEResultData.h` | `FEResultData`, `FESubcase`, `FEResultType`, `FEResultComponent` | 多工况结果层级 |
| `FEResultMapper.h` | `source/post/FEResultMapper.h` | `FEResultMapper`, `FEMappedScalars` | 结果场到渲染顶点标量数组的映射 |
| `FEResultRepository.h` | `source/post/FEResultRepository.h` | `FEResultRepository`, `FEResultFrame`, `FEResultFrameInfo`, `FEResultDomain` | 结果仓库与帧模型 |
| `FEDeformation.h` | `source/post/FEDeformation.h` | `FEDeformation`, `FEDeformationOptions` | 变形显示工具类 |
| `FEAnimationController.h` | `source/post/FEAnimationController.h` | `FEAnimationController` | 帧动画控制器（QTimer 驱动） |
| `FEProbe.h` | `source/post/FEProbe.h` | `FEProbe`, `FEProbeValue`, `FEPathSample` | 结果探针：点值查询、热点、路径采样、CSV 导出 |
| `FEPostFilter.h` | `source/post/FEPostFilter.h` | `FEPostFilter`, `FEPlane`, `FESliceResult` | 后处理过滤器：阈值、裁剪、切片 |
| `FEIsoSurface.h` | `source/post/FEIsoSurface.h` | `FEIsoSurface` | 等值面提取（Marching Tetrahedra） |
| `FEParser.h` | `source/io/FEParser.h` | `FEParser` | 有限元和几何文件解析器（INP/BDF/OP2/STL/STEP/IGES/UNV） |
| `Geometry.h` | `source/render/Geometry.h` | `Mesh`, `Geometry::*` | 网格结构与几何体生成 |
| `FERenderData.h` | `source/render/FERenderData.h` | `FERenderData` | 渲染数据包（Mesh + 映射表） |
| `FEMeshConverter.h` | `source/convert/FEMeshConverter.h` | `FEMeshConverter` | 网格转换器 |
| `Camera.h` | `source/render/Camera.h` | `Camera` | 轨道相机 |
| `Theme.h` | `source/common/Theme.h` | `Theme` | 主题颜色配置（供 `GLWidget::applyTheme()` 使用） |
| `RenderBackend.h` | `source/rhi/RenderBackend.h` | `RenderBackendKind`, `ModelDisplayMode`, `IRenderBackend`, `Scene*` | RHI 类型和通用渲染描述 |
| `RenderBackendFactory.h` | `source/rhi/RenderBackendFactory.h` | `createRenderBackend`, `isRenderBackendAvailable` | 渲染后端创建与可用性查询 |
| `RenderSettings.h` | `source/rhi/RenderSettings.h` | `RenderSettings` | 全局首选 RHI 持久化设置 |
| `GLWidget.h` | `source/render/GLWidget.h` | `GLWidget` | OpenGL 渲染窗口 |
| `RenderViewport.h` | `source/app/window/RenderViewport.h` | `RenderViewport` | 渲染视口宿主层 |
| `FEPickResult.h` | `source/render/FEPickResult.h` | `PickMode`, `ViewportInteractionMode`, `FEPickResult`, `FESelection` | 拾取、视口交互工具与选中 |
| `ferender_export.h` | 构建目录生成 | `FERENDER_EXPORT` 宏 | DLL 导出宏（自动生成） |

---

## 附录 B：单元类型速查表

| 类型 | 维度 | 节点数 | 角节点数 | 说明 |
|------|------|--------|----------|------|
| `BAR2` | 1D | 2 | 2 | 线性杆单元 |
| `BAR3` | 1D | 3 | 2 | 二次杆单元（含中点） |
| `TRI3` | 2D | 3 | 3 | 线性三角形 |
| `TRI6` | 2D | 6 | 3 | 二次三角形（3 角点 + 3 边中点） |
| `QUAD4` | 2D | 4 | 4 | 线性四边形 |
| `QUAD8` | 2D | 8 | 4 | 二次四边形（4 角点 + 4 边中点） |
| `TET4` | 3D | 4 | 4 | 线性四面体 |
| `TET10` | 3D | 10 | 4 | 二次四面体（4 角点 + 6 边中点） |
| `HEX8` | 3D | 8 | 8 | 线性六面体 |
| `HEX20` | 3D | 20 | 8 | 二次六面体（8 角点 + 12 边中点） |
| `WEDGE6` | 3D | 6 | 6 | 三棱柱（楔形体） |
| `PYRAMID5` | 3D | 5 | 5 | 四棱锥 |

**三角化产生的三角形数**（用于估算渲染开销）：

| 单元类型 | 渲染三角形数 | 说明 |
|----------|-------------|------|
| TRI3 | 1 | 本身即三角形 |
| QUAD4 | 2 | 拆分为 2 个三角形 |
| TET4 | 4 | 4 个三角面 |
| HEX8 | 12 | 6 面 × 2 三角形 |
| WEDGE6 | 8 | 2 三角面 + 3 四边形面(×2) |
| PYRAMID5 | 6 | 1 底面(×2) + 4 三角面 |

> **注意**：3D 实体单元仅渲染外表面。共享面（两个单元共有的面）会被自动剔除，实际三角形数通常远小于理论最大值。
