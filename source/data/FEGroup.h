/**
 * @file FEGroup.h
 * @brief 有限元分组（节点集 / 单元集 / 部件）
 *
 * FEM 模型中，节点和单元常常需要按逻辑分组：
 *   - 部件（Part）：结构中的一个零件，包含一组单元
 *   - 节点集（Node Set）：用于施加边界条件、载荷的节点集合
 *   - 单元集（Element Set）：用于指定材料属性、输出结果的单元集合
 *
 * 设计说明：
 *   - 使用 std::vector<int> 存储 ID 列表（而非 set），因为：
 *     1. 遍历性能更好（渲染时大量遍历操作）
 *     2. 内存连续，缓存友好
 *     3. 加载后通常不需要频繁增删
 *   - 如果需要快速查找，外部可以建立 unordered_set 索引
 *   - name 字段用于 UI 显示和文件中的引用
 */

#pragma once

#include <string>
#include <vector>

/**
 * @struct FENodeSet
 * @brief 节点集合
 *
 * 一组具有共同特征的节点（如"固定端节点"、"加载面节点"）。
 * 用于可视化中的选择性高亮、隐藏等操作。
 */
struct FENodeSet {
    std::string name;              // 集合名称（如 "FixedSupport", "LoadFace"）
    std::vector<int> nodeIds;      // 节点 ID 列表
};

/**
 * @struct FEElementSet
 * @brief 单元集合
 *
 * 一组具有共同属性的单元（如同一材料、同一部件）。
 * 用于可视化中的分部件显示、分材料着色等。
 */
struct FEElementSet {
    std::string name;              // 集合名称（如 "Steel_Part", "Rubber_Seal"）
    std::vector<int> elementIds;   // 单元 ID 列表
};

/**
 * @struct FEPart
 * @brief 部件（模型的逻辑组成部分）
 *
 * 一个部件通常对应实际结构中的一个零件。
 * 包含属于该部件的节点和单元。
 * 部件之间可能共享节点（装配体中的公共界面）。
 */
struct FEPart {
    std::string name;              // 部件名称
    std::vector<int> nodeIds;      // 属于该部件的节点 ID
    std::vector<int> elementIds;   // 属于该部件的单元 ID
    bool visible = true;           // 是否可见（UI 控制）
};
