#pragma once

#include <cstdint>

/**
 * @brief GPU-driven 主网格顶点布局。
 *
 * V1 与当前 Vulkan mesh graphics pipeline 使用的 position/normal/color/pickColor
 * 布局保持一致；每个三角形会展开 3 个顶点。
 */
struct VulkanGpuDrivenMeshVertex {
    float position[3];
    float normal[3];
    float color[3];
    float pickColor[3];
};

/**
 * @brief GPU-driven V2 源顶点布局。
 *
 * V2 目标是不再按三角形展开 surface vertex。position/normal/vertexColor/scalar
 * 只按原始 source vertex 存一份，part color 和 pick id 由 triangle metadata 提供。
 */
struct VulkanGpuDrivenSourceVertex {
    float position[4];
    float normal[4];
    float color[3];
    float scalar = 0.0f;
};

/**
 * @brief GPU-driven 线段/边线顶点布局。
 *
 * 与 Vulkan line/point pipeline 的 position + scalar 输入布局保持一致。
 */
struct VulkanGpuDrivenLineVertex {
    float position[3];
    float scalar = 0.0f;
};

/**
 * @brief 一个三角形对应的 GPU 可见性筛选元数据。
 */
struct VulkanGpuDrivenTriangleMeta {
    uint32_t index0 = 0;
    uint32_t index1 = 0;
    uint32_t index2 = 0;
    uint32_t indexPad = 0;
    int32_t elementId = -1;
    int32_t partId = -1;
    int32_t idPad0 = 0;
    int32_t idPad1 = 0;
    float boundsMin[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float boundsMax[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

/**
 * @brief 一条边线对应的 GPU 可见性筛选元数据。
 */
struct VulkanGpuDrivenEdgeMeta {
    uint32_t index0 = 0;
    uint32_t index1 = 0;
    uint32_t indexPad0 = 0;
    uint32_t indexPad1 = 0;
    int32_t elementId = -1;
    int32_t partId = -1;
    int32_t idPad0 = 0;
    int32_t idPad1 = 0;
};

/**
 * @brief 部件可见性和颜色状态。
 */
struct VulkanGpuDrivenPartState {
    uint32_t visible = 1;
    float color[3] = {0.48f, 0.72f, 0.76f};
};

/**
 * @brief 隐藏单元表条目。
 *
 * 第一版按 element id 列表上传；大量隐藏对象后续再升级为 dense bitset。
 */
struct VulkanGpuDrivenHiddenElement {
    int32_t elementId = -1;
};

/**
 * @brief visibility compute pass 每帧参数。
 */
struct VulkanGpuDrivenVisibilityUniforms {
    float viewProjection[16] = {};
    float frustumPlanes[6][4] = {};
    uint32_t triangleCount = 0;
    uint32_t hiddenElementCount = 0;
    uint32_t enableFrustumCulling = 0;
    uint32_t enablePartVisibility = 1;
    uint32_t partStateCount = 0;
    uint32_t edgeCount = 0;
    uint32_t sourceVertexCount = 0;
    uint32_t enablePointOutput = 0;
    uint32_t uniformPad2 = 0;
    uint32_t uniformPad3 = 0;
    uint32_t uniformPad4 = 0;
};

static_assert(sizeof(VulkanGpuDrivenMeshVertex) == 12 * sizeof(float),
              "Unexpected VulkanGpuDrivenMeshVertex layout");
static_assert(sizeof(VulkanGpuDrivenSourceVertex) == 12 * sizeof(float),
              "Unexpected VulkanGpuDrivenSourceVertex layout");
static_assert(sizeof(VulkanGpuDrivenLineVertex) == 4 * sizeof(float),
              "Unexpected VulkanGpuDrivenLineVertex layout");
static_assert(sizeof(VulkanGpuDrivenPartState) == 4 * sizeof(float),
              "Unexpected VulkanGpuDrivenPartState layout");
static_assert(sizeof(VulkanGpuDrivenTriangleMeta) == 64,
              "Unexpected VulkanGpuDrivenTriangleMeta layout");
static_assert(sizeof(VulkanGpuDrivenEdgeMeta) == 32,
              "Unexpected VulkanGpuDrivenEdgeMeta layout");
