/**
 * @file ResultPanel.h
 * @brief 右侧结果面板声明
 *
 * 提供工况/类型/分量/色谱的级联选择，控制云图显示。
 */

#pragma once

#include <QWidget>
#include <QComboBox>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QSlider>

#include <glm/glm.hpp>
#include "FEResultData.h"
#include "FEResultRepository.h"
#include "FEField.h"

struct Theme;

class ResultPanel : public QWidget {
    Q_OBJECT

public:
    explicit ResultPanel(QWidget* parent = nullptr);

    /** @brief 应用主题 */
    void applyTheme(const Theme& theme);

    /** @brief 设置结果数据（来自 OP2 解析，兼容旧接口） */
    void setResults(const FEResultData& results);

    /** @brief 设置结果仓库（帧/类型/分量三层选择） */
    void setRepository(const FEResultRepository& repo);

    /** @brief 清空面板 */
    void clearResults();

    /** @brief 设置变形比例（供 MainWindow 自动比例回填） */
    void setDeformScale(float scale);

    /** @brief 获取当前帧的位移矢量场（无位移数据时返回空） */
    FEVectorField currentDisplacement() const;

    /** @brief 获取当前选中的标量场和标题（未选中时 field 为空） */
    bool currentScalarField(FEScalarField& field, QString& title) const;

    /** @brief 当前结果帧数 */
    int frameCount() const;

    /** @brief 设置裁剪/切片平面的可用模型范围 */
    void setPlaneBounds(const glm::vec3& bbMin, const glm::vec3& bbMax);

public slots:
    /** @brief 跳到指定帧并应用云图（动画控制器调用） */
    void applyFrame(int frameIndex);

    /** @brief 只切换帧选择（不触发信号，供 MainWindow 控制顺序） */
    void selectFrame(int frameIndex);

signals:
    /** @brief 用户点击应用，发射标量场和标题 */
    void applyResult(const FEScalarField& field, const QString& title);

    /** @brief 用户点击清除 */
    void clearResult();

    /** @brief 用户请求应用变形 */
    void deformationRequested(float scale, bool overlayUndeformed);

    /** @brief 用户请求清除变形 */
    void deformationCleared();

    /** @brief 用户请求自动计算变形比例 */
    void autoScaleRequested();

    /** @brief 动画播放 */
    void animationPlay();

    /** @brief 动画暂停 */
    void animationPause();

    /** @brief 动画停止 */
    void animationStop();

    /** @brief 阈值过滤请求 */
    void thresholdRequested(float minVal, float maxVal);

    /** @brief 裁剪平面请求 */
    void clipPlaneRequested(const glm::vec3& origin, const glm::vec3& normal, bool keepPositive);

    /** @brief 切片平面请求 */
    void slicePlaneRequested(const glm::vec3& origin, const glm::vec3& normal);

    /** @brief 等值面请求 */
    void isoSurfaceRequested(float isoValue);

    /** @brief 清除所有过滤 */
    void filterCleared();

    /** @brief 裁剪/切片平面预览参数变化 */
    void planePreviewChanged(const glm::vec3& origin, const glm::vec3& normal);

    /** @brief 隐藏裁剪/切片平面预览 */
    void planePreviewCleared();

private slots:
    void onFrameChanged(int index);
    void onTypeChanged(int index);
    void onComponentChanged(int index);
    void onApplyClicked();
    void onClearClicked();
    void onDeformApplyClicked();
    void onDeformClearClicked();
    void onFilterApplyClicked();
    void onFilterClearClicked();

private:
    void setupUI();
    void populateFrameCombo();
    void refreshTypes();
    void refreshComponents();
    void updatePlaneControlsForAxis(bool resetToCenter);
    void updatePlaneSliderFromOffset();
    void updatePlaneOffsetFromSlider(int sliderValue);
    void emitPlanePreviewIfActive();
    float planeAxisMin(int axis) const;
    float planeAxisMax(int axis) const;

    FEResultRepository repo_;

    QComboBox* subcaseCombo_   = nullptr;
    QComboBox* typeCombo_      = nullptr;
    QComboBox* componentCombo_ = nullptr;
    QComboBox* colormapCombo_  = nullptr;
    QPushButton* applyBtn_     = nullptr;
    QPushButton* clearBtn_     = nullptr;
    QGroupBox* resultGroup_    = nullptr;
    QLabel* infoLabel_         = nullptr;
    std::vector<QLabel*> rowLabels_;

    // ── 变形控制 ──
    QGroupBox* deformGroup_       = nullptr;
    QDoubleSpinBox* scaleSpinBox_ = nullptr;
    QPushButton* autoScaleBtn_    = nullptr;
    QCheckBox* overlayCheck_      = nullptr;
    QPushButton* deformApplyBtn_  = nullptr;
    QPushButton* deformClearBtn_  = nullptr;

    // ── 动画控制 ──
    QPushButton* playBtn_  = nullptr;
    QPushButton* pauseBtn_ = nullptr;
    QPushButton* stopBtn_  = nullptr;

    // ── 过滤控制 ──
    QGroupBox* filterGroup_           = nullptr;
    QComboBox* filterTypeCombo_       = nullptr;

    // 阈值
    QDoubleSpinBox* threshMinSpin_    = nullptr;
    QDoubleSpinBox* threshMaxSpin_    = nullptr;

    // 裁剪/切片平面
    QComboBox* planeAxisCombo_        = nullptr;
    QDoubleSpinBox* planeOffsetSpin_  = nullptr;
    QSlider* planeOffsetSlider_       = nullptr;
    QLabel* planeRangeLabel_          = nullptr;
    QCheckBox* clipSideCheck_         = nullptr;

    // 等值面
    QDoubleSpinBox* isoValueSpin_     = nullptr;

    QPushButton* filterApplyBtn_      = nullptr;
    QPushButton* filterClearBtn_      = nullptr;

    // 过滤分组中的 widget 容器，按过滤类型显隐
    QWidget* threshWidget_  = nullptr;
    QWidget* clipWidget_    = nullptr;
    QWidget* sliceWidget_   = nullptr;
    QWidget* isoWidget_     = nullptr;

    glm::vec3 planeBbMin_{0.0f};
    glm::vec3 planeBbMax_{0.0f};
    bool planeBoundsValid_ = false;
    bool updatingPlaneControls_ = false;
};
