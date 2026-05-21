#pragma once

#include "RenderBackend.h"
#include "ferender_export.h"

#include <QString>

/**
 * @brief 全局渲染设置。
 *
 * 记录用户首选 RHI，并提供当前构建可用后端的兜底选择。
 * 首选项写入应用目录下的 config/settings.ini，启动时读取，运行时修改下次启动生效。
 */
class FERENDER_EXPORT RenderSettings {
public:
    /** @brief 从 config/settings.ini 读取用户首选 RHI，默认 OpenGL。 */
    static RenderBackendKind preferredBackend();

    /** @brief 写入用户首选 RHI，下次启动生效。 */
    static void setPreferredBackend(RenderBackendKind kind);

    /** @brief 返回当前构建实际可用的 RHI；首选不可用时回退到 OpenGL。 */
    static RenderBackendKind effectiveBackend();

    /** @brief RHI 枚举转稳定配置字符串。 */
    static QString backendKey(RenderBackendKind kind);

    /** @brief 稳定配置字符串转 RHI 枚举。 */
    static RenderBackendKind backendFromKey(const QString& key,
                                            RenderBackendKind fallback = RenderBackendKind::OpenGL);
};
