/**
 * @file FERenderData.h
 * @brief FEM 渲染数据包（Mesh + 反向映射表）
 *
 * 这是 FEMeshConverter 的输出结构，捆绑了：
 *   1. 可渲染的三角网格（Mesh）
 *   2. 渲染三角形 → 原始 FEM 单元的反向映射
 *   3. 渲染顶点 → 原始 FEM 节点的反向映射
 *
 * 为什么需要反向映射？
 * ──────────────────
 * FEMeshConverter 将 FEM 单元转换为三角形时，一个单元会生成多个三角形：
 *   - 一个 QUAD4 → 2 个三角形
 *   - 一个 HEX8 → 12 个三角形（6 面 × 2）
 *
 * 拾取时，用户点击的是渲染出来的三角形，需要反查：
 *   "这个三角形属于哪个 FEM 单元？" → triangleToElement
 *   "这个顶点对应哪个 FEM 节点？"   → vertexToNode
 *
 * ┌──────────────────────────────────────────────────────────┐
 * │              FERenderData 内部结构                       │
 * │                                                          │
 * │  mesh                                                    │
 * │  ├── vertices: [v0, v1, v2, v3, ...]   (渲染顶点)      │
 * │  └── indices:  [0,1,2, 3,4,5, ...]     (三角形索引)    │
 * │                                                          │
 * │  triangleToElement: [elemId, elemId, ...]               │
 * │  │  索引 = 三角形序号 (indices 中每 3 个为一组)         │
 * │  │  值   = 对应的 FEM 单元 ID                           │
 * │  │  例: triangleToElement[5] = 102                      │
 * │  │      → 第 5 个渲染三角形来自 FEM 单元 #102          │
 * │  │                                                      │
 * │  triangleToFace: [faceIdx, faceIdx, ...]                │
 * │  │  值 = 该三角形在其所属单元中的面索引                  │
 * │  │  例: triangleToFace[5] = 2                           │
 * │  │      → 来自单元 #102 的第 2 个面                     │
 * │  │                                                      │
 * │  vertexToNode: [nodeId, nodeId, ...]                    │
 * │  │  索引 = 渲染顶点序号                                 │
 * │  │  值   = 对应的 FEM 节点 ID（-1 表示无对应）          │
 * │  │  例: vertexToNode[10] = 57                           │
 * │  │      → 第 10 个渲染顶点来自 FEM 节点 #57            │
 * │  └                                                      │
 * └──────────────────────────────────────────────────────────┘
 *
 * 设计说明：
 *   - 映射表使用 std::vector<int>，索引对齐，O(1) 查找
 *   - 比 unordered_map 更紧凑高效（渲染数据量大时很重要）
 *   - Mesh 保持不变（纯渲染数据），映射表是附加的元数据
 */

#pragma once

#include <vector>
#include "Geometry.h"

struct FERenderData {
    // ── 渲染数据 ──
    Mesh mesh;                               // 三角网格（顶点 + 索引）

    // ── 反向映射表（渲染层 → FEM 层）──

    /**
     * 三角形 → 单元 ID 映射
     * 大小 = mesh.indices.size() / 3（三角形数量）
     * triangleToElement[i] = 第 i 个三角形所属的 FEM 单元 ID
     */
    std::vector<int> triangleToElement;

    /**
     * 三角形 → 面索引映射
     * 大小同上
     * triangleToFace[i] = 第 i 个三角形在其单元内的面序号
     *
     * 对于 2D 单元（TRI3/QUAD4），面索引始终为 0（单元本身就是一个面）
     * 对于 3D 单元（HEX8），面索引 0~5 对应六面体的 6 个面
     */
    std::vector<int> triangleToFace;

    /**
     * 顶点 → 节点 ID 映射
     * 大小 = mesh.vertices.size() / 6（顶点数量，每顶点 6 float）
     * vertexToNode[i] = 第 i 个渲染顶点对应的 FEM 节点 ID
     *
     * 注意：flat shading 模式下同一个 FEM 节点可能对应多个渲染顶点
     *（因为不同面上法线不同，所以顶点被复制了）
     * 所以这是 多对一 的映射。
     */
    std::vector<int> vertexToNode;

    /**
     * 三角形 → 部件索引映射
     * 大小同 triangleToElement
     * triangleToPart[i] = 第 i 个三角形所属的部件在 model.parts 中的索引
     * -1 表示该三角形不属于任何已定义的部件
     */
    std::vector<int> triangleToPart;

    /**
     * 边线 → 部件索引映射
     * 大小 = mesh.edgeIndices.size() / 2（边线数量）
     * edgeToPart[i] = 第 i 条边所属的部件在 model.parts 中的索引
     * -1 表示该边不属于任何已定义的部件
     */
    std::vector<int> edgeToPart;

    // ── 便捷方法 ──

    /** @brief 根据三角形索引查询所属单元 ID */
    int elementAtTriangle(int triIndex) const {
        if (triIndex >= 0 && triIndex < static_cast<int>(triangleToElement.size()))
            return triangleToElement[triIndex];
        return -1;
    }

    /** @brief 根据三角形索引查询面序号 */
    int faceAtTriangle(int triIndex) const {
        if (triIndex >= 0 && triIndex < static_cast<int>(triangleToFace.size()))
            return triangleToFace[triIndex];
        return -1;
    }

    /** @brief 根据渲染顶点索引查询 FEM 节点 ID */
    int nodeAtVertex(int vertIndex) const {
        if (vertIndex >= 0 && vertIndex < static_cast<int>(vertexToNode.size()))
            return vertexToNode[vertIndex];
        return -1;
    }

    /** @brief 获取三角形数量 */
    int triangleCount() const {
        return static_cast<int>(mesh.indices.size() / 3);
    }

    /** @brief 获取渲染顶点数量 */
    int vertexCount() const {
        return static_cast<int>(mesh.vertices.size() / 6);
    }

    /** @brief 清空所有数据 */
    void clear() {
        mesh.vertices.clear();
        mesh.indices.clear();
        triangleToElement.clear();
        triangleToFace.clear();
        vertexToNode.clear();
        triangleToPart.clear();
        edgeToPart.clear();
    }
};
