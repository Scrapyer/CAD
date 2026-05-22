# RHI Layer

这里放渲染后端抽象和具体图形 API 实现。公共抽象仍保留在 `source/rhi`，
具体后端按 API 分目录：

- `OpenGL/`：OpenGL 后端实现。
- `Vulkan/`：Vulkan 后端、macOS surface 工厂、资源封装和 shader。
- `Metal/`：预留 Metal 后端目录。

- `RenderBackend.h` 定义 `IRenderBackend` 和后端信息结构。
- `RenderBackendFactory.*` 提供后端创建入口，当前默认返回 OpenGL 后端。
- `RenderSettings.*` 使用应用目录下的 `config/settings.ini` 记录全局首选 RHI，并在后端不可用时提供回退选择；运行时修改只写配置，下次启动生效。
- `OpenGL/OpenGLRenderBackend.*` 是当前 OpenGL 后端的落脚点。
- `Vulkan/VulkanContext.*` 封装 Vulkan instance、API 版本选择和 portability enumeration。
- `Vulkan/VulkanDevice.*` 封装物理设备选择、逻辑设备创建和 graphics/present 队列。
- `Vulkan/VulkanSurface.*` 封装外部创建的 `VkSurfaceKHR` 生命周期。
- `Vulkan/VulkanSwapchain.*` 封装 surface 绑定后的 swapchain 创建入口。
- `Vulkan/VulkanMacOSSurfaceFactory.*` 在 macOS 上通过 `QWindow` 原生 `NSView/CAMetalLayer`
  创建 `VK_EXT_metal_surface` surface。
- `Vulkan/VulkanBufferResource.*` 封装 Vulkan buffer 的创建、上传、局部更新和销毁；主网格
  vertex/index、普通边线 vertex/index、iso surface vertex/index、clip preview vertex/index
  以及 overlay、slice、selection、clip preview 边框线已通过 staging buffer 上传到 device-local 内存；
  scalar SSBO 和 pick readback 仍按更新频率使用 host-visible 路径。
- `Vulkan/VulkanMeshBufferResources.*` 封装主 mesh、普通边线、scalar SSBO 和 scalar descriptor 资源组。
- `Vulkan/VulkanDepthResource.*` 封装 depth attachment 的 image / memory / image view 生命周期。
- `Vulkan/VulkanPickResources.*` 封装离屏 pick color image、pick depth、framebuffer 和单像素 readback buffer。
- `Vulkan/VulkanSwapchainFrameResources.*` 封装 swapchain image view 与 framebuffer 集合。
- `Vulkan/VulkanStagingUploadContext.*` 合并一次模型上传中的多段 staging copy，用单个 command buffer 和 fence 提交。
- `Vulkan/VulkanDescriptorResource.*` 封装 storage-buffer descriptor pool/set 的创建、更新和销毁，
  当前承接 mesh scalar SSBO 绑定。
- `Vulkan/VulkanDescriptorSetLayoutResource.*` 封装 descriptor set layout 生命周期，当前承接
  mesh scalar SSBO 的 set layout。
- `Vulkan/VulkanRenderPassResource.*` / `Vulkan/VulkanFramebufferResource.*` 封装主视口和拾取路径的
  render pass / framebuffer 生命周期，逐步把裸 Vulkan handle 收敛为 RHI 资源对象。
- `Vulkan/VulkanPipelineResource.*` 封装 graphics pipeline 与 pipeline layout 生命周期，当前承接
  background、triangle、mesh、iso surface、line 和 pick 管线。
- `Vulkan/VulkanFramePipelines.*` 封装 frame renderer 使用的 pipeline 资源组，后续可继续迁入具体创建逻辑。
- `Vulkan/VulkanCommandResource.*` 封装 resettable command pool 与主 command buffer 生命周期，
  当前承接主视口、拾取和 readback 录制，staging 上传上下文复用其 command pool 分配一次性 copy buffer。
- `Vulkan/VulkanMeshFramePass.*` 负责主网格帧 command buffer 内的 mesh、iso、clip preview、
  overlay、edge、slice 和 selection draw 录制。
