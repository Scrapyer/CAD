# FEModelViewer 已开发功能与重构验收清单

本文档记录当前版本已经具备的主要功能，用于后续 MainWindow / UI 重构后的回归检查。
它不是 API 参考，而是面向产品行为的验收清单：每次重构 UI、面板通信、RHI 或后处理入口后，
应按本文逐项确认功能仍可达、状态仍同步、资源生命周期仍稳定。

最后更新：2026-05-24

## 1. 文件导入与模型数据

| 功能 | 当前能力 | 重构后检查 |
|------|----------|------------|
| 模型文件导入 | 支持通过主窗口导入有限元模型文件，并填充 `FEModel` | 打开模型后视口显示、模型统计刷新、部件/结果面板状态同步 |
| INP 解析 | 支持 Abaqus INP 节点、单元和基础分组解析 | 导入 INP 后节点/单元数量正确，模型可渲染 |
| BDF/FEM 解析 | 支持 Nastran BDF/FEM 常用节点、单元和分组解析 | 导入 BDF/FEM 后部件树和拾取映射可用 |
| OP2 模型解析 | 支持 OP2 二进制模型解析路径 | 导入 OP2 模型后几何可显示，异常文件有错误提示 |
| OP2/UNV 结果解析 | 支持结果文件解析并生成结果仓库数据 | 结果列表可刷新，云图/变形入口可选择字段 |
| 渲染数据转换 | `FEMeshConverter` 将 `FEModel` 转为 `FERenderData` | 加载后 Mesh、三角形到单元映射、顶点到节点映射完整 |
| 导入路径状态 | 记录模型/结果文件路径，便于再次导入 | 重启或重新打开后路径状态不丢失 |

## 2. 主窗口与面板

| 功能 | 当前能力 | 重构后检查 |
|------|----------|------------|
| `MainWindow` | 负责导入、面板装配、视口连接和状态栏提示 | 菜单/工具栏改版后，原有操作入口仍可触发 |
| `FEModelPanel` | 显示模型信息、节点/单元统计、选择信息和搜索入口 | 加载模型、点选、搜索后内容实时刷新 |
| `PartsPanel` | 显示部件列表，支持显隐、选择和高亮联动 | 勾选显隐后视口同步，选择部件后高亮正确 |
| `ResultPanel` | 管理结果字段、云图、变形、阈值、切片、等值面等后处理控制 | 字段切换和过滤参数变化后，视口立即更新 |
| `ControlPanel` | 提供显示和交互相关控制入口 | 重构布局后控件状态仍与视口一致 |
| `MonitorPanel` | 绑定视口状态，显示渲染/交互信息 | 切换模型、RHI 首选项、选择状态后信息不滞后 |
| `PickPanel` | 提供节点、单元、部件拾取模式切换 | 模式切换后点选和框选目标正确 |

## 3. 渲染视口与基础显示

| 功能 | 当前能力 | 重构后检查 |
|------|----------|------------|
| 模型显示 | 支持三维有限元网格显示 | 加载模型后自动刷新，不出现空白视口 |
| 相机控制 | 支持旋转、平移、缩放和适配模型 | 鼠标交互顺畅，`fitToModel` 后模型居中 |
| 背景与坐标轴 | 支持背景绘制和视口坐标轴 | resize 后背景/坐标轴比例正常 |
| Mesh 上传 | 支持节点、三角面、边线等渲染数据上传 | 连续加载不同模型后旧数据不残留 |
| 部件颜色 | 支持按部件使用不同颜色显示 | 部件颜色稳定，隐藏/显示后颜色不串 |
| 部件显隐 | 支持部件级可见性控制 | 多部件连续切换显隐后渲染和拾取一致 |
| 标注显示 | 支持节点/单元等标签类显示入口 | 标签开关不影响基础渲染和拾取 |
| 色标叠加 | 支持云图色标 overlay | 云图切换、窗口 resize 后色标范围和标题正确 |

## 4. 交互选择与拾取

| 功能 | 当前能力 | 重构后检查 |
|------|----------|------------|
| 点选节点 | `PickMode::Node` 下可点选节点 | 点选后节点 ID、坐标和高亮同步到面板 |
| 点选单元 | `PickMode::Element` 下可点选单元 | 点选后单元 ID、类型和关联部件正确 |
| 点选部件 | `PickMode::Part` 下可点选部件 | 点选后部件列表选中并高亮整个部件 |
| 框选 | 支持区域框选并追加选择 | 大量对象框选后 UI 不阻塞，结果数量正确 |
| 取消选择 | 支持快捷组合或右键类操作取消选择 | 取消后高亮、面板和状态栏同步清空 |
| 高亮 | 支持节点、单元、部件高亮显示 | 隐藏部件、切换云图后高亮状态仍合理 |
| 搜索选择 | 支持按 ID 搜索并定位节点/单元 | 搜索不存在 ID 时有可理解提示，不破坏当前状态 |
| 选择信号 | 选择变化通过信号槽通知面板 | UI 重构后无重复触发、无丢失触发 |

