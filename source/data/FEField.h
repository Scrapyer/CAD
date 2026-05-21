/**
 * @file FEField.h
 * @brief 有限元结果场数据与色谱映射
 *
 * 结果场（Field）是 FEM 后处理的核心数据，表示在节点或单元上的物理量：
 *   - 标量场：温度、位移幅值、Von Mises 应力等
 *   - 矢量场：位移、速度、力等
 *
 * 色谱映射（ColorMap）将标量值映射到 RGB 颜色，用于云图显示。
 *
 * ┌──────────────────────────────────────────────────────┐
 * │                  数据流                              │
 * │                                                      │
 * │  FEM 求解结果 → FEScalarField (每节点/单元一个值)    │
 * │                       ↓                              │
 * │              ColorMap::map(value, min, max)           │
 * │                       ↓                              │
 * │              RGB 颜色 → 传入 Mesh 顶点颜色           │
 * │                       ↓                              │
 * │              GLWidget 渲染彩色云图                    │
 * └──────────────────────────────────────────────────────┘
 *
 * 设计说明：
 *   - 标量场使用 unordered_map<int, float>，key 是节点/单元 ID
 *     这样可以支持不连续 ID、部分节点有结果等场景
 *   - 矢量场使用 unordered_map<int, glm::vec3>
 *   - ColorMap 提供多种常用色谱（彩虹、冷暖、灰度等）
 *   - 色谱映射是纯函数，不依赖 OpenGL
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

/**
 * @enum FieldLocation
 * @brief 场数据的定义位置
 */
enum class FieldLocation {
    Node,       // 定义在节点上（如位移、温度）
    Element     // 定义在单元上（如应力、应变 — 通常是积分点值的平均）
};

/**
 * @struct FEScalarField
 * @brief 标量结果场
 *
 * 每个节点或单元对应一个标量值。
 * 例如：Von Mises 应力、温度、位移幅值。
 */
#include "ferender_export.h"

struct FERENDER_EXPORT FEScalarField {
    std::string name;                          // 场名称（如 "Von Mises Stress"）
    std::string unit;                          // 单位（如 "MPa", "mm", "°C"）
    FieldLocation location = FieldLocation::Node;  // 数据位置（节点/单元）

    std::unordered_map<int, float> values;     // ID → 标量值

    /**
     * @brief 计算场的值域范围
     * @param[out] minVal 最小值
     * @param[out] maxVal 最大值
     */
    void computeRange(float& minVal, float& maxVal) const;

    /**
     * @brief 计算场的值域范围，同时返回极值对应的 ID
     * @param[out] minVal 最小值
     * @param[out] maxVal 最大值
     * @param[out] minId  最小值对应的节点/单元 ID
     * @param[out] maxId  最大值对应的节点/单元 ID
     */
    void computeRangeWithIds(float& minVal, float& maxVal, int& minId, int& maxId) const;
};

/**
 * @struct FEVectorField
 * @brief 矢量结果场
 *
 * 每个节点或单元对应一个三维矢量。
 * 例如：位移场 (ux, uy, uz)、速度场。
 * 可用于：变形显示（位移叠加到坐标上）、矢量箭头显示。
 */
struct FERENDER_EXPORT FEVectorField {
    std::string name;                          // 场名称（如 "Displacement"）
    std::string unit;                          // 单位（如 "mm"）
    FieldLocation location = FieldLocation::Node;

    std::unordered_map<int, glm::vec3> values; // ID → 矢量值

    /**
     * @brief 计算矢量幅值的范围
     * @param[out] minMag 最小幅值
     * @param[out] maxMag 最大幅值
     */
    void computeMagnitudeRange(float& minMag, float& maxMag) const;
};

/**
 * @enum ColorMapType
 * @brief 色谱类型枚举
 */
enum class ColorMapType {
    Rainbow,     // 经典彩虹色谱（蓝 → 青 → 绿 → 黄 → 红）
    Jet,         // Jet 色谱（类似 MATLAB 默认）
    CoolWarm,    // 冷暖色谱（蓝 → 白 → 红，适合正负值对比）
    Grayscale,   // 灰度（黑 → 白）
    Viridis      // Viridis 色谱（感知均匀，色盲友好）
};

/**
 * @struct ColorMap
 * @brief 色谱映射器
 *
 * 将 [0, 1] 范围的归一化标量值映射为 RGB 颜色。
 * 使用前需要先用 (value - min) / (max - min) 归一化。
 */
struct FERENDER_EXPORT ColorMap {
    ColorMapType type = ColorMapType::Rainbow;
    int discreteLevels = 10;  // 分段色阶数（0=平滑渐变，>0=分段色带）

    /**
     * @brief 将归一化值 [0,1] 映射为 RGB 颜色
     * @param t 归一化值（0=最小, 1=最大）
     * @return RGB 颜色（各分量 0~1）
     */
    glm::vec3 map(float t) const;

    /**
     * @brief 便捷方法：直接从原始值映射
     * @param value  原始标量值
     * @param minVal 值域最小值
     * @param maxVal 值域最大值
     * @return RGB 颜色
     */
    glm::vec3 map(float value, float minVal, float maxVal) const;
};
