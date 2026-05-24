# Vulkan GPU-driven 渲染方案

> 状态：设计方案
> 日期：2026-05-24
> 范围：`source/rhi/Vulkan` 后端内部演进，不新建独立 `rhi/vulkan-gpu` 顶层后端

本文档描述 FEModelViewer 在现有 Vulkan RHI 上引入 GPU-driven rendering 的完整方案。
目标是让大模型的可见性筛选、部件显隐、后续 LOD/裁剪等渲染决策逐步从 CPU 侧转移到 GPU 侧，
同时保留当前 Vulkan 传统路径作为 fallback。

---

## 1. 结论

GPU-driven 不等于 mesh shader。对本项目，第一阶段应采用：

```text
Compute shader 生成可见索引和 VkDrawIndexedIndirectCommand
  -> Graphics pass 使用 vkCmdDrawIndexedIndirect 绘制
```

mesh shader、device generated commands、shader enqueue/Work Graphs 类能力只作为后续增强路径，不作为第一版依赖。

推荐落点：

```text
source/rhi/Vulkan/
  VulkanRenderBackend
  VulkanClearFrameRenderer
  VulkanMeshFramePass
  ...
  gpu_driven/
    VulkanGpuDrivenMeshResources.h/.cpp
    VulkanGpuDrivenUploadBuilder.h/.cpp
    VulkanVisibilityComputePass.h/.cpp
    VulkanIndirectMeshFramePass.h/.cpp
    VulkanGpuDrivenTypes.h
  shaders/
    vulkan_visibility.comp
```

外部 GUI 和 RHI 枚举仍只看到 `RenderBackendKind::Vulkan`。
GPU-driven 是 Vulkan 后端内部的一种 draw strategy，而不是一个新的用户可见后端。

---

## 2. 当前 Vulkan 后端基线

当前实现已经具备可复用基础：

| 模块 | 当前职责 | GPU-driven 改造价值 |
|------|----------|---------------------|
| `VulkanRenderBackend` | Vulkan 后端门面，转发上传、渲染、拾取请求 | 保持外部接口稳定，内部选择传统或 GPU-driven 策略 |
| `VulkanClearFrameRenderer` | 管理 frame/render pass/pipeline，上传 mesh 并录制 command buffer | 第一阶段可继续作为调度器，逐步把 GPU-driven 逻辑拆出 |
| `VulkanMeshBufferResources` | 主 mesh、edge、scalar descriptor 资源组 | 增加 GPU-driven 专用资源组，避免继续膨胀 |
| `VulkanMeshFramePass` | 录制传统 `vkCmdDrawIndexed` 主网格绘制 | 新增 indirect pass，保留传统 pass |
| `VulkanStagingUploadContext` | 设备本地 buffer 上传 | 继续用于上传静态几何和 metadata |
| `VulkanPickPass` / `VulkanPickResources` | 离屏拾取 | 第二阶段改为复用 GPU 可见集 |

当前主表面绘制链路：

```text
CPU uploadMesh()
  -> CPU 按 part/element 显隐过滤三角形
  -> CPU 展开 VulkanMeshVertex + index buffer
  -> upload vertex/index/scalar buffer
renderMeshFrame()
  -> recordMeshCommandBuffer()
  -> VulkanMeshFramePass::record()
  -> vkCmdDrawIndexed(meshIndexCount)
```

这条路径的问题是：部件显隐、隐藏单元、未来裁剪/LOD 都会触发 CPU 重建大 index buffer 或大 mesh buffer。
GPU-driven 的第一收益点就是让这些“每帧或高频变化的小状态”只更新小 buffer。

---

## 3. 目标与非目标

### 3.1 目标

1. 保留当前 Vulkan 传统路径，新增 GPU-driven indirect 路径。
2. 首版支持主三角面绘制，显示结果必须与传统路径一致。
3. 部件显隐、隐藏单元、视锥裁剪在 GPU compute 中完成。
4. GPU 写出可见 index buffer 和 `VkDrawIndexedIndirectCommand`。
5. graphics pass 使用 `vkCmdDrawIndexedIndirect` 绘制。
6. 运行时能力不足、shader 编译失败、buffer 创建失败时自动 fallback 到传统路径。
7. 不影响 OpenGL/Metal 后端行为。

### 3.2 非目标

第一版不做：

