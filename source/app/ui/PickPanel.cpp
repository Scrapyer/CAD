/**
 * @file PickPanel.cpp
 * @brief 右侧拾取控制面板实现
 *
 * 单个 QGroupBox 卡片，内含拾取模式、显示控制、ID标签三个区域。
 * 显隐和标签的操作对象由当前拾取模式决定。
 */

#include "PickPanel.h"
#include "Theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>

PickPanel::PickPanel(QWidget* parent)
    : QWidget(parent)
{
    setMinimumWidth(140);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(6);

    // ════════════════════════════════════════
    // 单个卡片：拾取
    // ════════════════════════════════════════
    pickGroup_ = new QGroupBox("拾取");
    auto* cardLayout = new QVBoxLayout(pickGroup_);
    cardLayout->setContentsMargins(8, 8, 8, 8);
    cardLayout->setSpacing(6);

    // ── 拾取模式 ──
    modeGroup_ = new QButtonGroup(this);
    modeGroup_->setExclusive(true);

    nodeRadio_ = new QRadioButton("节点");
    elemRadio_ = new QRadioButton("单元");
    partRadio_ = new QRadioButton("部件");
    nodeRadio_->setChecked(true);

    modeGroup_->addButton(nodeRadio_, 0);  // PickMode::Node
    modeGroup_->addButton(elemRadio_, 1);  // PickMode::Element
    modeGroup_->addButton(partRadio_, 2);  // PickMode::Part

    auto* modeRow = new QHBoxLayout;
    modeRow->setSpacing(10);
    modeRow->addWidget(nodeRadio_);
    modeRow->addWidget(elemRadio_);
    modeRow->addWidget(partRadio_);
    modeRow->addStretch();
    cardLayout->addLayout(modeRow);

    // ── 分隔线 ──
    auto* sep1 = new QFrame;
    sep1->setFrameShape(QFrame::HLine);
    sep1->setFrameShadow(QFrame::Plain);
    sep1->setFixedHeight(1);
    cardLayout->addWidget(sep1);

    // ── 显示控制 ──
    visCheck_ = new QCheckBox("显示");
    visCheck_->setChecked(true);
    cardLayout->addWidget(visCheck_);

    // ── 分隔线 ──
    auto* sep2 = new QFrame;
    sep2->setFrameShape(QFrame::HLine);
    sep2->setFrameShadow(QFrame::Plain);
    sep2->setFixedHeight(1);
    cardLayout->addWidget(sep2);

    // ── ID标签 ──
    labelCheck_ = new QCheckBox("标签");
    labelCheck_->setChecked(false);
    cardLayout->addWidget(labelCheck_);

    layout->addWidget(pickGroup_);

    connect(modeGroup_, &QButtonGroup::idClicked,
            this, &PickPanel::pickModeChanged);

    connect(visCheck_,   &QCheckBox::toggled, this, &PickPanel::visibilityChanged);
    connect(labelCheck_, &QCheckBox::toggled, this, &PickPanel::labelChanged);
}

void PickPanel::applyTheme(const Theme& t)
{
    setStyleSheet(QString(
        "QWidget { background: %1; color: %2; }"
        "QGroupBox {"
        "  background: %3; border: 1px solid %4;"
        "  border-radius: 8px; margin-top: 14px; padding: 14px 10px 10px 10px;"
        "  font-weight: bold; font-size: 12px; color: %5; }"
        "QGroupBox::title {"
        "  subcontrol-origin: margin; left: 12px; padding: 0 6px;"
        "  color: %6; }"
        "QLabel { font-size: 11px; font-weight: bold; color: %7; }"
        "QRadioButton { font-size: 12px; color: %2; spacing: 6px; padding: 2px 0; }"
        "QRadioButton::indicator {"
        "  width: 16px; height: 16px; border-radius: 8px;"
        "  border: 2px solid %8; background: %4; }"
        "QRadioButton::indicator:checked {"
        "  background: %6; border-color: %6; }"
        "QRadioButton::indicator:hover { border-color: %6; }"
        "QCheckBox { font-size: 12px; color: %2; spacing: 8px; padding: 2px 0; }"
        "QCheckBox::indicator {"
        "  width: 16px; height: 16px; border-radius: 4px;"
        "  border: 2px solid %8; background: %4; }"
        "QCheckBox::indicator:checked {"
        "  background: %6; border-color: %6; }"
        "QCheckBox::indicator:hover { border-color: %6; }"
        "QFrame[frameShape=\"4\"] { background: %4; border: none; margin: 4px 0; }"
    ).arg(t.base, t.text, t.mantle, t.surface0, t.subtext0, t.blue, t.subtext1, t.surface2));
}
