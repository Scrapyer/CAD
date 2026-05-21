/**
 * @file ResultPanel.cpp
 * @brief 右侧结果面板实现
 *
 * 级联选择：工况 → 类型 → 分量 → 色谱
 */

#include "ResultPanel.h"
#include "Theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QSignalBlocker>

#include <algorithm>
#include <cmath>

ResultPanel::ResultPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void ResultPanel::setupUI()
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(10);

    // ── 左列：结果显示 ──
    resultGroup_ = new QGroupBox("结果显示");
    resultGroup_->setMaximumWidth(360);
    auto* cardLayout = new QVBoxLayout(resultGroup_);
    cardLayout->setContentsMargins(8, 8, 8, 8);
    cardLayout->setSpacing(6);

    auto addRow = [&](const QString& text, QComboBox*& combo, bool enabled = false) {
        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        auto* label = new QLabel(text);
        rowLabels_.push_back(label);
        label->setFixedWidth(36);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(label);
        combo = new QComboBox;
        combo->setEnabled(enabled);
        combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        row->addWidget(combo);
        cardLayout->addLayout(row);
    };

    addRow("帧", subcaseCombo_);
    addRow("类型", typeCombo_);
    addRow("分量", componentCombo_);

    colormapCombo_ = new QComboBox;
    colormapCombo_->addItem("Jet");
    colormapCombo_->setCurrentIndex(0);
    colormapCombo_->hide();

    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(6);
    applyBtn_ = new QPushButton("应用");
    applyBtn_->setEnabled(false);
    btnRow->addWidget(applyBtn_);
    clearBtn_ = new QPushButton("清除");
    clearBtn_->setEnabled(false);
    btnRow->addWidget(clearBtn_);
    cardLayout->addLayout(btnRow);

    infoLabel_ = new QLabel;
    infoLabel_->setWordWrap(true);
    cardLayout->addWidget(infoLabel_);
    cardLayout->addStretch(1);

    layout->addWidget(resultGroup_);

    // ── 中列：变形显示 ──
    deformGroup_ = new QGroupBox("变形显示");
    deformGroup_->setMaximumWidth(320);
    auto* deformLayout = new QVBoxLayout(deformGroup_);
    deformLayout->setContentsMargins(8, 8, 8, 8);
    deformLayout->setSpacing(6);

    {
        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        auto* label = new QLabel("比例");
        rowLabels_.push_back(label);
        label->setFixedWidth(36);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(label);

        scaleSpinBox_ = new QDoubleSpinBox;
        scaleSpinBox_->setRange(0.0, 1e9);
        scaleSpinBox_->setDecimals(2);
        scaleSpinBox_->setValue(1.0);
        scaleSpinBox_->setSingleStep(0.1);
        scaleSpinBox_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        row->addWidget(scaleSpinBox_);

        autoScaleBtn_ = new QPushButton("自动");
        autoScaleBtn_->setFixedWidth(48);
        row->addWidget(autoScaleBtn_);

        deformLayout->addLayout(row);
    }

    overlayCheck_ = new QCheckBox("叠加原始模型");
    deformLayout->addWidget(overlayCheck_);

    {
        auto* btnRow2 = new QHBoxLayout;
        btnRow2->setSpacing(6);
        deformApplyBtn_ = new QPushButton("应用变形");
        btnRow2->addWidget(deformApplyBtn_);
        deformClearBtn_ = new QPushButton("清除变形");
        deformClearBtn_->setEnabled(false);
        btnRow2->addWidget(deformClearBtn_);
        deformLayout->addLayout(btnRow2);
    }

    // 动画控制行
    {
        auto* animRow = new QHBoxLayout;
        animRow->setSpacing(4);

        playBtn_ = new QPushButton("▶");
        pauseBtn_ = new QPushButton("⏸");
        stopBtn_ = new QPushButton("■");
        playBtn_->setFixedSize(32, 28);
        pauseBtn_->setFixedSize(32, 28);
        stopBtn_->setFixedSize(32, 28);
        playBtn_->setToolTip("播放动画");
        pauseBtn_->setToolTip("暂停动画");
        stopBtn_->setToolTip("停止动画");

        animRow->addStretch(1);
        animRow->addWidget(playBtn_);
        animRow->addWidget(pauseBtn_);
        animRow->addWidget(stopBtn_);
        animRow->addStretch(1);

        deformLayout->addLayout(animRow);
    }
    deformLayout->addStretch(1);

    layout->addWidget(deformGroup_);

    // ── 过滤控制分组 ──
    filterGroup_ = new QGroupBox("过滤");
    auto* filterLayout = new QVBoxLayout(filterGroup_);
    filterLayout->setContentsMargins(8, 12, 8, 8);
    filterLayout->setSpacing(6);

    {
        auto* typeRow = new QHBoxLayout;
        typeRow->addWidget(new QLabel("类型:"));
        filterTypeCombo_ = new QComboBox;
        filterTypeCombo_->setObjectName("filterTypeCombo");
        filterTypeCombo_->addItems({"阈值", "裁剪平面（隐藏一侧）", "切片线（不隐藏）", "等值面"});
        typeRow->addWidget(filterTypeCombo_);
        filterLayout->addLayout(typeRow);
    }

    // 阈值控件
    threshWidget_ = new QWidget;
    {
        auto* tl = new QVBoxLayout(threshWidget_);
        tl->setContentsMargins(0, 0, 0, 0);
        tl->setSpacing(4);
        auto* r1 = new QHBoxLayout;
        r1->addWidget(new QLabel("最小值:"));
        threshMinSpin_ = new QDoubleSpinBox;
        threshMinSpin_->setRange(-1e12, 1e12);
        threshMinSpin_->setDecimals(4);
        r1->addWidget(threshMinSpin_);
        tl->addLayout(r1);
        auto* r2 = new QHBoxLayout;
        r2->addWidget(new QLabel("最大值:"));
        threshMaxSpin_ = new QDoubleSpinBox;
        threshMaxSpin_->setRange(-1e12, 1e12);
        threshMaxSpin_->setDecimals(4);
        threshMaxSpin_->setValue(1000.0);
        r2->addWidget(threshMaxSpin_);
        tl->addLayout(r2);
    }
    filterLayout->addWidget(threshWidget_);

    // 裁剪平面控件
    clipWidget_ = new QWidget;
    {
        auto* cl = new QVBoxLayout(clipWidget_);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(4);
        auto* r1 = new QHBoxLayout;
        r1->addWidget(new QLabel("平面:"));
        planeAxisCombo_ = new QComboBox;
        planeAxisCombo_->setObjectName("planeAxisCombo");
        planeAxisCombo_->addItems({"X 平面", "Y 平面", "Z 平面"});
        planeAxisCombo_->setMinimumWidth(88);
        r1->addWidget(planeAxisCombo_);
        r1->addWidget(new QLabel("位置:"));
        planeOffsetSpin_ = new QDoubleSpinBox;
        planeOffsetSpin_->setObjectName("planeOffsetSpin");
        planeOffsetSpin_->setRange(-1e6, 1e6);
        planeOffsetSpin_->setDecimals(3);
        r1->addWidget(planeOffsetSpin_);
        cl->addLayout(r1);

        planeOffsetSlider_ = new QSlider(Qt::Horizontal);
        planeOffsetSlider_->setObjectName("planeOffsetSlider");
        planeOffsetSlider_->setRange(0, 1000);
        planeOffsetSlider_->setSingleStep(10);
        planeOffsetSlider_->setPageStep(50);
        cl->addWidget(planeOffsetSlider_);

        planeRangeLabel_ = new QLabel("范围: -");
        planeRangeLabel_->setObjectName("planeRangeLabel");
        cl->addWidget(planeRangeLabel_);

        clipSideCheck_ = new QCheckBox("保留正方向");
        clipSideCheck_->setChecked(true);
        cl->addWidget(clipSideCheck_);
    }
    filterLayout->addWidget(clipWidget_);
    clipWidget_->setVisible(false);

    // 切片平面控件（复用裁剪的轴/偏移参数）
    sliceWidget_ = new QWidget;
    {
        auto* sl = new QVBoxLayout(sliceWidget_);
        sl->setContentsMargins(0, 0, 0, 0);
        sl->addWidget(new QLabel("只显示交线，不隐藏单元"));
    }
    filterLayout->addWidget(sliceWidget_);
    sliceWidget_->setVisible(false);

    // 等值面控件
    isoWidget_ = new QWidget;
    {
        auto* il = new QVBoxLayout(isoWidget_);
        il->setContentsMargins(0, 0, 0, 0);
        auto* r1 = new QHBoxLayout;
        r1->addWidget(new QLabel("等值:"));
        isoValueSpin_ = new QDoubleSpinBox;
        isoValueSpin_->setRange(-1e12, 1e12);
        isoValueSpin_->setDecimals(4);
        r1->addWidget(isoValueSpin_);
        il->addLayout(r1);
    }
    filterLayout->addWidget(isoWidget_);
    isoWidget_->setVisible(false);

    {
        auto* btnRow = new QHBoxLayout;
        filterApplyBtn_ = new QPushButton("应用过滤");
        filterClearBtn_ = new QPushButton("清除过滤");
        filterClearBtn_->setEnabled(false);
        btnRow->addWidget(filterApplyBtn_);
        btnRow->addWidget(filterClearBtn_);
        filterLayout->addLayout(btnRow);
    }

    layout->addWidget(filterGroup_);
    layout->addStretch(1);

    // 默认主题在 MainWindow 中统一调用 applyTheme() 设置

    // ── 信号连接 ──
    connect(subcaseCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ResultPanel::onFrameChanged);
    connect(typeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ResultPanel::onTypeChanged);
    connect(componentCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ResultPanel::onComponentChanged);
    connect(applyBtn_, &QPushButton::clicked, this, &ResultPanel::onApplyClicked);
    connect(clearBtn_, &QPushButton::clicked, this, &ResultPanel::onClearClicked);

    // 变形控制
    connect(deformApplyBtn_, &QPushButton::clicked, this, &ResultPanel::onDeformApplyClicked);
    connect(deformClearBtn_, &QPushButton::clicked, this, &ResultPanel::onDeformClearClicked);
    connect(autoScaleBtn_, &QPushButton::clicked, this, &ResultPanel::autoScaleRequested);
    connect(playBtn_, &QPushButton::clicked, this, &ResultPanel::animationPlay);
    connect(pauseBtn_, &QPushButton::clicked, this, &ResultPanel::animationPause);
    connect(stopBtn_, &QPushButton::clicked, this, &ResultPanel::animationStop);

    // 过滤控制
    connect(filterTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        const bool usesPlane = (idx == 1 || idx == 2);
        threshWidget_->setVisible(idx == 0);
        clipWidget_->setVisible(usesPlane);
        clipSideCheck_->setVisible(idx == 1);
        sliceWidget_->setVisible(idx == 2);
        isoWidget_->setVisible(idx == 3);
        if (usesPlane)
            emitPlanePreviewIfActive();
        else
            emit planePreviewCleared();
    });
    connect(planeAxisCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        updatePlaneControlsForAxis(true);
        emitPlanePreviewIfActive();
    });
    connect(planeOffsetSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        updatePlaneSliderFromOffset();
        emitPlanePreviewIfActive();
    });
    connect(planeOffsetSlider_, &QSlider::valueChanged, this, [this](int value) {
        updatePlaneOffsetFromSlider(value);
        emitPlanePreviewIfActive();
    });
    connect(filterApplyBtn_, &QPushButton::clicked, this, &ResultPanel::onFilterApplyClicked);
    connect(filterClearBtn_, &QPushButton::clicked, this, &ResultPanel::onFilterClearClicked);

    updatePlaneControlsForAxis(true);
}