1. 不直接引入 mesh shader 作为主路径。
2. 不依赖 `VK_EXT_device_generated_commands`。
3. 不依赖 `VK_AMDX_shader_enqueue` 或 Work Graphs 类 provisional 扩展。
4. 不要求 edge、pick、iso surface、clip preview 全部首版 GPU-driven。
5. 不改变 `FEModel/FEMeshConverter/FERenderData` 公开 API。

---

## 4. Draw Strategy 设计

在 Vulkan 后端内部增加策略枚举：

```cpp
enum class VulkanDrawStrategy {
    Traditional,
    GpuDrivenIndirect,
    MeshShader
};
```

第一版只实现：

| 策略 | 状态 | 说明 |
|------|------|------|
| `Traditional` | 已有 | CPU 过滤，`vkCmdDrawIndexed` |
| `GpuDrivenIndirect` | 新增 | compute culling，`vkCmdDrawIndexedIndirect` |
| `MeshShader` | 预留 | 后续 meshlet/task/mesh shader 路线 |

策略选择规则：

```text
用户/配置请求 GpuDrivenIndirect
  -> 检查 Vulkan device feature
  -> 检查 shader module/pipeline/descriptor 创建
  -> 检查 GPU-driven mesh resource 完整
  -> 成功：使用 GpuDrivenIndirect
  -> 失败：记录原因，fallback Traditional
```

当前默认 Vulkan 绘制策略为 `GpuDrivenIndirect`。旧配置中未带策略版本标记的
`traditional` 会在启动读取时迁移为 GPU-driven；之后用户仍可在 UI 中手动选择
Traditional 作为兼容回退。

```text
config/settings.ini
[render]
vulkanDrawStrategy=gpu_driven_indirect
vulkanDrawStrategyVersion=2
```

也可以临时支持环境变量，便于调试和 CI：

```text
FEMODELVIEWER_VULKAN_DRAW_STRATEGY=gpu
```

---

## 5. GPU-driven 主链路

### 5.1 初始化

```text
VulkanDevice::initialize()
  -> 查询基础 Vulkan 特性
  -> 记录是否支持 indirect draw、compute queue/graphics queue compute

VulkanClearFrameRenderer::initialize()
  -> 创建传统 graphics pipeline
  -> 创建 GPU-driven descriptor set layout
  -> 创建 visibility compute pipeline
  -> 创建 indirect graphics pass 所需资源绑定
```

第一版可以使用 graphics queue 上的 compute capability，避免新增 queue ownership 转移。
如果当前选中的 graphics family 不支持 compute，则 GPU-driven 策略不可用。

### 5.2 上传

传统路径会按当前显隐状态重建 mesh；GPU-driven 路径应上传完整几何和 metadata：

```text
uploadMesh(mesh, options)
  -> Traditional:
       维持当前 CPU 过滤和上传路径

  -> GpuDrivenIndirect:
       构建完整 surface vertex buffer
       构建完整 source index buffer
       构建 triangle metadata buffer
       构建 host-visible part state buffer
       构建 host-visible hidden element state buffer 或 bitset
       分配 visible index buffer
       分配 indirect command buffer
       分配 counter/reset buffer
```

mesh 上传后，部件显隐和隐藏单元列表可通过内部
`updateGpuDrivenVisibilityState()` 只更新 part state、hidden element 和 frame uniform
小 buffer。当前该入口已在后端测试中强制启用验证，并已接入 `VulkanViewport`
在 `Solid` / `Points` 模式以及具备 surface+edge 数据的 `Wireframe` /
`SolidWireframe` 模式下的显隐热路径；线框-only 或缺少 edge metadata 的模型仍会回到
传统 CPU 过滤资源重传。pick pass 已可直接消费 GPU-driven visible index buffer。
当前 V1 点模式仍按可见三角形 corner 派生；V2 点模式已使用独立 visible point index
buffer 和 point indirect command。compute pass 只在 Points 模式启用 source-vertex flag
去重并绘制 unique visible source vertex，不再需要 CPU point buffer，也避免非 Points
模式支付点去重原子写入成本。

### 5.3 每帧渲染

```text
renderMeshFrame()
  -> begin command buffer
  -> reset indirect command / counter
  -> bind visibility compute pipeline
  -> dispatch ceil(triangleCount / workgroupSize)
  -> pipeline barrier:
       compute shader write
       -> indirect command read + index buffer read + vertex shader read
  -> begin render pass
  -> record background
  -> bind mesh graphics pipeline
  -> bind full vertex buffer
  -> bind visible index buffer
  -> vkCmdDrawIndexedIndirect(indirectCommandBuffer)
  -> Wireframe/SolidWireframe:
       bind gpu-driven edge vertex + visible edge index
       vkCmdDrawIndexedIndirect(edgeIndirectCommandBuffer)
  -> draw overlays/selection/axes using existing path
  -> end render pass
```

