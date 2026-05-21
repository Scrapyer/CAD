# FERender Simple Viewer

这个示例是一个独立 Qt 程序，演示外部项目如何调用安装后的 `FERender` 渲染引擎。

它刻意不使用源码树里的头文件路径，而是通过：

```cmake
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
find_package(FERender REQUIRED CONFIG)
target_link_libraries(simple_viewer PRIVATE
    FERender::FERender
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
)
```

## 构建 FERender 并安装

在仓库根目录执行：

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$PWD/install"
cmake --build build -j 4
cmake --install build
```

## 构建示例

```bash
cmake -S examples/simple_viewer -B build/examples/simple_viewer \
  -DCMAKE_PREFIX_PATH="$PWD/install;/path/to/Qt/6.8.3/macos"
cmake --build build/examples/simple_viewer -j 4
```

如果 Qt6 已经在系统默认搜索路径或当前 `PATH` 中，`CMAKE_PREFIX_PATH` 只写 FERender 安装目录也可以。macOS 本地 Qt 安装示例：

```bash
cmake -S examples/simple_viewer -B build/examples/simple_viewer \
  -DCMAKE_PREFIX_PATH="$PWD/install;/Users/xiebo/Qt/6.8.3/macos" \
  -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0
```

运行：

```bash
./build/examples/simple_viewer/simple_viewer
```

## 调用流程

示例代码展示了最小调用链：

1. 用 `FEModel` 构造一个简单的 HEX8 有限元模型。
2. 调用 `FEMeshConverter::toRenderData(model)` 得到 `FERenderData`。
3. 把 `FERenderData::mesh` 传给 `GLWidget::setMesh()`。
4. 把拾取映射表传给 `setTriangleToElementMap()`、`setVertexToNodeMap()`、`setTriangleToPartMap()` 和 `setEdgeToPartMap()`。
5. 调用 `FEResultMapper::mapScalarToVertices()` 把节点标量场映射为 `GLWidget::setVertexScalars()` 所需数组。
6. 调用 `fitToModel()` 自动适配视角。

关键代码在 `main.cpp` 的 `loadSampleModel()` 和 `showSampleContour()` 中。