void ResultPanel::setResults(const FEResultData& results)
{
    repo_ = FEResultRepository::fromResultData(results);
    populateFrameCombo();
}

void ResultPanel::setRepository(const FEResultRepository& repo)
{
    repo_ = repo;
    populateFrameCombo();
}

void ResultPanel::populateFrameCombo()
{
    subcaseCombo_->blockSignals(true);
    subcaseCombo_->clear();
    for (int i = 0; i < repo_.frameCount(); ++i) {
        const FEResultFrame* f = repo_.frame(i);
        subcaseCombo_->addItem(QString::fromStdString(f->info.valueLabel));
    }
    subcaseCombo_->blockSignals(false);

    bool hasData = !repo_.empty();
    subcaseCombo_->setEnabled(hasData);

    if (hasData) {
        subcaseCombo_->setCurrentIndex(0);
        onFrameChanged(0);
    }

    infoLabel_->setText(hasData
        ? QString("%1 个帧已加载").arg(repo_.frameCount())
        : "无结果数据");
}

void ResultPanel::clearResults()
{
    repo_.clear();
    subcaseCombo_->clear();
    typeCombo_->clear();
    componentCombo_->clear();
    subcaseCombo_->setEnabled(false);
    typeCombo_->setEnabled(false);
    componentCombo_->setEnabled(false);
    applyBtn_->setEnabled(false);
    clearBtn_->setEnabled(false);
    infoLabel_->setText("");
}

