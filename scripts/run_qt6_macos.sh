#!/usr/bin/env zsh
set -euo pipefail

SCRIPT_DIR="${0:A:h}"
ROOT_DIR="${SCRIPT_DIR:h}"

QT_PREFIX="${QT_PREFIX:-/Users/xiebo/Qt/6.8.3/macos}"
NINJA_DIR="${NINJA_DIR:-/Users/xiebo/Qt/Tools/Ninja}"
BUILD_DIR="${BUILD_DIR:-build-qt6}"
APP_BUNDLE="${ROOT_DIR}/${BUILD_DIR}/FEModelViewer.app"
APP_BIN="${APP_BUNDLE}/Contents/MacOS/FEModelViewer"

export PATH="${QT_PREFIX}/bin:${NINJA_DIR}:$PATH"

if [[ ! -x "${APP_BIN}" ]]; then
    echo "错误: 找不到可执行文件 ${APP_BIN}"
    echo "请先运行: ./scripts/build_qt6_macos.sh"
    exit 1
fi

if [[ "${1:-}" == "--open" ]]; then
    shift
    if [[ $# -gt 0 ]]; then
        open "${APP_BUNDLE}" --args "$@"
    else
        open "${APP_BUNDLE}"
    fi
else
    "${APP_BIN}" "$@"
fi
