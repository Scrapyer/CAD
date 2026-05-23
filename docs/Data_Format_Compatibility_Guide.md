# FEModelViewer 数据格式兼容与模型树设计指导

本文档记录 FEModelViewer 后续开发中关于几何文件、网格文件、后处理结果文件兼容的理论边界与设计原则。

目标定位：

> FEModelViewer 是有限元模型与结果查看器，负责导入、显示、拾取、查询、云图、变形和动画；不负责前处理建模、网格划分、载荷编辑、求解设置和计算提交。

因此，格式兼容的重点不是完整复现各求解器的计算语义，而是稳定表达有限元模型、显示数据和后处理结果。

---

## 1. 文件类型边界

工业 CAE 数据通常可以拆成三类：

| 类型 | 代表格式 | 核心内容 | FEModelViewer 关注点 |
|------|----------|----------|----------------------|
| 几何文件 | STEP、IGES、Parasolid、ACIS | CAD 装配、零件、体、面、边、曲线 | 三角化显示、装配树、几何拾取 |
| 网格文件 | INP、BDF/NAS、CDB、MSH、UNV | 节点、单元、集合、部件、属性 | FEModel、拾取、部件显隐、网格显示 |
| 后处理文件 | VTU/VTK、Exodus、OP2、ODB、RST、D3PLOT、CSV | 时间步、场变量、历史曲线、结果值 | 云图、变形、动画、探针查询 |

三类文件最终都可以进入渲染层显示，但导入层和数据模型不应混为一体。

推荐数据流：

```text
几何文件
  -> GeometryImporter
  -> GeometryModel
  -> GeometryRenderData
  -> RenderViewport

网格文件
  -> MeshImporter / FEParser
  -> FEModel
  -> FEMeshConverter
  -> FERenderData
  -> RenderViewport

后处理文件
  -> ResultImporter
  -> FEResultRepository / FEResultDatabase
  -> ResultMapper / DeformationMapper
  -> RenderViewport
```

---

## 2. 几何文件兼容

几何文件描述的是 CAD/B-Rep 或曲面实体，不是有限元网格。常见结构是：

```text
Assembly
  Part
    Body / Solid
      Face
      Edge
      Vertex
```

常见格式：

| 格式 | 特点 | 建议 |
|------|------|------|
| STEP `.step/.stp` | 最通用的 CAD 交换格式，适合实体和装配 | 中期支持，建议基于 OpenCASCADE |
| IGES `.igs/.iges` | 老牌 CAD 格式，曲面多，实体语义较弱 | 中期支持，建议基于 OpenCASCADE |
| Parasolid `.x_t/.x_b` | 工业 CAD 内核格式，质量高 | 依赖授权或第三方内核，暂不优先 |
| ACIS `.sat/.sab` | CAD 内核格式 | 依赖授权或第三方内核，暂不优先 |
| STL `.stl` | 三角面片，不是精确 CAD | 可作为 DisplayMesh 支持 |
| OBJ `.obj` | 图形网格格式，不含 FE 语义 | 可作为 DisplayMesh 支持 |

几何文件的兼容目标：

- 读取装配、零件、体、面等层级。
- 对 CAD 几何进行 tessellation，生成可渲染三角网格。
- 支持几何零件显隐、透明、高亮。
- 后续可扩展面、边、体拾取。

不建议在早期自行实现 STEP/IGES 解析器。CAD 格式的拓扑、容差、曲面类型和装配语义复杂，后续如果需要几何查看，优先引入 OpenCASCADE。

---

## 3. 网格文件兼容

网格文件描述的是有限元离散模型，核心结构是：

```text
Node
Element
ElementType
Part / Component
NodeSet
ElementSet
Material
Property / Section
CoordinateSystem
```

常见格式：

| 格式 | 特点 | 建议 |
|------|------|------|
| Abaqus `.inp` | 文本关键字格式，工程常见 | 优先支持节点、单元、集合、部件 |
| Nastran `.bdf/.nas/.fem` | 卡片格式，航空航天和结构分析常见 | 优先支持常用 GRID、CTRIA、CQUAD、CTETRA、CHEXA 等 |
| Ansys `.cdb` | Ansys 网格/模型交换常用 | 可作为后续重点格式 |
| Gmsh `.msh` | 开放格式，适合自研和学术 | 推荐支持，利于测试和交换 |
| UNV `.unv` | 通用网格/结果交换格式 | 已有基础能力，继续按需要补齐 |
| VTK/VTU `.vtk/.vtu` | 可视化友好，可同时包含网格和结果 | 推荐作为开放交换格式 |
| Exodus `.exo` | 工程计算常用，支持块、集合、时间步和结果 | 后续可重点支持 |

网格文件的兼容目标：

