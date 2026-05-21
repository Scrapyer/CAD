/**
 * @file FEModel.h
 * @brief 有限元模型顶层容器
 *
 * FEModel 是整个有限元数据的统一入口，持有模型的全部信息：
 *   - 节点表（nodes）：所有节点及其坐标
 *   - 单元表（elements）：所有单元及其连接关系
 *   - 部件列表（parts）：模型的逻辑组成部分
 *   - 节点集/单元集（nodeSets/elementSets）：命名分组
 *   - 结果场（scalarFields/vectorFields）：后处理数据
 *
 * ┌─────────────────────────────────────────────────────────────┐
 * │                      FEModel                               │
 * │                                                             │
 * │  ┌──────────────┐   ┌──────────────┐   ┌──────────────┐   │
 * │  │   nodes      │   │  elements    │   │    parts     │   │
 * │  │ map<id,Node> │   │map<id,Elem> │   │ vector<Part> │   │
 * │  └──────────────┘   └──────────────┘   └──────────────┘   │
 * │                                                             │
 * │  ┌──────────────┐   ┌──────────────┐                      │
 * │  │  nodeSets    │   │ elementSets  │                      │
 * │  │ vector<NSet> │   │ vector<ESet> │                      │
 * │  └──────────────┘   └──────────────┘                      │
 * │                                                             │
 * │  ┌──────────────────┐   ┌──────────────────┐              │
 * │  │  scalarFields    │   │  vectorFields    │              │
 * │  │ vector<SField>   │   │ vector<VField>   │              │
 * │  └──────────────────┘   └──────────────────┘              │
 * └─────────────────────────────────────────────────────────────┘
 *
 * 设计说明：
 *   - 节点和单元使用 unordered_map<int, T>，支持不连续 ID
 *   - 提供便捷方法：添加节点/单元、按 ID 查找、统计信息等
 *   - 提供包围盒计算（用于相机自动定位）
 *   - 不包含任何渲染逻辑（纯数据层）
 *   - 线程安全性：当前为非线程安全（单线程加载、单线程渲染）
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

#include "FENode.h"
#include "FEElement.h"
#include "FEGroup.h"
#include "FEField.h"
#include "ferender_export.h"

class FERENDER_EXPORT FEModel {
public:
    // ── 模型元数据 ──
    std::string name;            // 模型名称（通常来自文件名）
    std::string filePath;        // 源文件路径

    // ── 核心数据 ──
    std::unordered_map<int, FENode>    nodes;       // 节点表：ID → 节点
    std::unordered_map<int, FEElement> elements;    // 单元表：ID → 单元

    // ── 分组数据 ──
    std::vector<FEPart>       parts;          // 部件列表
    std::vector<FENodeSet>    nodeSets;       // 节点集列表
    std::vector<FEElementSet> elementSets;    // 单元集列表

    // ── 结果场数据 ──
    std::vector<FEScalarField> scalarFields;  // 标量结果场列表
    std::vector<FEVectorField> vectorFields;  // 矢量结果场列表

    // ════════════════════════════════════════════════════════
    // 便捷方法
    // ════════════════════════════════════════════════════════

    /** @brief 添加一个节点 */
    void addNode(int id, const glm::vec3& coords);

    /** @brief 添加一个单元 */
    void addElement(int id, ElementType type, const std::vector<int>& nodeIds);

    /**
     * @brief 按 ID 查找节点坐标
     * @param id 节点 ID
     * @return 指向节点坐标的指针，未找到返回 nullptr
     */
    const glm::vec3* nodeCoords(int id) const;

    /** @brief 获取节点总数 */
    int nodeCount() const { return static_cast<int>(nodes.size()); }

    /** @brief 获取单元总数 */
    int elementCount() const { return static_cast<int>(elements.size()); }

    /**
     * @brief 计算模型的轴对齐包围盒（AABB）
     * @param[out] bbMin 包围盒最小角点
     * @param[out] bbMax 包围盒最大角点
     *
     * 用于：
     *   1. 相机自动定位（将模型居中在视口中）
     *   2. 计算合适的缩放比例
     *   3. 裁剪平面设置
     */
    void computeBoundingBox(glm::vec3& bbMin, glm::vec3& bbMax) const;

    /**
     * @brief 计算模型中心点
     * @return 包围盒的几何中心
     */
    glm::vec3 computeCenter() const;

    /**
     * @brief 计算模型的最大尺寸（包围盒对角线长度）
     * @return 对角线长度
     *
     * 用于确定相机的初始距离。
     */
    float computeSize() const;

    /** @brief 清空所有数据 */
    void clear();

    /** @brief 检查模型是否为空 */
    bool isEmpty() const { return nodes.empty(); }
};