## 5. 后处理功能

| 功能 | 当前能力 | 重构后检查 |
|------|----------|------------|
| 标量云图 | 支持标量场映射到颜色并显示 | 切换字段后颜色、范围、色标同步 |
| 结果范围 | 支持最小/最大值统计和色标范围显示 | 极值、标题、单位类文本不被 UI 改版遮挡 |
| 变形显示 | 支持基于位移结果的变形模型 | 自动比例、手动比例、关闭变形均正常 |
| 变形动画 | 支持帧动画控制 | 播放/暂停/重置后视口状态稳定 |
| 未变形叠加 | 支持显示未变形参考网格 | 与云图、部件显隐组合时不混乱 |
| 阈值过滤 | 支持按结果范围过滤显示 | 调整上下限后可见对象和色标一致 |
| 裁剪平面 | 支持裁剪模型显示 | 开关和参数调整后无资源泄漏或崩溃 |
| 切片 | 支持切片显示入口和数据更新 | 切片参数变化后显示结果可预期 |
| 等值面 | 支持等值面提取和显示 | 等值面与原模型显示关系清晰，可清除 |
| 探针查询 | 支持查询结果值和热点信息 | 点选对象后结果值与当前字段一致 |

## 6. RHI 与渲染后端

| 功能 | 当前能力 | 重构后检查 |
|------|----------|------------|
| OpenGL 后端 | 默认完整渲染路径，覆盖主要显示和交互能力 | 作为基准后端，所有 UI 功能先在 OpenGL 验收 |
| Vulkan 后端 | 可选渲染后端，已覆盖模型、边线、部件、拾取、云图和部分后处理路径 | macOS 本机可启动并完成基础显示/拾取/resize |
| RHI 配置 | RHI 首选项和 Vulkan 绘制策略保存到 `config/settings.ini`，重启后读取；Vulkan 绘制策略未配置时默认使用 GPU-driven Indirect，旧版无策略版本标记的 Traditional 配置会迁移到 GPU-driven，Mesh Shader 仍显示为禁用预留 | 手动切换 RHI 不再运行时销毁视口，提示重启生效；GPU-driven 能力不足时运行时回退传统路径，用户仍可手动选择 Traditional |
| RHI 生命周期 | `RenderViewport` 负责当前活动后端和首选后端状态 | UI 高频操作下切换首选项不崩溃，重启后生效 |
| Vulkan pick 资源 | Vulkan 拾取图像、framebuffer、readback buffer 已对象化 | resize/recreate 后点选和框选仍正确 |
| Vulkan depth 资源 | 深度资源已从主 renderer 拆出 | swapchain 重建后深度附件尺寸正确 |
| Vulkan frame 资源 | swapchain frame 相关资源已拆出 | resize、最小化/恢复后不访问失效资源 |
| Vulkan staging | 上传路径已从 `vkQueueWaitIdle()` 改为 fence，并引入上传上下文 | 连续加载模型后无明显卡死或资源增长 |
| Vulkan GPU-driven 基础路径 | 已新增 GPU-driven 资源/上传构建器、visibility compute pass、主三角面/点模式/边线 `vkCmdDrawIndexedIndirect` 绘制和 GPU-driven pick pass；part/hidden 显隐状态已有内部小 buffer 更新入口，Vulkan 视口在 Solid、Points 以及具备 surface+edge 数据的 Wireframe/SolidWireframe 模式可走该热路径；UI 默认选择 GPU-driven，并保留 Traditional 手动回退，运行时诊断会显示 actual/fallback 状态、资源计数、CPU surface/point 保留数量、fallback 次数、dispatch 次数、frustum culling 状态、可见三角/点/边数量、visibility compute GPU timestamp 耗时和 CPU 侧 upload/update/render/pick 耗时 | GPU-driven 默认策略下 macOS Vulkan surface 和默认压力测试通过；line-only 或能力不足时传统路径不受影响 |
| Vulkan GPU-driven 上传 V2 | 已新增 `docs/Vulkan_GPU_Driven_Upload_V2.md`、CPU 侧 source-vertex builder、V2-only 上传入口、V2 visibility/mesh/point/pick shader、V2 surface descriptor layout、graphics/point/pick pipeline 和 V2 visibility compute variant；验证 source vertex 不展开、triangle metadata 保存 source index，V2 可跳过 V1 展开 surface 资源、传统 mesh surface 上传和 CPU point buffer，点模式按需使用 unique visible source vertex | GPU-driven Indirect 现在默认优先走 V2 surface/point/pick，并保留 V1 fallback；M4/MoltenVK benchmark 显示 V2 静态 surface bytes 较 V1 少约 60%，`320` grid 总上传约 `43.734ms` |
| Vulkan GPU-driven benchmark | 新增手动 target `benchmark_vulkan_gpu_driven`，输出 Traditional、GPU-driven V1 与 GPU-driven V2 的 CSV 对比，不加入默认 `ctest` | 手动运行可查看不同 grid 规模下 V1/V2 surface bytes、CPU surface/point 保留数量、GPU 上传、显隐更新、render、离屏 pick frame、读回 pick、visibility GPU 耗时、可见三角/点/边数量、fallback 和 dispatch 数据 |
| Vulkan GPU-driven soak | 新增手动 target `soak_vulkan_gpu_driven`，连续切换显隐、显示模式、pick 和 swapchain recreate，不加入默认 `ctest` | 手动运行可验证长时间 GPU-driven active 状态、visible count 和 fallback 稳定性 |