- 稳定读入节点、单元和单元类型。
- 保留节点集、单元集、部件、材料名、属性名等可视化元数据。
- 支持按部件或集合显隐、选择、高亮。
- 支持节点、单元、部件拾取。
- 不承诺完整还原载荷、接触、求解控制和分析步。

求解语义的边界：

| 数据 | 建议处理 |
|------|----------|
| 节点、单元、集合 | 应完整导入 |
| 材料、属性、截面 | 优先保存名称、ID 和归属关系 |
| 载荷、边界条件 | 可作为元数据显示，暂不参与计算 |
| 接触、MPC、连接器 | 可记录原始定义或摘要，不做无损转换承诺 |
| 分析步、求解控制 | 仅作为可查看信息，非核心目标 |

---

## 4. 后处理文件兼容

后处理文件描述的是求解结果，核心结构是：

```text
ResultCase
  Step
    Frame
      Field
        Component
        Value
HistoryOutput
```

结果场还必须记录位置：

```text
Node
Element
IntegrationPoint
ElementNode
CellCenter
```

常见格式：

| 格式 | 特点 | 建议 |
|------|------|------|
| VTK/VTU `.vtk/.vtu` | 开放、可视化友好，支持 point data 和 cell data | 优先作为通用结果交换格式 |
| Exodus `.exo` | 支持块、集合、时间步、结果变量 | 推荐中期支持 |
| CSV/TXT | 适合探针、历史曲线、简单结果表 | 可作为轻量导入格式 |
| Nastran OP2 `.op2` | 二进制结果，工程价值高，解析复杂 | 已有能力可继续增强 |
| Abaqus ODB `.odb` | 私有结果数据库，依赖 Abaqus API | 建议通过脚本导出中间格式 |
| Ansys RST `.rst` | 私有/复杂二进制结果 | 后期按需求评估 |
| LS-DYNA D3PLOT `.d3plot` | 显式动力学动画结果 | 后期按需求评估 |

后处理兼容的核心难点：

- 多工况、多分析步、多时间帧。
- 节点值、单元值、积分点值之间的映射。
- 壳单元上表面、下表面、中面结果。
- 张量分量、主值、等效应力等派生量。
- 大模型结果数据的内存和加载性能。
- 私有二进制格式的版本差异。

FEModelViewer 应优先实现：

- 标量云图。
- 矢量位移变形。
- 时间步切换和动画。
- 节点/单元拾取后的当前结果查询。
- 最小值、最大值、色标范围。
- 基础派生量，例如向量模、von Mises。

不建议早期直接追求对所有商业结果数据库的无损读取。更稳妥的路径是先支持开放结果格式，再对高价值商业格式逐个增强。

---

## 5. 统一 Project Tree 设计

几何、网格、后处理文件的模型树并不一样，但可以在 UI 中统一成一棵项目树。

推荐 UI 结构：

```text
Project
  Geometry
    Assembly / Part / Body / Face / Edge
  FE Model
    Parts
    Node Sets
    Element Sets
    Materials
    Properties
    Coordinate Systems
  Results
    Cases
    Steps
    Frames
    Field Outputs
    History Outputs
```

背后仍应保持三套语义模型：

```text
GeometryTreeModel
FETreeModel
ResultTreeModel
```

统一树节点只负责 UI 索引、显隐、高亮、选择和属性入口，不直接承载全部业务数据。

推荐节点类型枚举方向：

```cpp
enum class ProjectTreeNodeKind {
    GeometryRoot,
    Assembly,
    GeometryPart,
    Body,
    Face,
    Edge,

    FEModelRoot,
    FEPart,
    NodeSet,
    ElementSet,
    Material,
    Property,
    CoordinateSystem,

    ResultRoot,
    ResultCase,
    ResultStep,
    ResultFrame,
    ResultField,
    ResultComponent,
    HistoryOutput
};
```

树节点可以保存轻量引用：

```cpp
struct ProjectTreeNode {
    ProjectTreeNodeKind kind;
    QString name;
    QVariant entityId;
    bool visible = true;
};
```

真实数据继续保存在：

```text
GeometryModel
FEModel
FEResultRepository / FEResultDatabase
```

---

## 6. 拾取语义

拾取结果也应区分来源：

```cpp
enum class PickKind {
    GeometryFace,
    GeometryEdge,
    FENode,
    FEElement,
    FEPart,
    ResultProbe
};
```

不同拾取对象进入同一个属性面板，但显示内容不同：

| 拾取对象 | 属性面板内容 |
|----------|--------------|
| 几何面 | Face ID、面积、所属 Body、颜色、可见性 |
| 几何边 | Edge ID、长度、所属 Face/Body |
| FE 节点 | Node ID、坐标、所属节点集、当前结果值 |
| FE 单元 | Element ID、类型、节点连接、材料/属性、当前结果值 |
| FE 部件 | Part 名称、节点数、单元数、显隐状态 |
| 结果探针 | 当前 Case/Step/Frame、Field、Component、Value |

