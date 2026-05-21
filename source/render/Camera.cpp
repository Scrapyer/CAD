/**
 * @file Camera.cpp
 * @brief 轨道相机实现
 */

#include "Camera.h"

#include <glm/gtc/matrix_transform.hpp>  // glm::lookAt, glm::radians, glm::clamp
#include <cmath>                         // cos, sin

glm::vec3 Camera::eye() const {
    // 将角度转换为弧度
    float ry = glm::radians(yaw);
    float rp = glm::radians(pitch);

    // 球坐标 -> 笛卡尔坐标
    // X = cos(pitch) * sin(yaw)   → 水平平面上的左右偏移
    // Y = sin(pitch)              → 上下偏移（仰俯）
    // Z = cos(pitch) * cos(yaw)   → 水平平面上的前后偏移
    return target + distance * glm::vec3(
        cos(rp) * sin(ry),
        sin(rp),
        cos(rp) * cos(ry)
    );
}

glm::mat4 Camera::viewMatrix() const {
    // glm::lookAt(eye, center, up) 生成观察矩阵
    // eye:    相机位置
    // center: 注视目标点
    // up:     根据 pitch 判断是否翻转（当相机倒置时 up 方向取反）
    //         利用 cos(pitch) 的符号判断：pitch 在 (-90°,90°) 区间时 cos>0，up 朝 +Y
    //         pitch 在 (90°,270°)（即 cos<0）时 up 朝 -Y，保证 lookAt 不退化
    float cp = cos(glm::radians(pitch));
    glm::vec3 up(0.0f, (cp >= 0.0f) ? 1.0f : -1.0f, 0.0f);
    return glm::lookAt(eye(), target, up);
}

void Camera::rotate(float dx, float dy) {
    // 当相机倒置时（cos(pitch) < 0），水平拖动方向需要取反，否则左右旋转会反直觉
    float cp = cos(glm::radians(pitch));
    float yawSign = (cp >= 0.0f) ? 1.0f : -1.0f;

    // 鼠标水平移动 → 改变 yaw（左右旋转）
    yaw   -= dx * rotateSensitivity * yawSign;
    // 鼠标垂直移动 → 改变 pitch（上下仰俯），允许自由旋转
    pitch += dy * rotateSensitivity;

    // 将角度归一化到 [-180°, 180°)，避免浮点累积
    yaw   = fmod(yaw,   360.0f);
    pitch = fmod(pitch, 360.0f);
}

void Camera::pan(float dx, float dy) {
    // 从视线方向和世界 up 推导出相机坐标系的真实 right 和 up 向量
    // 这样无论相机俯仰多少度，平移方向都与屏幕像素方向完全对齐
    glm::vec3 viewDir = glm::normalize(target - eye());
    float cp = cos(glm::radians(pitch));
    glm::vec3 worldUp(0.0f, (cp >= 0.0f) ? 1.0f : -1.0f, 0.0f);
    glm::vec3 right  = glm::normalize(glm::cross(viewDir, worldUp));
    glm::vec3 up     = glm::cross(right, viewDir);

    float speed = distance * panSensitivity;

    target -= right * dx * speed;
    target += up    * dy * speed;
}

void Camera::zoom(float delta) {
    // 按比例缩放：每次滚轮缩放 10%，适应任意尺寸模型
    float factor = 1.0f - delta * 0.1f;
    distance *= factor;
    // 限制距离在合理范围内
    distance  = glm::clamp(distance, minDist, maxDist);
}