void ResultPanel::onFrameChanged(int index)
{
    (void)index;
    refreshTypes();
}

void ResultPanel::onTypeChanged(int index)
{
    (void)index;
    refreshComponents();
}

void ResultPanel::onComponentChanged(int index)
{
    applyBtn_->setEnabled(index >= 0);
}

void ResultPanel::refreshTypes()
{
    typeCombo_->blockSignals(true);
    typeCombo_->clear();

    int frameIdx = subcaseCombo_->currentIndex();
    const FEResultFrame* f = repo_.frame(frameIdx);
    if (f) {
        for (const auto& rt : f->resultTypes) {
            typeCombo_->addItem(QString::fromStdString(rt.name));
        }
    }

    typeCombo_->blockSignals(false);
    typeCombo_->setEnabled(typeCombo_->count() > 0);

    if (typeCombo_->count() > 0) {
        typeCombo_->setCurrentIndex(0);
        onTypeChanged(0);
    } else {
        refreshComponents();
    }
}

void ResultPanel::refreshComponents()
{
    componentCombo_->blockSignals(true);
    componentCombo_->clear();

    int frameIdx = subcaseCombo_->currentIndex();
    int rtIdx = typeCombo_->currentIndex();
    const FEResultFrame* f = repo_.frame(frameIdx);
    if (f && rtIdx >= 0 && rtIdx < static_cast<int>(f->resultTypes.size())) {
        const auto& rt = f->resultTypes[rtIdx];
        for (const auto& comp : rt.components) {
            componentCombo_->addItem(QString::fromStdString(comp.name));
        }
    }

    componentCombo_->blockSignals(false);
    componentCombo_->setEnabled(componentCombo_->count() > 0);
    applyBtn_->setEnabled(componentCombo_->count() > 0);

    if (componentCombo_->count() > 0)
        componentCombo_->setCurrentIndex(0);
}