这样可以保证 UI 入口统一，但数据含义清晰。

---

## 7. 兼容策略与优先级

## 7. 模型显隐与选择策略

当前阶段先实现有限元模型级显隐，以 `FEPart` 作为最小稳定显隐单元。这样可以覆盖导入网格、拾取、框选、云图、变形、阈值/裁剪过滤以及 OpenGL/Vulkan/Metal 后端切换的共同需求。

显隐状态源：

```text
PartsPanel
  -> partVisibilityChanged(partIndex, visible)
  -> RenderViewport::setPartVisibility()
  -> GLWidget / VulkanViewport / MetalViewport
```

设计规则：

1. `PartsPanel` 是模型级显隐的 UI 状态源，树节点、右键菜单和视口右键菜单都应同步到这里。
2. `RenderViewport::setMesh()` 可能因为变形、阈值、裁剪等操作重新上传网格，因此上传后必须重新从 `PartsPanel` 同步 Part 显隐状态。
3. 拾取和框选必须过滤隐藏 Part，避免用户选中当前不可见对象。
4. Node/Element 选择用于显隐时，应先反查所属 `FEPart`，然后按 Part 执行隐藏或隔离。
5. 线/梁单元没有三角面，也必须通过 `Mesh::edgeToElement` 和 `edgeToPart` 参与部件归属、显隐和拾取过滤。
6. Vulkan/Metal/OpenGL 的显隐语义必须一致；允许实现细节不同，不允许用户交互结果不同。

后处理显示边界：

- 阈值和裁剪过滤必须同时处理三角面和线/梁边线，并保持 `edgeToElement`、`edgeToPart`、`elemEdgeToElement` 可反查。
- 元素结果和节点结果映射应包含线/梁单元；纯线模型没有面顶点时，结果值应写入 `edgeScalars`。
- OpenGL、Vulkan 和 Metal 云图通道均已支持三角面 per-vertex scalar 和线/梁 edge scalar；普通辅助线、选中高亮线、切片线仍使用纯色线段，不参与云图着色。

后续做几何级显隐时，不应替代当前 Part 显隐，而应叠加一层几何可见性：

```text
最终可见 = PartVisible && GeometryVisible && ResultFilterVisible
```

推荐扩展顺序：

1. 有限元 Part 显隐：已作为当前基础能力。
2. FE Element/Node 临时隐藏：用于局部检查，但不改变 Part 树结构。
3. Geometry Part / Body / Face 显隐：等引入 GeometryModel 后再做。
4. Result Filter 显隐：阈值、裁剪、切片作为后处理过滤层独立存在。

---

## 8. 兼容策略与优先级

推荐分阶段目标：

### 阶段一：有限元网格查看器

- 强化 INP、BDF/NAS/FEM、UNV。
- 增加或完善 Gmsh MSH。
- 保证节点、单元、部件、集合、拾取和显隐稳定。
- 不处理完整前处理计算语义。

### 阶段二：开放结果查看器

- 支持 VTK/VTU 结果。
- 支持 CSV/TXT 结果表和历史曲线。
- 支持结果与现有 FEModel 绑定。
- 支持云图、变形、动画、探针。

### 阶段三：工程结果格式增强

- 深化 OP2 模型和结果解析。
- 评估 Exodus。
- 对 ODB、RST、D3PLOT 采用“导出中间格式优先，直接读取后置”的策略。

### 阶段四：几何查看

- 先把 STL/OBJ 作为 DisplayMesh 支持。
- 后续引入 OpenCASCADE 支持 STEP/IGES。
- 几何显示与 FE 网格显示保持独立数据流。

---

## 9. 设计原则总结

1. 几何、网格、结果是三类不同语义的数据，不应强行塞入同一个模型类。
2. UI 可以统一为 Project Tree，但内部应分为 GeometryModel、FEModel 和 ResultDatabase。
3. 网格格式优先支持有限元可视化核心数据：节点、单元、集合、部件和属性归属。
4. 后处理格式优先支持开放交换格式，再逐步增强商业二进制格式。
5. 前处理求解语义只作为可查看元数据，不作为 FEModelViewer 的核心目标。
6. STL/OBJ 属于 DisplayMesh，不等同于 CAD 几何，也不等同于 FE 网格。
7. 结果数据不要直接塞进 FENode 或 FEElement，应使用独立结果仓库，以支持多场、多步、多帧。
8. 拾取、属性面板和模型树应统一交互入口，但必须保留明确的实体类型。

这一边界可以让 FEModelViewer 逐步演进为稳定的有限元模型与结果查看器，而不是过早变成难以维护的通用前处理系统。