当前基础实现已替换主三角面、点模式、拾取和具备 edge metadata 的边线。overlay、
selection、axes 继续使用传统资源。

---

## 6. 数据布局

### 6.1 顶点

当前 `VulkanMeshVertex` 在 `VulkanClearFrameRenderer.cpp` 匿名命名空间中定义。
GPU-driven 需要 compute shader 和 graphics shader 共享数据语义，建议移到：

```text
source/rhi/Vulkan/gpu_driven/VulkanGpuDrivenTypes.h
```

建议保留现有 layout：

```cpp
struct VulkanMeshVertex {
    float position[3];
    float normal[3];
    float color[3];
    float pickColor[3];
};
```

后续若 fragment shader 改为按 `partId` 取色，可减少顶点中的 `color[3]` 冗余。

### 6.2 三角元数据

每个三角形一条 metadata：

```cpp
struct VulkanGpuTriangleMeta {
    uint32_t index0;
    uint32_t index1;
    uint32_t index2;
    uint32_t indexPad;
    int32_t elementId;
    int32_t partId;
    int32_t idPad0;
    int32_t idPad1;
    float boundsMin[4];
    float boundsMax[4];
};
```

说明：

1. `index0/index1/index2` 指向完整 vertex buffer。
2. `elementId` 用于隐藏单元和拾取颜色。
3. `partId` 用于部件显隐和部件颜色。
4. `boundsMin/boundsMax` 第一版用于视锥裁剪；如果先做显隐，可暂时只上传中心点或不启用 bounds。
5. C++ 和 GLSL SSBO 采用 16 字节分组对齐，避免 `std430` 中 `vec3` 结构体对齐差异；当前每三角 metadata 为 64 bytes。

### 6.3 Part 状态

```cpp
struct VulkanGpuPartState {
    uint32_t visible;
    float color[3];
};
```

`partId < 0` 时使用默认对象颜色。

### 6.4 隐藏单元

隐藏单元数量可能很小，也可能较大。建议分两步：

第一版：

```cpp
struct VulkanGpuHiddenElement {
    int32_t elementId;
};
```

compute shader 线性扫描隐藏列表。适合隐藏数量较少的交互场景。

第二版：

```text
elementId -> dense element ordinal
dense bitset SSBO
```

用 bitset 将查询降为 O(1)，适合大量过滤。

### 6.5 Indirect 命令

使用 Vulkan 标准结构：

```cpp
struct VkDrawIndexedIndirectCommand {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t vertexOffset;
    uint32_t firstInstance;
};
```

初始化/重置后：

```text
indexCount = 0
instanceCount = 1
firstIndex = 0
vertexOffset = 0
firstInstance = 0
```

compute shader 对 `indexCount` 做 atomic add，每个可见三角形追加 3 个 index。

---

## 7. Buffer 资源规划

新增资源组：

```cpp
class VulkanGpuDrivenMeshResources {
public:
    void destroy(const VulkanDevice& device);
    bool isReady() const;

    VulkanBufferResource vertexResource;
    VulkanBufferResource triangleMetaResource;
    VulkanBufferResource partStateResource;
    VulkanBufferResource hiddenElementResource;
    VulkanBufferResource visibleIndexResource;
    VulkanBufferResource indirectCommandResource;
    VulkanBufferResource frameUniformResource;

    uint32_t vertexCount = 0;
    uint32_t triangleCount = 0;
    uint32_t maxVisibleIndexCount = 0;
};
```

建议 usage：

| Buffer | Usage |
|--------|-------|
| `vertexResource` | `VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT` |
| `triangleMetaResource` | `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT` |
| `partStateResource` | `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT` |
| `hiddenElementResource` | `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT` |
| `visibleIndexResource` | `VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT` |
| `indirectCommandResource` | `VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT` |
| `visibilityReadbackResource` | host-visible `VK_BUFFER_USAGE_TRANSFER_DST_BIT` |
| `frameUniformResource` | `VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT` 或 host-visible |

第一版可以用 host-visible 更新 `partStateResource/frameUniformResource`，静态几何继续使用 device-local。

---

## 8. Descriptor 设计

### 8.1 Visibility Compute Descriptor

