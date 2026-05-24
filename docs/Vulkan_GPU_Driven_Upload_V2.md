# Vulkan GPU-driven 上传结构 V2 方案

## 1. 背景

当前 GPU-driven V1 为了复用现有 `vulkan_mesh.vert` / `vulkan_pick.vert`
顶点输入，上传时会把每个三角形展开为 3 个独立顶点：

```text
source vertex count ~= (N + 1)^2
expanded vertex count = triangle count * 3
```

在 Apple M4 / MoltenVK 基线中，V1 的 `320` grid GPU-driven 初次上传约
`167.013ms`，V2 跳过传统 surface 和 CPU point buffer 后约 `43.734ms`，
Traditional 约 `86.525ms`。V2 静态 surface
bytes 从 `45056000` bytes 降到 `18053168` bytes，且诊断中 `cpuSurface=0`，表示
传统 surface vertex/index/scalar 已不再上传；`cpuPoints=0` 表示点模式也不再需要 CPU
point buffer。显隐更新仍保持 `1.2ms` 左右。

## 2. V2 目标

1. surface vertex 不再按三角形展开，改为 source vertex 只存一份。
2. triangle metadata 继续按三角形存 element id、part id、source indices 和 bounds。
3. visibility compute 输出 draw corner id，而不是展开顶点 index。
4. graphics shader 用 draw corner id 反查 triangle metadata 和 source vertex。
5. part color、pick color、element id 保持 per-triangle 语义，不塞回共享 source vertex。
6. edge 路径保持当前结构；边线本来已经按 source edge vertex 存储，收益不大。

## 3. 数据布局

### 3.1 Source Vertex

```cpp
struct VulkanGpuDrivenSourceVertex {
    float position[4]; // xyz + 1.0，按 std430 vec4 对齐
    float normal[4];   // xyz + 0.0，按 std430 vec4 对齐
    float color[3];   // 仅用于 useVertexColor=true 的源顶点颜色
    float scalar;
};
```

source vertex 只表达“节点/渲染源顶点属性”。如果没有启用 `useVertexColor`，shader 应使用
triangle 的 part id 去 `PartState` 取颜色，source vertex 的 `color` 只作为 object color
fallback。

### 3.2 Triangle Metadata

继续复用现有 `VulkanGpuDrivenTriangleMeta`：

```cpp
struct VulkanGpuDrivenTriangleMeta {
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

V1 中 `index0/1/2` 是展开顶点 index；V2 中它们是 source vertex index。

### 3.3 Visible Index Buffer

V1：

```glsl
visibleIndices[offset + 0] = meta.indices.x; // expanded vertex index
visibleIndices[offset + 1] = meta.indices.y;
visibleIndices[offset + 2] = meta.indices.z;
```

V2：

```glsl
uint cornerBase = triangleIndex * 3;
visibleIndices[offset + 0] = cornerBase + 0;
visibleIndices[offset + 1] = cornerBase + 1;
visibleIndices[offset + 2] = cornerBase + 2;
```

`gl_VertexIndex` 会成为 draw corner id：

```glsl
uint cornerId = uint(gl_VertexIndex);
uint triangleId = cornerId / 3;
uint corner = cornerId % 3;
TriangleMeta tri = triangles[triangleId];
uint sourceIndex = corner == 0 ? tri.index0 : (corner == 1 ? tri.index1 : tri.index2);
SourceVertex vertex = sourceVertices[sourceIndex];
```

这样 visible index buffer 仍然可用 `vkCmdDrawIndexedIndirect`，但不再要求展开 vertex buffer。

## 4. Shader / Pipeline 变化

V2 不能继续直接复用当前 mesh/pick pipeline 的 vertex attribute：

- 当前 mesh pipeline 读取 `location 0/1/2` 的 position/normal/color。
- 当前 pick pipeline 读取 `location 0/3` 的 position/pickColor。
- V2 shader 需要通过 storage buffer 读取 source vertex、triangle metadata、part state。

建议新增独立 pipeline：

| Pipeline | 用途 |
|----------|------|
| `gpuDrivenMeshPipeline` | surface / point 的 V2 shader-storage-buffer draw |
| `gpuDrivenPickPipeline` | pick pass 的 V2 shader-storage-buffer draw |

descriptor set 建议：

| Binding | 资源 | Shader stage |
|---------|------|--------------|
| 0 | source vertex SSBO | vertex |
| 1 | triangle metadata SSBO | vertex |
| 2 | part state SSBO | vertex/fragment |
| 3 | scalar/contour params 或保留 | vertex/fragment |

`VisibilityComputePass` 的 descriptor 已扩展为同时写 surface、edge 和 unique point
draw 资源。V2 点模式会使用 source-vertex flag buffer 去重，再输出 visible point index
buffer 和独立 point indirect command。

## 5. Pick Color

V1 把 pick color 写进每个展开顶点。V2 不再存 pick color，pick shader 直接用
`TriangleMeta.elementId` 编码：

```glsl
int encoded = elementId + 1;
vec3 pickColor = vec3(
    float(encoded & 0xFF) / 255.0,
    float((encoded >> 8) & 0xFF) / 255.0,
    float((encoded >> 16) & 0xFF) / 255.0);
