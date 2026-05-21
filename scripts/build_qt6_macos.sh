#!/usr/bin/env zsh
set -euo pipefail

SCRIPT_DIR="${0:A:h}"
ROOT_DIR="${SCRIPT_DIR:h}"

QT_PREFIX="${QT_PREFIX:-/Users/xiebo/Qt/6.8.3/macos}"
NINJA_DIR="${NINJA_DIR:-/Users/xiebo/Qt/Tools/Ninja}"
BUILD_DIR="${BUILD_DIR:-build-qt6}"
MACOS_SDK="${MACOS_SDK:-/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk}"
MACOS_DEPLOYMENT_TARGET="${MACOS_DEPLOYMENT_TARGET:-26.0}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"

if [[ "${1:-}" == "--clean" ]]; then
    rm -rf "${ROOT_DIR}/${BUILD_DIR}"
    shift
fi

export PATH="${QT_PREFIX}/bin:${NINJA_DIR}:$PATH"

cmake -S "${ROOT_DIR}" -B "${ROOT_DIR}/${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_PREFIX_PATH="${QT_PREFIX}" \
    -DCMAKE_OSX_SYSROOT="${MACOS_SDK}" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOS_DEPLOYMENT_TARGET}" \
    "$@"

cmake --build "${ROOT_DIR}/${BUILD_DIR}"
