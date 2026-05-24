#pragma once

#include "RenderBackend.h"
#include "ferender_export.h"

#include <QString>

/**
 * @brief Vulkan 后端内部绘制策略。
 *
 * GPU-driven Indirect 为当前默认 Vulkan 路径；Traditional 保留为兼容回退；
 * MeshShader 仍作为后续 meshlet/task/mesh shader 路线预留。
 */
enum class VulkanDrawStrategy {
    Traditional,
    GpuDrivenIndirect,
    MeshShader
};

/**
 * @brief 全局渲染设置。
 *
 * 记录用户首选 RHI 和 Vulkan 绘制策略，并提供当前构建可用后端的兜底选择。
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

    /** @brief 从 config/settings.ini 读取 Vulkan 绘制策略，默认 GPU-driven Indirect。 */
    static VulkanDrawStrategy preferredVulkanDrawStrategy();

    /** @brief 写入 Vulkan 绘制策略；GPU-driven 能力不足时运行时回退传统路径。 */
    static void setPreferredVulkanDrawStrategy(VulkanDrawStrategy strategy);

    /** @brief 返回当前配置层可用的 Vulkan 绘制策略；运行时仍可能回退 Traditional。 */
    static VulkanDrawStrategy effectiveVulkanDrawStrategy();

    /** @brief 查询 Vulkan 绘制策略当前是否可由用户选择。 */
    static bool isVulkanDrawStrategyAvailable(VulkanDrawStrategy strategy);

    /** @brief Vulkan 绘制策略枚举转稳定配置字符串。 */
    static QString vulkanDrawStrategyKey(VulkanDrawStrategy strategy);

    /** @brief 稳定配置字符串转 Vulkan 绘制策略枚举。 */
    static VulkanDrawStrategy vulkanDrawStrategyFromKey(
        const QString& key,
        VulkanDrawStrategy fallback = VulkanDrawStrategy::Traditional);

    /** @brief Vulkan 绘制策略的 UI 显示名称。 */
    static QString vulkanDrawStrategyName(VulkanDrawStrategy strategy);
};