```text
set 0 binding 0: readonly triangle meta SSBO
set 0 binding 1: readonly part state SSBO
set 0 binding 2: readonly hidden element SSBO
set 0 binding 3: writeonly visible index SSBO
set 0 binding 4: indirect command SSBO
set 0 binding 5: frame uniform UBO/SSBO
```

`frame uniform` 包含：

```cpp
struct VulkanGpuVisibilityUniforms {
    float viewProj[16];
    float frustumPlanes[6][4];
    uint32_t triangleCount;
    uint32_t hiddenElementCount;
    uint32_t enableFrustumCulling;
    uint32_t enablePartVisibility;
};
```

### 8.2 Graphics Descriptor

主 mesh graphics pipeline 第一版可以继续使用现有 push constants 和 scalar descriptor。
如果 fragment shader 需要按 part 动态取色，再增加：

```text
set 1 binding 0: readonly triangle meta SSBO
set 1 binding 1: readonly part state SSBO
```

首版为了降低风险，仍沿用上传时写入的 vertex color。
这样部件颜色变化仍需更新 vertex 或等第二阶段再 GPU 化；部件显隐可以先 GPU 化。

---

## 9. Shader 方案

### 9.1 `vulkan_visibility.comp`

伪代码：

```glsl
#version 450

layout(local_size_x = 128) in;

struct TriangleMeta {
    uvec4 indices;
    ivec4 ids;       // x = elementId, y = partId
    vec4 boundsMin;
    vec4 boundsMax;
};

struct DrawIndexedIndirectCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

layout(set = 0, binding = 0) readonly buffer TriangleMetaBuffer {
    TriangleMeta triangles[];
};

layout(set = 0, binding = 3) buffer VisibleIndexBuffer {
    uint visibleIndices[];
};

layout(set = 0, binding = 4) buffer IndirectCommandBuffer {
    DrawIndexedIndirectCommand drawCommand;
};

void main() {
    uint tri = gl_GlobalInvocationID.x;
    if (tri >= triangleCount) {
        return;
    }

    TriangleMeta meta = triangles[tri];
    if (!isPartVisible(meta.ids.y)) {
        return;
    }
    if (isElementHidden(meta.ids.x)) {
        return;
    }
    if (enableFrustumCulling != 0 && !aabbIntersectsFrustum(meta.boundsMin.xyz, meta.boundsMax.xyz)) {
        return;
    }

    uint offset = atomicAdd(drawCommand.indexCount, 3);
    visibleIndices[offset + 0] = meta.indices.x;
    visibleIndices[offset + 1] = meta.indices.y;
    visibleIndices[offset + 2] = meta.indices.z;
}
```

`instanceCount/firstIndex/vertexOffset/firstInstance` 不应由每个线程写。
推荐在 dispatch 前通过 `vkCmdFillBuffer` 或一个小 reset compute pass 初始化。

### 9.2 Shader 编译接入

在 `CMakeLists.txt` 的 Vulkan shader 段增加：

```cmake
set(VULKAN_VISIBILITY_COMP_SPV "${VULKAN_SHADER_BINARY_DIR}/vulkan_visibility.comp.spv")

add_custom_command(
    OUTPUT "${VULKAN_VISIBILITY_COMP_SPV}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${VULKAN_SHADER_BINARY_DIR}"
    COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "--target-env=${FERENDER_VULKAN_SHADER_TARGET_ENV}"
            "${VULKAN_SHADER_SOURCE_DIR}/vulkan_visibility.comp"
            -o "${VULKAN_VISIBILITY_COMP_SPV}"
    DEPENDS "${VULKAN_SHADER_SOURCE_DIR}/vulkan_visibility.comp"
    VERBATIM
)
```

并加入 `FERenderVulkanShaders` 依赖。

---

## 10. 同步与 Barrier

GPU-driven 路径必须显式处理 compute 写入到 graphics 读取的同步。

如果仍使用 Vulkan 1.0 风格 barrier：

```cpp
VkBufferMemoryBarrier barriers[] = {
    // visible index buffer: shader write -> index read
    {
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_INDEX_READ_BIT,
        .buffer = visibleIndexBuffer,
        .offset = 0,
        .size = VK_WHOLE_SIZE
    },
    // indirect command buffer: shader write -> indirect command read
    {
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
        .buffer = indirectCommandBuffer,
        .offset = 0,
        .size = VK_WHOLE_SIZE
    }
};

vkCmdPipelineBarrier(
    commandBuffer,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
    0,
    0, nullptr,
    barrierCount, barriers,
    0, nullptr);
```

如果项目后续统一到 `VK_KHR_synchronization2`，再切到 `vkCmdPipelineBarrier2`。

