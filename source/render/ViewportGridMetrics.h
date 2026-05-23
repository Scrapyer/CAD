#pragma once

#include <algorithm>

struct ViewportGridMetrics {
    float alpha = 1.0f;
    float minorStep = 24.0f;
    float fineAlpha = 0.0f;
};

inline ViewportGridMetrics computeViewportGridMetrics(float modelSize,
                                                      float cameraDistance,
                                                      bool visible)
{
    const float safeModelSize = std::max(modelSize, 1.0e-4f);
    const float safeDistance = std::max(cameraDistance, 1.0e-4f);
    const float zoomRatio = std::clamp(safeModelSize / safeDistance, 0.02f, 200.0f);

    // 适配默认视图时约为 24px；缩放过程中保持 18-36px 的舒适线距，
    // 并用半步细分线淡入淡出，接近 Blender 的分级缩放感。
    float minorStep = 36.0f * zoomRatio;
    while (minorStep < 18.0f) {
        minorStep *= 2.0f;
    }
    while (minorStep >= 36.0f) {
        minorStep *= 0.5f;
    }

    const float fineAlpha = std::clamp((minorStep - 24.0f) / 12.0f, 0.0f, 1.0f);
    return {visible ? 1.0f : 0.0f, minorStep, fineAlpha};
}
