/**
 * @file FEResultData.h
 * @brief OP2 结果数据层级结构
 *
 * 层级：FEResultData → FESubcase → FEResultType → FEResultComponent
 *
 *   FEResultData          顶层容器（多个工况）
 *     └─ FESubcase         一个工况 (subcase)
 *          └─ FEResultType  一种结果类型（位移/应力）
 *               └─ FEResultComponent  单个分量（名称 + FEScalarField）
 */

#pragma once

#include "FEField.h"
#include <string>
#include <vector>

/**
 * @struct FEResultComponent
 * @brief 结果的单个分量（如位移 X、Von Mises 等）
 */
struct FEResultComponent {
    std::string name;        // 分量名称（如 "X", "Y", "Z", "Magnitude", "Von Mises"）
    FEScalarField field;     // 标量场数据
};

/**
 * @struct FEResultType
 * @brief 一种结果类型（如位移、应力），包含多个分量
 */
struct FEResultType {
    std::string name;                           // 类型名称（如 "Displacement", "Stress"）
    std::vector<FEResultComponent> components;  // 分量列表
    FEVectorField vectorField;                  // 可选的矢量场（如位移矢量）
    bool hasVector = false;                     // 是否有矢量场
};

/**
 * @struct FESubcase
 * @brief 一个工况 (Subcase)，包含多种结果类型
 */
struct FESubcase {
    int id = 0;                              // 工况 ID
    std::string name;                        // 工况名称
    std::vector<FEResultType> resultTypes;   // 结果类型列表
};

/**
 * @struct FEResultData
 * @brief 顶层结果数据容器，包含多个工况
 */
struct FEResultData {
    std::vector<FESubcase> subcases;   // 工况列表

    /** @brief 是否有任何结果数据 */
    bool empty() const { return subcases.empty(); }

    /** @brief 清空所有结果 */
    void clear() { subcases.clear(); }
};