注意：

1. `vkCmdFillBuffer` 重置 indirect/counter 后，也需要 transfer write -> compute shader read/write 的 barrier。
2. 若 compute 和 graphics 使用不同 queue family，需 queue ownership transfer；第一版避免这个复杂度。
3. 每帧 `visibleIndexResource` 容量按最大三角形数预分配，不在帧内重新创建。

---

## 11. 拾取路径演进

### 11.1 第一版（已完成历史阶段）

该阶段拾取仍走传统 path：

```text
renderPickFrame()
  -> 使用当前 CPU 过滤后的 meshResources_
```

这曾意味着 GPU-driven 主视图和 pick 可能在极端情况下短暂不同步。
当前代码已推进到第二版，pick pass 不再需要依赖传统过滤资源。

### 11.2 第二版（当前基础实现）

pick pass 复用 GPU visible index buffer：

```text
visibility compute
  -> visibleIndexBuffer
main pass
  -> draw indirect visibleIndexBuffer
pick pass
  -> draw indirect visibleIndexBuffer 到 pick framebuffer
```

这样显示和拾取天然一致。
当前 pick command buffer 会在 render pass 前录制 visibility compute，然后用
`vkCmdDrawIndexedIndirect` 把同一份 `visibleIndexResource` 绘制到 pick framebuffer。
全隐藏时 pick pass 仍成功执行，但读回颜色为 0，对外返回 `elementId = -1`。

### 11.3 第三版

如果后续 fragment shader 按 `elementId` 或 `triangleId` 动态生成 pick color，
可以减少顶点中的 `pickColor[3]` 冗余。

---

## 12. 边线与点模式

边线的拓扑和三角面不同，不能直接复用三角形 visible index。当前基础版已新增独立
edge metadata、visible edge index buffer 和 edge indirect command，并由同一次
visibility compute dispatch 同时筛三角形和边线。

当前状态：

1. 主三角面 GPU-driven 已完成基础版。
2. part/hidden 显隐 SSBO 已驱动三角面、V1/V2 点模式和 edge metadata 边线过滤。
3. `Wireframe` / `SolidWireframe` 可使用 visible edge index + edge indirect 绘制。
4. V2 点模式已改为按需 unique visible source-vertex point draw。

边线资源很大时，当前已有 `kMaxInteractiveEdgeIndices` 限制。
GPU-driven 后也应保留类似上限，避免线框成为大模型瓶颈。

---

## 13. Mesh Shader 后续路线

mesh shader 不是第一阶段必需，但可以作为第三阶段高级路径：

```text
FE mesh triangles
  -> CPU 或 GPU 分组为 meshlet
  -> meshlet metadata + compressed vertex/index
  -> task shader 做 meshlet culling/LOD
  -> mesh shader 输出 primitives
```

适合做：

1. meshlet 级视锥裁剪。
2. cone/backface culling。
3. LOD。
4. 减少 vertex/index bandwidth。

不适合首版的原因：

1. 需要 meshlet 构建和资产布局重做。
2. Vulkan/MoltenVK/老硬件支持不稳定。
3. pick、云图、边线、part 显隐都要重新接入。
4. 当前项目的收益点首先是减少 CPU 重建 mesh，而 indirect compute 已能覆盖。

---

## 14. Device Generated Commands 与 Work Graphs 类能力

### 14.1 `VK_EXT_device_generated_commands`

该扩展适合在 GPU 上生成更复杂的命令序列，可以作为更远期优化。
本项目第一阶段不依赖它，原因是：

1. 基础目标只需要一个 indexed indirect draw。
2. Vulkan 标准 indirect draw 已足够表达。
3. 扩展支持面和调试复杂度高于收益。

### 14.2 `VK_AMDX_shader_enqueue`

该扩展更接近 D3D12 Work Graphs，可由 shader enqueue compute/mesh work。
但它仍是 provisional，不应进入生产主路径。

项目策略：

```text
主路径：compute + indirect draw
增强路径：mesh shader
实验路径：device generated commands / shader enqueue
```

---

## 15. 能力探测

在 `VulkanDevice` 增加内部 capability 结构：

```cpp
struct VulkanDeviceCapabilities {
    bool graphicsQueueSupportsCompute = false;
    bool drawIndirectFirstInstance = false;
    bool multiDrawIndirect = false;
    bool drawIndirectCount = false;
    bool meshShader = false;
    bool taskShader = false;
    bool deviceGeneratedCommands = false;
    bool shaderEnqueue = false;
};
```

