# Source Layout

源码按职责分层放置，公开头文件安装后仍平铺到 `include/FERender`。

| 目录 | 说明 |
|------|------|
| `app/` | FEModelViewer GUI 应用入口、主窗口和面板 |
| `common/` | 主题和应用状态等共享轻量结构 |
| `data/` | FEM 纯数据结构与结果数据结构 |
| `io/` | INP/BDF/OP2/UNV 文件解析 |
| `convert/` | FEModel 到渲染 Mesh 的转换 |
| `rhi/` | 渲染后端抽象和具体图形 API 后端（OpenGL，可选 Vulkan，未来 QRhi） |
| `render/` | 渲染控件、相机、几何网格和拾取数据 |
| `post/` | 结果映射、变形、动画、探针、过滤和等值面 |
