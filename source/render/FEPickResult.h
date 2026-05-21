/**
 * @file FEPickResult.h
 * @brief 有限元拾取结果与选中状态管理
 *
 * 拾取（Picking）是 FEM 可视化的核心交互功能：
 *   - 点击选中节点/单元/面，查看详细信息
 *   - 框选批量选中
 *   - 选中后高亮显示
 *
 * ┌──────────────────────────────────────────────────────────────┐
 * │                    拾取流程                                  │
 * │                                                              │
 * │  鼠标点击 (屏幕坐标 x, y)                                   │
 * │       ↓                                                      │
 * │  ┌──────────────────────────────────────┐                   │
 * │  │  方案A: GPU Color Picking            │                   │
 * │  │  - 离屏渲染：每个单元用唯一颜色 ID   │                   │
 * │  │  - glReadPixels 读取点击处像素        │                   │
 * │  │  - 颜色 ID → 单元 ID                 │                   │
 * │  ├──────────────────────────────────────┤                   │
 * │  │  方案B: CPU Ray Casting              │                   │
 * │  │  - 屏幕坐标 → 世界空间射线           │                   │
 * │  │  - 射线与三角形求交                   │                   │
 * │  │  - 三角形索引 → 单元 ID（需映射表）  │                   │
 * │  └──────────────────────────────────────┘                   │
 * │       ↓                                                      │
 * │  FEPickResult（拾取到的实体信息）                            │
 * │       ↓                                                      │
 * │  FESelection（累计的选中状态）                               │
 * │       ↓                                                      │
 * │  高亮渲染 / 信息面板显示                                     │
 * └──────────────────────────────────────────────────────────────┘
 *
 * 设计说明：
 *   - FEPickResult 描述单次拾取的结果（拾取到了什么）
 *   - FESelection 描述累计的选中状态（当前选中了哪些节点/单元）
 *   - 两者分离：拾取是瞬时动作，选中是持久状态
 *   - 选中状态用 unordered_set 存储，支持快速增删查
 */

#pragma once

#include <unordered_set>
#include <glm/glm.hpp>

/**
 * @enum PickMode
 * @brief 拾取模式
 *
 * 决定点击时选中的实体粒度。
 */
enum class PickMode {
    Node,       // 节点拾取：选中最近的节点
    Element,    // 单元拾取：选中点击处的单元
    Part        // 部件拾取：选中整个部件（同一 Property ID 下的所有单元）
};

/**
 * @struct FEPickResult
 * @brief 单次拾取的结果
 *
 * 描述一次鼠标点击拾取到了什么。
 * 如果未拾取到任何实体，hit 为 false。
 */
struct FEPickResult {
    bool hit = false;              // 是否拾取到了有效实体

    // ── 拾取到的实体信息 ──
    int nodeId    = -1;            // 拾取到的节点 ID（节点模式下有效）
    int elementId = -1;            // 拾取到的单元 ID（单元/面模式下有效）
    int faceIndex = -1;            // 拾取到的面索引（面模式下有效，在单元的面列表中的索引）

    // ── 空间信息 ──
    glm::vec3 worldPos{0.0f};     // 拾取点的世界空间坐标（射线与表面的交点）
    float depth = 0.0f;           // 拾取点的深度值（用于多层遮挡时取最近的）

    // ── 渲染层信息（用于反向映射）──
    int triangleIndex = -1;        // 命中的渲染三角形索引
};

/**
 * @struct FESelection
 * @brief 选中状态管理
 *
 * 维护当前被选中的节点和单元集合。
 * 支持单选、多选（Ctrl+点击）、框选、全选/取消等操作。
 *
 * 使用 unordered_set 存储 ID，保证：
 *   - O(1) 查找（判断某个节点/单元是否被选中）
 *   - O(1) 增删（点击选中/取消选中）
 */
struct FESelection {
    std::unordered_set<int> selectedNodes;      // 选中的节点 ID 集合
    std::unordered_set<int> selectedElements;   // 选中的单元 ID 集合

    /** @brief 清空所有选中状态 */
    void clear() {
        selectedNodes.clear();
        selectedElements.clear();
    }

    /** @brief 判断指定节点是否被选中 */
    bool isNodeSelected(int nodeId) const {
        return selectedNodes.count(nodeId) > 0;
    }

    /** @brief 判断指定单元是否被选中 */
    bool isElementSelected(int elemId) const {
        return selectedElements.count(elemId) > 0;
    }

    /** @brief 切换节点的选中状态（选中↔取消） */
    void toggleNode(int nodeId) {
        if (selectedNodes.count(nodeId))
            selectedNodes.erase(nodeId);
        else
            selectedNodes.insert(nodeId);
    }

    /** @brief 切换单元的选中状态（选中↔取消） */
    void toggleElement(int elemId) {
        if (selectedElements.count(elemId))
            selectedElements.erase(elemId);
        else
            selectedElements.insert(elemId);
    }

    /** @brief 获取选中的节点数量 */
    int selectedNodeCount() const { return static_cast<int>(selectedNodes.size()); }

    /** @brief 获取选中的单元数量 */
    int selectedElementCount() const { return static_cast<int>(selectedElements.size()); }

    /** @brief 是否有任何选中项 */
    bool hasSelection() const { return !selectedNodes.empty() || !selectedElements.empty(); }
};