void ResultPanel::onApplyClicked()
{
    int frameIdx = subcaseCombo_->currentIndex();
    int rtIdx = typeCombo_->currentIndex();
    int cpIdx = componentCombo_->currentIndex();

    if (frameIdx < 0 || rtIdx < 0 || cpIdx < 0) return;

    const FEResultFrame* f = repo_.frame(frameIdx);
    if (!f) return;
    if (rtIdx >= static_cast<int>(f->resultTypes.size())) return;

    const auto& rt = f->resultTypes[rtIdx];
    if (cpIdx >= static_cast<int>(rt.components.size())) return;

    const auto& comp = rt.components[cpIdx];

    QString title = QString("%1 - %2 - %3")
        .arg(QString::fromStdString(f->info.valueLabel))
        .arg(QString::fromStdString(rt.name))
        .arg(QString::fromStdString(comp.name));

    clearBtn_->setEnabled(true);
    emit applyResult(comp.field, title);
}

void ResultPanel::onClearClicked()
{
    clearBtn_->setEnabled(false);
    emit clearResult();
}

void ResultPanel::setDeformScale(float scale)
{
    scaleSpinBox_->setValue(static_cast<double>(scale));
}

void ResultPanel::onDeformApplyClicked()
{
    deformClearBtn_->setEnabled(true);
    emit deformationRequested(
        static_cast<float>(scaleSpinBox_->value()),
        overlayCheck_->isChecked());
}

void ResultPanel::onDeformClearClicked()
{
    deformClearBtn_->setEnabled(false);
    emit deformationCleared();
}

FEVectorField ResultPanel::currentDisplacement() const
{
    int frameIdx = subcaseCombo_->currentIndex();
    const FEResultFrame* f = repo_.frame(frameIdx);
    if (!f) return {};

    for (const auto& rt : f->resultTypes) {
        if (rt.hasVector)
            return rt.vectorField;
    }
    return {};
}

