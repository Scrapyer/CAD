#pragma once

#include "RenderBackend.h"

#include <memory>

/** @brief 创建渲染后端实例。 */
FERENDER_EXPORT std::unique_ptr<IRenderBackend> createRenderBackend(
    RenderBackendKind kind = RenderBackendKind::OpenGL);

/** @brief 查询指定后端是否已编译进当前构建。 */
FERENDER_EXPORT bool isRenderBackendAvailable(RenderBackendKind kind);

/** @brief 返回后端名称，用于日志和界面显示。 */
FERENDER_EXPORT const char* renderBackendName(RenderBackendKind kind);
