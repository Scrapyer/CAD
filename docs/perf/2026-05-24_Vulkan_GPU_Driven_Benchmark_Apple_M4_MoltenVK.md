# Vulkan GPU-driven benchmark 基线：Apple M4 / MoltenVK

日期：2026-05-24
设备：Apple M4
Vulkan API：1.4.334
构建：`build-qt6`，Qt 6.8.3，macOS Vulkan / MoltenVK

## 命令

```bash
cmake --build build-qt6 --target benchmark_vulkan_gpu_driven soak_vulkan_gpu_driven
./build-qt6/benchmark_vulkan_gpu_driven
./build-qt6/soak_vulkan_gpu_driven 80 80
```

benchmark 默认规模：`20, 40, 80, 160, 320`。每个规模分别跑 Traditional、
`GpuDrivenIndirectV1` 和 `GpuDrivenIndirectV2`。V1 通过内部环境变量
`FEMODELVIEWER_VULKAN_GPU_DRIVEN_DISABLE_V2=1` 强制关闭 V2；V2 为当前
GPU-driven 默认路径。显隐更新场景为隐藏 part 1 和 part 3，约保留 50% 三角形。

## benchmark 汇总

| Strategy | Divisions | Triangles | Edges | V1 surface bytes | V2 surface bytes | V2 savings | Upload ms | GPU upload ms | Visibility update ms | Render avg ms | Pick frame avg ms | Pick readback ms | Visibility GPU ms | Visible triangles | Visible points | Visible edges | V2 active | Source V2 | CPU surface | CPU points | Fallbacks |
|----------|-----------|-----------|-------|------------------|------------------|------------|-----------|---------------|----------------------|---------------|-------------------|------------------|-------------------|------------------|----------------|---------------|-----------|-----------|-------------|------------|-----------|
| Traditional | 20 | 800 | 80 | 176000 | 72368 | 58.88% | 3.248 | 0.00 | 4.690 | 5.017 | 0.433 | 2.362 | 0.0000 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| GPU-driven V1 | 20 | 800 | 80 | 176000 | 72368 | 58.88% | 5.383 | 3.98 | 0.026 | 1.491 | 1.128 | 1.254 | 0.0258 | 400 | 0 | 40 | 0 | 0 | 2400 | 441 | 0 |
| GPU-driven V2 | 20 | 800 | 80 | 176000 | 72368 | 58.88% | 4.320 | 3.59 | 0.030 | 1.284 | 1.178 | 0.677 | 0.0157 | 400 | 0 | 40 | 1 | 441 | 0 | 0 | 0 |
| Traditional | 40 | 3200 | 160 | 704000 | 285488 | 59.45% | 3.072 | 0.00 | 6.582 | 0.433 | 0.962 | 1.150 | 0.0000 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| GPU-driven V1 | 40 | 3200 | 160 | 704000 | 285488 | 59.45% | 6.417 | 3.33 | 0.047 | 1.552 | 1.198 | 0.559 | 0.0234 | 1600 | 0 | 80 | 0 | 0 | 9600 | 1681 | 0 |
| GPU-driven V2 | 40 | 3200 | 160 | 704000 | 285488 | 59.45% | 7.581 | 3.47 | 0.050 | 1.158 | 1.986 | 0.657 | 0.0269 | 1600 | 0 | 80 | 1 | 1681 | 0 | 0 | 0 |
| Traditional | 80 | 12800 | 320 | 2816000 | 1134128 | 59.73% | 7.062 | 0.00 | 10.386 | 5.196 | 0.662 | 0.619 | 0.0000 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| GPU-driven V1 | 80 | 12800 | 320 | 2816000 | 1134128 | 59.73% | 16.100 | 3.73 | 0.111 | 5.243 | 1.329 | 0.917 | 0.1103 | 6400 | 0 | 160 | 0 | 0 | 38400 | 6561 | 0 |
| GPU-driven V2 | 80 | 12800 | 320 | 2816000 | 1134128 | 59.73% | 8.247 | 3.22 | 0.104 | 5.356 | 1.240 | 0.614 | 0.0210 | 6400 | 0 | 160 | 1 | 6561 | 0 | 0 | 0 |
| Traditional | 160 | 51200 | 640 | 11264000 | 4521008 | 59.86% | 22.638 | 0.00 | 25.294 | 5.722 | 1.105 | 0.438 | 0.0000 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| GPU-driven V1 | 160 | 51200 | 640 | 11264000 | 4521008 | 59.86% | 44.613 | 4.81 | 0.326 | 5.881 | 0.856 | 0.632 | 0.0519 | 25600 | 0 | 320 | 0 | 0 | 153600 | 25921 | 0 |
| GPU-driven V2 | 160 | 51200 | 640 | 11264000 | 4521008 | 59.86% | 15.014 | 3.85 | 0.315 | 4.907 | 1.274 | 1.951 | 0.2111 | 25600 | 0 | 320 | 1 | 25921 | 0 | 0 | 0 |
| Traditional | 320 | 204800 | 1280 | 45056000 | 18053168 | 59.93% | 86.525 | 0.00 | 97.374 | 4.568 | 1.870 | 1.236 | 0.0000 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| GPU-driven V1 | 320 | 204800 | 1280 | 45056000 | 18053168 | 59.93% | 167.013 | 8.66 | 1.134 | 11.070 | 2.895 | 0.869 | 0.1356 | 102400 | 0 | 640 | 0 | 0 | 614400 | 103041 | 0 |
| GPU-driven V2 | 320 | 204800 | 1280 | 45056000 | 18053168 | 59.93% | 43.734 | 5.83 | 1.137 | 6.564 | 1.591 | 0.848 | 0.1456 | 102400 | 0 | 640 | 1 | 103041 | 0 | 0 | 0 |

