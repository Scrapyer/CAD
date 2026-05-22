/**
 * @file ControlPanel.h
 * @brief 控制面板声明
 *
 * 左侧边栏中的控制面板，包含三个分组：
 *   - 形状选择：点击按钮切换不同的几何体
 *   - 显示选项：选择渲染模式（实体/线框/混合）和自动旋转
 *   - 颜色选择：下拉菜单切换物体颜色
 *
 * 所有用户操作通过 Qt 信号发射，由 MainWindow 连接到 GLWidget。
 */

#pragma once

#include <QWidget>
#include <QGroupBox>
#include <QLabel>
#include <vector>
#include <glm/glm.hpp>

#include "Geometry.h"

struct Theme;

class ControlPanel : public QWidget {
    Q_OBJECT

public:
    explicit ControlPanel(QWidget* parent = nullptr);

    /** @brief 应用主题 */
    void applyTheme(const Theme& theme);

signals:
    /** @brief 用户选择了新的几何体形状 */
    void shapeSelected(const Mesh& mesh);

    /** @brief 显示模式改变（0=实体, 1=线框, 2=实体+线框） */
    void displayModeChanged(int mode);

    /** @brief 自动旋转开关状态改变 */
    void autoRotateChanged(bool enabled);

    /** @brief 物体颜色改变 */
    void colorChanged(const glm::vec3& color);

private:
    /** @brief 创建形状选择分组（包含 7 个几何体按钮） */
    QGroupBox* createShapeGroup();

    /** @brief 创建显示选项分组（渲染模式 + 自动旋转） */
    QGroupBox* createDisplayGroup();

    /** @brief 创建颜色选择分组（预设颜色下拉菜单） */
    QGroupBox* createColorGroup();

    /** @brief 颜色条目（名称 + RGB 值） */
    struct ColorEntry { QString name; glm::vec3 color; };

    /** @brief 预设颜色列表 */
    std::vector<ColorEntry> colors_;

    /** @brief 底部提示标签（主题更新用） */
    QLabel* infoLabel_ = nullptr;
};
