/**
 * @file MainWindow.h
 * @brief 主窗口声明
 *
 * Tecplot 风格布局：
 *   - Tecplot 式菜单栏 + 紧凑快捷工具栏
 *   - 左侧停靠：项目树 / 部件列表
 *   - 中央渲染视口 + 底部工作流标签页
 *   - 状态栏
 */

#pragma once

#include <QMainWindow>
#include <QActionGroup>
#include <QColor>
#include <QLabel>
#include <QProgressBar>
#include <QToolBar>
#include <QMenu>
#include <QStringList>
#include <QDockWidget>
#include <QDialog>
#include <QTreeWidget>

#include <unordered_set>

#include "Theme.h"
#include "FERenderData.h"
#include "FEModel.h"
#include "FEField.h"
#include "FEPickResult.h"
#include "ImportPathState.h"
#include "PostState.h"
#include "RenderBackend.h"

class RenderViewport;
class FEModelPanel;
class PartsPanel;
class ResultPanel;
class FEAnimationController;
class ViewportContextMenu;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupMenuBar();
    void setupToolBar();
    void setupStatusBar();
    QWidget* createModelNavigatorPanel();
    QDialog* createPostDialog(const QString& title, ResultPanel* panel);
    void showPostDialog(QDialog* dialog);
    void applyTheme(const Theme& theme);
    void loadBackgroundSettings();
    void saveBackgroundSettings() const;
    void applyViewportBackground();
    void showBackgroundSettingsDialog();
    void showModelInfoDialog();
    void updateRhiActionText();
    void setModelStructureVisible(bool visible);
    void syncSidebarActions();
    void updateProjectTreeSummary();
    void updateStatusSummaries();
    void updatePickModeSummary(PickMode mode);
    std::vector<int> selectedPartIndicesForVisibility() const;
    std::vector<int> selectedElementIdsForVisibility() const;
    std::vector<int> allModelElementIds() const;
    bool areVisibilityPartsVisible(const std::vector<int>& partIndices) const;
    bool anyVisibilityPartsVisible(const std::vector<int>& partIndices) const;
    bool anyVisibilityPartsHidden(const std::vector<int>& partIndices) const;
    bool anyVisibilityElementsVisible(const std::vector<int>& elementIds) const;
    bool anyVisibilityElementsHidden(const std::vector<int>& elementIds) const;
    bool hasHiddenModelItems() const;
    bool hasHiddenModelElements() const;
    bool hasHiddenModelParts() const;
    void syncPartVisibilityToViewport();
    void rememberVisibilitySelection(const std::vector<int>& partIndices, bool highlightVisibleParts);
    void hideSelectedModelParts();
    void hideAllModelElements();
    void hideAllModelParts();
    void hideAllModelObjects();
    void showSelectedModelParts();
    void showAllModelElements();
    void showAllModelParts();
    void isolateSelectedModelParts();
    void showAllModelObjects();

    void clearCurrentLayout();
    bool browseModelFile();
    bool browseUnifiedImportFile();
    void browseImportFile();
    void browseResultFile();
    void applyFiles();

    void applyDeformation(float scale, bool overlayUndeformed);
    void clearDeformation();
    void applyContour(const FEScalarField& field, const QString& title);
    void applyThreshold(float minVal, float maxVal);
    void applyClipPlane(const glm::vec3& origin, const glm::vec3& normal, bool keepPositive);
    void applySlicePlane(const glm::vec3& origin, const glm::vec3& normal);
    void applyIsoSurface(float isoValue);
    void clearFilters();
    void updateFilterPlaneBounds();

    const FERenderData& activeRenderData() const;
    const FEModel& activeModel() const;
    const FERenderData& displayRenderData() const;
    void pushRenderDataToGL(const FERenderData& rd);
    void beginPostEffect(PostEffectMode mode);
    void reapplyContourIfNeeded();

    RenderViewport*        renderViewport_  = nullptr;
    FEModelPanel*          feModelPanel_    = nullptr;
    PartsPanel*            partsPanel_      = nullptr;
    ResultPanel*           resultPanel_     = nullptr;
    ResultPanel*           deformationPanel_ = nullptr;
    ResultPanel*           thresholdPanel_   = nullptr;
    FEAnimationController* animController_  = nullptr;
    QDialog*               contourDialog_    = nullptr;
    QDialog*               deformationDialog_ = nullptr;
    QDialog*               thresholdDialog_   = nullptr;
    ViewportContextMenu*   viewportContextMenu_ = nullptr;

    // 工具栏拾取模式动作组
    QActionGroup*  pickGroup_        = nullptr;
    QActionGroup*  rhiGroup_         = nullptr;
    QActionGroup*  displayModeGroup_ = nullptr;

    // 状态栏
    QLabel*        statusLabel_    = nullptr;
    QProgressBar*  statusProgress_ = nullptr;
    QLabel*        progressText_   = nullptr;
    QLabel*        pickModeSummaryLabel_ = nullptr;
    QLabel*        fpsSummaryLabel_      = nullptr;
    QLabel*        frameTimeSummaryLabel_ = nullptr;
    QLabel*        vertexSummaryLabel_   = nullptr;
    QLabel*        triangleSummaryLabel_ = nullptr;
    QLabel*        modelSizeSummaryLabel_ = nullptr;

    // 最近的导入路径
    ImportPathState importPaths_;

    // 侧边栏停靠
    QDockWidget*   partsDock_      = nullptr;   // 左侧：模型结构
    QWidget*       modelNavigatorPanel_ = nullptr;
    QTreeWidget*   projectTree_         = nullptr;
    int            loadedResultFrameCount_ = 0;
    PickMode       lastSelectionMode_ = PickMode::Node;
    std::vector<int> lastSelectionIds_;
    std::vector<int> lastSelectedPartIndices_;
    std::unordered_set<int> hiddenElementIds_;

    // 后处理显示状态
    DeformState     deform_;
    PostEffectState postEffect_;
    ContourState    contour_;

    // 主题相关
    enum class BackgroundMode { Solid = 0, Gradient = 1 };

    Theme          currentTheme_;
    QToolBar*      toolbar_        = nullptr;   // Tecplot 式快捷工具栏
    QToolBar*      postToolBar_    = nullptr;
    QAction*       themeAction_    = nullptr;
    QMenu*         themeMenu_      = nullptr;
    QAction*       rhiAction_      = nullptr;
    QMenu*         rhiMenu_        = nullptr;
    QAction*       sidebarsAction_ = nullptr;
    QAction*       leftPanelAction_ = nullptr;
    int            themeIndex_     = 0;
    BackgroundMode backgroundMode_ = BackgroundMode::Gradient;
    QColor         backgroundSolidColor_{210, 217, 230};
    QColor         backgroundGradientTopColor_{150, 166, 190};
    QColor         backgroundGradientBottomColor_{210, 217, 230};
    bool           viewportGridVisible_ = true;
};
