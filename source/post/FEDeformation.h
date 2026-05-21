/**
 * @file FEDeformation.h
 * @brief 变形显示工具类
 *
 * 将位移矢量场叠加到 FEModel 坐标，生成变形后的模型。
 * 支持比例缩放和自动比例计算。
 */

#pragma once

#include "ferender_export.h"
#include "FEModel.h"
#include "FEField.h"

struct FERENDER_EXPORT FEDeformationOptions {
    float scale = 1.0f;
    bool overlayUndeformed = false;
};

class FERENDER_EXPORT FEDeformation {
public:
    /**
     * @brief 生成变形后的 FEModel
     *
     * 新坐标 = 原始坐标 + displacement × scale
     * 缺失位移的节点保持原坐标不变。
     */
    static FEModel apply(const FEModel& model,
                         const FEVectorField& displacement,
                         const FEDeformationOptions& options);

    /**
     * @brief 计算自动缩放比例
     *
     * 使模型最大变形约为模型尺寸的 10%，便于观察微小变形。
     * @return 推荐的 scale 值（若位移为零返回 1.0）
     */
    static float autoScale(const FEModel& model,
                           const FEVectorField& displacement,
                           float targetRatio = 0.1f);
};
