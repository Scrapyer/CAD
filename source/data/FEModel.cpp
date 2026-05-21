/**
 * @file FEModel.cpp
 * @brief FEModel 类的方法实现
 *
 * 实现有限元模型的数据操作：节点/单元增删、包围盒计算等。
 */

#include "FEModel.h"
#include <algorithm>
#include <limits>

// ════════════════════════════════════════════════════════════
// 节点/单元操作
// ════════════════════════════════════════════════════════════

/**
 * @brief 添加一个节点到模型
 * @param id     节点 ID（全局唯一）
 * @param coords 节点坐标 (x, y, z)
 *
 * 如果 ID 已存在，会覆盖原有节点数据。
 */
void FEModel::addNode(int id, const glm::vec3& coords) {
    FENode node;
    node.id = id;
    node.coords = coords;
    nodes[id] = node;
}

/**
 * @brief 添加一个单元到模型
 * @param id      单元 ID（全局唯一）
 * @param type    单元类型（如 TRI3, HEX8 等）
 * @param nodeIds 单元的节点 ID 列表（顺序遵循标准约定）
 */
void FEModel::addElement(int id, ElementType type, const std::vector<int>& nodeIds) {
    FEElement elem;
    elem.id = id;
    elem.type = type;
    elem.nodeIds = nodeIds;
    elements[id] = elem;
}

/**
 * @brief 按 ID 查找节点坐标
 * @param id 节点 ID
 * @return 指向节点坐标的指针；如果 ID 不存在返回 nullptr
 *
 * 返回指针而非引用，便于调用方判断节点是否存在。
 */
const glm::vec3* FEModel::nodeCoords(int id) const {
    auto it = nodes.find(id);
    if (it != nodes.end()) {
        return &it->second.coords;
    }
    return nullptr;
}

// ════════════════════════════════════════════════════════════
// 包围盒与空间信息
// ════════════════════════════════════════════════════════════

/**
 * @brief 计算模型的轴对齐包围盒（AABB）
 * @param[out] bbMin 包围盒最小角点
 * @param[out] bbMax 包围盒最大角点
 *
 * 遍历所有节点，取各轴的最小/最大值。
 * 如果模型为空，bbMin = bbMax = (0,0,0)。
 */
void FEModel::computeBoundingBox(glm::vec3& bbMin, glm::vec3& bbMax) const {
    if (nodes.empty()) {
        bbMin = glm::vec3(0.0f);
        bbMax = glm::vec3(0.0f);
        return;
    }

    // 初始化为第一个节点的坐标
    auto it = nodes.begin();
    bbMin = it->second.coords;
    bbMax = it->second.coords;

    // 遍历所有节点，更新最小/最大值
    for (const auto& [id, node] : nodes) {
        bbMin = glm::min(bbMin, node.coords);
        bbMax = glm::max(bbMax, node.coords);
    }
}

/**
 * @brief 计算模型的几何中心（包围盒中点）
 * @return 包围盒的几何中心坐标
 *
 * 用于相机自动定位——将模型居中在视口中。
 */
glm::vec3 FEModel::computeCenter() const {
    glm::vec3 bbMin, bbMax;
    computeBoundingBox(bbMin, bbMax);
    return (bbMin + bbMax) * 0.5f;
}

/**
 * @brief 计算模型的最大尺寸
 * @return 包围盒对角线长度
 *
 * 用于确定相机的初始距离——距离 ≈ size × 某个系数。
 */
float FEModel::computeSize() const {
    glm::vec3 bbMin, bbMax;
    computeBoundingBox(bbMin, bbMax);
    return glm::length(bbMax - bbMin);
}

/**
 * @brief 清空模型中的所有数据
 *
 * 释放节点、单元、分组、结果场的全部内存。
 * 调用后 isEmpty() 返回 true。
 */
void FEModel::clear() {
    name.clear();
    filePath.clear();
    nodes.clear();
    elements.clear();
    parts.clear();
    nodeSets.clear();
    elementSets.clear();
    scalarFields.clear();
    vectorFields.clear();
}
