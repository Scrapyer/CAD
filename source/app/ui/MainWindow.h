/**
 * @file MainWindow.h
 * @brief 主窗口声明
 *
 * Tecplot 风格布局：
 *   - Tecplot 式菜单栏 + 紧凑快捷工具栏
 *   - 左侧停靠：项目树 / 部件列表
 *   - 中央渲染视口 + 底部工作流标签页
 *   - 右侧停靠：选择详情 / 显示控制
 *   - 状态栏
 */

#pragma once

#include <QMainWindow>
#include <QActionGroup>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QToolBar>
#include <QMenu>
#include <QStringList>
#include <QDockWidget>
#include <QDialog>
#include <QTreeWidget>

#include "Theme.h"
#include "FERenderData.h"
#include "FEModel.h"
#include "FEField.h"
#include "ImportPathState.h"
#include "PostState.h"
#include "RenderBackend.h"

class RenderViewport;
class FEModelPanel;
class PartsPanel;
class ResultPanel;
class FEAnimationController;

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
    QWidget* createInspectorPanel();
    QWidget* createDisplayControlPanel();
    QDialog* createPostDialog(const QString& title, ResultPanel* panel);
    void showPostDialog(QDialog* dialog);
    void applyTheme(const Theme& theme);
    void updateRhiActionText();
    void syncSidebarActions();
    void updateProjectTreeSummary();
    void updateStatusSummaries();

    void browseModelFile();
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

    // 工具栏拾取模式动作组
    QActionGroup*  pickGroup_        = nullptr;
    QActionGroup*  rhiGroup_         = nullptr;
    QActionGroup*  displayModeGroup_ = nullptr;

    // 状态栏
    QLabel*        statusLabel_    = nullptr;
    QProgressBar*  statusProgress_ = nullptr;
    QLabel*        progressText_   = nullptr;
    QLabel*        fpsSummaryLabel_      = nullptr;
    QLabel*        frameTimeSummaryLabel_ = nullptr;
    QLabel*        vertexSummaryLabel_   = nullptr;
    QLabel*        triangleSummaryLabel_ = nullptr;

    // 最近的导入路径
    ImportPathState importPaths_;

    // 侧边栏停靠
    QDockWidget*   partsDock_      = nullptr;   // 左侧：模型结构
    QDockWidget*   modelInfoDock_  = nullptr;   // 右侧：属性/控制
    QWidget*       modelNavigatorPanel_ = nullptr;
    QWidget*       inspectorPanel_      = nullptr;
    QWidget*       displayControlPanel_ = nullptr;
    QTreeWidget*   projectTree_         = nullptr;
    int            loadedResultFrameCount_ = 0;

    // 后处理显示状态
    DeformState     deform_;
    PostEffectState postEffect_;
    ContourState    contour_;

    // 主题相关
    Theme          currentTheme_;
    QToolBar*      toolbar_        = nullptr;   // Tecplot 式快捷工具栏
    QToolBar*      postToolBar_    = nullptr;
    QAction*       themeAction_    = nullptr;
    QMenu*         themeMenu_      = nullptr;
    QAction*       rhiAction_      = nullptr;
    QMenu*         rhiMenu_        = nullptr;
    QAction*       sidebarsAction_ = nullptr;
    QAction*       leftPanelAction_ = nullptr;
    QAction*       rightPanelAction_ = nullptr;
    int            themeIndex_     = 0;
};