## 关键观察

- V2 静态 surface bytes 比 V1 展开顶点结构少约 `59% - 60%`。`320` 网格从
  `45056000` bytes 降到 `18053168` bytes。
- V2 已经实际启用：所有 V2 case 的 `v2=1`，`sourceV2` 等于 source vertex count；
  V1 case 的 `v2=0`，`sourceV2=0`。V2 case 的 `cpuSurface=0`、`cpuPoints=0`，
  表示传统 surface vertex/index/scalar 和 CPU point buffer 都已跳过。
- V2 点模式已改为按需 unique visible source vertex。当前表格的 render/pick 路径是
  `SolidWireframe` + pick，因此 `visiblePoints=0`，不会为非 Points 模式支付点去重原子写入。
  Points 模式切换时仍会生成 unique source vertex 点列表。
- 跳过传统 surface 后，总 `upload_ms` 明显下降：`320` 网格 V2 从上一轮约
  `187ms` 降到 `43.734ms`，低于本轮 Traditional 的 `86.525ms`，也显著低于
  V1 的 `167.013ms`。
- V2 的 GPU-driven 专用上传耗时在大规模下低于 V1：`320` 网格 V1 为 `8.66ms`，
  V2 为 `5.83ms`。小规模 case 受 MoltenVK/调度噪声影响，不单独解读。
- 显隐更新收益仍然明显：`320` 网格下 Traditional 重新过滤/上传耗时 `97.374ms`；
  GPU-driven 只更新 part/hidden 小 buffer，V1 为 `1.134ms`，V2 为 `1.137ms`。
- visibility compute 的 GPU timestamp 很低且随规模平滑增长，`320` 网格 V1/V2 约
  `0.14ms` 量级。
- `pick_frame_avg_ms` 是离屏 pick frame 的同步绘制耗时，不含像素读回；`pick_ms`
  是 `pickElementAt()` 的读回拾取路径。`render_avg_ms` 仍包含 swapchain
  acquire/present、command record 和 MoltenVK 行为；当前数据不应单独作为最终帧率结论。
- 所有 GPU-driven case 的 `fallbacks=0`，`visibleTriangles`、`visiblePoints` 和
  `visibleEdges` 符合 50% part visibility 场景。

## soak 样本

命令：

```bash
./build-qt6/soak_vulkan_gpu_driven 80 80
```

结果：

| Iterations | Divisions | Triangles | Edges | Recreates | Elapsed ms | Max visible triangles | Max visible points | Max visible edges | Fallbacks | Dispatches | Timestamps |
|------------|-----------|-----------|-------|-----------|------------|-----------------------|--------------------|-------------------|-----------|------------|------------|
| 80 | 80 | 12800 | 320 | 1 | 678.967 | 12800 | 0 | 320 | 0 | 160 | 160 |

soak 连续切换 Solid/Wireframe/SolidWireframe/Points、part visibility、hidden element、
全隐藏 pick 和 swapchain recreate。最终诊断保持 `actual=GPU-driven Indirect`、
`fallbacks=0`、`lastFrameGpu=1`、`lastPickGpu=1`、`frustum=1`、`timestamp=1`。

## 下一步判断

1. 保留 GPU-driven 作为显隐热路径优先优化对象，收益已经确定。
2. pick 路径已经拆出离屏帧计时和读回计时；下一步优先补主 mesh render 的离屏或固定
   present benchmark，减少 swapchain/present 噪声。
3. V2 point flag 原子去重已拆成 Points 模式按需生成；后续可单独补 Points 模式
   benchmark，观察超大节点数下的点绘制成本。
4. 在 Windows MinGW/MSVC + 原生 Vulkan 上重复同一 benchmark，确认 MoltenVK 之外的收益曲线。
