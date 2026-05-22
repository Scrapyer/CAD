/**
 * @file ControlPanel.cpp
 * @brief 控制面板实现
 */

#include "ControlPanel.h"
#include "Theme.h"

#include <QVBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <functional>

ControlPanel::ControlPanel(QWidget* parent) : QWidget(parent) {
    // 固定面板宽度
    setFixedWidth(200);

    // 垂直布局：形状 → 显示 → 颜色 → 弹性空白 → 操作提示
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);

    layout->addWidget(createShapeGroup());
    layout->addWidget(createDisplayGroup());
    layout->addWidget(createColorGroup());
    layout->addStretch();  // 弹性空白，将底部提示推到最下方

    // 底部操作提示标签
    auto* infoLabel = new QLabel("左键旋转 | 右键平移\n滚轮缩放 | ESC退出");
    infoLabel_ = infoLabel;
    layout->addWidget(infoLabel);

    // 默认主题在 MainWindow 中统一调用 applyTheme() 设置
}

void ControlPanel::applyTheme(const Theme& t) {
    infoLabel_->setStyleSheet(QString("color: %1; font-size: 11px; padding: 4px 0;").arg(t.overlay0));

    setStyleSheet(QString(
        "QWidget { background: %1; color: %2; }"

        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "    stop:0 %3, stop:1 %4);"
        "  border: 1px solid %5; border-radius: 6px;"
        "  padding: 8px 14px; color: %2; font-size: 12px; }"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "    stop:0 %6, stop:1 %7);"
        "  border-color: %8; }"
        "QPushButton:pressed {"
        "  background: %8; color: %9; }"

        "QGroupBox {"
        "  background: %10; border: 1px solid %4;"
        "  border-radius: 8px; margin-top: 14px; padding: 14px 10px 10px 10px;"
        "  font-weight: bold; font-size: 12px; color: %11; }"
        "QGroupBox::title {"
        "  subcontrol-origin: margin; left: 12px; padding: 0 6px;"
        "  color: %8; }"

        "QLabel { font-size: 12px; color: %12; }"

        "QCheckBox { font-size: 12px; color: %12; spacing: 8px; padding: 2px 0; }"
        "QCheckBox::indicator {"
        "  width: 16px; height: 16px; border-radius: 4px;"
        "  border: 2px solid %5; background: %4; }"
        "QCheckBox::indicator:checked {"
        "  background: %8; border-color: %8; }"
        "QCheckBox::indicator:hover { border-color: %8; }"

        "QComboBox {"
        "  background: %4; border: 1px solid %13;"
        "  border-radius: 5px; padding: 5px 10px; color: %2; min-height: 24px; }"
        "QComboBox:hover { border-color: %8; }"
        "QComboBox::drop-down { border: none; width: 24px; }"
        "QComboBox::down-arrow {"
        "  image: none; border-left: 5px solid transparent;"
        "  border-right: 5px solid transparent;"
        "  border-top: 6px solid %8; margin-right: 8px; }"
        "QComboBox QAbstractItemView {"
        "  background: %10; border: 1px solid %13; border-radius: 4px;"
        "  padding: 4px; selection-background-color: %8;"
        "  selection-color: %9; color: %2; outline: none; }"
    ).arg(t.base, t.text, t.gradTop, t.surface0, t.surface2,
          t.gradTopHov, t.gradBotHov, t.blue, t.btnText)
     .arg(t.mantle, t.subtext0, t.subtext1, t.surface1));
}

QGroupBox* ControlPanel::createShapeGroup() {
    auto* group = new QGroupBox("形状");
    auto* layout = new QVBoxLayout(group);

    // 形状条目：显示名称 + 对应的网格生成函数
    struct Entry { QString name; std::function<Mesh()> gen; };
    std::vector<Entry> shapes = {
        {"正方体", []{ return Geometry::cube(); }},
        {"三棱锥", []{ return Geometry::tetrahedron(); }},
        {"三棱柱", []{ return Geometry::triangularPrism(); }},
        {"圆柱",   []{ return Geometry::cylinder(); }},
        {"圆锥",   []{ return Geometry::cone(); }},
        {"球体",   []{ return Geometry::sphere(); }},
        {"圆环",   []{ return Geometry::torus(); }},
    };

    // 为每个形状创建按钮，点击时生成网格并通过信号发射
    for (auto& s : shapes) {
        auto* btn = new QPushButton(s.name);
        layout->addWidget(btn);
        auto gen = s.gen;  // 捕获生成函数到 lambda
        connect(btn, &QPushButton::clicked, this, [this, gen]{
            emit shapeSelected(gen());
        });
    }
    return group;
}

QGroupBox* ControlPanel::createDisplayGroup() {
    auto* group = new QGroupBox("显示");
    auto* layout = new QVBoxLayout(group);

    // 渲染模式下拉框（索引与 GLWidget::DisplayMode 枚举对应）
    auto* modeCombo = new QComboBox;
    modeCombo->addItem("实体");          // 索引 0 → Solid
    modeCombo->addItem("线框");          // 索引 1 → Wireframe
    modeCombo->addItem("实体 + 线框");   // 索引 2 → SolidWireframe
    layout->addWidget(modeCombo);
    connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ControlPanel::displayModeChanged);

    // 自动旋转复选框（默认开启）
    auto* rotateCheck = new QCheckBox("自动旋转");
    rotateCheck->setChecked(true);
    layout->addWidget(rotateCheck);
    connect(rotateCheck, &QCheckBox::toggled,
            this, &ControlPanel::autoRotateChanged);

    return group;
}

QGroupBox* ControlPanel::createColorGroup() {
    auto* group = new QGroupBox("颜色");
    auto* layout = new QVBoxLayout(group);

    // 预设颜色列表
    colors_ = {
        {"天蓝",   {0.4f, 0.7f, 1.0f}},
        {"珊瑚红", {1.0f, 0.5f, 0.4f}},
        {"翡翠绿", {0.2f, 0.9f, 0.5f}},
        {"金色",   {1.0f, 0.84f, 0.0f}},
        {"紫罗兰", {0.7f, 0.4f, 1.0f}},
        {"银白",   {0.85f, 0.85f, 0.9f}},
    };

    // 颜色下拉菜单
    auto* combo = new QComboBox;
    for (auto& c : colors_) combo->addItem(c.name);
    layout->addWidget(combo);

    // 选中颜色时发射信号
    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        if (idx >= 0 && idx < static_cast<int>(colors_.size()))
            emit colorChanged(colors_[idx].color);
    });

    return group;
}