第一版 GPU-driven indirect 必需：

1. graphics queue family 支持 `VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT`。
2. 支持 storage buffer。
3. 支持 indirect draw。
4. 支持 shader atomic add on SSBO uint。

可选：

1. `multiDrawIndirect`：后续多 draw 分桶。
2. `drawIndirectCount`：后续 compact 多命令。
3. `VK_EXT_mesh_shader`：后续 mesh shader。
4. `VK_EXT_device_generated_commands`：后续增强。
5. `VK_AMDX_shader_enqueue`：实验。

---

## 16. 分阶段实施计划

当前实现进度：

- P0 已完成：设置和 UI 已接入；GPU-driven Indirect 已作为 Vulkan 默认绘制策略，
  Traditional 保留为手动兼容回退，Mesh Shader 仍禁用预留。
- P1 已完成：已新增 GPU-driven 上传构建器和资源组，可上传完整 surface vertex、triangle metadata、part state、hidden element、visible index、indirect command 和 scalar buffer。
- P2 已完成基础版：`vulkan_visibility.comp` 已接入 compute descriptor/pipeline，并在主 mesh frame command buffer 内重置 indirect command、dispatch visibility compute、写入 visible index buffer。
- P3 已完成基础版：主三角面可在隐藏策略强制开启时通过 `vkCmdDrawIndexedIndirect` 绘制；overlay、selection、axes 仍沿用传统资源。
- 动态状态更新已完成内部入口：`updateGpuDrivenVisibilityState()` 可只更新 part state、hidden element 和 visibility uniform 小 buffer，不重传完整 vertex/triangle metadata。
- `VulkanViewport` 已在 `Solid` / `Points` 以及具备 surface+edge 数据的 `Wireframe` / `SolidWireframe` 模式下接入 GPU-driven 小状态更新热路径；线框-only 或缺少 edge metadata 的模型仍会回到传统过滤资源。
- P4 已完成基础版：pick pass 可复用 GPU-driven visible index buffer 和 indirect command，隐藏部件不会被拾取。
- P5 已完成基础版：V1 Points 复用 surface visible index buffer 和 indirect command；V2 Points 使用独立 visible point index buffer、point indirect command 和 source-vertex flag 去重，通过 source-vertex point pipeline 绘制，且仅在 Points 模式生成 visible point list，不再上传 CPU point buffer；边线已新增 edge metadata、visible edge index buffer 和 edge indirect command，Wireframe/SolidWireframe 可按 part/element 状态在 compute pass 中筛选。
- V2 上传/绘制已完成基础版：GPU-driven 上传会准备 source-vertex 数据，surface/pick 优先使用 V2 visibility compute variant 和无 vertex input 的 V2 graphics/pick pipeline；V2 默认路径会跳过 V1 展开 surface 资源和传统 mesh surface 上传，V2 不可用时保留 V1 GPU-driven fallback。
- 可用化前置已完成基础版：Vulkan 后端会检查 graphics queue compute 能力，shader/pipeline/descriptor 或 GPU-driven 资源创建失败时自动回退传统路径；diagnostics 会显示 requested/effective/actual 策略、GPU-driven 计数、sourceV2/v2 状态、CPU surface/point 保留数量、fallback 原因、fallback 次数、visibility dispatch 次数、frustum culling 状态、可见三角/点/边数量、visibility compute GPU timestamp 耗时和 CPU 侧 upload/update/render/pick 耗时。

### P0：整理 Vulkan mesh 类型和策略开关

目标：

1. 增加 `VulkanDrawStrategy`。
2. 增加 capability 探测和日志。
3. 把 `VulkanMeshVertex/VulkanLineVertex` 移出匿名命名空间，建立共享类型头。
4. 不改变渲染结果。

验收：

1. Vulkan 传统路径测试通过。
2. OpenGL/Metal 不受影响。

### P1：GPU-driven 资源组和上传构建器

目标：

1. 新增 `VulkanGpuDrivenMeshResources`。
2. 新增 `VulkanGpuDrivenUploadBuilder`。
3. 从 `Mesh + VulkanMeshUploadOptions` 构建完整 vertex、triangle metadata、part state。
4. 创建 visible index 和 indirect command buffer。

验收：

1. 加载模型后资源创建成功。
2. GPU-driven 关闭时不影响传统路径。

### P2：Visibility compute pass

目标：

1. 新增 `vulkan_visibility.comp`。
2. 新增 compute descriptor/pipeline。
3. 每帧 reset indirect command。
4. dispatch 后生成 visible index buffer。

