# third_party 平台目录

第三方二进制库按平台分区存放：

- `windows/`：Windows 预编译库。
- `macos/`：macOS 预编译库。
- `linux/`：Linux 预编译库，当前预留为空。

## OpenCASCADE

- `windows/opencascade`：Open CASCADE Technology 7.9.3 官方 Windows `vc14` x64 包。
- `macos/opencascade`：Open CASCADE Technology 7.9.3 Homebrew/macOS arm64 包。

Windows 包为 MSVC ABI。CMake 在 MSVC 构建中会启用它；MinGW 构建如果只发现该 `vc14` 包，会自动禁用 OpenCASCADE 导入，避免 C++ ABI 不兼容导致链接或运行问题。

注意：官方 Windows OCCT 本体包不包含全部第三方运行时 DLL。`TKernel.dll` 依赖 `tbb12.dll` 和 `jemalloc.dll`；如果目标机器没有这些 DLL，需要从 Open CASCADE 同版本 release 的 `3rdparty-vc14-64.zip` 补到运行时 `PATH` 或应用输出目录。
