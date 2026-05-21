/**
 * @file FEResultMapper.h
 * @brief 结果场到渲染顶点数据的映射工具
 */

#pragma once

#include <vector>

#include "FEField.h"
#include "FEModel.h"
#include "FERenderData.h"
#include "ferender_export.h"

struct FERENDER_EXPORT FEMappedScalars {
    std::vector<float> scalars;               // 每个渲染顶点对应的标量值
    float minValue = 0.0f;                    // 色标最小值
    float maxValue = 0.0f;                    // 色标最大值
    int minId = -1;                           // 最小值对应的节点/单元 ID
    int maxId = -1;                           // 最大值对应的节点/单元 ID
    FieldLocation location = FieldLocation::Node;
};

class FERENDER_EXPORT FEResultMapper {
public:
    /**
     * @brief 将节点/单元标量场映射为 GLWidget::setVertexScalars() 所需数组
     * @param field 标量结果场
     * @param renderData FEMeshConverter 生成的渲染数据和反查映射
     * @param model 原始 FEM 模型（用于节点 ID 重映射）
     */
    static FEMappedScalars mapScalarToVertices(const FEScalarField& field,
                                               const FERenderData& renderData,
                                               const FEModel& model);
};
