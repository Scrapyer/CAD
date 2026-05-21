/**
 * @file FEDeformation.cpp
 * @brief 变形显示工具类实现
 */

#include "FEDeformation.h"
#include <cmath>

FEModel FEDeformation::apply(const FEModel& model,
                              const FEVectorField& displacement,
                              const FEDeformationOptions& options)
{
    FEModel deformed = model;

    for (auto& [id, node] : deformed.nodes) {
        auto it = displacement.values.find(id);
        if (it != displacement.values.end()) {
            node.coords += it->second * options.scale;
        }
    }

    return deformed;
}

float FEDeformation::autoScale(const FEModel& model,
                                const FEVectorField& displacement,
                                float targetRatio)
{
    float maxDisp = 0.0f;
    for (const auto& [id, vec] : displacement.values) {
        float mag = std::sqrt(vec.x * vec.x + vec.y * vec.y + vec.z * vec.z);
        if (mag > maxDisp) maxDisp = mag;
    }

    if (maxDisp < 1e-20f) return 1.0f;

    float modelSize = model.computeSize();
    if (modelSize < 1e-20f) return 1.0f;

    return (modelSize * targetRatio) / maxDisp;
}
