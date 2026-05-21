/**
 * @file FEPostFilter.h
 * @brief 后处理空间过滤器（阈值、裁剪、切片）
 *
 * 提供 CPU 端的网格过滤操作：
 *   - 阈值过滤：按单元标量值范围保留/丢弃完整三角形
 *   - 裁剪平面：按平面对三角形做半空间几何裁剪
 *   - 切片平面：生成网格与平面的交线
 */

#pragma once

#include <vector>
#include <glm/glm.hpp>
#include "ferender_export.h"
#include "FERenderData.h"
#include "FEField.h"

struct FERENDER_EXPORT FEPlane {
    glm::vec3 origin{0.0f};
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
};

struct FERENDER_EXPORT FESliceResult {
    std::vector<float> lineVertices;  // 交线顶点 [x,y,z, x,y,z, ...]
    int lineCount = 0;                // 线段数量（每条线段 2 个顶点）
};

class FERENDER_EXPORT FEPostFilter {
public:
    /**
     * @brief 按单元标量值阈值过滤
     *
     * 保留 field 值在 [minValue, maxValue] 范围内的单元对应的三角形。
     * 以完整三角形为单位保留/丢弃，不做几何裁切。
     * 映射表（triangleToElement, vertexToNode 等）保持正确。
     */
    static FERenderData thresholdByElementValue(const FERenderData& input,
                                                const FEScalarField& field,
                                                float minValue,
                                                float maxValue);

    /**
     * @brief 按裁剪平面过滤
     *
     * 对跨越平面的三角形做几何裁切，并丢弃不在保留侧的部分。
     * keepPositiveSide=true 时保留法线方向一侧。
     */
    static FERenderData clipByPlane(const FERenderData& input,
                                    const FEPlane& plane,
                                    bool keepPositiveSide);

    /**
     * @brief 生成切片平面与网格的交线
     *
     * 遍历所有三角形，计算与平面的交线段。
     */
    static FESliceResult sliceByPlane(const FERenderData& input,
                                      const FEPlane& plane);
};
