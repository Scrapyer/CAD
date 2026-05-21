/**
 * @file FENode.h
 * @brief 有限元节点数据结构
 *
 * 节点是有限元模型的基础：每个节点有一个全局唯一 ID 和三维坐标。
 *
 * 设计说明：
 *   - 使用 int 作为节点 ID（与主流 FEM 软件一致，如 Abaqus/Nastran）
 *   - ID 不要求连续（实际 FEM 模型中 ID 常有跳跃），因此 FEModel
 *     使用 unordered_map<int, FENode> 存储，而非 vector
 *   - 坐标使用 glm::vec3（单精度），满足可视化精度需求
 *     （如果后续需要双精度计算，可扩展为 glm::dvec3）
 */

#pragma once

#include <glm/glm.hpp>

struct FENode {
    int id = 0;                              // 节点全局编号
    glm::vec3 coords{0.0f, 0.0f, 0.0f};     // 节点坐标 (x, y, z)
};