- `Vulkan/VulkanPickPass.*` 负责离屏拾取 command buffer 录制和 1x1 readback copy/barrier 录制。
- `Vulkan/shaders/vulkan_background.*` / `Vulkan/shaders/vulkan_triangle.*` / `Vulkan/shaders/vulkan_mesh.*` /
  `Vulkan/shaders/vulkan_iso.*` / `Vulkan/shaders/vulkan_line.*` / `Vulkan/shaders/vulkan_pick.*` 是 Vulkan 渐变背景、最小图形管线、主网格管线、等值面叠加管线、边线管线和拾取管线的
  GLSL 源码，由 CMake 调用
  `glslc` 编译为 SPIR-V。
- `Vulkan/VulkanClearFrameRenderer.*` 创建 render pass、graphics pipeline、command pool、同步对象，
  并协调清屏、固定三角形、主网格、swapchain frame resources 和拾取帧提交。
- `Vulkan/VulkanRenderBackend.*` 组合 Vulkan 基础设施，是传统 Vulkan 图形管线后端的落脚点。
- 后续 Metal/QRhi 后端也放到对应子目录，避免继续扩大 `source/render/GLWidget.*`。

Vulkan 后端通过 `FEMODELVIEWER_ENABLE_VULKAN_RHI` 控制，默认在找到 Vulkan SDK 时编译。
运行时 `VulkanContext` 会优先请求 SDK 和 loader 共同支持的 Vulkan 1.4 API；shader 编译默认使用
`FERENDER_VULKAN_SHADER_TARGET_ENV=vulkan1.4`，老工具链可在 CMake 配置时改成 `vulkan1.3`
或 `vulkan1.2`。当前已负责创建 Vulkan instance、处理 macOS/MoltenVK 需要的 portability enumeration、
枚举物理设备、创建逻辑设备和基础队列，并提供 surface/swapchain 封装类。当前 Qt 6.8.3
macOS 包禁用了 QtGui 的 Vulkan 特性（没有 `QVulkanInstance`/`QVulkanWindow`），因此
RHI 不依赖 Qt 的 Vulkan 封装，而采用外部 `VkSurfaceKHR` 接入协议：

1. 调用 `VulkanRenderBackend::initializeContext(requiredExtensions)` 创建带平台扩展的 instance。
2. 平台层使用 `backend.instance()` 创建原生 `VkSurfaceKHR`。
3. 调用 `initializeSwapchain(surface, width, height, vsync)` 创建带 present 队列的 device 和 swapchain。

`tests/test_macos_vulkan_surface.cpp` 已验证 macOS 工厂可创建真实 `VkSurfaceKHR`，并可用该
surface 初始化带 present 队列的 Vulkan device、创建 `VkSwapchainKHR`、获取 swapchain
images，并通过 `VulkanClearFrameRenderer` 清屏、绘制固定三角形、上传 `Mesh` 的
vertex/index buffer 和 edge vertex/index buffer、用 push constant MVP/基础颜色接入
`fitToModel()`、`setObjectColor()`、`setTriangleToPartMap()`、`setEdgeToPartMap()`、
`setPartVisibility()` 和基础轨道相机状态，并 present 主网格与普通边线。Vulkan 上传阶段
会按部件可见性过滤三角形/边线，并把部件颜色写入 mesh vertex buffer；主网格/普通边线几何
上传时会先写入 staging buffer，再通过 `VulkanStagingUploadContext` 合并多个 copy command 并用单个 fence 同步到 device-local vertex/index/line buffer，把 per-vertex scalar 写入独立 storage buffer。
当前还会创建离屏 pick render pass / framebuffer，用 `triangleToElement` 编码每个可见三角形的拾取颜色，
并通过 1x1 staging buffer 读回点击像素；`VulkanViewport` 已接入 Node / Element / Part 模式点选、
Ctrl/Shift 左键框选添加、Ctrl/Shift 右键点选/框选取消、`selectionChanged` 信号、Part 模式的 `partsPicked` 信号和选中高亮线。
`RenderViewport` 已新增 macOS `VulkanViewport` 分支，可在主界面切换到 Vulkan 主网格视口；
`setVertexScalars()` 已可把 per-vertex scalar 上传为 Vulkan storage buffer，并通过 descriptor set 绑定到 mesh pipeline；vertex shader 用 `gl_VertexIndex` 读取 scalar，fragment shader 通过 push constant 中的 min/max/bands 做 Jet 分段映射。首次 mesh 上传会建立 scalar SSBO 和 descriptor，后续切换云图 field 时只更新 scalar buffer 与 contour 参数，不重传 mesh geometry。`RenderViewport` 会用宿主层 Qt overlay 在 Vulkan 视口上显示 `setColorBar*()` 色标；Vulkan 已有独立 overlay line buffer、slice line buffer、iso surface buffer 和 clip preview buffer，可用于变形显示中的未变形半透明线框、基础切片交线绘制、半透明等值面叠加和裁剪/切片平面预览。
现有完整功能运行路径仍默认使用 OpenGL。

