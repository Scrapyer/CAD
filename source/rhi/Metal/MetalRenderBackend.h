#pragma once

#include "RenderBackend.h"

#include <QString>

/**
 * @brief Metal 渲染后端骨架。
 *
 * 当前负责探测系统 Metal 设备并填充后端信息；实际 CAMetalLayer 视口、
 * command queue 和 draw pass 会在后续阶段接入。
 */
class MetalRenderBackend final : public IRenderBackend {
public:
    void initialize() override;
    const RenderBackendInfo& info() const override { return info_; }

    bool isInitialized() const { return initialized_; }
    const QString& lastError() const { return lastError_; }

    /** @brief 当前系统是否可创建默认 Metal 设备。 */
    static bool isSystemAvailable();

private:
    RenderBackendInfo info_;
    bool initialized_ = false;
    QString lastError_;
};