bool ResultPanel::currentScalarField(FEScalarField& field, QString& title) const
{
    int frameIdx = subcaseCombo_->currentIndex();
    int rtIdx = typeCombo_->currentIndex();
    int cpIdx = componentCombo_->currentIndex();

    if (frameIdx < 0 || rtIdx < 0 || cpIdx < 0) return false;

    const FEResultFrame* f = repo_.frame(frameIdx);
    if (!f) return false;
    if (rtIdx >= static_cast<int>(f->resultTypes.size())) return false;

    const auto& rt = f->resultTypes[rtIdx];
    if (cpIdx >= static_cast<int>(rt.components.size())) return false;

    const auto& comp = rt.components[cpIdx];
    field = comp.field;
    title = QString("%1 - %2 - %3")
        .arg(QString::fromStdString(f->info.valueLabel))
        .arg(QString::fromStdString(rt.name))
        .arg(QString::fromStdString(comp.name));
    return true;
}

int ResultPanel::frameCount() const
{
    return repo_.frameCount();
}

void ResultPanel::setPlaneBounds(const glm::vec3& bbMin, const glm::vec3& bbMax)
{
    planeBbMin_ = bbMin;
    planeBbMax_ = bbMax;
    planeBoundsValid_ = true;
    updatePlaneControlsForAxis(true);
    emitPlanePreviewIfActive();
}

float ResultPanel::planeAxisMin(int axis) const
{
    if (!planeBoundsValid_ || axis < 0 || axis > 2) return -1.0f;
    return planeBbMin_[axis];
}

float ResultPanel::planeAxisMax(int axis) const
{
    if (!planeBoundsValid_ || axis < 0 || axis > 2) return 1.0f;
    return planeBbMax_[axis];
}

void ResultPanel::updatePlaneControlsForAxis(bool resetToCenter)
{
    if (!planeAxisCombo_ || !planeOffsetSpin_ || !planeOffsetSlider_ || !planeRangeLabel_)
        return;

    const int axis = planeAxisCombo_->currentIndex();
    float mn = planeAxisMin(axis);
    float mx = planeAxisMax(axis);
    if (mx < mn) std::swap(mx, mn);
    if (std::abs(mx - mn) < 1.0e-6f) {
        mn -= 1.0f;
        mx += 1.0f;
    }

    updatingPlaneControls_ = true;
    {
        QSignalBlocker spinBlocker(planeOffsetSpin_);
        QSignalBlocker sliderBlocker(planeOffsetSlider_);
        planeOffsetSpin_->setRange(static_cast<double>(mn), static_cast<double>(mx));
        planeOffsetSpin_->setSingleStep(static_cast<double>((mx - mn) / 100.0f));

        if (resetToCenter) {
            planeOffsetSpin_->setValue(static_cast<double>((mn + mx) * 0.5f));
            planeOffsetSlider_->setValue(500);
        } else {
            double clamped = std::max(static_cast<double>(mn),
                                      std::min(static_cast<double>(mx), planeOffsetSpin_->value()));
            planeOffsetSpin_->setValue(clamped);
            const double ratio = (clamped - mn) / (mx - mn);
            planeOffsetSlider_->setValue(static_cast<int>(std::round(ratio * 1000.0)));
        }
    }
    planeRangeLabel_->setText(QString("范围: %1 ~ %2")
        .arg(static_cast<double>(mn), 0, 'g', 6)
        .arg(static_cast<double>(mx), 0, 'g', 6));
    updatingPlaneControls_ = false;
}

void ResultPanel::updatePlaneSliderFromOffset()
{
    if (updatingPlaneControls_ || !planeOffsetSpin_ || !planeOffsetSlider_) return;

    const int axis = planeAxisCombo_->currentIndex();
    const float mn = planeAxisMin(axis);
    const float mx = planeAxisMax(axis);
    const float span = mx - mn;
    if (std::abs(span) < 1.0e-6f) return;

    const double ratio = (planeOffsetSpin_->value() - mn) / span;
    const int sliderValue = static_cast<int>(std::round(std::max(0.0, std::min(1.0, ratio)) * 1000.0));
    QSignalBlocker blocker(planeOffsetSlider_);
    planeOffsetSlider_->setValue(sliderValue);
}

