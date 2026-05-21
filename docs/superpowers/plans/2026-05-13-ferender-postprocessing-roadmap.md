# FERender Postprocessing Roadmap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 FERender 从“有限元模型渲染控件”推进为可独立安装、可被其他 Qt 项目调用的仿真后处理渲染引擎。

**Architecture:** 保持 `FERender` 作为可安装共享库，新增结果数据仓库、结果映射、变形/动画、过滤/截面、探针曲线、矢量/张量可视化和导出能力。GUI 应用和 `examples/` 只作为引擎 API 的消费者，避免把核心后处理逻辑继续放在 `MainWindow` 或面板类里。

**Tech Stack:** C++17, Qt6.8 Widgets/OpenGL, QOpenGLWidget, OpenGL 4.1 Core Profile, GLM, CMake/CTest.

---

## 1. 调研范围和依据

本计划对照的是通用结构/热/模态/CFD 后处理工作流，而不是单一软件的 UI 复刻。调研来源优先采用官方文档和官方产品页：

- [Abaqus/CAE Visualization module](https://abaqus.uclouvain.be/English/SIMACAECAERefMap/simacae-m-VISConcepts-sb.htm)：后处理环境围绕变形图、云图、符号图、X-Y 图、路径、动画和报告/打印展开。
- [Abaqus/CAE output database concepts](https://abaqus-docs.mit.edu/2017/English/SIMACAECAERefMap/simacae-c-odbintroreadregpyc.htm)：ODB 结果以 step、frame、field output、history output 组织，是后处理结果数据模型的重要参考。
- [Ansys Mechanical result evaluation](https://ansyshelp.ansys.com/public/Views/Secured/corp/v251/en/wb_sim/ds_Results.html)：结果对象可评估全模型、命名选择、几何、路径和表面，支持等值线和图表类输出。
- [Ansys Mechanical deformation results](https://ansyshelp.ansys.com/public/Views/Secured/corp/v251/en/wb_sim/ds_Deformation_Results.html)：变形后处理强调总/方向位移、真实比例或自动缩放，以及结果显示控制。
- [Altair HyperView official overview](https://altair.com/hyperview)：面向多求解器结果的可视化、动画、结果比较、报告生成和高级图形渲染。
- [Altair HyperView model/result interaction](https://2023.help.altair.com/2023/hwdesktop/hwx/topics/hyperview/model_result_interaction_c.htm)：包含 contour、iso、vector、tensor、trace、measure、notes、hotspot、section cut、exploded view、capture/video 等操作类别。
- [ParaView basic usage and filters](https://docs.paraview.org/en/v5.13.3/Tutorials/SelfDirectedTutorial/basicUsage.html)：常见通用后处理过滤器包括 contour、clip、slice、threshold、glyph、stream tracer、warp by vector 等。
- [COMSOL postprocessing and visualization](https://www.comsol.com/features-benefits/postprocessing-and-visualization)：强调等值面、切片、流线、箭头、粒子追踪、动画、报告等多物理场后处理能力。
- [Tecplot 360 feature detail](https://tecplot.azureedge.net/products/tecplot-360/tecplot-360-feature-detail/)：面向 CFD/CAE 的切片、等值面、流线、矢量、探针和高质量图形输出。

## 2. 当前 FERender 能力盘点

### 2.1 已具备的引擎能力

| 类别 | 当前能力 | 主要位置 |
|------|----------|----------|
| 几何/网格数据 | `FEModel`, `FENode`, `FEElement`, `FEPart`, node/element set | `FEModel.h`, `FEGroup.h` |
| 基础结果数据 | 标量场、矢量场、简单 subcase/type/component 层级 | `FEField.h`, `FEResultData.h` |
| 文件解析 | Abaqus INP, Nastran BDF/FEM, Nastran OP2 几何, OP2/UNV 结果 | `FEParser.h`, `FEParser_*.cpp` |
| 网格转换 | 2D/3D 单元三角化、外表面提取、平滑法线、拾取映射表 | `FEMeshConverter.h/.cpp` |
| 渲染控件 | `GLWidget` 渲染 mesh、边线、坐标轴、背景、主题 | `GLWidget.h/.cpp` |
| 拾取/选择 | 节点/单元/部件点选和框选、ID 标签、高亮轮廓 | `GLWidget.h/.cpp`, `FEPickResult.h` |
| 部件控制 | 部件显隐、部件高亮、部件颜色 | `GLWidget`, `PartsPanel` |
| 基础云图 | per-vertex scalar VBO、分段色带、colorbar、极值 ID | `GLWidget::setVertexScalars`, `MainWindow.cpp` |
| 基础变形接口 | `FEMeshConverter::toDeformedMesh()` 已声明并文档化 | `FEMeshConverter.h`, `docs/FERender_API.md` |
| 安装包调用示例 | 独立 Qt example 使用安装后的 `FERender::FERender` | `examples/simple_viewer` |
| 构建验证 | 公开头文件、CLI parse、安装包 example 的 CTest | `CMakeLists.txt`, `cmake/*.cmake` |

### 2.2 当前能力的边界

- 结果显示逻辑仍有一部分在 `MainWindow.cpp` 中，尤其是 field 到 per-vertex scalar 的映射、ID remap、colorbar 配置。这些应该下沉到 `FERender`。
- `FEResultData` 当前能承载 subcase/type/component，但缺少 step/frame/time/frequency/mode/load factor 等真实后处理时间轴。
- `FEVectorField` 有数据结构，但没有矢量箭头、tensor glyph、streamline、principal direction 等可视化管线。
- `toDeformedMesh()` 是可用方向，但没有统一的 `deformed + contour + original overlay + animation` 场景状态。
- 没有裁剪、切片、等值面、阈值、爆炸图、路径采样、探针曲线、报告导出和脚本自动化。
- 对大模型仍是一次性 CPU 转换和一次性 VBO 上传，缺少 LOD、分块、异步上传和结果帧缓存策略。

## 3. 主流后处理功能对照表

| 功能域 | 主流软件常见能力 | FERender 现状 | 缺口等级 |
|--------|------------------|---------------|----------|
| 结果数据库 | Step/frame/time/frequency/mode，field/history output，单位和位置元数据 | 仅 subcase/type/component，缺 frame/history | P0 |
| 变量派生 | 位移幅值、Von Mises、主应力、Tresca、应变能、用户表达式 | 有标量/矢量容器，派生体系不足 | P0 |
| 云图 | nodal/element/integration point，平均/不平均，离散/连续色谱，范围锁定 | 基础 per-vertex scalar 和 colorbar | P0 |
| 变形图 | 原始/变形/叠加，自动比例、真实比例、方向位移 | helper 级别，未成为场景模式 | P0 |
| 动画 | 时间帧、模态循环、变形比例动画、视频导出 | 缺失 | P1 |
| 切片/裁剪 | clip plane, slice plane, section cut, cap, 多平面 | 缺失 | P1 |
| 阈值/隔离 | 按结果值、部件、材料、集合、可见性过滤 | 仅部件显隐 | P1 |
| 等值面 | scalar iso-surface / iso-contour | 缺失 | P1 |
| 矢量/张量 | 箭头、位移/速度矢量、主应力方向、tensor glyph | 数据结构有矢量，渲染缺失 | P2 |
| 路径/探针 | pick point value, path sampling, XY curve, min/max/hotspot | 仅拾取 ID，不查询结果值 | P2 |
| 多视图比较 | 多结果/多工况同步视角、差值云图 | 缺失 | P2 |
| 注释/测量 | 距离、角度、面积、局部坐标、标注 | ID 标签有，测量和注释缺失 | P2 |
| 报告/导出 | 截图、视频、CSV、曲线、HTML/PDF 报告 | 缺失 | P2 |
| 文件格式 | ODB/H3D/VTK/VTU/CGNS/Ensight/Exodus 等 | INP/BDF/OP2/UNV 局部支持 | P3 |
| 自动化 API | 可脚本驱动的场景状态和导出 | 公共 API 有基础，后处理状态不完整 | P3 |

等级定义：

- **P0**：成为后处理引擎必须补齐，先做。
- **P1**：主流后处理高频功能，第二阶段做。
- **P2**：专业工作流增强，第三阶段做。
- **P3**：生态和工程化扩展，稳定后做。

## 4. 推荐总体架构

### 4.1 新增核心模块

| 模块 | 目标 | 建议文件 |
|------|------|----------|
| ResultRepository | 管理工况、步、帧、变量、单位、历史数据 | `FEResultRepository.h/.cpp` |
| ResultDerivation | 位移幅值、Von Mises、主值等派生结果 | `FEResultDerivation.h/.cpp` |
| ResultMapper | 把 node/element/integration point field 映射到 render vertex scalar/vector | `FEResultMapper.h/.cpp` |
| RenderState | 保存当前显示模式：undeformed/deformed/contour/vector/cut/filter/frame | `FERenderState.h/.cpp` |
| Deformation | 变形坐标、叠加、比例控制、变形动画 | `FEDeformation.h/.cpp` |
| Filters | clip/slice/threshold/iso/explode 的数据过滤与渲染描述 | `FEPostFilter.h/.cpp` |
| Probe | 点值查询、路径采样、曲线数据 | `FEProbe.h/.cpp` |
| Glyphs | vector glyph、tensor glyph、streamline 数据生成 | `FEGlyph.h/.cpp` |
| Export | 截图、动画帧、CSV 曲线、场数据导出 | `FEExport.h/.cpp` |

### 4.2 公共 API 原则

- `GLWidget` 继续负责 OpenGL 绘制和交互，不负责业务级结果推导。
- `FERenderData` 保持 “mesh + 映射表” 的定位，不把复杂结果仓库塞进渲染数据包。
- 新的结果/过滤/探针模块必须是纯 C++ 或最少 Qt Core 依赖，便于单元测试。
- GUI 应用、examples 和未来第三方项目只调用公共 API，不直接复制 `MainWindow.cpp` 中的结果映射逻辑。
- 所有新增公开类都加 `FERENDER_EXPORT`，并同步 `docs/FERender_API.md` 和 `CMakeLists.txt` 的 `RENDER_HEADERS`。

## 5. 分阶段开发计划

### Phase 0: 后处理基础设施下沉到 FERender

**目标：** 把现有云图映射和结果选择逻辑从 GUI 移到安装包 API 内，形成稳定的结果显示基础。

**文件：**

- Create: `FEResultMapper.h`
- Create: `FEResultMapper.cpp`
- Create: `tests/test_result_mapper.cpp`
- Modify: `CMakeLists.txt`
- Modify: `MainWindow.cpp`
- Modify: `docs/FERender_API.md`
- Modify: `examples/simple_viewer/main.cpp`

**任务：**

- [ ] 新增 `FEResultMapper`，提供节点/单元标量到 per-vertex scalar 的映射。

```cpp
struct FERENDER_EXPORT FEMappedScalars {
    std::vector<float> scalars;
    float minValue = 0.0f;
    float maxValue = 0.0f;
    int minId = -1;
    int maxId = -1;
    FieldLocation location = FieldLocation::Node;
};

class FERENDER_EXPORT FEResultMapper {
public:
    static FEMappedScalars mapScalarToVertices(
        const FEScalarField& field,
        const FERenderData& renderData,
        const FEModel& model);
};
```

- [ ] 写失败测试：节点结果映射到共享渲染顶点时，每个 `vertexToNode` 对应值正确。

Run:

```bash
cmake --build build -j 4
ctest --test-dir build -R result_mapper --output-on-failure
```

Expected first failure: `No tests were found` 或 `FEResultMapper` 未定义。

- [ ] 实现节点场映射。
- [ ] 写失败测试：单元结果映射时，每个三角形的三个顶点使用 `triangleToElement` 对应值。
- [ ] 实现单元场映射。
- [ ] 在 `MainWindow.cpp` 中删除本地 remap/scalar 拼装逻辑，改用 `FEResultMapper::mapScalarToVertices()`。
- [ ] 在 `examples/simple_viewer` 加一个按钮 `Show Sample Contour`，用安装包 API 展示节点云图。
- [ ] 更新 `docs/FERender_API.md`，新增 `FEResultMapper` 的调用示例。

**验收：**

```bash
cmake -S . -B build
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

### Phase 1: 结果仓库和帧模型

**目标：** 支持真实后处理的 step/frame/time/mode 结构，为动画、比较和结果曲线打底。

**文件：**

- Create: `FEResultRepository.h`
- Create: `FEResultRepository.cpp`
- Create: `tests/test_result_repository.cpp`
- Modify: `FEResultData.h`
- Modify: `FEParser_op2results.cpp`
- Modify: `FEParser_unv.cpp`
- Modify: `ResultPanel.cpp`
- Modify: `docs/FERender_API.md`

**建议 API：**

```cpp
enum class FEResultDomain {
    Static,
    Time,
    Frequency,
    Mode
};

struct FEResultFrameInfo {
    int subcaseId = 0;
    int stepIndex = 0;
    int frameIndex = 0;
    double value = 0.0;
    std::string valueLabel;
    FEResultDomain domain = FEResultDomain::Static;
};

struct FEResultFrame {
    FEResultFrameInfo info;
    std::vector<FEResultType> resultTypes;
};

class FERENDER_EXPORT FEResultRepository {
public:
    void clear();
    void addFrame(const FEResultFrame& frame);
    int frameCount() const;
    const FEResultFrame* frame(int index) const;
    std::vector<std::string> resultTypeNames(int frameIndex) const;
};
```

**任务：**

- [ ] 写失败测试：repository 能按 index 取回 frame info 和 result type。
- [ ] 实现 `FEResultRepository` 基本容器。
- [ ] 写失败测试：空 repository 查询返回安全状态，不崩溃。
- [ ] 实现安全查询。
- [ ] 保持 `FEResultData` 兼容旧 UI，同时新增从旧结构生成 repository 的转换函数。
- [ ] 修改 OP2/UNV 解析，把当前 subcase 数据填到 frame 0；后续解析到时间/频率信息时填真实 frame info。
- [ ] 修改 `ResultPanel`，内部使用 frame/type/component 三层选择。
- [ ] 更新文档，说明旧 `FEResultData` 和新 `FEResultRepository` 的关系。

**验收：**

- 单元测试覆盖空仓库、单帧、多帧。
- 旧 OP2/UNV 示例仍能显示结果。
- `examples/simple_viewer` 仍能独立构建。

### Phase 2: 变形显示和结果动画

**目标：** 提供后处理中最常用的 deformed shape、undeformed overlay、scale factor 和 frame animation。

**文件：**

- Create: `FEDeformation.h`
- Create: `FEDeformation.cpp`
- Create: `FEAnimationController.h`
- Create: `FEAnimationController.cpp`
- Create: `tests/test_deformation.cpp`
- Modify: `FEMeshConverter.h/.cpp`
- Modify: `GLWidget.h/.cpp`
- Modify: `ResultPanel.cpp`
- Modify: `examples/simple_viewer/main.cpp`

**建议 API：**

```cpp
struct FEDeformationOptions {
    float scale = 1.0f;
    bool overlayUndeformed = false;
};

class FERENDER_EXPORT FEDeformation {
public:
    static FEModel apply(const FEModel& model,
                         const FEVectorField& displacement,
                         const FEDeformationOptions& options);
};

struct FEAnimationState {
    int frameIndex = 0;
    bool playing = false;
    double fps = 12.0;
};
```

**任务：**

- [ ] 写失败测试：单节点位移按 scale 改变坐标。
- [ ] 实现 `FEDeformation::apply()`。
- [ ] 写失败测试：缺失某节点位移时，该节点保持原坐标。
- [ ] 在 `GLWidget` 增加 undeformed overlay mesh 的轻量接口。

```cpp
void setOverlayMesh(const Mesh& mesh);
void setOverlayVisible(bool visible);
```

- [ ] 在 `ResultPanel` 增加变形比例输入、真实比例、自动比例、叠加原始模型开关。
- [ ] 新增 frame animation controller，使用 `QTimer` 驱动当前 frame index，禁止把 timer 逻辑放入 `FEResultRepository`。
- [ ] 在 example 中构造一个简单位移场，演示 `Show Deformed`。

**验收：**

- 静态位移场能显示变形模型。
- scale=0 时模型回到原始形状。
- overlay 只影响显示，不改变 `FEModel` 原始数据。
- CTest 覆盖 deformation 数据逻辑。

### Phase 3: 值查询、探针、路径和热点

**目标：** 让拾取结果能回答“这里的结果值是多少”，并支持路径曲线和热点列表。

**文件：**

- Create: `FEProbe.h`
- Create: `FEProbe.cpp`
- Create: `tests/test_probe.cpp`
- Modify: `FEPickResult.h`
- Modify: `GLWidget.h/.cpp`
- Modify: `FEModelPanel.cpp`
- Modify: `ResultPanel.cpp`
- Modify: `docs/FERender_API.md`

**建议 API：**

```cpp
struct FEProbeValue {
    bool valid = false;
    int entityId = -1;
    FieldLocation location = FieldLocation::Node;
    float value = 0.0f;
};

struct FEPathSample {
    float distance = 0.0f;
    glm::vec3 position{0.0f};
    FEProbeValue value;
};

class FERENDER_EXPORT FEProbe {
public:
    static FEProbeValue valueAtEntity(const FEScalarField& field, int entityId);
    static std::vector<FEProbeValue> topHotspots(const FEScalarField& field, int count, bool descending);
    static std::vector<FEPathSample> sampleNodePath(const FEModel& model,
                                                    const FEScalarField& field,
                                                    const std::vector<int>& nodeIds);
};
```

**任务：**

- [ ] 写失败测试：node field 查询 node id 返回正确值。
- [ ] 写失败测试：element field 查询 element id 返回正确值。
- [ ] 实现 `valueAtEntity()`。
- [ ] 写失败测试：热点排序返回最大 N 个 ID。
- [ ] 实现 `topHotspots()`。
- [ ] 写失败测试：节点路径采样距离单调递增。
- [ ] 实现 `sampleNodePath()`。
- [ ] 修改 UI：拾取后显示当前结果值、单位、entity id。
- [ ] 增加导出路径曲线 CSV 的引擎 API，不在 UI 内手写 CSV。

**验收：**

- 点击模型时能看到当前云图变量的节点/单元值。
- 可列出最大/最小热点。
- 路径曲线数据能导出 CSV。

### Phase 4: 裁剪、切片、阈值和等值面

**目标：** 覆盖主流后处理的空间过滤能力：section cut、clip、slice、threshold、iso-surface。

**文件：**

- Create: `FEPostFilter.h`
- Create: `FEPostFilter.cpp`
- Create: `tests/test_post_filter.cpp`
- Create: `FEIsoSurface.h`
- Create: `FEIsoSurface.cpp`
- Modify: `FERenderData.h`
- Modify: `GLWidget.h/.cpp`
- Modify: `docs/FERender_API.md`

**建议 API：**

```cpp
struct FEPlane {
    glm::vec3 origin{0.0f};
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
};

class FERENDER_EXPORT FEPostFilter {
public:
    static FERenderData thresholdByElementValue(const FERenderData& input,
                                                const FEScalarField& field,
                                                float minValue,
                                                float maxValue);

    static FERenderData clipByPlane(const FERenderData& input,
                                    const FEPlane& plane,
                                    bool keepPositiveSide);
};
```

**任务：**

- [ ] 先实现 threshold，保留完整三角形，不做几何裁切。
- [ ] 写测试：两个单元值不同，阈值后只剩目标单元三角形。
- [ ] 实现 clip plane 第一版：以 triangle centroid 判断保留/丢弃。
- [ ] 写测试：平面一侧三角形被过滤。
- [ ] 增加 `GLWidget` 可视化裁剪平面参数，但第一版数据过滤在 CPU 端完成。
- [ ] 实现 slice plane：生成与网格相交的 polyline，用 `Mesh::edgeVertices` 显示。
- [ ] 实现 iso-surface 第一版：仅支持四面体/六面体线性插值，Marching Tetrahedra 优先于 Marching Cubes，降低拓扑表复杂度。
- [ ] example 增加 `Threshold High Values` 和 `Clip Half` 两个按钮。

**验收：**

- 阈值过滤不破坏 `triangleToElement` 和 `vertexToNode` 映射。
- clip/slice/iso 均有 CTest 覆盖小模型。
- UI 能开关过滤并恢复原始模型。

### Phase 5: 矢量、张量和流线

**目标：** 覆盖位移/速度/力箭头、主应力方向、CFD 流线等专业可视化。

**文件：**

- Create: `FEGlyph.h`
- Create: `FEGlyph.cpp`
- Create: `FEStreamline.h`
- Create: `FEStreamline.cpp`
- Create: `tests/test_glyph.cpp`
- Modify: `GLWidget.h/.cpp`
- Modify: `FEResultData.h`
- Modify: `FEField.h`

**建议 API：**

```cpp
struct FEGlyphOptions {
    float scale = 1.0f;
    int maxGlyphs = 5000;
    bool normalize = false;
};

class FERENDER_EXPORT FEGlyph {
public:
    static Mesh vectorArrows(const FEModel& model,
                             const FEVectorField& field,
                             const FEGlyphOptions& options);
};
```

**任务：**

- [ ] 写失败测试：单个节点矢量生成一支箭头 mesh。
- [ ] 实现简化箭头 glyph：线段 + cone head。
- [ ] 增加 glyph decimation，避免百万节点全量画箭头。
- [ ] 增加 tensor 数据结构，支持 6 分量对称张量。
- [ ] 实现主值/主方向计算，先在纯数学测试中验证。
- [ ] 实现 tensor glyph 第一版：三根 principal direction 线段。
- [ ] CFD 流线作为独立后续任务，要求先有体网格插值器。

**验收：**

- 位移矢量能以箭头显示。
- glyph 数量可限制。
- tensor 主方向测试覆盖对角矩阵和旋转矩阵。

### Phase 6: 多视图比较和结果差值

**目标：** 支持两个工况/两个文件/两个 frame 同屏比较，以及差值云图。

**文件：**

- Create: `FEComparison.h`
- Create: `FEComparison.cpp`
- Create: `tests/test_comparison.cpp`
- Modify: `GLWidget.h/.cpp`
- Modify: `MainWindow.cpp`
- Modify: `examples/simple_viewer/main.cpp`

**任务：**

- [ ] 新增 `FEComparison::subtractFields(a, b)`，要求 field location 和 ID 集合兼容。
- [ ] 写测试：两个 node field 差值得到正确结果。
- [ ] 对 ID 不完整情况返回诊断结构，列出缺失 ID 数量。
- [ ] `GLWidget` 增加相机状态导出/导入，支持多视图同步。

```cpp
CameraState cameraState() const;
void setCameraState(const CameraState& state);
```

- [ ] GUI 层实现两个 viewer 的同步视角，不进入引擎核心。

**验收：**

- 能显示 A、B、A-B 三种 field。
- 多 viewer 视角同步不影响单 viewer 使用。

### Phase 7: 导出、报告和自动化

**目标：** 让 FERender 可用于工程报告：截图、视频帧、CSV、场数据、轻量脚本驱动。

**文件：**

- Create: `FEExport.h`
- Create: `FEExport.cpp`
- Create: `tests/test_export.cpp`
- Modify: `GLWidget.h/.cpp`
- Modify: `docs/FERender_API.md`

**建议 API：**

```cpp
class FERENDER_EXPORT FEExport {
public:
    static bool writeScalarFieldCsv(const QString& filePath, const FEScalarField& field);
    static bool writePathSamplesCsv(const QString& filePath, const std::vector<FEPathSample>& samples);
};
```

`GLWidget` 增加：

```cpp
QImage grabRenderImage();
```

**任务：**

- [ ] 写 CSV 导出测试，验证 header 和数值行。
- [ ] 实现 field/path CSV 导出。
- [ ] 封装 `GLWidget::grabFramebuffer()`，提供稳定公开方法。
- [ ] 增加批量导出 frame images 的工具类，动画视频先导出图片序列。
- [ ] 报告生成先保持在 GUI/example 层，核心库只提供数据和图片。

**验收：**

- 可导出当前视图 PNG。
- 可导出当前云图 field CSV。
- 可导出路径曲线 CSV。

### Phase 8: 大模型性能和工程化

**目标：** 支撑工程级模型，避免后处理功能完成后被性能卡住。

**文件：**

- Create: `FERenderCache.h`
- Create: `FERenderCache.cpp`
- Create: `FEModelStats.h`
- Create: `tests/test_render_cache.cpp`
- Modify: `FEMeshConverter.cpp`
- Modify: `GLWidget.cpp`
- Modify: `docs/OpenGL_Rendering_Guide.md`

**任务：**

- [ ] 增加模型统计：节点数、单元数、外表面三角形数、边数、部件数、field value 数量。
- [ ] 增加 render data cache key：model id + visible parts + deformation scale + frame + filter state。
- [ ] 对大模型转换过程增加取消标志。
- [ ] 对 `GLWidget` 上传增加耗时统计和错误诊断。
- [ ] 研究 index buffer 32-bit 上限、VBO 内存估算和分块渲染接口。
- [ ] 第一版分块只按 part 拆分 draw calls，不引入复杂空间树。

**验收：**

- 100 万三角以内模型有明确内存估算。
- 转换和上传耗时能显示在 monitor panel。
- part 分块不改变拾取结果。

### Phase 9: 文件格式扩展

**目标：** 增强实际工程可用性，但在核心后处理管线稳定后推进。

**优先级建议：**

1. VTK/VTU：最适合验证后处理通用性，也方便和 ParaView 生态对接。
2. H3D：与 HyperView 工作流相关，适合结构后处理结果。
3. ODB：商业格式复杂，优先通过 Python/外部转换方案验证需求。
4. CGNS/Ensight/Exodus：面向 CFD/多物理场，放在 vector/streamline 后。

**文件：**

- Create: `FEParser_vtk.cpp`
- Create: `FEParser_h3d.cpp`
- Create: `tests/data/`
- Modify: `FEParser.h`
- Modify: `docs/FERender_API.md`

**任务：**

- [ ] 先引入小型 ASCII VTU/legacy VTK 解析测试数据。
- [ ] 支持 point data scalar/vector。
- [ ] 支持 cell data scalar。
- [ ] 支持导入后直接走 `FEResultRepository`。
- [ ] H3D/ODB 进入前写格式风险评估，明确是否依赖外部 SDK。

**验收：**

- VTK 示例文件能加载几何和至少一个结果场。
- example 可以用 VTK 文件替换内置 HEX8 模型。

## 6. 建议里程碑

| 里程碑 | 包含阶段 | 产出 |
|--------|----------|------|
| M1 基础后处理 API | Phase 0-1 | 结果映射下沉、frame/repository 模型、文档和 example |
| M2 结构后处理可用版 | Phase 2-4 | 变形、动画、探针、热点、阈值、切片/裁剪 |
| M3 专业可视化增强 | Phase 5-7 | 矢量/张量、多视图比较、截图/CSV/帧导出 |
| M4 工程化和格式生态 | Phase 8-9 | 大模型缓存/分块、VTK/H3D 等格式入口 |

推荐先实现 M1。原因是当前引擎已经能渲染和显示基础云图，但 API 责任边界还不干净；先把结果映射、仓库和 example 固化，后面的变形、动画、过滤都能基于同一套状态模型开发。

## 7. 近期首个开发切片

如果下一步马上开工，建议只做 Phase 0：

1. 新增 `FEResultMapper`。
2. 把 `MainWindow.cpp` 里的 scalar mapping 迁到引擎。
3. 让 `examples/simple_viewer` 展示一份内置节点云图。
4. 增加 CTest 验证 example 仍通过安装包构建。
5. 更新 `docs/FERender_API.md` 的“最小云图示例”。

这个切片小而关键：它直接服务“安装出来的那部分就是渲染引擎”的目标，并且能让外部调用者看到结果显示的标准调用方式。

## 8. 风险和取舍

- **不要先做 ODB/H3D。** 文件格式会消耗大量时间，但不能解决后处理管线缺失的问题。
- **不要把更多逻辑堆到 `MainWindow.cpp`。** GUI 只能编排，核心计算和映射必须进 `FERender`。
- **clip/iso 第一版接受近似。** 先用 centroid threshold、plane side filtering、Marching Tetrahedra 小范围实现，稳定后再做精确 cap 和复杂拓扑。
- **CFD 流线后置。** 流线需要体场插值和种子管理，依赖比结构后处理更重。
- **导出视频先用帧序列。** 直接编码 mp4 会引入额外依赖，先保持核心库轻量。

## 9. 验证策略

每个阶段至少包含四层验证：

1. **纯数据单元测试**：`FEResultMapper`, `FEProbe`, `FEPostFilter`, `FEExport` 等不依赖 OpenGL 的模块必须用 CTest 覆盖。
2. **安装包示例构建测试**：继续维护 `installed_simple_viewer_example`，保证外部项目能 `find_package(FERender)`。
3. **小模型人工检查**：HEX8、TET4、QUAD4、TRI3 各至少一个样例，验证云图、变形、拾取、过滤。
4. **性能基准**：记录 10k、100k、1M 三角的转换时间、上传时间、显存估算。

推荐常用命令：

```bash
cmake -S . -B build
cmake --build build -j 4
ctest --test-dir build --output-on-failure
cmake --install build --prefix build/install-test
cmake -S examples/simple_viewer -B build/examples/manual-simple-viewer -DCMAKE_PREFIX_PATH="$PWD/build/install-test"
cmake --build build/examples/manual-simple-viewer -j 4
```