验收：

1. 全部 part 可见时，visible index count 等于传统 index count。
2. 隐藏 part 后，visible index count 下降且无越界。

### P3：Indirect graphics pass

目标：

1. 新增 `VulkanIndirectMeshFramePass`。
2. 使用 `vkCmdDrawIndexedIndirect` 绘制主三角面。
3. overlay、selection、axes 继续传统录制。

验收：

1. 主模型显示与传统路径一致。
2. 部件显隐无需重传大 mesh。
3. resize、连续加载模型稳定。

### P4：拾取一致性

目标：

1. pick pass 复用 visible index buffer。（已完成基础版）
2. GPU-driven 主视图和 pick 结果一致。（已完成基础版）

验收：

1. 隐藏部件无法被拾取。
2. 点选/框选结果与当前显示一致。

### P5：边线/点模式 GPU-driven

目标：

1. edge metadata + visible edge index buffer。（已完成基础版）
2. 线框模式按 part/element GPU 筛选。（已完成基础版）
3. 点模式按可见 source vertex 或可见 triangle 派生。（V1 已完成可见 triangle 派生版；V2 已完成按需 unique visible source vertex，且不再上传 CPU point buffer）

验收：

1. Solid/Wireframe/SolidWireframe/Points 语义一致。（已完成基础测试）
2. 大模型边线仍有上限保护。

### P6：Mesh shader 实验路径

目标：

1. 探测 `VK_EXT_mesh_shader`。
2. 引入 meshlet 数据结构。
3. 实验 task/mesh shader culling。

验收：

1. 默认关闭。
2. 不影响 indirect 主路径。

---

## 17. 测试计划

### 17.1 单元测试

建议新增：

| 测试 | 目的 |
|------|------|
| `test_vulkan_gpu_driven_upload_builder` | 验证 metadata、part state、最大 index 数构建正确 |
| `test_vulkan_draw_strategy` | 验证配置解析和 fallback 策略 |
| `test_vulkan_device_capabilities` | 验证 capability 结构默认值和特性映射 |

### 17.2 集成测试

建议扩展现有 Vulkan smoke test：

1. 启动 Vulkan 后端。
2. 上传 `hex_cube.bdf` 或 `hex_row.bdf`。
3. 传统路径截图/拾取基线。
4. GPU-driven 路径截图/拾取比对。
5. 连续切换 part visibility。
6. resize 后继续绘制和拾取。

当前已新增默认 CTest `vulkan_gpu_driven_stress`，在 macOS Vulkan triangle pipeline
可用时运行。该测试会启用 `GpuDrivenIndirect`，上传 30x30 grid，循环切换
Solid/Wireframe/SolidWireframe/Points、part visibility、hidden element、全隐藏
pick、swapchain recreate + reupload，并校验诊断保持 `actual=GPU-driven Indirect`、
`fallbacks=0`、`dispatches` 持续递增、`lastFrameGpu=1`、`lastPickGpu=1`、
`frustum=1`，并检查全隐藏时 GPU readback 的 `visibleTris=0` / `visibleEdges=0`。

### 17.3 性能指标

记录：

1. `uploadMesh()` 耗时。
2. part visibility 切换耗时。
3. 每帧 CPU command recording 耗时。
4. GPU visibility compute 耗时。
5. visible triangle count。
6. draw call 数量。

当前 GPU-driven 诊断已接入轻量 timestamp query；如果设备不支持 graphics/compute
timestamp，渲染继续执行，只保留 CPU 侧耗时和可见数量。

当前已新增手动 benchmark target，不加入默认 `ctest`：

```bash
cmake --build build-qt6 --target benchmark_vulkan_gpu_driven
./build-qt6/benchmark_vulkan_gpu_driven
./build-qt6/benchmark_vulkan_gpu_driven 20 40 80 160
```

输出为 CSV，默认比较 `Traditional`、`GpuDrivenIndirectV1` 与
`GpuDrivenIndirectV2`，字段包含网格规模、V1/V2 静态 surface bytes、V2 节省比例、
初次上传耗时、GPU-driven 专用上传耗时、显隐更新耗时、平均 render frame 耗时、离屏
pick frame 耗时、读回 pick 耗时、visibility compute GPU timestamp 耗时、可见三角/点/边
数量、V2 active/source 计数、CPU surface/point 保留数量、actual strategy、fallback 次数、
visibility dispatch 次数和完整 `renderDiagnostics()`。