void ResultPanel::updatePlaneOffsetFromSlider(int sliderValue)
{
    if (updatingPlaneControls_ || !planeOffsetSpin_) return;

    const int axis = planeAxisCombo_->currentIndex();
    const float mn = planeAxisMin(axis);
    const float mx = planeAxisMax(axis);
    const double ratio = std::max(0, std::min(1000, sliderValue)) / 1000.0;
    const double value = static_cast<double>(mn) + (static_cast<double>(mx) - mn) * ratio;

    QSignalBlocker blocker(planeOffsetSpin_);
    planeOffsetSpin_->setValue(value);
}

void ResultPanel::emitPlanePreviewIfActive()
{
    if (!filterTypeCombo_ || !planeAxisCombo_ || !planeOffsetSpin_) return;

    const int filterType = filterTypeCombo_->currentIndex();
    if (filterType != 1 && filterType != 2) return;

    glm::vec3 normal(0.0f);
    const int axis = planeAxisCombo_->currentIndex();
    if (axis < 0 || axis > 2) return;
    normal[axis] = 1.0f;

    const float offset = static_cast<float>(planeOffsetSpin_->value());
    emit planePreviewChanged(normal * offset, normal);
}

void ResultPanel::selectFrame(int frameIndex)
{
    if (frameIndex < 0 || frameIndex >= subcaseCombo_->count()) return;

    int typeIdx = typeCombo_->currentIndex();
    int compIdx = componentCombo_->currentIndex();

    subcaseCombo_->setCurrentIndex(frameIndex);

    if (typeIdx >= 0 && typeIdx < typeCombo_->count())
        typeCombo_->setCurrentIndex(typeIdx);
    if (compIdx >= 0 && compIdx < componentCombo_->count())
        componentCombo_->setCurrentIndex(compIdx);
}

void ResultPanel::applyFrame(int frameIndex)
{
    selectFrame(frameIndex);
    onApplyClicked();
}

