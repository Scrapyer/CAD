#pragma once

#include <QString>

#include <cstdint>

/**
 * @brief Vulkan GPU-driven 运行时统计。
 *
 * 第一版只记录 CPU 侧耗时和资源/回退状态；真实 GPU 时间后续用 timestamp query 扩展。
 */
struct VulkanGpuDrivenRuntimeStats {
    bool requested = false;
    bool active = false;
    bool lastMeshFrameGpuDriven = false;
    bool lastPickFrameGpuDriven = false;
    bool lastPickElementGpuDriven = false;
    bool lastFrustumCullingEnabled = false;
    bool timestampSupported = false;
    bool lastTimestampValid = false;

    uint64_t uploadMeshCount = 0;
    uint64_t visibilityUpdateCount = 0;
    uint64_t meshFrameCount = 0;
    uint64_t pickFrameCount = 0;
    uint64_t pickElementCount = 0;
    uint64_t visibilityDispatchCount = 0;
    uint64_t visibilityTimestampCount = 0;
    uint64_t fallbackCount = 0;
    QString lastFallbackReason;

    uint32_t vertexCount = 0;
    uint32_t triangleCount = 0;
    uint32_t edgeVertexCount = 0;
    uint32_t edgeCount = 0;
    uint32_t visibleIndexCapacity = 0;
    uint32_t visiblePointIndexCapacity = 0;
    uint32_t visibleEdgeIndexCapacity = 0;
    uint32_t lastVisibleTriangleCount = 0;
    uint32_t lastVisiblePointCount = 0;
    uint32_t lastVisibleEdgeCount = 0;
    uint32_t lastVisibleIndexCount = 0;
    uint32_t lastVisiblePointIndexCount = 0;
    uint32_t lastVisibleEdgeIndexCount = 0;
    uint32_t partStateCount = 0;
    uint32_t hiddenElementCount = 0;

    double lastUploadMeshMs = 0.0;
    double lastGpuDrivenUploadMs = 0.0;
    double lastVisibilityUpdateMs = 0.0;
    double lastMeshFrameMs = 0.0;
    double lastPickFrameMs = 0.0;
    double lastPickElementMs = 0.0;
    double lastVisibilityGpuMs = 0.0;

    void clearMeshState()
    {
        requested = false;
        active = false;
        lastMeshFrameGpuDriven = false;
        lastPickFrameGpuDriven = false;
        lastPickElementGpuDriven = false;
        lastFrustumCullingEnabled = false;
        lastTimestampValid = false;
        vertexCount = 0;
        triangleCount = 0;
        edgeVertexCount = 0;
        edgeCount = 0;
        visibleIndexCapacity = 0;
        visiblePointIndexCapacity = 0;
        visibleEdgeIndexCapacity = 0;
        lastVisibleTriangleCount = 0;
        lastVisiblePointCount = 0;
        lastVisibleEdgeCount = 0;
        lastVisibleIndexCount = 0;
        lastVisiblePointIndexCount = 0;
        lastVisibleEdgeIndexCount = 0;
        partStateCount = 0;
        hiddenElementCount = 0;
        lastFallbackReason.clear();
        lastGpuDrivenUploadMs = 0.0;
        lastVisibilityUpdateMs = 0.0;
        lastVisibilityGpuMs = 0.0;
    }
};
