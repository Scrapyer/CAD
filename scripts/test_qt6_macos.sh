#!/usr/bin/env zsh
set -euo pipefail

SCRIPT_DIR="${0:A:h}"
ROOT_DIR="${SCRIPT_DIR:h}"

BUILD_DIR="${BUILD_DIR:-build-qt6}"

if [[ ! -d "${ROOT_DIR}/${BUILD_DIR}" ]]; then
    echo "错误: 找不到构建目录 ${ROOT_DIR}/${BUILD_DIR}"
    echo "请先运行: ./scripts/build_qt6_macos.sh"
    exit 1
fi

ctest --test-dir "${ROOT_DIR}/${BUILD_DIR}" --output-on-failure "$@"