void ResultPanel::applyTheme(const Theme& t) {
    // 按钮单独设样式
    applyBtn_->setStyleSheet(QString(
        "QPushButton {"
        "  background: %1; color: %2; border: none;"
        "  border-radius: 5px; padding: 7px 16px; font-size: 12px; font-weight: bold; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:pressed { background: %4; }"
        "QPushButton:disabled { background: %5; color: %6; }"
    ).arg(t.blue, t.btnText, t.blueHover, t.bluePressed, t.surface1, t.overlay0));

    QString secondaryBtnStyle = QString(
        "QPushButton {"
        "  background: %1; color: %2; border: 1px solid %3;"
        "  border-radius: 5px; padding: 7px 16px; font-size: 12px; }"
        "QPushButton:hover { background: %3; border-color: %4; color: %4; }"
        "QPushButton:pressed { background: %5; }"
        "QPushButton:disabled { background: %6; color: %3; border-color: %1; }"
    ).arg(t.surface0, t.text, t.surface1, t.blue, t.surface2, t.base);

    clearBtn_->setStyleSheet(secondaryBtnStyle);
    deformClearBtn_->setStyleSheet(secondaryBtnStyle);
    filterClearBtn_->setStyleSheet(secondaryBtnStyle);

    deformApplyBtn_->setStyleSheet(QString(
        "QPushButton {"
        "  background: %1; color: %2; border: none;"
        "  border-radius: 5px; padding: 7px 16px; font-size: 12px; font-weight: bold; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:pressed { background: %4; }"
        "QPushButton:disabled { background: %5; color: %6; }"
    ).arg(t.blue, t.btnText, t.blueHover, t.bluePressed, t.surface1, t.overlay0));

    filterApplyBtn_->setStyleSheet(QString(
        "QPushButton {"
        "  background: %1; color: %2; border: none;"
        "  border-radius: 5px; padding: 7px 16px; font-size: 12px; font-weight: bold; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:pressed { background: %4; }"
        "QPushButton:disabled { background: %5; color: %6; }"
    ).arg(t.blue, t.btnText, t.blueHover, t.bluePressed, t.surface1, t.overlay0));

    autoScaleBtn_->setStyleSheet(QString(
        "QPushButton {"
        "  background: %1; color: %2; border: 1px solid %3;"
        "  border-radius: 5px; padding: 5px 8px; font-size: 11px; }"
        "QPushButton:hover { background: %3; color: %4; }"
    ).arg(t.surface0, t.text, t.surface1, t.blue));

    QString animBtnStyle = QString(
        "QPushButton {"
        "  background: %1; color: %2; border: 1px solid %3;"
        "  border-radius: 5px; padding: 4px; font-size: 14px; }"
        "QPushButton:hover { background: %3; color: %4; }"
    ).arg(t.surface0, t.text, t.surface1, t.blue);
    playBtn_->setStyleSheet(animBtnStyle);
    pauseBtn_->setStyleSheet(animBtnStyle);
    stopBtn_->setStyleSheet(animBtnStyle);

    // 卡片 + ComboBox 统一主题
    setStyleSheet(QString(
        "QWidget { background: %1; color: %2; }"
        "QGroupBox {"
        "  background: %3; border: 1px solid %4;"
        "  border-radius: 8px; margin-top: 14px; padding: 14px 10px 10px 10px;"
        "  font-weight: bold; font-size: 12px; color: %5; }"
        "QGroupBox::title {"
        "  subcontrol-origin: margin; left: 12px; padding: 0 6px;"
        "  color: %6; }"
        "QLabel { font-size: 11px; color: %7; padding: 1px 0; }"
        "QComboBox {"
        "  background: %4; border: 1px solid %8; border-radius: 5px;"
        "  padding: 5px 10px; font-size: 12px; color: %2;"
        "  min-height: 24px; }"
        "QComboBox:hover { border-color: %6; }"
        "QComboBox:disabled { background: %1; color: %8; border-color: %4; }"
        "QComboBox::drop-down { border: none; width: 24px; }"
        "QComboBox::down-arrow {"
        "  image: none; border-left: 5px solid transparent;"
        "  border-right: 5px solid transparent;"
        "  border-top: 6px solid %6; margin-right: 8px; }"
        "QComboBox QAbstractItemView {"
        "  background: %3; border: 1px solid %8; border-radius: 4px;"
        "  padding: 4px; selection-background-color: %8; color: %2;"
        "  outline: none; }"
        "QDoubleSpinBox {"
        "  background: %4; border: 1px solid %8; border-radius: 5px;"
        "  padding: 5px 10px; font-size: 12px; color: %2;"
        "  min-height: 24px; }"
        "QDoubleSpinBox:hover { border-color: %6; }"
        "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {"
        "  width: 18px; border: none; background: %4; }"
        "QDoubleSpinBox::up-arrow { border-left: 4px solid transparent;"
        "  border-right: 4px solid transparent;"
        "  border-bottom: 5px solid %6; }"
        "QDoubleSpinBox::down-arrow { border-left: 4px solid transparent;"
        "  border-right: 4px solid transparent;"
        "  border-top: 5px solid %6; }"
        "QCheckBox { font-size: 11px; color: %7; spacing: 6px; }"
        "QCheckBox::indicator {"
        "  width: 16px; height: 16px; border-radius: 3px;"
        "  border: 1px solid %8; background: %4; }"
        "QCheckBox::indicator:checked {"
        "  background: %6; border-color: %6; }"
    ).arg(t.base, t.text, t.mantle, t.surface0, t.subtext0, t.blue, t.subtext1, t.surface1));
}

void ResultPanel::onFilterApplyClicked()
{
    int type = filterTypeCombo_->currentIndex();
    switch (type) {
        case 0: // 阈值
            emit thresholdRequested(threshMinSpin_->value(), threshMaxSpin_->value());
            break;
        case 1: { // 裁剪平面
            glm::vec3 normal(0.0f);
            int axis = planeAxisCombo_->currentIndex();
            normal[axis] = 1.0f;
            float offset = static_cast<float>(planeOffsetSpin_->value());
            glm::vec3 origin = normal * offset;
            emit clipPlaneRequested(origin, normal, clipSideCheck_->isChecked());
            break;
        }
        case 2: { // 切片平面
            glm::vec3 normal(0.0f);
            int axis = planeAxisCombo_->currentIndex();
            normal[axis] = 1.0f;
            float offset = static_cast<float>(planeOffsetSpin_->value());
            glm::vec3 origin = normal * offset;
            emit slicePlaneRequested(origin, normal);
            break;
        }
        case 3: // 等值面
            emit isoSurfaceRequested(static_cast<float>(isoValueSpin_->value()));
            break;
    }
    filterApplyBtn_->setEnabled(true);
    filterClearBtn_->setEnabled(true);
}

void ResultPanel::onFilterClearClicked()
{
    emit filterCleared();
    filterClearBtn_->setEnabled(false);
}