## 7. 配置与持久化

| 功能 | 当前能力 | 重构后检查 |
|------|----------|------------|
| RHI 全局配置 | 软件运行时生成 `config/settings.ini` 保存 RHI 首选项 | 删除配置后使用默认 OpenGL，写入失败时有合理退化 |
| 重启生效策略 | RHI 切换只保存配置，不做运行时热切换 | UI 文案明确“下次启动生效” |
| 导入路径状态 | 模型和结果导入路径可保存/恢复 | UI 重构后文件选择器默认路径仍可用 |
| 面板状态同步 | 选择、结果字段、显隐等状态在面板间同步 | 拖拽布局、dock 重排后信号连接不丢 |

## 8. 自动化测试覆盖

| 测试方向 | 已有覆盖 | 重构后检查 |
|----------|----------|------------|
| 结果映射 | `test_result_mapper` | 标量/位移映射算法不被 UI 改动影响 |
| 结果仓库 | `test_result_repository` | 工况、字段、步次管理稳定 |
| 变形 | `test_deformation` | 变形比例和向量映射稳定 |
| 探针 | `test_probe` | 查询值和热点导出稳定 |
| 后处理状态 | `test_post_state`, `test_post_filter` | 阈值、裁剪、切片状态可复制和清除 |
| 等值面/阈值数据 | `test_iso_cube_data`, `test_threshold_row_data` | 数据生成结果稳定 |
| 导入路径 | `test_import_path_state` | 路径保存/恢复稳定 |
| RHI 工厂 | `test_render_backend_factory` | 后端枚举和创建路径稳定 |
| RHI 配置 | `test_render_settings` | `config/settings.ini` 写入和读取稳定 |
| 视口状态 | `test_render_viewport_state` | RHI 首选项和活动后端状态符合重启生效策略 |
| macOS Vulkan surface | `test_macos_vulkan_surface` | macOS Vulkan surface 创建逻辑稳定 |
| Vulkan GPU-driven 压力测试 | `test_vulkan_gpu_driven_stress` | GPU-driven 在绘制模式切换、显隐更新、视锥 uniform、可见数量读回、拾取和 swapchain recreate 后保持 active 且不 fallback |
| ResultPanel | `test_result_panel` | 面板到视口的后处理信号稳定 |
| 公开头 | `test_ferender_public_headers` | 安装头文件和公开 API 可被外部项目引用 |
| 解析探针 | `manual_parse_probe` | 手动解析诊断仍可用 |

## 9. UI 重构前置验收建议

重构 MainWindow 前，建议先固定以下基线：

- OpenGL 后端：加载模型、部件显隐、节点/单元/部件点选、框选、高亮、云图、变形、阈值、切片、等值面。
- Vulkan 后端：启动、加载模型、resize、点选/框选、部件显隐、云图和基础后处理入口。
- 配置路径：切换 RHI 首选项后生成 `config/settings.ini`，当前运行不崩溃，重启后读取新 RHI。
- 连续操作：连续加载模型、隐藏部件、点选、高亮、切换结果字段，面板和视口状态保持一致。
- 测试基线：`cmake --build build-qt6` 和 `ctest --test-dir build-qt6 --output-on-failure` 通过。

## 10. 当前已知边界

以下内容不是 UI 重构的前置条件，但需要在发布或更大范围验证前继续补齐：

- Windows Vulkan 路径还需要 MinGW/MSVC 实机验证。
- Vulkan pipeline 创建逻辑仍在 `VulkanClearFrameRenderer` 里，资源组已拆出，但 builder/factory 还没完全独立。
- 还没有更极端的大模型长时间 soak test；GPU-driven 已有默认确定性压力测试、手动 benchmark 和手动 soak，仍需积累多机器数据。
- RHI 切换在真实 UI 高频操作下还可以继续手测。
- 运行时 RHI 热切换当前已故意关闭，设计目标是“保存配置，下次启动生效”。