## Vulkan 当前能力表

| 功能 | OpenGL | Vulkan 传统管线 |
|------|--------|------------------|
| 主网格/普通边线 | 完整 | 已完成 |
| 部件颜色/显隐 | 完整 | 已完成，上传阶段过滤可见三角形/边线 |
| Node / Element / Part 点选 | 完整 | 已完成，离屏 color picking + CPU 映射 |
| Node / Element / Part 框选添加 | 完整 | 已完成，复用 CPU 投影和拾取颜色映射思路 |
| 点选/框选取消 | 完整 | 已完成，Ctrl/Shift + 右键路径 |
| 选中高亮 | 完整 | 已完成，Element 完整单元边、Part 边界/开放/特征/视角轮廓边、Node 三轴标记 |
| shader 端云图 | 完整 | 已完成，scalar SSBO + descriptor set，切换 field 不重传 mesh geometry |
| 渐变背景 | 完整 | 已完成，独立 Vulkan fullscreen triangle pipeline |
| 色标 overlay | 完整 | 已完成，由 `RenderViewport` 的 Qt overlay 承载 |
| 角落坐标轴 | 完整 | 已完成，复用 Vulkan line pipeline 绘制左下角 XYZ 轴线，并用 Qt overlay 显示 X/Y/Z 标签 |
| 未变形 overlay 线框 | 完整 | 已完成，独立 Vulkan line buffer |
| 切片交线 | 完整 | 已完成，独立 Vulkan line buffer |
| 等值面叠加 | 完整 | 已完成，半透明三角面 pipeline |
| 裁剪/切片平面预览 | 完整 | 已完成，半透明平面 + 边框线 |
| resize / swapchain 过期恢复 | 完整 | 已完成，acquire/present 检测 out-of-date/suboptimal 后下一帧重建 |
| 高级 RHI 资源模型 | OpenGL 后端资源托管 | 已开始，buffer、mesh buffer group、frame pipeline group、depth、pick、swapchain frame resources、scalar descriptor/set layout、render pass、framebuffer、pipeline/layout、command pool/buffer 生命周期已独立封装，mesh/pick pass 录制和批量 staging 上传已独立 |
| Vulkan 集成测试 | 完整 | 已补充连续加载网格模型、快速 swapchain recreate、pick 后 recreate、隐藏部件 pick、overlay/slice/iso/clip/selection 组合、错误输入恢复路径 |

## Vulkan 后续验证日志

- Windows Vulkan 路径还需要 MinGW/MSVC 实机验证。
- pipeline 创建逻辑仍在 `VulkanClearFrameRenderer` 里，资源组已拆出，但 builder/factory 还没完全独立。
- 还没有更极端的大模型性能基准和长时间 soak test。
- RHI 切换在真实 UI 高频操作下还可以继续手测。