```

这保持 Element/Part pick 和 visible triangle set 一致。

## 6. 迁移步骤

### P0：CPU Builder 和测试

已新增 `buildVulkanGpuDrivenUploadV2Data()`。V2 路径会直接构建 source-vertex
数据和 common metadata，不再为了默认 V2 上传额外构建 V1 展开 surface。
单元测试验证：

1. source vertex count 等于原始 source vertex count。
2. triangle metadata 的 `index0/1/2` 保存 source index。
3. bounds、part state、hidden element、edge metadata 与 V1 语义一致。
4. V2 静态 surface bytes 小于 V1 展开顶点布局。

### P1：V2 资源组

新增/扩展资源：

```text
sourceVertexResource
triangleMetaV2Resource
partStateResource
visibleIndexResource
indirectCommandResource
visiblePointIndexResource
pointIndirectCommandResource
visiblePointFlagResource
scalar/source descriptor
```

已新增 `uploadVulkanGpuDrivenMeshV2SidecarResources()` 和 V2-only 上传入口。V2-only
入口会直接上传 source vertex、V2 triangle metadata、edge metadata、part state、
hidden element、visible index、surface/point indirect command、visible point index、
visible point flag 和 readback/uniform 资源，并记录
`sourceVertexCount`、`triangleV2Count`、`maxVisibleIndexV2Count`、`staticSurfaceV2Bytes`。
V2 默认启用时不会上传 V1 的展开 surface vertex、triangle metadata 和 scalar
descriptor；V1 路径仍可通过内部开关启用，用于回退验证和 benchmark 对比。

### P2：Visibility Compute V2

增加 shader variant 或 push constant flag：

- V1 输出 expanded vertex indices。
- V2 输出 draw corner ids。

已新增独立 `vulkan_visibility_v2.comp` 编译目标。V2 surface 输出 draw-corner id，
即第 N 个三角形输出 `N * 3 + 0/1/2`，由 V2 vertex shader 再按 triangle metadata
取 source vertex。V2 point 输出 unique source vertex index：可见三角形命中后用
source-vertex flag buffer 做 `atomicExchange` 去重，首次命中的 source vertex 写入
visible point index buffer，并递增独立 point indirect command。

### P3：Graphics / Pick Pipeline V2

新增：

```text
vulkan_gpu_driven_mesh.vert
vulkan_gpu_driven_point.vert
vulkan_gpu_driven_pick.vert
```

使用 storage buffer fetch，不绑定 surface vertex input。fragment shader 可继续复用现有 mesh/pick
fragment 逻辑，或先复制一份保持风险可控。

已新增三个 vertex shader 编译目标，并已创建 V2 surface descriptor layout、
`gpuDrivenMeshV2` / `gpuDrivenPointV2` / `gpuDrivenPickV2` graphics pipeline，以及 frame/pick pass 的 V2
descriptor 绑定入口。当前 GPU-driven Indirect 会优先启用 V2 surface/point/pick；如果 V2
sidecar、descriptor、graphics pipeline 或 visibility V2 compute variant 不完整，会回到 V1。

### P4：Benchmark 对比

新增 benchmark strategy：

```text
Traditional
GpuDrivenIndirectV1
GpuDrivenIndirectV2
```

至少记录：

1. upload ms
2. static surface bytes
3. visibility update ms
4. render avg ms
5. pick frame avg ms
6. pick readback ms
7. visibilityGpuMs
8. visibleTris / visiblePoints / visibleEdges

已完成基础对比。`benchmark_vulkan_gpu_driven` 现在输出 Traditional、
`GpuDrivenIndirectV1`、`GpuDrivenIndirectV2` 三种策略，并额外记录
`gpu_v1_surface_bytes`、`gpu_v2_surface_bytes`、`gpu_v2_savings_pct`、
`gpu_upload_ms`、`pick_frame_avg_ms`、`visible_points`、`v2`、`source_v2`、
`cpu_surface` 和 `cpu_points`。Apple M4 / MoltenVK 当前结果显示 V2 静态
surface bytes 少约 `59% - 60%`；`320` grid 的 V2 总 `upload_ms` 约
`43.734ms`，低于 V1 的 `167.013ms` 和 Traditional 的 `86.525ms`。V2
visible point draw 使用按需 unique source vertex：非 Points
模式不生成 visible point list，Points 模式再启用 point flag 去重和 point indirect command。

## 7. 风险

| 风险 | 说明 | 对策 |
|------|------|------|
| shader storage fetch 变慢 | V2 顶点阶段多一次 metadata/source fetch | 用 benchmark 判断上传收益是否抵消 draw 成本 |
| per-triangle 颜色语义错误 | 共享 source vertex 不能直接保存 part color | part color 从 triangle part id 动态取 |
| pick 编码变化 | V1 从 vertex attribute 读 pick color | V2 pick shader 由 element id 动态编码 |
| pipeline 分支增多 | V1/V2 并存增加资源路径 | 先内部 flag，测试稳定后再替换 V1 |
| MoltenVK storage buffer 行为差异 | Apple/MoltenVK 可能和原生 Vulkan 曲线不同 | 保留 Windows 原生 Vulkan benchmark 计划 |

## 8. 当前状态

- V2 CPU builder 和单元测试已完成。
- V2 sidecar resource upload helper 和 V2-only uploader 已挂入运行时，GPU-driven V2
  上传会跳过 V1 展开 surface 资源和传统 mesh surface 资源。
- V2 visibility/mesh/point/pick shader 已加入 CMake 编译脚手架。
- V2 descriptor layout、graphics/point/pick pipeline、visibility compute variant 和 draw/pass 绑定入口已完成。
- GPU-driven Indirect 现在默认优先走 V2 surface/point/pick，并保留 V1 fallback。
- benchmark 已完成 Traditional/V1/V2 对比，确认 V2 静态 surface bytes 少约 `60%`，
  且 `cpuSurface=0`、`cpuPoints=0`。
- V2 点模式已从可见 triangle corner 派生升级为按需 unique visible source vertex，
  减少重复点绘制，并避免非 Points 模式支付 point flag 原子去重成本。
