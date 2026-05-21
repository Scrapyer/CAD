# FEModelViewer

基于 Qt6.8 + OpenGL 4.1 的有限元模型查看器。分为两个模块：
- **FERender**（共享库）：数据层 + 转换层 + 渲染层，可独立安装供其他项目使用
- **FEModelViewer**（GUI 应用）：主窗口 + 各功能面板，链接 FERender

## 构建

```bash
# MinGW 构建（主力）
cmake -B build -G "MinGW Makefiles" -DCMAKE_INSTALL_PREFIX=install
cmake --build build
cmake --install build

# MSVC 构建
cmake -B build-vs -DCMAKE_INSTALL_PREFIX=install-vs
cmake --build build-vs --config Release
cmake --install build-vs --config Release

# macOS Qt 6.8.3 构建
export PATH="/Users/xiebo/Qt/6.8.3/macos/bin:/Users/xiebo/Qt/Tools/Ninja:$PATH"
cmake -B build-qt6 -G Ninja \
  -DCMAKE_PREFIX_PATH=/Users/xiebo/Qt/6.8.3/macos \
  -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0
cmake --build build-qt6
ctest --test-dir build-qt6 --output-on-failure
```

Qt 路径由 CMakeLists.txt 自动探测（C:/Qt 或 D:/Qt），也可手动指定：
`-DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64`

macOS 上当前将部署目标设为 26.0，以匹配 Homebrew Vulkan loader 的构建目标；Qt 6.8.3 的 OpenGLWidgets 仍需要包含 `AGL.framework` 的 CommandLineTools 15.4 SDK 作为 sysroot：
`-DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0`

GLM 通过 FetchContent 自动下载，无需手动安装。

## 架构约定

- 数据层（FEModel/FENode/FEElement/FEField）是纯数据，不依赖 OpenGL
- 转换层（FEMeshConverter）是无状态静态工具类，负责 FEModel → Mesh 三角化
- 渲染层（GLWidget/Camera）负责 GPU 上传和交互
- GUI 面板（*Panel.h）只在应用层，不属于 FERender 库
- 源码按模块放在 `source/` 下：`data`、`io`、`convert`、`render`、`post`、`app`、`common`
- FERender 公开头文件安装后仍平铺到 `include/FERender`，不要在公开示例里暴露源码目录路径

## 代码风格

- C++17，类名 PascalCase，成员变量尾随下划线（`mesh_`、`cam_`）
- 枚举用 `enum class`，头文件用 `#pragma once`
- 公开 API 必须加 `FERENDER_EXPORT` 宏
- 中文注释，保留已有注释风格和 ASCII 框图
- MSVC 编译需 `/utf-8`（已在 CMakeLists.txt 中配置）

## DLL 导出

FERender 编译为共享库。所有公开的 class/struct 需标记 `FERENDER_EXPORT`：
```cpp
#include "ferender_export.h"
class FERENDER_EXPORT MyClass { ... };
```
`ferender_export.h` 由 CMake 的 `GenerateExportHeader` 自动生成到 build 目录。

## 关键数据流

```
用户加载文件 → 解析填充 FEModel
  → FEMeshConverter::toRenderData() → FERenderData (Mesh + 映射表)
    → GLWidget::setMesh() + 设置映射表 → GPU 渲染
      → 鼠标拾取 → 映射表反查 FEM 实体
```

## API 文档同步

每次新增、修改或删除 FERender 公开 API（带 `FERENDER_EXPORT` 的类/结构体/函数，包括 GLWidget、FEField、FEMeshConverter、Camera 等渲染接口的公开方法），**必须同步更新** `docs/FERender_API.md`。

## 提交规范

- 中文提交信息，格式：`类别: 简要描述`
- 类别：feat / fix / refactor / docs / chore
- 不要推送 build/install 产物
