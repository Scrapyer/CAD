/**
 * @file FEField.cpp
 * @brief 结果场数据与色谱映射的方法实现
 *
 * 实现标量场/矢量场的统计方法和色谱的颜色计算。
 */

#include "FEField.h"
#include <algorithm>
#include <cmath>
#include <limits>

// ════════════════════════════════════════════════════════════
// FEScalarField 方法
// ════════════════════════════════════════════════════════════

/**
 * @brief 计算标量场的值域范围
 * @param[out] minVal 最小值
 * @param[out] maxVal 最大值
 *
 * 遍历所有值找极值。如果场为空，返回 (0, 0)。
 */
void FEScalarField::computeRange(float& minVal, float& maxVal) const {
    if (values.empty()) {
        minVal = 0.0f;
        maxVal = 0.0f;
        return;
    }

    minVal =  std::numeric_limits<float>::max();
    maxVal = -std::numeric_limits<float>::max();

    for (const auto& [id, val] : values) {
        minVal = std::min(minVal, val);
        maxVal = std::max(maxVal, val);
    }
}

/**
 * @brief 计算标量场的值域范围，同时返回极值对应的 ID
 */
void FEScalarField::computeRangeWithIds(float& minVal, float& maxVal, int& minId, int& maxId) const {
    if (values.empty()) {
        minVal = 0.0f; maxVal = 0.0f;
        minId = -1; maxId = -1;
        return;
    }

    minVal =  std::numeric_limits<float>::max();
    maxVal = -std::numeric_limits<float>::max();
    minId = -1;
    maxId = -1;

    for (const auto& [id, val] : values) {
        if (val < minVal) { minVal = val; minId = id; }
        if (val > maxVal) { maxVal = val; maxId = id; }
    }
}

// ════════════════════════════════════════════════════════════
// FEVectorField 方法
// ════════════════════════════════════════════════════════════

/**
 * @brief 计算矢量场的幅值范围
 * @param[out] minMag 最小幅值
 * @param[out] maxMag 最大幅值
 *
 * 幅值 = glm::length(vec)，即矢量的模长。
 * 用于确定云图的显示范围。
 */
void FEVectorField::computeMagnitudeRange(float& minMag, float& maxMag) const {
    if (values.empty()) {
        minMag = 0.0f;
        maxMag = 0.0f;
        return;
    }

    minMag =  std::numeric_limits<float>::max();
    maxMag = -std::numeric_limits<float>::max();

    for (const auto& [id, vec] : values) {
        float mag = glm::length(vec);
        minMag = std::min(minMag, mag);
        maxMag = std::max(maxMag, mag);
    }
}

// ════════════════════════════════════════════════════════════
// ColorMap 方法
// ════════════════════════════════════════════════════════════

/**
 * @brief 将归一化值 [0,1] 映射为 RGB 颜色
 * @param t 归一化值（0 = 最小, 1 = 最大）
 * @return RGB 颜色（各分量 0~1）
 *
 * 根据当前色谱类型选择不同的颜色计算方式。
 * t 会被 clamp 到 [0, 1] 范围内，防止越界。
 */
glm::vec3 ColorMap::map(float t) const {
    // 将 t 限制在 [0, 1] 范围内
    t = std::clamp(t, 0.0f, 1.0f);

    // 分段色带：将 t 量化到离散色阶（每个色阶内颜色相同）
    if (discreteLevels > 0) {
        int band = static_cast<int>(t * discreteLevels);
        if (band >= discreteLevels) band = discreteLevels - 1;
        t = (band + 0.5f) / discreteLevels;  // 用色阶中点采样颜色
    }

    switch (type) {
        case ColorMapType::Rainbow: {
            // 经典彩虹色谱：蓝(0) → 青(0.25) → 绿(0.5) → 黄(0.75) → 红(1)
            // 使用 HSV 色相旋转：H 从 240°(蓝) → 0°(红)
            float h = (1.0f - t) * 240.0f;  // 色相角度：240 → 0
            float s = 1.0f;                   // 饱和度：满
            float v = 1.0f;                   // 明度：满

            // HSV → RGB 转换
            float c = v * s;                  // 色度
            float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
            float m = v - c;

            float r, g, b;
            if      (h < 60)  { r = c; g = x; b = 0; }
            else if (h < 120) { r = x; g = c; b = 0; }
            else if (h < 180) { r = 0; g = c; b = x; }
            else if (h < 240) { r = 0; g = x; b = c; }
            else              { r = x; g = 0; b = c; }

            return glm::vec3(r + m, g + m, b + m);
        }

        case ColorMapType::Jet: {
            // Jet 色谱（类似 MATLAB 默认配色）
            // 蓝(0) → 青(0.35) → 黄(0.65) → 红(1)
            // 使用分段线性插值
            float r = std::clamp(1.5f - std::fabs(t - 0.75f) * 4.0f, 0.0f, 1.0f);
            float g = std::clamp(1.5f - std::fabs(t - 0.50f) * 4.0f, 0.0f, 1.0f);
            float b = std::clamp(1.5f - std::fabs(t - 0.25f) * 4.0f, 0.0f, 1.0f);
            return glm::vec3(r, g, b);
        }

        case ColorMapType::CoolWarm: {
            // 冷暖色谱：蓝(0) → 白(0.5) → 红(1)
            // 适合显示正/负值的对比（如应力的拉/压）
            float r = std::clamp(t * 2.0f, 0.0f, 1.0f);
            float b = std::clamp((1.0f - t) * 2.0f, 0.0f, 1.0f);
            float g = std::min(r, b);  // 中间区域偏白
            return glm::vec3(r, g, b);
        }

        case ColorMapType::Grayscale: {
            // 灰度色谱：黑(0) → 白(1)
            return glm::vec3(t, t, t);
        }

        case ColorMapType::Viridis: {
            // Viridis 色谱的简化近似（感知均匀，色盲友好）
            // 深紫(0) → 蓝绿(0.5) → 亮黄(1)
            // 使用多项式近似真实 Viridis 色谱
            float r = std::clamp(0.267004f + t * (0.003991f + t * (2.244061f + t * (-7.553693f + t * (5.038770f)))), 0.0f, 1.0f);
            float g = std::clamp(0.004874f + t * (1.014753f + t * (0.327592f + t * (-2.614120f + t * (1.655697f)))), 0.0f, 1.0f);
            float b = std::clamp(0.329415f + t * (1.561444f + t * (-5.394512f + t * (8.349633f + t * (-4.446321f)))), 0.0f, 1.0f);
            return glm::vec3(r, g, b);
        }
    }

    // 默认返回白色（不应到达此处）
    return glm::vec3(1.0f);
}

/**
 * @brief 便捷方法：直接从原始标量值映射为 RGB 颜色
 * @param value  原始标量值
 * @param minVal 值域最小值
 * @param maxVal 值域最大值
 * @return RGB 颜色
 *
 * 自动将 value 归一化到 [0, 1]，然后调用 map(t)。
 * 如果 minVal == maxVal（退化情况），统一映射到 t=0.5。
 */
glm::vec3 ColorMap::map(float value, float minVal, float maxVal) const {
    // 处理退化情况：值域宽度为 0 时，映射到中间值
    float range = maxVal - minVal;
    float t = (range > 1e-10f) ? (value - minVal) / range : 0.5f;
    return map(t);
}
