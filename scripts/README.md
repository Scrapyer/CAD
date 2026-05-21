# Scripts

这里存放项目常用的本地构建、测试和运行脚本。

## macOS Qt 6.8.3

```bash
# 构建
./scripts/build_qt6_macos.sh

# 清理后重新构建
./scripts/build_qt6_macos.sh --clean

# 运行测试
./scripts/test_qt6_macos.sh

# 启动 GUI
./scripts/run_qt6_macos.sh

# CLI 解析模式
./scripts/run_qt6_macos.sh --parse /path/to/model.op2
```

脚本默认使用：

```bash
QT_PREFIX=/Users/xiebo/Qt/6.8.3/macos
NINJA_DIR=/Users/xiebo/Qt/Tools/Ninja
BUILD_DIR=build-qt6
MACOS_SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk
MACOS_DEPLOYMENT_TARGET=26.0
```

这些值都可以通过环境变量覆盖，例如：

```bash
QT_PREFIX=/path/to/Qt/6.8.3/macos BUILD_DIR=build-local ./scripts/build_qt6_macos.sh
```

## OP2 批量解析

```bash
./scripts/test_op2_batch.sh /path/to/op2/files 10
```