首份本机基线已保存到
`docs/perf/2026-05-24_Vulkan_GPU_Driven_Benchmark_Apple_M4_MoltenVK.md`。

上传结构 V2 方案已补充到 `docs/Vulkan_GPU_Driven_Upload_V2.md`。当前 V2 已完成
CPU 侧 source-vertex builder、sidecar 上传、visibility compute variant、mesh/point/pick
pipeline 和默认启用路径；M4/MoltenVK benchmark 显示 V2 静态 surface bytes 较 V1 少约
`60%`，`320` grid 总上传约 `43.734ms`，且 `cpuSurface=0`、`cpuPoints=0` 验证传统
mesh surface 和 CPU point 上传已跳过；V2 点模式已从可见 triangle corner 重复点
降为按需 unique source vertex，benchmark 的 pick 路径也已拆成离屏帧计时和读回计时。

当前还新增了手动 soak target，不加入默认 `ctest`：

```bash
cmake --build build-qt6 --target soak_vulkan_gpu_driven
./build-qt6/soak_vulkan_gpu_driven
./build-qt6/soak_vulkan_gpu_driven 600 160
```

参数为 `iterations divisions`，默认 `240 80`。soak 会连续切换显示模式、part/element
显隐、pick 和周期性 swapchain recreate，校验 GPU-driven 不 fallback、dispatch 递增、
全隐藏时 visible count 为 0。

---

## 18. 风险与对策

| 风险 | 表现 | 对策 |
|------|------|------|
| macOS/MoltenVK 能力差异 | compute/indirect 行为或扩展不可用 | runtime capability 探测，fallback traditional |
| SSBO 原子追加顺序不稳定 | 可见 index 顺序不等于传统路径 | 允许顺序不同；拾取依赖 elementId/pickColor，不依赖 triangle 顺序 |
| hidden element 查询慢 | 隐藏列表大时 compute 变慢 | 第二版改 dense bitset |
| visible index buffer 越界 | metadata 或容量计算错误 | 容量固定为 `triangleCount * 3`，shader 边界检查，debug validation |
| 主视图和 pick 不一致 | GPU-driven 状态未同步到 pick pass | pick pass 复用 visible index buffer，并在自身 command buffer 内重跑 visibility compute |
| 代码继续集中到 `VulkanClearFrameRenderer` | 文件过大难维护 | 新增 pass/resource/builder 类，renderer 只调度 |
| shader 和 C++ layout 不一致 | 渲染错乱 | 统一 types 文档，使用 `static_assert(sizeof)` 和 offset 检查 |

---

## 19. 对公开 API 和文档的影响

第一版不新增 `FERENDER_EXPORT` 公开类/函数，不需要修改 `docs/FERender_API.md`。

如果后续暴露用户可选策略，例如：

```cpp
void RenderViewport::setVulkanDrawStrategy(...);
```

则必须同步更新：

1. `docs/FERender_API.md`
2. `docs/Implemented_Features_Checklist.md`
3. 示例或配置说明

---

## 20. 推荐首个 PR 范围

首个 PR 不直接做完整 GPU-driven 绘制，建议只做 P0 + P1：

1. 新增 `VulkanDrawStrategy` 内部枚举。
2. 新增 `VulkanGpuDrivenMeshResources` 空资源组。
3. 新增 `VulkanGpuDrivenUploadBuilder`，只在 CPU 侧构建并测试 metadata。
4. 增加单元测试，不改变实际渲染路径。

这样可以先把数据结构和可维护边界固定下来，再接 shader 和 command buffer。

---

## 21. 参考资料

1. Vulkan `vkCmdDrawIndexedIndirect` 和 `VkDrawIndexedIndirectCommand`：
   <https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDrawIndexedIndirect.html>
2. Vulkan `VK_EXT_mesh_shader`：
   <https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_mesh_shader.html>
3. Khronos Vulkan mesh shader culling sample：
   <https://docs.vulkan.org/samples/latest/samples/extensions/mesh_shader_culling/README.html>
4. Vulkan `VK_EXT_device_generated_commands` proposal：
   <https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_device_generated_commands.html>
5. Vulkan `VK_AMDX_shader_enqueue` refpage：
   <https://docs.vulkan.org/refpages/latest/refpages/source/VK_AMDX_shader_enqueue.html>
6. NVIDIA D3D12 Work Graphs 文章：
   <https://developer.nvidia.com/blog/advancing-gpu-driven-rendering-with-work-graphs-in-direct3d-12/>