当前阶段已抽离后端边界、通用 `Scene*` pass 描述、OpenGL 上下文信息、shader program 创建、scene/axes shader uniform 设置、默认 OpenGL 状态、基础资源创建、主网格 VAO/VBO/IBO/颜色/标量缓冲托管、普通边线 VAO/VBO/IBO 托管、选中高亮/轮廓边 VAO/VBO 托管、叠加线框、切片交线、等值面、裁剪/切片平面预览 VAO/VBO/IBO 托管、部件索引 texture buffer 资源托管、常规 VAO/VBO/IBO 上传、固定 position+color 几何资源托管、拾取 framebuffer 和拾取 VAO 托管、基础 viewport/clear/depth/blend/cull 状态切换、pass 状态应用、聚合 scene pass 执行、托管资源 scene pass 执行、常规 `drawArrays` / `drawElements` 调用，以及拾取绘制和像素读取。Vulkan 路径已经开始把裸 buffer 生命周期收敛到 `VulkanBufferResource`，当前覆盖主网格、普通边线、scalar SSBO、overlay、slice、selection line、iso surface、clip preview 和 pick readback；主网格、普通边线、iso surface、clip preview 三角面和动态线几何已从长期 host-visible buffer 改为 staging 上传到 device-local buffer，主 mesh / iso surface / clip preview 的多段 staging copy 已通过 `VulkanStagingUploadContext` 合并为单次 command buffer + fence 提交；主 mesh/edge/scalar/descriptor 已收敛到 `VulkanMeshBufferResources`，frame pipeline 句柄已收敛到 `VulkanFramePipelines`；主 depth 已收敛到 `VulkanDepthResource`，pick color/depth/framebuffer/readback 已收敛到 `VulkanPickResources`，swapchain image view/framebuffer 集合已收敛到 `VulkanSwapchainFrameResources`；scalar descriptor set layout 已收敛到 `VulkanDescriptorSetLayoutResource`，主视口和拾取路径的 render pass / framebuffer 已收敛到 `VulkanRenderPassResource` / `VulkanFramebufferResource`，graphics pipeline 和 pipeline layout 已收敛到 `VulkanPipelineResource`，主 command pool / command buffer 已收敛到 `VulkanCommandResource`，mesh frame 内的 iso/clip/overlay/edge/slice/selection draw 录制已收敛到 `VulkanMeshFramePass`，拾取绘制和 readback barrier/copy 录制已收敛到 `VulkanPickPass`。

`RenderBackend.h` 放置与具体图形 API 无关的描述结构，例如 `RenderBackendKind`、`SceneFrameUniforms`、`SceneDrawUniforms`、`ScenePassState`、`ScenePrimitive` 和 `PickDrawItem`。`RenderSettings` 记录用户首选 RHI；`RenderViewport` 是应用层依赖的视口宿主，按全局设置在 `GLWidget` 和 macOS `VulkanViewport` 之间切换。`GLWidget` 通过 `createRenderBackend()` 创建当前视口可用后端，`OpenGLRenderBackend` 负责把这些通用描述映射到具体 OpenGL 常量和调用。

`GLWidget` 仍负责 OpenGL 路径的 Qt 生命周期、交互状态、矩阵计算、渲染顺序和完整拾取业务；`VulkanViewport` 负责 Vulkan 路径的原生窗口、surface/swapchain 生命周期、resize 或 acquire/present 返回 out-of-date/suboptimal 后的 swapchain 重建、主网格 present、左下角坐标轴和标签、Node / Element / Part 点选、框选添加/取消、部件轮廓/特征边高亮、节点标记、shader 端基础云图映射、未变形 overlay 线框、基础切片交线、半透明等值面叠加和裁剪/切片平面预览，`RenderViewport` 负责 Vulkan 路径的色标 overlay。`OpenGLRenderBackend` 负责具体 OpenGL 资源创建、顶点属性绑定、GPU 数据上传、uniform 写入、pass 状态执行、聚合 pass 调度、基础绘制命令、主网格资源、普通边线资源、选中高亮资源、叠加线框、切片交线、等值面、裁剪/切片平面预览资源、背景/坐标轴这类固定几何资源，以及拾取 framebuffer / VAO 的创建和底层状态保护。常规 scene pass 已可直接接收后端托管资源，`GLWidget` 不再需要取出 VAO 指针。
