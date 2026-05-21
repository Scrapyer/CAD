/**
 * @file Camera.h
 * @brief 轨道相机（Orbit Camera）声明
 *
 * 轨道相机围绕一个目标点（target）旋转，通过球坐标系（yaw/pitch/distance）
 * 来确定观察者的位置。支持三种交互操作：
 *   - 旋转（rotate）：改变 yaw/pitch 角度
 *   - 平移（pan）：移动目标点
 *   - 缩放（zoom）：改变相机到目标点的距离
 */

#pragma once

#include <glm/glm.hpp>
#include "ferender_export.h"

class FERENDER_EXPORT Camera {
public:
    // ── 轨道参数 ──
    float yaw      = 0.0f;                    // 水平旋转角度（绕 Y 轴，单位：度）
    float pitch    = 0.0f;                    // 垂直旋转角度（仰俯角，单位：度）
    float distance = 3.0f;                    // 相机到目标点的距离
    glm::vec3 target{0.0f, 0.0f, 0.0f};      // 相机注视的目标点（世界坐标）

    // ── 灵敏度参数 ──
    float rotateSensitivity = 0.15f;          // 旋转灵敏度（像素 -> 角度的映射系数）
    float panSensitivity    = 0.001f;         // 平移灵敏度（相对于当前距离的比例系数）
    float zoomSensitivity   = 0.3f;           // 缩放灵敏度（滚轮增量 -> 距离变化的映射系数）
    float minDist = 0.1f, maxDist = 50.0f;    // 缩放距离的上下限

    /**
     * @brief 计算相机在世界空间中的位置
     * @return 相机眼睛位置的三维坐标
     *
     * 利用球坐标转换：以 target 为中心，根据 yaw、pitch、distance
     * 计算出相机的世界坐标位置。
     */
    glm::vec3 eye() const;

    /**
     * @brief 生成观察矩阵（View Matrix）
     * @return 4x4 观察矩阵，用于将世界坐标变换到相机空间
     */
    glm::mat4 viewMatrix() const;

    /**
     * @brief 处理旋转输入
     * @param dx 鼠标水平移动量（像素）
     * @param dy 鼠标垂直移动量（像素）
     *
     * 更新 yaw 和 pitch 角度，允许 360° 自由旋转（无万向锁限制）。
     */
    void rotate(float dx, float dy);

    /**
     * @brief 处理平移输入
     * @param dx 鼠标水平移动量（像素）
     * @param dy 鼠标垂直移动量（像素）
     *
     * 根据当前 yaw 角度计算相机的右方向和上方向，
     * 平移速度与距离成正比（远处平移更快，近处更精细）。
     */
    void pan(float dx, float dy);

    /**
     * @brief 处理缩放输入
     * @param delta 滚轮增量（正值放大，负值缩小）
     *
     * 调整 distance 并限制在 [minDist, maxDist] 范围内。
     */
    void zoom(float delta);
};
