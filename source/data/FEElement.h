/**
 * @file FEElement.h
 * @brief 有限元单元数据结构与单元类型定义
 *
 * 单元（Element）定义了节点之间的连接关系（拓扑），是有限元网格的核心。
 * 每个单元有一个全局唯一 ID、一个类型标识和一组节点 ID 列表。
 *
 * ┌──────────────────────────────────────────────────────────┐
 * │                   支持的单元类型                          │
 * ├────────────┬──────────┬──────────────────────────────────┤
 * │  类别      │  类型    │  说明                            │
 * ├────────────┼──────────┼──────────────────────────────────┤
 * │  1D 线单元 │  BAR2    │  2 节点杆单元                   │
 * │            │  BAR3    │  3 节点二次杆单元                │
 * ├────────────┼──────────┼──────────────────────────────────┤
 * │  2D 壳单元 │  TRI3    │  3 节点三角形                    │
 * │            │  TRI6    │  6 节点二次三角形                │
 * │            │  QUAD4   │  4 节点四边形                    │
 * │            │  QUAD8   │  8 节点二次四边形                │
 * ├────────────┼──────────┼──────────────────────────────────┤
 * │  3D 实体   │  TET4    │  4 节点四面体                    │
 * │            │  TET10   │  10 节点二次四面体               │
 * │            │  HEX8    │  8 节点六面体                    │
 * │            │  HEX20   │  20 节点二次六面体               │
 * │            │  WEDGE6  │  6 节点三棱柱（楔形体）          │
 * │            │  PYRAMID5│  5 节点四棱锥                    │
 * └────────────┴──────────┴──────────────────────────────────┘
 *
 * 设计说明：
 *   - nodeIds 存储的是节点 ID（不是索引），通过 FEModel 中的
 *     节点表查找实际坐标
 *   - 节点顺序遵循主流 FEM 软件约定（Abaqus/Nastran），
 *     决定了法线朝向和面的提取方式
 *   - 高阶单元（TRI6/QUAD8/TET10/HEX20）的中间节点放在
 *     角节点之后，与标准约定一致
 */

#pragma once

#include <vector>

/**
 * @enum ElementType
 * @brief 有限元单元类型枚举
 *
 * 命名规则：几何形状 + 节点数（如 TRI3 = 三角形 + 3 节点）
 */
enum class ElementType {
    // ── 1D 线单元 ──
    BAR2,       // 2 节点杆单元（线段）
    BAR3,       // 3 节点二次杆单元（含中点）

    // ── 2D 壳/板单元 ──
    TRI3,       // 3 节点三角形（线性）
    TRI6,       // 6 节点三角形（二次，3 角点 + 3 边中点）
    QUAD4,      // 4 节点四边形（线性）
    QUAD8,      // 8 节点四边形（二次，4 角点 + 4 边中点）

    // ── 3D 实体单元 ──
    TET4,       // 4 节点四面体（线性）
    TET10,      // 10 节点四面体（二次，4 角点 + 6 边中点）
    HEX8,       // 8 节点六面体（线性）
    HEX20,      // 20 节点六面体（二次，8 角点 + 12 边中点）
    WEDGE6,     // 6 节点三棱柱 / 楔形体
    PYRAMID5,   // 5 节点四棱锥
};

/**
 * @struct FEElement
 * @brief 有限元单元
 *
 * 存储单元的拓扑信息：类型 + 节点连接关系。
 * 不包含材料、截面等物理属性（由 FEGroup 或 FEProperty 管理）。
 */
struct FEElement {
    int id = 0;                    // 单元全局编号
    ElementType type = ElementType::TRI3;  // 单元类型
    std::vector<int> nodeIds;      // 节点 ID 列表（顺序遵循标准约定）
};

/**
 * @brief 获取单元类型的维度
 * @param type 单元类型
 * @return 1（线单元）、2（壳/板单元）、3（实体单元）
 */
inline int elementDimension(ElementType type) {
    switch (type) {
        case ElementType::BAR2:
        case ElementType::BAR3:
            return 1;
        case ElementType::TRI3:
        case ElementType::TRI6:
        case ElementType::QUAD4:
        case ElementType::QUAD8:
            return 2;
        default:
            return 3;
    }
}

/**
 * @brief 获取单元类型的角节点数（不含中间节点）
 * @param type 单元类型
 * @return 角节点数量
 *
 * 用于渲染时的线性近似：高阶单元可以只用角节点做快速预览，
 * 或用所有节点做精确渲染。
 */
inline int elementCornerNodeCount(ElementType type) {
    switch (type) {
        case ElementType::BAR2:     return 2;
        case ElementType::BAR3:     return 2;
        case ElementType::TRI3:     return 3;
        case ElementType::TRI6:     return 3;
        case ElementType::QUAD4:    return 4;
        case ElementType::QUAD8:    return 4;
        case ElementType::TET4:     return 4;
        case ElementType::TET10:    return 4;
        case ElementType::HEX8:     return 8;
        case ElementType::HEX20:    return 8;
        case ElementType::WEDGE6:   return 6;
        case ElementType::PYRAMID5: return 5;
    }
    return 0;
}
