/**
 * @file FEIsoSurface.h
 * @brief 等值面提取（Marching Tetrahedra）
 *
 * 从体网格中提取标量等值面，生成可渲染的三角网格。
 * 第一版仅支持四面体和六面体的线性插值。
 */

#pragma once

#include "ferender_export.h"
#include "Geometry.h"
#include "FEModel.h"
#include "FEField.h"

class FERENDER_EXPORT FEIsoSurface {
public:
    /**
     * @brief 从体网格中提取等值面
     *
     * 将六面体分解为五个四面体，然后对每个四面体执行 Marching Tetrahedra。
     * 仅处理 TET4 和 HEX8 单元。
     *
     * @param model  FEM 模型（提供节点坐标和单元拓扑）
     * @param field  节点标量场
     * @param isoValue  等值面值
     * @return 等值面三角网格
     */
    static Mesh extract(const FEModel& model,
                        const FEScalarField& field,
                        float isoValue);

    struct TetVertex {
        glm::vec3 pos;
        float value;
    };

private:
    static void marchTet(const TetVertex tet[4], float isoValue, Mesh& output);
};
