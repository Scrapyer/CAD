/**
 * @file MainWindow.h
 * @brief 主窗口声明
 *
 * HyperView 风格布局：
 *   - 工具栏（拾取模式切换 + 主题选择）
 *   - 左侧停靠：部件面板
 *   - 中央渲染视口 + 底部标签页（文件导入 / 结果显示 / 监控）
 *   - 右侧停靠：模型信息面板
 *   - 状态栏
 */

#pragma once

#include <QMainWindow>
#include <QActionGroup>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QToolBar>
#include <QMenu>
#include <QTabWidget>
#include <QDockWidget>

#include "Theme.h"
#include "FERenderData.h"
#include "FEModel.h"
#include "FEField.h"
#include "ImportPathState.h"
#include "PostState.h"
#include "RenderBackend.h"

class RenderViewport;
class MonitorPanel;
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
    void setupToolBar();
    void setupStatusBar();
    QWidget* createFilePanel();
    void applyTheme(const Theme& theme);
    void updateRhiActionText();

    void browseModelFile();
    void browseResultFile();
    void applyFiles();
    void refreshFilePathEdits();
    void syncImportPathsFromEdits();

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
    MonitorPanel*          monitorPanel_    = nullptr;
    FEModelPanel*          feModelPanel_    = nullptr;
    PartsPanel*            partsPanel_      = nullptr;
    ResultPanel*           resultPanel_     = nullptr;
    FEAnimationController* animController_  = nullptr;

    // 工具栏拾取模式动作组
    QActionGroup*  pickGroup_     = nullptr;
    QActionGroup*  rhiGroup_      = nullptr;

    // 状态栏
    QLabel*        statusLabel_    = nullptr;
    QProgressBar*  statusProgress_ = nullptr;
    QLabel*        progressText_   = nullptr;

    // 底部标签页面板
    QTabWidget*    bottomTabs_     = nullptr;
    QLineEdit*     modelPathEdit_  = nullptr;
    QLineEdit*     resultPathEdit_ = nullptr;
    ImportPathState importPaths_;

    // 侧边栏停靠
    QDockWidget*   partsDock_      = nullptr;
    QDockWidget*   modelInfoDock_  = nullptr;

    // 后处理显示状态
    DeformState     deform_;
    PostEffectState postEffect_;
    ContourState    contour_;

    // 主题相关
    Theme          currentTheme_;
    QWidget*       filePanel_      = nullptr;
    QToolBar*      toolbar_        = nullptr;
    QPushButton*   filePanelApplyBtn_ = nullptr;
    QAction*       themeAction_    = nullptr;
    QMenu*         themeMenu_      = nullptr;
    QAction*       rhiAction_      = nullptr;
    QMenu*         rhiMenu_        = nullptr;
    int            themeIndex_     = 0;
};
