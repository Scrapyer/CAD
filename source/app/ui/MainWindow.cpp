/**
 * @file MainWindow.cpp
 * @brief 主窗口实现
 *
 * Tecplot 风格布局：
 *   ┌─────────────────────────────────────────────────────────┐
 *   │ Tecplot 式菜单栏 + 紧凑快捷工具栏                        │
 *   ├────────────┬────────────────────────────────────────────┤
 *   │ 模型结构   │      RenderViewport                         │
 *   │ 项目树/部件│                                            │
 *   ├────────────┴────────────────────────────────────────────┤
 *   │ 状态栏                                                   │
 *   └─────────────────────────────────────────────────────────┘
 */

#include "MainWindow.h"
#include "RenderViewport.h"
#include "FEModelPanel.h"
#include "PartsPanel.h"
#include "ResultPanel.h"
#include "ViewportContextMenu.h"
#include "FEGroup.h"
#include "FEPickResult.h"
#include "FEMeshConverter.h"
#include "FEResultData.h"
#include "FEResultMapper.h"
#include "FEAnimationController.h"
#include "FEDeformation.h"
#include "FEPostFilter.h"
#include "FEIsoSurface.h"
#include "RenderBackendFactory.h"
#include "RenderSettings.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <unordered_set>
#include <utility>
#include <vector>
#include <QVBoxLayout>
#include <QToolBar>
#include <QAction>
#include <QActionGroup>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenuBar>
#include <QStatusBar>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QTreeWidget>
#include <QTimer>
#include <QStyle>
#include <QKeySequence>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include <QCloseEvent>
#include <QStorageInfo>
#include <QUrl>
#include <QColorDialog>

namespace {

constexpr const char* kThemeIndexKey = "appearance/themeIndex";
constexpr const char* kConfigEnvVar = "FEMODELVIEWER_CONFIG_DIR";
constexpr const char* kConfigDirName = "config";
constexpr const char* kSettingsFileName = "settings.ini";
constexpr const char* kBackgroundModeKey = "appearance/backgroundMode";
constexpr const char* kBackgroundSolidKey = "appearance/backgroundSolid";
constexpr const char* kBackgroundGradientTopKey = "appearance/backgroundGradientTop";
constexpr const char* kBackgroundGradientBottomKey = "appearance/backgroundGradientBottom";

QIcon toolbarIcon(const QString& name)
{
    return QIcon(QStringLiteral(":/icons/toolbar/") + name + QStringLiteral(".svg"));
}

QString configDirectoryPath()
{
    const QByteArray overridePath = qgetenv(kConfigEnvVar);
    if (!overridePath.isEmpty()) {
        return QString::fromLocal8Bit(overridePath);
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        return QDir(appDir).filePath(QString::fromLatin1(kConfigDirName));
    }
    return QDir::current().filePath(QString::fromLatin1(kConfigDirName));
}

QSettings makeAppSettings()
{
    const QString configDirPath = configDirectoryPath();
    QDir dir;
    dir.mkpath(configDirPath);
    return QSettings(QDir(configDirPath).filePath(QString::fromLatin1(kSettingsFileName)),
                     QSettings::IniFormat);
}

QString modelDisplayModeText(ModelDisplayMode mode)
{
    switch (mode) {
    case ModelDisplayMode::Solid:
        return QStringLiteral("实体");
    case ModelDisplayMode::Wireframe:
        return QStringLiteral("线框");
    case ModelDisplayMode::SolidWireframe:
        return QStringLiteral("实体+线框");
    case ModelDisplayMode::Points:
        return QStringLiteral("点");
    }
    return QStringLiteral("实体+线框");
}

QString pickModeText(PickMode mode)
{
    switch (mode) {
    case PickMode::Node:
        return QStringLiteral("节点");
    case PickMode::Element:
        return QStringLiteral("单元");
    case PickMode::Part:
        return QStringLiteral("部件");
    }
    return QStringLiteral("节点");
}

QString elementTypeText(ElementType type)
{
    switch (type) {
    case ElementType::BAR2: return QStringLiteral("BAR2");
    case ElementType::BAR3: return QStringLiteral("BAR3");
    case ElementType::TRI3: return QStringLiteral("TRI3");
    case ElementType::TRI6: return QStringLiteral("TRI6");
    case ElementType::QUAD4: return QStringLiteral("QUAD4");
    case ElementType::QUAD8: return QStringLiteral("QUAD8");
    case ElementType::TET4: return QStringLiteral("TET4");
    case ElementType::TET10: return QStringLiteral("TET10");
    case ElementType::HEX8: return QStringLiteral("HEX8");
    case ElementType::HEX20: return QStringLiteral("HEX20");
    case ElementType::WEDGE6: return QStringLiteral("WEDGE6");
    case ElementType::PYRAMID5: return QStringLiteral("PYRAMID5");
    }
    return QStringLiteral("Unknown");
}

QString vec3Text(const glm::vec3& value)
{
    return QStringLiteral("%1, %2, %3")
        .arg(value.x, 0, 'g', 7)
        .arg(value.y, 0, 'g', 7)
        .arg(value.z, 0, 'g', 7);
}

struct StandardViewActionSpec {
    QString text;
    QString icon;
    QString status;
    StandardView view;
};

std::vector<StandardViewActionSpec> standardViewActionSpecs()
{
    return {
        {QStringLiteral("前"), QStringLiteral("view-front"), QStringLiteral("前视图"), StandardView::Front},
        {QStringLiteral("后"), QStringLiteral("view-back"), QStringLiteral("后视图"), StandardView::Back},
        {QStringLiteral("左"), QStringLiteral("view-left"), QStringLiteral("左视图"), StandardView::Left},
        {QStringLiteral("右"), QStringLiteral("view-right"), QStringLiteral("右视图"), StandardView::Right},
        {QStringLiteral("上"), QStringLiteral("view-top"), QStringLiteral("俯视图"), StandardView::Top},
        {QStringLiteral("下"), QStringLiteral("view-bottom"), QStringLiteral("仰视图"), StandardView::Bottom}
    };
}

int loadThemeIndex()
{
    QSettings settings = makeAppSettings();
    const int index = settings.value(QString::fromLatin1(kThemeIndexKey), 0).toInt();
    return std::clamp(index, 0, Theme::count() - 1);
}

void saveThemeIndex(int index)
{
    QSettings settings = makeAppSettings();
    settings.setValue(QString::fromLatin1(kThemeIndexKey),
                      std::clamp(index, 0, Theme::count() - 1));
    settings.sync();
}

QColor themeColor(float r, float g, float b)
{
    return QColor::fromRgbF(std::clamp(r, 0.0f, 1.0f),
                            std::clamp(g, 0.0f, 1.0f),
                            std::clamp(b, 0.0f, 1.0f));
}

void writeThemeBackgroundColor(Theme& theme, const QColor& topColor, const QColor& bottomColor)
{
    theme.bgTopR = static_cast<float>(topColor.redF());
    theme.bgTopG = static_cast<float>(topColor.greenF());
    theme.bgTopB = static_cast<float>(topColor.blueF());
    theme.bgBotR = static_cast<float>(bottomColor.redF());
    theme.bgBotG = static_cast<float>(bottomColor.greenF());
    theme.bgBotB = static_cast<float>(bottomColor.blueF());
}

void configureFileDialog(QFileDialog& dialog)
{
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
#ifdef Q_OS_MAC
    QList<QUrl> urls = dialog.sidebarUrls();
    auto addUrl = [&urls](const QString& path) {
        if (path.isEmpty() || !QDir(path).exists()) return;
        const QUrl url = QUrl::fromLocalFile(path);
        if (!urls.contains(url)) urls.prepend(url);
    };

    addUrl("/Volumes");
    const auto volumes = QStorageInfo::mountedVolumes();
    for (const QStorageInfo& volume : volumes) {
        if (volume.isValid() && volume.isReady()) {
            addUrl(volume.rootPath());
        }
    }
    const QFileInfoList volumeDirs = QDir("/Volumes").entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);
    for (const QFileInfo& info : volumeDirs) {
        addUrl(info.absoluteFilePath());
    }
    dialog.setSidebarUrls(urls);
#endif
}

} // namespace

MainWindow::MainWindow() {
    setWindowTitle("FEModelViewer");
    resize(1280, 860);

    // ── Tecplot 式紧凑快捷工具栏 ──
    setupToolBar();

    // ── 渲染视口 ──
    renderViewport_ = new RenderViewport;
    updateRhiActionText();

    // ── 创建面板 ──
    partsPanel_ = new PartsPanel;
    feModelPanel_ = new FEModelPanel(this);
    resultPanel_ = new ResultPanel;
    resultPanel_->setPanelMode(ResultPanel::PanelMode::Contour);
    deformationPanel_ = new ResultPanel;
    deformationPanel_->setPanelMode(ResultPanel::PanelMode::Deformation);
    thresholdPanel_ = new ResultPanel;
    thresholdPanel_->setPanelMode(ResultPanel::PanelMode::Threshold);
    {
        QSettings settings("FEModelViewer", "FEModelViewer");
        const QString modelPath = settings.value("lastModelPath", QString()).toString();
        const QString resultPath = settings.value("lastResultPath", QString()).toString();
        const bool defaultAutoFilled = ImportPathState::looksAutoFilled(modelPath, resultPath);
        const bool autoFilled =
            settings.value("lastResultPathAutoFilled", defaultAutoFilled).toBool();
        importPaths_.restore(modelPath, resultPath, autoFilled);
    }

    // ── 左侧组合面板 ──
    modelNavigatorPanel_ = createModelNavigatorPanel();

    // ── 后处理小弹窗 ──
    contourDialog_ = createPostDialog("云图显示", resultPanel_);
    deformationDialog_ = createPostDialog("变形显示", deformationPanel_);
    thresholdDialog_ = createPostDialog("阈值设置", thresholdPanel_);

    // ── 中央区域：渲染视口 ──
    setCentralWidget(renderViewport_);

    // ── 左侧停靠：模型结构 ──
    partsDock_ = new QDockWidget("模型结构", this);
    partsDock_->setWidget(modelNavigatorPanel_);
    partsDock_->setFeatures(QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::LeftDockWidgetArea, partsDock_);

    resizeDocks({partsDock_}, {240}, Qt::Horizontal);

    // ── 状态栏 ──
    setupStatusBar();
    updateProjectTreeSummary();

    // ── Tecplot 式菜单栏：主功能从菜单下拉进入 ──
    setupMenuBar();

    viewportContextMenu_ = new ViewportContextMenu(this);
    connect(renderViewport_, &RenderViewport::contextMenuRequested,
            this, [this](const QPoint& globalPos) {
        QTimer::singleShot(0, this, [this, globalPos]() {
            if (viewportContextMenu_) {
                const std::vector<int> partIndices = selectedPartIndicesForVisibility();
                const std::vector<int> elementIds = selectedElementIdsForVisibility();
                const bool hasVisibilitySelection = !partIndices.empty() || !elementIds.empty();
                const bool selectionHasVisibleItems =
                    !partIndices.empty() ? anyVisibilityPartsVisible(partIndices)
                                         : anyVisibilityElementsVisible(elementIds);
                const bool selectionHasHiddenItems =
                    !partIndices.empty() ? anyVisibilityPartsHidden(partIndices)
                                         : anyVisibilityElementsHidden(elementIds);
                viewportContextMenu_->setModelVisibilityState(!activeModel().isEmpty(),
                                                              hasVisibilitySelection,
                                                              lastSelectionMode_,
                                                              selectionHasVisibleItems,
                                                              selectionHasHiddenItems,
                                                              hasHiddenModelItems(),
                                                              hasHiddenModelElements(),
                                                              hasHiddenModelParts());
                viewportContextMenu_->popup(renderViewport_, globalPos);
            }
        });
    });
    connect(viewportContextMenu_, &ViewportContextMenu::fitRequested, this, [this]() {
        const FEModel& model = activeModel();
        if (!model.isEmpty()) {
            renderViewport_->fitToModel(model.computeCenter(), model.computeSize());
        } else {
            renderViewport_->refresh();
        }
    });
    connect(viewportContextMenu_, &ViewportContextMenu::standardViewRequested,
            this, [this](StandardView view) {
        if (renderViewport_) {
            renderViewport_->setStandardView(view);
        }
        if (statusLabel_) {
            QString text = QStringLiteral("标准视图");
            for (const auto& spec : standardViewActionSpecs()) {
                if (spec.view == view) {
                    text = spec.status;
                    break;
                }
            }
            statusLabel_->setText(QStringLiteral("  已切换到%1").arg(text));
        }
    });
    connect(viewportContextMenu_, &ViewportContextMenu::displayModeRequested,
            this, [this](ModelDisplayMode mode) {
        if (renderViewport_) {
            renderViewport_->setModelDisplayMode(mode);
        }
        if (viewportContextMenu_) {
            viewportContextMenu_->setDisplayMode(mode);
        }
        if (displayModeGroup_) {
            for (QAction* action : displayModeGroup_->actions()) {
                const bool block = action->blockSignals(true);
                action->setChecked(action->data().toInt() == static_cast<int>(mode));
                action->blockSignals(block);
            }
        }
        if (statusLabel_) {
            statusLabel_->setText(QStringLiteral("  显示模式：%1").arg(modelDisplayModeText(mode)));
        }
    });
    connect(viewportContextMenu_, &ViewportContextMenu::modelInfoRequested,
            this, &MainWindow::showModelInfoDialog);
    connect(viewportContextMenu_, &ViewportContextMenu::hideSelectedRequested,
            this, &MainWindow::hideSelectedModelParts);
    connect(viewportContextMenu_, &ViewportContextMenu::hideAllElementsRequested,
            this, &MainWindow::hideAllModelElements);
    connect(viewportContextMenu_, &ViewportContextMenu::hideAllPartsRequested,
            this, &MainWindow::hideAllModelParts);
    connect(viewportContextMenu_, &ViewportContextMenu::hideAllRequested,
            this, &MainWindow::hideAllModelObjects);
    connect(viewportContextMenu_, &ViewportContextMenu::showAllElementsRequested,
            this, &MainWindow::showAllModelElements);
    connect(viewportContextMenu_, &ViewportContextMenu::showAllPartsRequested,
            this, &MainWindow::showAllModelParts);
    connect(viewportContextMenu_, &ViewportContextMenu::isolateSelectedRequested,
            this, &MainWindow::isolateSelectedModelParts);
    connect(viewportContextMenu_, &ViewportContextMenu::showAllRequested,
            this, &MainWindow::showAllModelObjects);
    connect(viewportContextMenu_, &ViewportContextMenu::backgroundSettingsRequested,
            this, &MainWindow::showBackgroundSettingsDialog);
    connect(viewportContextMenu_, &ViewportContextMenu::gridVisibleChanged,
            this, [this](bool visible) {
        viewportGridVisible_ = visible;
        if (renderViewport_) {
            renderViewport_->setViewportGridVisible(visible);
        }
        if (statusLabel_) {
            statusLabel_->setText(visible
                ? QStringLiteral("  辅助网格已显示")
                : QStringLiteral("  辅助网格已隐藏"));
        }
    });

    // ── 信号/槽连接 ──

    connect(feModelPanel_, &FEModelPanel::meshGenerated,
            this, [this](const Mesh& mesh, const glm::vec3& center, float size,
                         const std::vector<int>& triToElem,
                         const std::vector<int>& vertexToNode){
        // 新模型加载，清除全部旧状态
        deform_.clear();
        postEffect_.clear();
        contour_.clear();
        renderViewport_->clearSliceLines();
        renderViewport_->clearIsoSurface();
        renderViewport_->clearClipPlanePreview();
        renderViewport_->setOverlayVisible(false);
        renderViewport_->setUseVertexColor(false);
        renderViewport_->setColorBarVisible(false);
        resultPanel_->clearResults();
        deformationPanel_->clearResults();
        thresholdPanel_->clearResults();
        if (animController_) animController_->setFrameCount(0);
        loadedResultFrameCount_ = 0;

        renderViewport_->setMesh(mesh);
        renderViewport_->setTriangleToElementMap(triToElem);
        renderViewport_->setVertexToNodeMap(vertexToNode);
        renderViewport_->setObjectColor(glm::vec3(0.55f, 0.75f, 0.73f));
        lastSelectionMode_ = PickMode::Node;
        lastSelectionIds_.clear();
        lastSelectedPartIndices_.clear();
        hiddenElementIds_.clear();
        if (size > 0) {
            renderViewport_->fitToModel(center, size);
        }
        if (modelSizeSummaryLabel_) {
            modelSizeSummaryLabel_->setText(size > 0
                ? QString("尺寸 %1").arg(size, 0, 'f', 2)
                : QStringLiteral("尺寸 --"));
        }
        updateProjectTreeSummary();
        updateStatusSummaries();
        updateFilterPlaneBounds();
    });

    connect(feModelPanel_, &FEModelPanel::partsChanged,
            this, [this](const QString& modelName, const std::vector<FEPart>& parts,
                         const std::vector<int>& triToPart, const std::vector<int>& edgeToPart) {
        renderViewport_->setTriangleToPartMap(triToPart);
        renderViewport_->setEdgeToPartMap(edgeToPart);
        partsPanel_->setParts(modelName, parts, renderViewport_->partColors());
        syncPartVisibilityToViewport();
        updateProjectTreeSummary();
        updateStatusSummaries();
    });

    // 加载进度 → 状态栏进度条
    connect(feModelPanel_, &FEModelPanel::loadProgress,
            this, [this](int percent, const QString& text) {
        if (percent > 0 && !text.isEmpty()) {
            statusProgress_->setVisible(true);
            statusProgress_->setValue(percent);
            progressText_->setVisible(true);
            progressText_->setText(text);
            statusLabel_->setVisible(false);
        } else {
            statusProgress_->setVisible(false);
            progressText_->setVisible(false);
            statusLabel_->setVisible(true);
        }
    });

    // 加载完成 → 更新状态栏文字
    connect(feModelPanel_, &FEModelPanel::loadFinished,
            this, [this](bool success, const QString& message) {
        statusProgress_->setVisible(false);
        progressText_->setVisible(false);
        statusLabel_->setVisible(true);
        statusLabel_->setText("  " + message);
        statusLabel_->setStyleSheet(success
            ? QString("color: %1; font-weight: bold;").arg(currentTheme_.green)
            : QString("color: %1; font-weight: bold;").arg(currentTheme_.red));
        updateProjectTreeSummary();
        updateStatusSummaries();
    });

    connect(partsPanel_, &PartsPanel::partVisibilityChanged,
            this, [this](int partIndex, bool visible) {
        const std::vector<int> partIndices = selectedPartIndicesForVisibility();
        renderViewport_->setPartVisibility(partIndex, visible);
        if (!partIndices.empty()) {
            rememberVisibilitySelection(partIndices, areVisibilityPartsVisible(partIndices));
        }
    });

    connect(partsPanel_, &PartsPanel::partSelectionChanged,
            this, [this](const std::vector<int>& selectedParts) {
        lastSelectionMode_ = PickMode::Part;
        lastSelectionIds_ = selectedParts;
        lastSelectedPartIndices_ = selectedParts;
        updatePickModeSummary(PickMode::Part);
        renderViewport_->highlightParts(selectedParts);
    });

    // 部件拾取 → 同步模型树选中状态
    connect(renderViewport_, &RenderViewport::partsPicked,
            this, [this](const std::vector<int>& partIndices) {
        lastSelectedPartIndices_ = partIndices;
        partsPanel_->selectParts(partIndices);
    });

    connect(renderViewport_, &RenderViewport::selectionChanged,
            this, [this](PickMode mode, int, const std::vector<int>& ids) {
        lastSelectionMode_ = mode;
        lastSelectionIds_ = ids;
        updatePickModeSummary(mode);
        if (mode != PickMode::Part || ids.empty()) {
            lastSelectedPartIndices_.clear();
        }
    });

    // ── 结果面板连接 ──

    // resultsLoaded → 填充右侧面板
    connect(feModelPanel_, &FEModelPanel::resultsLoaded,
            resultPanel_, &ResultPanel::setResults);
    connect(feModelPanel_, &FEModelPanel::resultsLoaded,
            deformationPanel_, &ResultPanel::setResults);
    connect(feModelPanel_, &FEModelPanel::resultsLoaded,
            thresholdPanel_, &ResultPanel::setResults);

    connect(feModelPanel_, &FEModelPanel::resultsLoaded,
            this, [this](const FEResultData&) {
        loadedResultFrameCount_ = resultPanel_->frameCount();
        updateProjectTreeSummary();
        updateStatusSummaries();
    });

    // 应用云图
    connect(resultPanel_, &ResultPanel::applyResult,
            this, [this](const FEScalarField& field, const QString& title) {
        applyContour(field, title);
    });

    // 清除云图 → 恢复部件颜色
    connect(resultPanel_, &ResultPanel::clearResult,
            this, [this]() {
        contour_.clear();
        renderViewport_->setUseVertexColor(false);
        renderViewport_->setColorBarVisible(false);
        renderViewport_->refresh();
    });

    // ── 动画控制器 ──
    animController_ = new FEAnimationController(this);

    connect(deformationPanel_, &ResultPanel::animationPlay,
            animController_, &FEAnimationController::play);
    connect(deformationPanel_, &ResultPanel::animationPause,
            animController_, &FEAnimationController::pause);
    connect(deformationPanel_, &ResultPanel::animationStop,
            animController_, &FEAnimationController::stop);

    // 动画切帧：切帧 → 变形（如果开启）→ 云图
    connect(animController_, &FEAnimationController::frameChanged,
            this, [this](int frameIndex) {
        resultPanel_->selectFrame(frameIndex);
        deformationPanel_->selectFrame(frameIndex);
        thresholdPanel_->selectFrame(frameIndex);

        if (deform_.active)
            applyDeformation(deform_.scale, deform_.overlay);

        FEScalarField field;
        QString title;
        if (resultPanel_->currentScalarField(field, title))
            applyContour(field, title);
    });

    // 结果加载后设置帧数
    connect(feModelPanel_, &FEModelPanel::resultsLoaded,
            this, [this](const FEResultData&) {
        animController_->setFrameCount(resultPanel_->frameCount());
    });

    // ── 变形显示 ──
    connect(deformationPanel_, &ResultPanel::deformationRequested,
            this, [this](float scale, bool overlayUndeformed) {
        applyDeformation(scale, overlayUndeformed);
    });

    connect(deformationPanel_, &ResultPanel::deformationCleared,
            this, [this]() {
        clearDeformation();
    });

    connect(deformationPanel_, &ResultPanel::autoScaleRequested,
            this, [this]() {
        const FEModel& model = feModelPanel_->currentModel();
        if (model.nodes.empty()) return;

        FEVectorField disp = deformationPanel_->currentDisplacement();
        if (disp.values.empty()) return;

        float scale = FEDeformation::autoScale(model, disp);
        deformationPanel_->setDeformScale(scale);
    });

    // ── 过滤 ──
    connect(thresholdPanel_, &ResultPanel::thresholdRequested,
            this, &MainWindow::applyThreshold);
    connect(thresholdPanel_, &ResultPanel::clipPlaneRequested,
            this, &MainWindow::applyClipPlane);
    connect(thresholdPanel_, &ResultPanel::slicePlaneRequested,
            this, &MainWindow::applySlicePlane);
    connect(thresholdPanel_, &ResultPanel::isoSurfaceRequested,
            this, &MainWindow::applyIsoSurface);
    connect(thresholdPanel_, &ResultPanel::filterCleared,
            this, &MainWindow::clearFilters);
    connect(thresholdPanel_, &ResultPanel::planePreviewChanged,
            this, [this](const glm::vec3& origin, const glm::vec3& normal) {
        const FEModel& model = activeModel();
        if (model.nodes.empty()) {
            renderViewport_->clearClipPlanePreview();
            return;
        }
        glm::vec3 bbMin, bbMax;
        model.computeBoundingBox(bbMin, bbMax);
        renderViewport_->setClipPlanePreview(bbMin, bbMax, origin, normal);
    });
    connect(thresholdPanel_, &ResultPanel::planePreviewCleared,
            renderViewport_, &RenderViewport::clearClipPlanePreview);

    // ── 初始主题 ──
    themeIndex_ = loadThemeIndex();
    currentTheme_ = Theme::byIndex(themeIndex_);
    loadBackgroundSettings();
    applyTheme(currentTheme_);
}

void MainWindow::clearCurrentLayout()
{
    if (!renderViewport_ || !feModelPanel_ || !resultPanel_) return;
    deform_.clear();
    postEffect_.clear();
    contour_.clear();
    renderViewport_->clearSliceLines();
    renderViewport_->clearIsoSurface();
    renderViewport_->clearClipPlanePreview();
    renderViewport_->setOverlayVisible(false);
    renderViewport_->setUseVertexColor(false);
    renderViewport_->setColorBarVisible(false);
    resultPanel_->clearResults();
    deformationPanel_->clearResults();
    thresholdPanel_->clearResults();
    if (animController_) animController_->setFrameCount(0);
    loadedResultFrameCount_ = 0;
    lastSelectionMode_ = PickMode::Node;
    lastSelectionIds_.clear();
    lastSelectedPartIndices_.clear();
    hiddenElementIds_.clear();
    importPaths_ = ImportPathState{};
    feModelPanel_->clearModel();
    if (statusProgress_) statusProgress_->setVisible(false);
    if (progressText_) progressText_->setVisible(false);
    if (statusLabel_) {
        statusLabel_->setVisible(true);
        statusLabel_->setStyleSheet(
            QString("color: %1; font-weight: bold;").arg(currentTheme_.green));
    }
    updateProjectTreeSummary();
    updateStatusSummaries();
    if (modelSizeSummaryLabel_) {
        modelSizeSummaryLabel_->setText(QStringLiteral("尺寸 --"));
    }
    if (statusLabel_) statusLabel_->setText("  已新建空布局");
}

bool MainWindow::browseModelFile() {
    QSettings settings("FEModelViewer", "FEModelViewer");
    QString lastDir = settings.value("lastOpenDir", QString()).toString();
    if (lastDir.isEmpty() || !QDir(lastDir).exists()) {
        lastDir = QDir::homePath() + "/Desktop";
        if (!QDir(lastDir).exists()) lastDir = QDir::homePath();
    }

    QFileDialog dialog(this, "选择模型文件", lastDir,
        "所有支持格式 (*.inp *.bdf *.fem *.op2 *.stl);;"
        "ABAQUS Input (*.inp);;"
        "Nastran BDF (*.bdf *.fem);;"
        "Nastran OP2 (*.op2);;"
        "STL Geometry (*.stl);;"
        "所有文件 (*)");
    dialog.setFileMode(QFileDialog::ExistingFile);
    configureFileDialog(dialog);
    if (dialog.exec() != QDialog::Accepted) return false;
    QString path = dialog.selectedFiles().first();

    if (!path.isEmpty()) {
        importPaths_.selectModelFile(path);
        settings.setValue("lastOpenDir", QFileInfo(path).absolutePath());
        return true;
    }
    return false;
}

void MainWindow::browseImportFile() {
    QSettings settings("FEModelViewer", "FEModelViewer");
    QString lastDir = settings.value("lastOpenDir", QString()).toString();
    if (lastDir.isEmpty() || !QDir(lastDir).exists()) {
        lastDir = QDir::homePath() + "/Desktop";
        if (!QDir(lastDir).exists()) lastDir = QDir::homePath();
    }

    QFileDialog dialog(this, "导入几何文件", lastDir,
        "CAD / Geometry (*.step *.stp *.iges *.igs *.stl);;"
        "STEP (*.step *.stp);;"
        "IGES (*.iges *.igs);;"
        "STL Geometry (*.stl);;"
        "所有文件 (*)");
    dialog.setFileMode(QFileDialog::ExistingFile);
    configureFileDialog(dialog);
    if (dialog.exec() != QDialog::Accepted) return;
    QString path = dialog.selectedFiles().first();

    if (!path.isEmpty()) {
        importPaths_.selectModelFile(path);
        settings.setValue("lastOpenDir", QFileInfo(path).absolutePath());
    }
}

bool MainWindow::browseUnifiedImportFile() {
    QSettings settings("FEModelViewer", "FEModelViewer");
    QString lastDir = settings.value("lastOpenDir", QString()).toString();
    if (lastDir.isEmpty() || !QDir(lastDir).exists()) {
        lastDir = QDir::homePath() + "/Desktop";
        if (!QDir(lastDir).exists()) lastDir = QDir::homePath();
    }

    QFileDialog dialog(this, "导入文件", lastDir,
        "所有支持导入格式 (*.step *.stp *.iges *.igs *.stl *.op2 *.unv);;"
        "CAD / Geometry (*.step *.stp *.iges *.igs *.stl);;"
        "结果文件 (*.op2 *.unv);;"
        "所有文件 (*)");
    dialog.setFileMode(QFileDialog::ExistingFile);
    configureFileDialog(dialog);
    if (dialog.exec() != QDialog::Accepted) return false;

    const QString path = dialog.selectedFiles().first();
    if (path.isEmpty()) {
        return false;
    }

    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "op2" || suffix == "unv") {
        importPaths_.selectResultFile(path);
    } else if (suffix == "step" || suffix == "stp" ||
               suffix == "iges" || suffix == "igs" ||
               suffix == "stl") {
        importPaths_.selectModelFile(path);
    } else {
        QMessageBox::information(this, "导入文件",
            QString("暂不支持该格式的导入文件。\n\n文件: %1").arg(path));
        return false;
    }
    settings.setValue("lastOpenDir", QFileInfo(path).absolutePath());
    return true;
}

void MainWindow::browseResultFile() {
    QSettings settings("FEModelViewer", "FEModelViewer");
    QString lastDir = settings.value("lastOpenDir", QString()).toString();
    if (lastDir.isEmpty() || !QDir(lastDir).exists()) {
        lastDir = QDir::homePath() + "/Desktop";
        if (!QDir(lastDir).exists()) lastDir = QDir::homePath();
    }

    QFileDialog dialog(this, "选择结果文件", lastDir,
        "结果文件 (*.op2 *.unv);;"
        "Nastran OP2 (*.op2);;"
        "Universal UNV (*.unv);;"
        "所有文件 (*)");
    dialog.setFileMode(QFileDialog::ExistingFile);
    configureFileDialog(dialog);
    if (dialog.exec() != QDialog::Accepted) return;
    QString path = dialog.selectedFiles().first();

    if (!path.isEmpty()) {
        importPaths_.selectResultFile(path);
        settings.setValue("lastOpenDir", QFileInfo(path).absolutePath());
    }
}

void MainWindow::applyFiles() {
    QString modelPath = importPaths_.modelPath;
    QString resultPath = importPaths_.resultPath;

    if (modelPath.isEmpty() && resultPath.isEmpty()) {
        QMessageBox::information(this, "提示", "请先选择模型文件或结果文件。");
        return;
    }

    // 加载模型文件
    if (!modelPath.isEmpty()) {
        feModelPanel_->loadModelFromPath(modelPath);
    }

    // 加载结果文件
    if (!resultPath.isEmpty()) {
        QString suffix = QFileInfo(resultPath).suffix().toLower();
        FEResultData results;
        bool ok = false;

        if (suffix == "op2") {
            ok = feModelPanel_->parseNastranOp2Results(resultPath, results);
        } else if (suffix == "unv") {
            ok = feModelPanel_->parseUnvResults(resultPath, results);
        }

        if (ok) {
            emit feModelPanel_->resultsLoaded(results);
            statusLabel_->setText("  结果加载完成");
            statusLabel_->setStyleSheet(
                QString("color: %1; font-weight: bold;").arg(currentTheme_.green));
        } else if (suffix == "op2" || suffix == "unv") {
            QMessageBox::warning(this, "结果文件",
                QString("未能从结果文件中解析到数据。\n\n文件: %1").arg(resultPath));
        } else {
            QMessageBox::information(this, "结果文件",
                QString("暂不支持该格式的结果文件。\n\n文件: %1").arg(resultPath));
        }
    }
}

// ════════════════════════════════════════════════════════════
// Tecplot 风格组合面板
// ════════════════════════════════════════════════════════════

QWidget* MainWindow::createModelNavigatorPanel() {
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    layout->addWidget(partsPanel_, 1);
    return panel;
}

QDialog* MainWindow::createPostDialog(const QString& title, ResultPanel* panel) {
    auto* dialog = new QDialog(this);
    dialog->setWindowTitle(title);
    dialog->setModal(false);
    dialog->setAttribute(Qt::WA_DeleteOnClose, false);

    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(0);
    layout->addWidget(panel);

    dialog->resize(panel->sizeHint().expandedTo(QSize(340, 200)));
    return dialog;
}

void MainWindow::showPostDialog(QDialog* dialog) {
    if (!dialog) return;
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::updateProjectTreeSummary() {
    if (!projectTree_ || projectTree_->topLevelItemCount() == 0) return;

    const FEModel& model = feModelPanel_->currentModel();
    auto* root = projectTree_->topLevelItem(0);
    root->setText(0, model.name.empty()
        ? QStringLiteral("FEModelViewer")
        : QString::fromStdString(model.name));

    if (root->childCount() < 4) return;
    root->child(0)->setText(0, QString("Model  (%1 节点 / %2 单元)")
                                .arg(model.nodeCount())
                                .arg(model.elementCount()));
    root->child(1)->setText(0, QString("Parts  (%1)").arg(model.parts.size()));
    root->child(2)->setText(0, QString("Fields  (%1)").arg(loadedResultFrameCount_ > 0 ? 1 : 0));
    root->child(3)->setText(0, QString("Results  (%1 帧)").arg(loadedResultFrameCount_));
    projectTree_->expandAll();
}

void MainWindow::updateStatusSummaries() {
    if (!renderViewport_) {
        return;
    }

    if (fpsSummaryLabel_)
        fpsSummaryLabel_->setText(QString("FPS %1").arg(renderViewport_->currentFps(), 0, 'f', 1));
    if (frameTimeSummaryLabel_)
        frameTimeSummaryLabel_->setText(QString("帧时间 %1 ms").arg(renderViewport_->frameTimeMs(), 0, 'f', 2));
    if (vertexSummaryLabel_)
        vertexSummaryLabel_->setText(QString("顶点数 %1").arg(renderViewport_->vertexCount()));
    if (triangleSummaryLabel_)
        triangleSummaryLabel_->setText(QString("三角面 %1").arg(renderViewport_->triangleCount()));
}

void MainWindow::updatePickModeSummary(PickMode mode)
{
    if (pickModeSummaryLabel_) {
        pickModeSummaryLabel_->setText(QStringLiteral("拾取模式：%1").arg(pickModeText(mode)));
    }
}

std::vector<int> MainWindow::selectedPartIndicesForVisibility() const
{
    const FEModel& model = activeModel();
    if (model.parts.empty() || lastSelectionMode_ != PickMode::Part) {
        return {};
    }

    std::set<int> partSet;
    for (int partIndex : lastSelectedPartIndices_) {
        if (partIndex >= 0 && partIndex < static_cast<int>(model.parts.size())) {
            partSet.insert(partIndex);
        }
    }

    return std::vector<int>(partSet.begin(), partSet.end());
}

std::vector<int> MainWindow::selectedElementIdsForVisibility() const
{
    if (lastSelectionMode_ != PickMode::Element || lastSelectionIds_.empty()) {
        return {};
    }

    std::set<int> elementSet;
    for (int elementId : lastSelectionIds_) {
        if (elementId >= 0) {
            elementSet.insert(elementId);
        }
    }
    return std::vector<int>(elementSet.begin(), elementSet.end());
}

std::vector<int> MainWindow::allModelElementIds() const
{
    const FEModel& model = activeModel();
    std::set<int> elementSet;
    for (const auto& [elementId, element] : model.elements) {
        (void)element;
        elementSet.insert(elementId);
    }
    return std::vector<int>(elementSet.begin(), elementSet.end());
}

bool MainWindow::areVisibilityPartsVisible(const std::vector<int>& partIndices) const
{
    if (!partsPanel_ || partIndices.empty()) {
        return false;
    }

    std::set<int> expected(partIndices.begin(), partIndices.end());
    for (const auto& [partIndex, visible] : partsPanel_->partVisibilityStates()) {
        if (expected.count(partIndex) > 0 && !visible) {
            return false;
        }
    }
    return true;
}

bool MainWindow::anyVisibilityPartsVisible(const std::vector<int>& partIndices) const
{
    if (!partsPanel_ || partIndices.empty()) {
        return false;
    }

    std::set<int> expected(partIndices.begin(), partIndices.end());
    for (const auto& [partIndex, visible] : partsPanel_->partVisibilityStates()) {
        if (expected.count(partIndex) > 0 && visible) {
            return true;
        }
    }
    return false;
}

bool MainWindow::anyVisibilityPartsHidden(const std::vector<int>& partIndices) const
{
    if (!partsPanel_ || partIndices.empty()) {
        return false;
    }

    std::set<int> expected(partIndices.begin(), partIndices.end());
    for (const auto& [partIndex, visible] : partsPanel_->partVisibilityStates()) {
        if (expected.count(partIndex) > 0 && !visible) {
            return true;
        }
    }
    return false;
}

bool MainWindow::anyVisibilityElementsVisible(const std::vector<int>& elementIds) const
{
    for (int elementId : elementIds) {
        if (hiddenElementIds_.count(elementId) == 0) {
            return true;
        }
    }
    return false;
}

bool MainWindow::anyVisibilityElementsHidden(const std::vector<int>& elementIds) const
{
    for (int elementId : elementIds) {
        if (hiddenElementIds_.count(elementId) > 0) {
            return true;
        }
    }
    return false;
}

bool MainWindow::hasHiddenModelItems() const
{
    return hasHiddenModelElements() || hasHiddenModelParts();
}

bool MainWindow::hasHiddenModelElements() const
{
    return !hiddenElementIds_.empty();
}

bool MainWindow::hasHiddenModelParts() const
{
    if (!partsPanel_) {
        return false;
    }

    for (const auto& [partIndex, visible] : partsPanel_->partVisibilityStates()) {
        (void)partIndex;
        if (!visible) {
            return true;
        }
    }
    return false;
}

void MainWindow::syncPartVisibilityToViewport()
{
    if (!renderViewport_ || !partsPanel_) {
        return;
    }

    for (const auto& [partIndex, visible] : partsPanel_->partVisibilityStates()) {
        renderViewport_->setPartVisibility(partIndex, visible);
    }
}

void MainWindow::rememberVisibilitySelection(const std::vector<int>& partIndices, bool highlightVisibleParts)
{
    lastSelectionMode_ = PickMode::Part;
    lastSelectionIds_ = partIndices;
    lastSelectedPartIndices_ = partIndices;

    if (partsPanel_) {
        partsPanel_->selectParts(partIndices);
    }

    if (highlightVisibleParts && renderViewport_) {
        renderViewport_->highlightParts(partIndices);
    }
}

void MainWindow::hideSelectedModelParts()
{
    if (lastSelectionMode_ == PickMode::Element) {
        const std::vector<int> elementIds = selectedElementIdsForVisibility();
        if (elementIds.empty()) {
            if (statusLabel_) {
                statusLabel_->setText(QStringLiteral("  请先选择要隐藏的单元"));
            }
            return;
        }
        for (int elementId : elementIds) {
            hiddenElementIds_.insert(elementId);
        }
        renderViewport_->setElementsVisibility(elementIds, false);
        if (statusLabel_) {
            statusLabel_->setText(QStringLiteral("  已隐藏 %1 个单元").arg(elementIds.size()));
        }
        return;
    }

    const std::vector<int> partIndices = selectedPartIndicesForVisibility();
    if (partIndices.empty()) {
        if (statusLabel_) {
            statusLabel_->setText(QStringLiteral("  请先选择要隐藏的部件"));
        }
        return;
    }

    for (int partIndex : partIndices) {
        partsPanel_->setPartVisibleByIndex(partIndex, false, true);
    }
    rememberVisibilitySelection(partIndices, false);
    if (statusLabel_) {
        statusLabel_->setText(QStringLiteral("  已隐藏 %1 个部件").arg(partIndices.size()));
    }
}

void MainWindow::hideAllModelElements()
{
    const std::vector<int> elementIds = allModelElementIds();
    if (elementIds.empty()) {
        if (statusLabel_) {
            statusLabel_->setText(QStringLiteral("  当前没有可隐藏的单元"));
        }
        return;
    }

    hiddenElementIds_.insert(elementIds.begin(), elementIds.end());
    renderViewport_->setElementsVisibility(elementIds, false);
    if (statusLabel_) {
        statusLabel_->setText(QStringLiteral("  已隐藏全部单元"));
    }
}

void MainWindow::hideAllModelParts()
{
    const FEModel& model = activeModel();
    if (model.parts.empty()) {
        if (statusLabel_) {
            statusLabel_->setText(QStringLiteral("  当前没有可隐藏的部件"));
        }
        return;
    }

    partsPanel_->setAllPartsVisible(false, true);
    if (statusLabel_) {
        statusLabel_->setText(QStringLiteral("  已隐藏全部部件"));
    }
}

void MainWindow::hideAllModelObjects()
{
    const std::vector<int> elementIds = allModelElementIds();
    if (!elementIds.empty()) {
        hiddenElementIds_.insert(elementIds.begin(), elementIds.end());
        renderViewport_->setElementsVisibility(elementIds, false);
    }

    const FEModel& model = activeModel();
    if (!model.parts.empty()) {
        partsPanel_->setAllPartsVisible(false, true);
    }

    if (elementIds.empty() && model.parts.empty()) {
        if (statusLabel_) {
            statusLabel_->setText(QStringLiteral("  当前没有可隐藏的对象"));
        }
        return;
    }
    if (statusLabel_) {
        statusLabel_->setText(QStringLiteral("  已隐藏全部对象"));
    }
}

void MainWindow::showSelectedModelParts()
{
    if (lastSelectionMode_ == PickMode::Element) {
        const std::vector<int> elementIds = selectedElementIdsForVisibility();
        if (elementIds.empty()) {
            if (statusLabel_) {
                statusLabel_->setText(QStringLiteral("  请先选择要显示的单元"));
            }
            return;
        }
        for (int elementId : elementIds) {
            hiddenElementIds_.erase(elementId);
        }
        renderViewport_->setElementsVisibility(elementIds, true);
        if (statusLabel_) {
            statusLabel_->setText(QStringLiteral("  已显示 %1 个单元").arg(elementIds.size()));
        }
        return;
    }

    const std::vector<int> partIndices = selectedPartIndicesForVisibility();
    if (partIndices.empty()) {
        if (statusLabel_) {
            statusLabel_->setText(QStringLiteral("  请先选择要显示的部件"));
        }
        return;
    }

    for (int partIndex : partIndices) {
        partsPanel_->setPartVisibleByIndex(partIndex, true, true);
    }
    rememberVisibilitySelection(partIndices, true);
    if (statusLabel_) {
        statusLabel_->setText(QStringLiteral("  已显示 %1 个部件").arg(partIndices.size()));
    }
}

void MainWindow::showAllModelElements()
{
    if (hiddenElementIds_.empty()) {
        if (statusLabel_) {
            statusLabel_->setText(QStringLiteral("  当前没有隐藏的单元"));
        }
        return;
    }

    hiddenElementIds_.clear();
    renderViewport_->setAllElementsVisible();
    if (statusLabel_) {
        statusLabel_->setText(QStringLiteral("  已显示全部单元"));
    }
}

void MainWindow::showAllModelParts()
{
    const FEModel& model = activeModel();
    if (model.parts.empty()) {
        if (statusLabel_) {
            statusLabel_->setText(QStringLiteral("  当前没有可显示的部件"));
        }
        return;
    }

    partsPanel_->setAllPartsVisible(true, true);
    if (statusLabel_) {
        statusLabel_->setText(QStringLiteral("  已显示全部部件"));
    }
}

void MainWindow::isolateSelectedModelParts()
{
    if (lastSelectionMode_ == PickMode::Element) {
        const std::vector<int> selectedElementIds = selectedElementIdsForVisibility();
        if (selectedElementIds.empty()) {
            if (statusLabel_) {
                statusLabel_->setText(QStringLiteral("  请先选择要隔离显示的单元"));
            }
            return;
        }

        const std::unordered_set<int> selectedSet(selectedElementIds.begin(), selectedElementIds.end());
        std::vector<int> hideElementIds;
        for (int elementId : allModelElementIds()) {
            if (selectedSet.count(elementId) == 0) {
                hideElementIds.push_back(elementId);
                hiddenElementIds_.insert(elementId);
            } else {
                hiddenElementIds_.erase(elementId);
            }
        }
        if (!hideElementIds.empty()) {
            renderViewport_->setElementsVisibility(hideElementIds, false);
        }
        renderViewport_->setElementsVisibility(selectedElementIds, true);
        if (statusLabel_) {
            statusLabel_->setText(QStringLiteral("  已仅显示 %1 个单元").arg(selectedElementIds.size()));
        }
        return;
    }

    const std::vector<int> partIndices = selectedPartIndicesForVisibility();
    if (partIndices.empty()) {
        if (statusLabel_) {
            statusLabel_->setText(QStringLiteral("  请先选择要隔离显示的部件"));
        }
        return;
    }

    partsPanel_->isolateParts(partIndices, true);
    rememberVisibilitySelection(partIndices, true);
    if (statusLabel_) {
        statusLabel_->setText(QStringLiteral("  已仅显示 %1 个部件").arg(partIndices.size()));
    }
}

void MainWindow::showAllModelObjects()
{
    const FEModel& model = activeModel();
    if (model.parts.empty() && hiddenElementIds_.empty()) {
        if (statusLabel_) {
            statusLabel_->setText(QStringLiteral("  当前没有可显示的部件"));
        }
        return;
    }

    if (!model.parts.empty()) {
        partsPanel_->setAllPartsVisible(true, true);
    }
    hiddenElementIds_.clear();
    renderViewport_->setAllElementsVisible();
    if (statusLabel_) {
        statusLabel_->setText(QStringLiteral("  已显示全部对象"));
    }
}

// ════════════════════════════════════════════════════════════
// 菜单栏 / 工具栏
// ════════════════════════════════════════════════════════════

void MainWindow::setupMenuBar() {
    auto statusOnly = [this](const QString& text) {
        if (statusLabel_) statusLabel_->setText("  " + text);
    };

    auto* fileMenu = menuBar()->addMenu("File");
    auto* newLayoutAction = fileMenu->addAction(style()->standardIcon(QStyle::SP_FileIcon), "New Layout");
    newLayoutAction->setShortcut(QKeySequence("Ctrl+N"));
    connect(newLayoutAction, &QAction::triggered, this, &MainWindow::clearCurrentLayout);

    auto* openLayoutAction = fileMenu->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), "Open Layout...");
    openLayoutAction->setShortcut(QKeySequence("Ctrl+O"));
    connect(openLayoutAction, &QAction::triggered, this, [statusOnly]() {
        statusOnly("Open Layout 入口已预留");
    });

    auto* recentMenu = fileMenu->addMenu("Recent Layouts");
    auto* noRecentAction = recentMenu->addAction("(none)");
    noRecentAction->setEnabled(false);

    auto* saveLayoutAction = fileMenu->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), "Save Layout");
    saveLayoutAction->setShortcut(QKeySequence("Ctrl+S"));
    connect(saveLayoutAction, &QAction::triggered, this, [statusOnly]() {
        statusOnly("Save Layout 入口已预留");
    });

    auto* saveAsAction = fileMenu->addAction("Save Layout As...");
    saveAsAction->setShortcut(QKeySequence("Ctrl+W"));
    connect(saveAsAction, &QAction::triggered, this, [statusOnly]() {
        statusOnly("Save Layout As 入口已预留");
    });

    fileMenu->addSeparator();
    auto* openFileAction = fileMenu->addAction(toolbarIcon("open"), "Open file...");
    connect(openFileAction, &QAction::triggered, this, [this]() {
        if (browseModelFile()) {
            applyFiles();
        }
    });

    auto* importAction = fileMenu->addAction(toolbarIcon("import"), "Import...");
    connect(importAction, &QAction::triggered, this, [this]() {
        browseImportFile();
        applyFiles();
    });

    auto* openResultFileAction = fileMenu->addAction(toolbarIcon("import-result"), "Open result file...");
    connect(openResultFileAction, &QAction::triggered, this, [this]() {
        browseResultFile();
        applyFiles();
    });

    auto* writeDataAction = fileMenu->addAction("Write Data...");
    writeDataAction->setEnabled(false);

    fileMenu->addSeparator();
    auto* printAction = fileMenu->addAction(style()->standardIcon(QStyle::SP_DialogApplyButton), "Print...");
    printAction->setShortcut(QKeySequence("Ctrl+P"));
    connect(printAction, &QAction::triggered, this, [statusOnly]() {
        statusOnly("Print 入口已预留");
    });
    auto* paperSetupAction = fileMenu->addAction("Paper Setup...");
    connect(paperSetupAction, &QAction::triggered, this, [statusOnly]() {
        statusOnly("Paper Setup 入口已预留");
    });

    fileMenu->addSeparator();
    auto* exportAction = fileMenu->addAction("Export...");
    connect(exportAction, &QAction::triggered, this, [statusOnly]() {
        statusOnly("Export 入口已预留");
    });

    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction("Exit");
    quitAction->setShortcut(QKeySequence("Ctrl+Q"));
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto* editMenu = menuBar()->addMenu("Edit");
    auto* undoAction = editMenu->addAction("Undo");
    undoAction->setShortcut(QKeySequence("Ctrl+Z"));
    undoAction->setEnabled(false);
    auto* redoAction = editMenu->addAction("Redo");
    redoAction->setShortcut(QKeySequence("Ctrl+Y"));
    redoAction->setEnabled(false);
    editMenu->addSeparator();
    auto* clearSelectionAction = editMenu->addAction("Clear Selection");
    connect(clearSelectionAction, &QAction::triggered, this, [statusOnly]() {
        statusOnly("Clear Selection 入口已预留");
    });

    auto* viewMenu = menuBar()->addMenu("View");
    auto* fitAction = viewMenu->addAction("Fit to Full Size");
    connect(fitAction, &QAction::triggered, this, [this]() {
        if (renderViewport_) renderViewport_->refresh();
        if (statusLabel_) statusLabel_->setText("  Fit to Full Size");
    });
    auto* refreshAction = viewMenu->addAction("Refresh");
    connect(refreshAction, &QAction::triggered, this, [this]() {
        if (renderViewport_) renderViewport_->refresh();
    });
    auto* labelsAction = viewMenu->addAction("Show ID Labels");
    labelsAction->setCheckable(true);
    connect(labelsAction, &QAction::toggled, this, [this](bool checked) {
        if (renderViewport_) renderViewport_->setShowLabels(checked);
    });

    viewMenu->addSeparator();
    auto* standardViewsMenu = viewMenu->addMenu("Standard Views");
    for (const auto& actionSpec : standardViewActionSpecs()) {
        auto* action = standardViewsMenu->addAction(toolbarIcon(actionSpec.icon), actionSpec.status);
        connect(action, &QAction::triggered, this, [this, actionSpec]() {
            if (renderViewport_) {
                renderViewport_->setStandardView(actionSpec.view);
            }
            if (statusLabel_) {
                statusLabel_->setText(QStringLiteral("  已切换到%1").arg(actionSpec.status));
            }
        });
    }

    viewMenu->addSeparator();
    if (sidebarsAction_) {
        viewMenu->addAction(sidebarsAction_);
    }

    leftPanelAction_ = viewMenu->addAction("Model Structure");
    leftPanelAction_->setCheckable(true);
    leftPanelAction_->setChecked(true);
    leftPanelAction_->setShortcut(QKeySequence("Ctrl+Shift+M"));

    if (sidebarsAction_) {
        connect(sidebarsAction_, &QAction::toggled,
                this, &MainWindow::setModelStructureVisible);
    }

    connect(leftPanelAction_, &QAction::toggled,
            this, &MainWindow::setModelStructureVisible);

    auto* plotMenu = menuBar()->addMenu("Plot");
    const QStringList plotActions = {"Contour", "Deformation", "Slice", "Iso Surface", "Threshold"};
    for (const QString& text : plotActions) {
        auto* action = plotMenu->addAction(text);
        connect(action, &QAction::triggered, this, [this, text]() {
            if (text == "Contour") showPostDialog(contourDialog_);
            else if (text == "Deformation") showPostDialog(deformationDialog_);
            else if (text == "Slice") {
                thresholdPanel_->setFilterMode(ResultPanel::FilterMode::Slice);
                thresholdDialog_->setWindowTitle("切片设置");
                showPostDialog(thresholdDialog_);
            } else if (text == "Iso Surface") {
                thresholdPanel_->setFilterMode(ResultPanel::FilterMode::IsoSurface);
                thresholdDialog_->setWindowTitle("等值面设置");
                showPostDialog(thresholdDialog_);
            } else if (text == "Threshold") {
                thresholdPanel_->setFilterMode(ResultPanel::FilterMode::Threshold);
                thresholdDialog_->setWindowTitle("阈值设置");
                showPostDialog(thresholdDialog_);
            }
        });
    }
    plotMenu->addSeparator();
    auto* clearPostAction = plotMenu->addAction("Clear Post Effects");
    connect(clearPostAction, &QAction::triggered, this, [this]() {
        clearFilters();
        clearDeformation();
    });

    auto* insertMenu = menuBar()->addMenu("Insert");
    for (const QString& text : QStringList{"Text...", "Image...", "Geometry..."}) {
        auto* action = insertMenu->addAction(text);
        connect(action, &QAction::triggered, this, [statusOnly, text]() {
            statusOnly(text + " 入口已预留");
        });
    }

    auto* animateMenu = menuBar()->addMenu("Animate");
    auto* playAction = animateMenu->addAction("Play");
    connect(playAction, &QAction::triggered, deformationPanel_, &ResultPanel::animationPlay);
    auto* pauseAction = animateMenu->addAction("Pause");
    connect(pauseAction, &QAction::triggered, deformationPanel_, &ResultPanel::animationPause);
    auto* stopAction = animateMenu->addAction("Stop");
    connect(stopAction, &QAction::triggered, deformationPanel_, &ResultPanel::animationStop);

    auto* dataMenu = menuBar()->addMenu("Data");
    auto* loadModelAction = dataMenu->addAction("Load Model Data...");
    connect(loadModelAction, &QAction::triggered, this, [this]() {
        if (browseModelFile()) {
            applyFiles();
        }
    });
    auto* loadResultAction = dataMenu->addAction("Load Result Data...");
    connect(loadResultAction, &QAction::triggered, this, [this]() {
        browseResultFile();
        applyFiles();
    });
    dataMenu->addSeparator();
    auto* applyDataAction = dataMenu->addAction("Apply Current Paths");
    connect(applyDataAction, &QAction::triggered, this, &MainWindow::applyFiles);

    auto* optionsMenu = menuBar()->addMenu("Options");
    if (themeMenu_) optionsMenu->addMenu(themeMenu_);
    if (rhiMenu_) optionsMenu->addMenu(rhiMenu_);
    auto* modelInfoAction = optionsMenu->addAction("Model Info...");
    connect(modelInfoAction, &QAction::triggered,
            this, &MainWindow::showModelInfoDialog);
    auto* backgroundAction = optionsMenu->addAction("Background...");
    connect(backgroundAction, &QAction::triggered,
            this, &MainWindow::showBackgroundSettingsDialog);

    auto* scriptingMenu = menuBar()->addMenu("Scripting");
    auto* macroAction = scriptingMenu->addAction("Record Macro...");
    macroAction->setEnabled(false);
    auto* runScriptAction = scriptingMenu->addAction("Run Script...");
    runScriptAction->setEnabled(false);

    auto* toolsMenu = menuBar()->addMenu("Tools");
    auto* probeAction = toolsMenu->addAction("Probe");
    connect(probeAction, &QAction::triggered, this, [this]() {
        statusLabel_->setText("  Probe 可通过工具栏节点/单元/部件拾取使用");
    });
    auto* resetSettingsAction = toolsMenu->addAction("Reset View State");
    connect(resetSettingsAction, &QAction::triggered, this, [this]() {
        if (renderViewport_) renderViewport_->refresh();
    });

    auto* helpMenu = menuBar()->addMenu("Help");
    auto* aboutAction = helpMenu->addAction("About FEModelViewer");
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, "About FEModelViewer",
                                 "FEModelViewer\nQt6 + OpenGL finite element model viewer");
    });
}

void MainWindow::setupToolBar() {
    toolbar_ = addToolBar("Quick Tools");
    toolbar_->setMovable(false);
    toolbar_->setIconSize(QSize(20, 20));
    toolbar_->setToolButtonStyle(Qt::ToolButtonIconOnly);

    sidebarsAction_ = new QAction(toolbarIcon("sidebar"), "边栏", this);
    sidebarsAction_->setCheckable(true);
    sidebarsAction_->setChecked(true);
    sidebarsAction_->setShortcut(QKeySequence("Ctrl+Shift+B"));
    sidebarsAction_->setToolTip("显示/隐藏模型结构栏");
    sidebarsAction_->setStatusTip("显示/隐藏模型结构栏");
    connect(sidebarsAction_, &QAction::toggled,
            this, &MainWindow::setModelStructureVisible);
    toolbar_->addAction(sidebarsAction_);
    toolbar_->addSeparator();

    auto* newLayoutAction = toolbar_->addAction(toolbarIcon("new-layout"), "新建");
    newLayoutAction->setToolTip("新建布局 / 清空当前模型");
    newLayoutAction->setStatusTip("新建布局 / 清空当前模型");
    connect(newLayoutAction, &QAction::triggered,
            this, &MainWindow::clearCurrentLayout);

    auto* openAction = toolbar_->addAction(toolbarIcon("open"), "打开");
    openAction->setToolTip("打开模型文件");
    connect(openAction, &QAction::triggered, this, [this]() {
        if (browseModelFile()) {
            applyFiles();
        }
    });

    auto* saveAction = toolbar_->addAction(toolbarIcon("save"), "保存");
    saveAction->setToolTip("保存入口预留");
    connect(saveAction, &QAction::triggered, this, [this]() {
        statusLabel_->setText("  保存入口已预留");
    });

    auto* importAction = toolbar_->addAction(toolbarIcon("import"), "导入");
    importAction->setToolTip("导入几何或结果文件");
    connect(importAction, &QAction::triggered, this, [this]() {
        if (browseUnifiedImportFile()) {
            applyFiles();
        }
    });

    auto* printAction = toolbar_->addAction(toolbarIcon("print"), "打印");
    printAction->setToolTip("打印入口预留");
    connect(printAction, &QAction::triggered, this, [this]() {
        if (statusLabel_) statusLabel_->setText("  Print 入口已预留");
    });

    toolbar_->addSeparator();

    struct ViewToolActionSpec {
        QString text;
        QString icon;
        ViewportInteractionMode mode;
    };
    auto* viewToolGroup = new QActionGroup(this);
    viewToolGroup->setExclusive(true);
    const std::vector<ViewToolActionSpec> viewActions = {
        {QStringLiteral("拾取"), QStringLiteral("select"), ViewportInteractionMode::Pick},
        {QStringLiteral("平移"), QStringLiteral("pan"), ViewportInteractionMode::Pan},
        {QStringLiteral("旋转"), QStringLiteral("rotate"), ViewportInteractionMode::Rotate},
        {QStringLiteral("缩放"), QStringLiteral("zoom"), ViewportInteractionMode::Zoom}
    };
    for (const auto& actionSpec : viewActions) {
        auto* action = toolbar_->addAction(toolbarIcon(actionSpec.icon), actionSpec.text);
        action->setCheckable(true);
        action->setData(static_cast<int>(actionSpec.mode));
        action->setToolTip(actionSpec.mode == ViewportInteractionMode::Pick
            ? QStringLiteral("拾取工具：左键按当前拾取模式选择对象，拖拽框选")
            : QStringLiteral("%1视图工具").arg(actionSpec.text));
        if (actionSpec.mode == ViewportInteractionMode::Pick) {
            action->setChecked(true);
        }
        viewToolGroup->addAction(action);
    }
    connect(viewToolGroup, &QActionGroup::triggered, this, [this](QAction* action) {
        const auto mode = static_cast<ViewportInteractionMode>(action->data().toInt());
        if (renderViewport_) {
            renderViewport_->setInteractionMode(mode);
        }
        if (statusLabel_) {
            if (mode == ViewportInteractionMode::Pick) {
                statusLabel_->setText(QStringLiteral("  拾取工具：当前按%1拾取").arg(pickModeText(lastSelectionMode_)));
            } else {
                statusLabel_->setText(QStringLiteral("  视图工具：%1").arg(action->text()));
            }
        }
    });

    auto* fitAction = toolbar_->addAction(toolbarIcon("fit"), "适配");
    fitAction->setToolTip("适配当前模型到窗口");
    connect(fitAction, &QAction::triggered, this, [this]() {
        const FEModel& model = activeModel();
        if (renderViewport_ && !model.isEmpty()) {
            renderViewport_->fitToModel(model.computeCenter(), model.computeSize());
        } else if (renderViewport_) {
            renderViewport_->refresh();
        }
        if (statusLabel_) {
            statusLabel_->setText(QStringLiteral("  已适配窗口"));
        }
    });

    toolbar_->addSeparator();

    for (const auto& actionSpec : standardViewActionSpecs()) {
        auto* action = toolbar_->addAction(toolbarIcon(actionSpec.icon), actionSpec.text);
        action->setToolTip(actionSpec.status);
        connect(action, &QAction::triggered, this, [this, actionSpec]() {
            if (renderViewport_) {
                renderViewport_->setStandardView(actionSpec.view);
            }
            if (statusLabel_) {
                statusLabel_->setText(QStringLiteral("  已切换到%1").arg(actionSpec.status));
            }
        });
    }

    toolbar_->addSeparator();

    // ── 模型显示模式 ──
    displayModeGroup_ = new QActionGroup(this);
    displayModeGroup_->setExclusive(true);

    struct DisplayModeActionSpec {
        QString text;
        QString icon;
        ModelDisplayMode mode;
    };
    const std::vector<DisplayModeActionSpec> displayModeActions = {
        {QStringLiteral("实体"), QStringLiteral("display-solid"), ModelDisplayMode::Solid},
        {QStringLiteral("线框"), QStringLiteral("display-wireframe"), ModelDisplayMode::Wireframe},
        {QStringLiteral("实体+线框"), QStringLiteral("display-solid-wireframe"), ModelDisplayMode::SolidWireframe},
        {QStringLiteral("点"), QStringLiteral("display-points"), ModelDisplayMode::Points}
    };
    for (const auto& actionSpec : displayModeActions) {
        auto* action = toolbar_->addAction(toolbarIcon(actionSpec.icon), actionSpec.text);
        action->setCheckable(true);
        action->setToolTip(QStringLiteral("模型显示：%1").arg(actionSpec.text));
        action->setData(static_cast<int>(actionSpec.mode));
        if (actionSpec.mode == ModelDisplayMode::SolidWireframe) {
            action->setChecked(true);
        }
        displayModeGroup_->addAction(action);
    }
    connect(displayModeGroup_, &QActionGroup::triggered, this, [this](QAction* action) {
        const auto mode = static_cast<ModelDisplayMode>(action->data().toInt());
        if (renderViewport_) {
            renderViewport_->setModelDisplayMode(mode);
        }
        if (viewportContextMenu_) {
            viewportContextMenu_->setDisplayMode(mode);
        }
        if (statusLabel_) {
            statusLabel_->setText(QStringLiteral("  显示模式：%1").arg(modelDisplayModeText(mode)));
        }
    });

    toolbar_->addSeparator();

    auto* screenshotAction = toolbar_->addAction(toolbarIcon("screenshot"), "截图");
    screenshotAction->setToolTip("截图入口预留");
    connect(screenshotAction, &QAction::triggered, this, [this]() {
        statusLabel_->setText("  截图入口已预留");
    });

    auto* exportAction = toolbar_->addAction(toolbarIcon("export"), "导出");
    exportAction->setToolTip("导出入口预留");
    connect(exportAction, &QAction::triggered, this, [this]() {
        statusLabel_->setText("  导出入口已预留");
    });

    toolbar_->addSeparator();

    auto* clearAction = toolbar_->addAction(toolbarIcon("clear"), "清空");
    clearAction->setToolTip("清空当前模型");
    connect(clearAction, &QAction::triggered, this, &MainWindow::clearCurrentLayout);
    toolbar_->addSeparator();

    // ── 拾取模式 ──
    pickGroup_ = new QActionGroup(this);
    pickGroup_->setExclusive(true);

    auto* nodeAction = toolbar_->addAction(toolbarIcon("pick-node"), "节点");
    nodeAction->setCheckable(true);
    nodeAction->setChecked(true);
    nodeAction->setToolTip("节点拾取模式");
    nodeAction->setData(static_cast<int>(PickMode::Node));
    pickGroup_->addAction(nodeAction);

    auto* elemAction = toolbar_->addAction(toolbarIcon("pick-element"), "单元");
    elemAction->setCheckable(true);
    elemAction->setToolTip("单元拾取模式");
    elemAction->setData(static_cast<int>(PickMode::Element));
    pickGroup_->addAction(elemAction);

    auto* partAction = toolbar_->addAction(toolbarIcon("pick-part"), "部件");
    partAction->setCheckable(true);
    partAction->setToolTip("部件拾取模式");
    partAction->setData(static_cast<int>(PickMode::Part));
    pickGroup_->addAction(partAction);

    auto* labelsAction = toolbar_->addAction(toolbarIcon("labels"), "标签");
    labelsAction->setCheckable(true);
    labelsAction->setToolTip("显示选中项 ID 标签");
    connect(labelsAction, &QAction::toggled, this, [this](bool checked) {
        if (renderViewport_) renderViewport_->setShowLabels(checked);
    });

    toolbar_->addSeparator();

    const std::vector<std::pair<QString, QString>> postActions = {
        {"云图", "contour"},
        {"变形", "deform"},
        {"切片", "slice"},
        {"等值面", "iso-surface"},
        {"阈值", "threshold"}
    };
    for (const auto& actionSpec : postActions) {
        const QString text = actionSpec.first;
        const QString icon = actionSpec.second;
        auto* action = toolbar_->addAction(toolbarIcon(icon), text);
        action->setToolTip(QString("%1后处理入口").arg(text));
        connect(action, &QAction::triggered, this, [this, text]() {
            if (text == "云图") showPostDialog(contourDialog_);
            else if (text == "变形") showPostDialog(deformationDialog_);
            else if (text == "切片") {
                thresholdPanel_->setFilterMode(ResultPanel::FilterMode::Slice);
                thresholdDialog_->setWindowTitle("切片设置");
                showPostDialog(thresholdDialog_);
            } else if (text == "等值面") {
                thresholdPanel_->setFilterMode(ResultPanel::FilterMode::IsoSurface);
                thresholdDialog_->setWindowTitle("等值面设置");
                showPostDialog(thresholdDialog_);
            } else if (text == "阈值") {
                thresholdPanel_->setFilterMode(ResultPanel::FilterMode::Threshold);
                thresholdDialog_->setWindowTitle("阈值设置");
                showPostDialog(thresholdDialog_);
            }
        });
    }

    // ── RHI 选择 ──
    rhiGroup_ = new QActionGroup(this);
    rhiGroup_->setExclusive(true);
    rhiMenu_ = new QMenu(this);
    rhiMenu_->setTitle("RHI");
    rhiMenu_->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
    const RenderBackendKind preferredRhi = RenderSettings::preferredBackend();
    const RenderBackendKind rhiKinds[] = {
        RenderBackendKind::OpenGL,
        RenderBackendKind::Vulkan,
        RenderBackendKind::Metal
    };
    for (RenderBackendKind kind : rhiKinds) {
        QAction* act = rhiMenu_->addAction(QString::fromLatin1(renderBackendName(kind)));
        act->setCheckable(true);
        act->setEnabled(kind == RenderBackendKind::Metal || isRenderBackendAvailable(kind));
        act->setChecked(kind == preferredRhi);
        act->setData(static_cast<int>(kind));
        if (kind == RenderBackendKind::Metal) {
            act->setToolTip(isRenderBackendAvailable(kind)
                                ? QStringLiteral("Metal 基础模型视口、渐变背景、云图、坐标轴、部件显隐、点选/框选、高亮、叠加线框、切片交线、等值面、裁剪预览和轨道相机已可用")
                                : QStringLiteral("Metal 后端预留：保存首选项，后端接入后启用"));
        }
        rhiGroup_->addAction(act);
    }
    rhiMenu_->addSeparator();
    vulkanDrawStrategyMenu_ = rhiMenu_->addMenu(QStringLiteral("Vulkan 渲染策略"));
    vulkanDrawStrategyMenu_->setToolTipsVisible(true);
    vulkanDrawStrategyGroup_ = new QActionGroup(this);
    vulkanDrawStrategyGroup_->setExclusive(true);
    const VulkanDrawStrategy vulkanStrategies[] = {
        VulkanDrawStrategy::Traditional,
        VulkanDrawStrategy::GpuDrivenIndirect,
        VulkanDrawStrategy::MeshShader
    };
    for (VulkanDrawStrategy strategy : vulkanStrategies) {
        QAction* act = vulkanDrawStrategyMenu_->addAction(RenderSettings::vulkanDrawStrategyName(strategy));
        act->setCheckable(true);
        act->setData(static_cast<int>(strategy));
        const bool available = RenderSettings::isVulkanDrawStrategyAvailable(strategy);
        act->setEnabled(available);
        if (strategy == VulkanDrawStrategy::Traditional) {
            act->setToolTip(QStringLiteral("兼容 Vulkan 绘制路径：CPU 过滤 + vkCmdDrawIndexed；用于 GPU-driven 回退验证"));
        } else if (strategy == VulkanDrawStrategy::GpuDrivenIndirect) {
            act->setText(QStringLiteral("GPU-driven Indirect（默认）"));
            act->setToolTip(QStringLiteral("默认 Vulkan 绘制路径：compute 生成可见索引和 indirect draw 命令；能力不足或资源创建失败时运行时回退传统路径"));
        } else {
            act->setText(QStringLiteral("Mesh Shader（实验预留）"));
            act->setToolTip(QStringLiteral("预留路径：meshlet/task/mesh shader；当前暂不可启用"));
        }
        vulkanDrawStrategyGroup_->addAction(act);
    }
    updateVulkanDrawStrategyMenu();
    connect(vulkanDrawStrategyGroup_, &QActionGroup::triggered, this, [this](QAction* action) {
        const auto strategy = static_cast<VulkanDrawStrategy>(action->data().toInt());
        if (!RenderSettings::isVulkanDrawStrategyAvailable(strategy)) {
            updateVulkanDrawStrategyMenu();
            if (statusLabel_) {
                statusLabel_->setText(QStringLiteral("  Vulkan 渲染策略尚不可用，已保持传统路径"));
            }
            return;
        }
        RenderSettings::setPreferredVulkanDrawStrategy(strategy);
        updateVulkanDrawStrategyMenu();
        const bool reloadActiveVulkanMesh =
            renderViewport_ &&
            renderViewport_->activeRenderBackendKind() == RenderBackendKind::Vulkan;
        if (reloadActiveVulkanMesh) {
            pushRenderDataToGL(displayRenderData());
            reapplyContourIfNeeded();
            renderViewport_->refresh();
        }
        if (statusLabel_) {
            const QString suffix = strategy == VulkanDrawStrategy::GpuDrivenIndirect
                ? QStringLiteral("；运行时失败会自动回退")
                : QString();
            const QString reloadSuffix = reloadActiveVulkanMesh
                ? QStringLiteral("；已重传当前模型")
                : QString();
            statusLabel_->setText(QStringLiteral("  Vulkan 渲染策略已保存为 %1%2")
                                      .arg(RenderSettings::vulkanDrawStrategyName(strategy),
                                           suffix + reloadSuffix));
        }
    });
    rhiAction_ = new QAction(style()->standardIcon(QStyle::SP_ComputerIcon), "RHI", this);
    rhiAction_->setToolTip("选择下次启动使用的渲染后端");
    rhiAction_->setMenu(rhiMenu_);
    connect(rhiGroup_, &QActionGroup::triggered, this, [this](QAction* action) {
        const auto kind = static_cast<RenderBackendKind>(action->data().toInt());
        RenderSettings::setPreferredBackend(kind);
        if (renderViewport_) {
            renderViewport_->setPreferredRenderBackend(kind);
        }
        updateRhiActionText();
        updateStatusSummaries();
        const char* requestedName = renderBackendName(kind);
        const char* activeName = renderViewport_
            ? renderBackendName(renderViewport_->activeRenderBackendKind())
            : renderBackendName(RenderSettings::effectiveBackend());
        if (kind == (renderViewport_ ? renderViewport_->activeRenderBackendKind() : RenderSettings::effectiveBackend())) {
            statusLabel_->setText(QStringLiteral("  RHI 当前已是 %1；配置已保存")
                                      .arg(QString::fromLatin1(requestedName)));
        } else if (kind == RenderBackendKind::Metal) {
            statusLabel_->setText(QStringLiteral("  RHI 首选已保存为 %1；当前仍使用 %2，重启后启用 Metal 视口")
                                      .arg(QString::fromLatin1(requestedName), QString::fromLatin1(activeName)));
        } else if (!isRenderBackendAvailable(kind)) {
            statusLabel_->setText(QStringLiteral("  RHI 首选已保存为 %1；当前仍使用 %2，后端可用后重启生效")
                                      .arg(QString::fromLatin1(requestedName), QString::fromLatin1(activeName)));
        } else {
            statusLabel_->setText(QStringLiteral("  RHI 首选已保存为 %1；当前仍使用 %2，重启后生效")
                                      .arg(QString::fromLatin1(requestedName), QString::fromLatin1(activeName)));
        }
    });

    // ── 主题切换（下拉菜单） ──
    themeIndex_ = loadThemeIndex();
    themeMenu_ = new QMenu(this);
    themeMenu_->setTitle("Theme");
    themeMenu_->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
    for (int i = 0; i < Theme::count(); ++i) {
        Theme th = Theme::byIndex(i);
        QAction* act = themeMenu_->addAction(th.name);
        connect(act, &QAction::triggered, this, [this, i]() {
            themeIndex_ = i;
            currentTheme_ = Theme::byIndex(i);
            applyTheme(currentTheme_);
            saveThemeIndex(i);
            if (statusLabel_) {
                statusLabel_->setText(QStringLiteral("  Theme 已保存为 %1").arg(currentTheme_.name));
            }
        });
    }
    themeAction_ = new QAction(style()->standardIcon(QStyle::SP_FileDialogContentsView), "Theme", this);
    themeAction_->setToolTip("切换主题风格");
    themeAction_->setMenu(themeMenu_);

    // ── 连接 ──
    connect(pickGroup_, &QActionGroup::triggered, this, [this](QAction* action) {
        const auto mode = static_cast<PickMode>(action->data().toInt());
        lastSelectionMode_ = mode;
        lastSelectionIds_.clear();
        lastSelectedPartIndices_.clear();
        renderViewport_->setPickMode(mode);
        updatePickModeSummary(mode);
        if (statusLabel_) {
            statusLabel_->setText(QStringLiteral("  拾取模式：%1").arg(pickModeText(mode)));
        }
    });
}

void MainWindow::updateRhiActionText()
{
    if (!rhiAction_) {
        return;
    }

    const RenderBackendKind requested = renderViewport_
        ? renderViewport_->requestedRenderBackendKind()
        : RenderSettings::preferredBackend();
    const RenderBackendKind active = renderViewport_
        ? renderViewport_->activeRenderBackendKind()
        : RenderSettings::effectiveBackend();

    QString text = QStringLiteral("RHI: %1").arg(QString::fromLatin1(renderBackendName(requested)));
    if (active != requested) {
        text += QStringLiteral("→%1").arg(QString::fromLatin1(renderBackendName(active)));
    }
    rhiAction_->setText(text);

    if (rhiMenu_) {
        for (QAction* action : rhiMenu_->actions()) {
            if (action->menu() || !action->isCheckable()) {
                continue;
            }
            action->setChecked(action->data().toInt() == static_cast<int>(requested));
        }
    }
    updateVulkanDrawStrategyMenu();
}

void MainWindow::updateVulkanDrawStrategyMenu()
{
    if (!vulkanDrawStrategyMenu_) {
        return;
    }

    const VulkanDrawStrategy requested = RenderSettings::preferredVulkanDrawStrategy();
    const VulkanDrawStrategy active = RenderSettings::effectiveVulkanDrawStrategy();
    const bool vulkanRequested = renderViewport_
        ? renderViewport_->requestedRenderBackendKind() == RenderBackendKind::Vulkan
        : RenderSettings::preferredBackend() == RenderBackendKind::Vulkan;

    QString title = QStringLiteral("Vulkan 渲染策略: %1")
        .arg(RenderSettings::vulkanDrawStrategyName(active));
    if (requested != active) {
        title += QStringLiteral("（%1 未启用）")
            .arg(RenderSettings::vulkanDrawStrategyName(requested));
    }
    vulkanDrawStrategyMenu_->setTitle(title);
    vulkanDrawStrategyMenu_->setToolTipsVisible(true);
    vulkanDrawStrategyMenu_->setToolTip(vulkanRequested
        ? QStringLiteral("选择 Vulkan 后端内部绘制策略；GPU-driven 为默认路径，运行时能力不足会回退")
        : QStringLiteral("切换到 Vulkan RHI 后此策略生效；GPU-driven 为默认路径，运行时能力不足会回退"));

    for (QAction* action : vulkanDrawStrategyMenu_->actions()) {
        const auto strategy = static_cast<VulkanDrawStrategy>(action->data().toInt());
        const bool available = RenderSettings::isVulkanDrawStrategyAvailable(strategy);
        const bool block = action->blockSignals(true);
        action->setEnabled(available);
        action->setChecked(strategy == active);
        action->blockSignals(block);
    }
}

void MainWindow::setModelStructureVisible(bool visible)
{
    if (partsDock_) {
        partsDock_->setVisible(visible);
    }

    if (sidebarsAction_) {
        const bool block = sidebarsAction_->blockSignals(true);
        sidebarsAction_->setChecked(visible);
        sidebarsAction_->blockSignals(block);
    }
    if (leftPanelAction_) {
        const bool block = leftPanelAction_->blockSignals(true);
        leftPanelAction_->setChecked(visible);
        leftPanelAction_->blockSignals(block);
    }
    if (statusLabel_) {
        statusLabel_->setText(visible
            ? QStringLiteral("  模型结构栏已显示")
            : QStringLiteral("  模型结构栏已隐藏"));
    }
}

void MainWindow::syncSidebarActions()
{
    if (!sidebarsAction_) {
        return;
    }

    const bool leftVisible = leftPanelAction_
        ? leftPanelAction_->isChecked()
        : (partsDock_ && partsDock_->isVisible());
    if (sidebarsAction_) {
        const bool block = sidebarsAction_->blockSignals(true);
        sidebarsAction_->setChecked(leftVisible);
        sidebarsAction_->blockSignals(block);
    }
}

// ════════════════════════════════════════════════════════════
// 状态栏
// ════════════════════════════════════════════════════════════

void MainWindow::setupStatusBar() {
    auto* sb = statusBar();
    sb->setFixedHeight(30);
    // 左侧：状态文字
    statusLabel_ = new QLabel("  就绪");
    sb->addWidget(statusLabel_, 1);

    // 右侧：进度文字 + 进度条（默认隐藏）
    progressText_ = new QLabel;
    progressText_->setVisible(false);
    sb->addPermanentWidget(progressText_);

    statusProgress_ = new QProgressBar;
    statusProgress_->setFixedWidth(200);
    statusProgress_->setFixedHeight(14);
    statusProgress_->setRange(0, 100);
    statusProgress_->setTextVisible(true);
    statusProgress_->setFormat("%p%");
    statusProgress_->setVisible(false);
    sb->addPermanentWidget(statusProgress_);

    pickModeSummaryLabel_ = new QLabel(QStringLiteral("拾取模式：节点"));
    sb->addPermanentWidget(pickModeSummaryLabel_);

    fpsSummaryLabel_ = new QLabel("FPS --");
    sb->addPermanentWidget(fpsSummaryLabel_);

    frameTimeSummaryLabel_ = new QLabel("帧时间 -- ms");
    sb->addPermanentWidget(frameTimeSummaryLabel_);

    vertexSummaryLabel_ = new QLabel("顶点数 --");
    sb->addPermanentWidget(vertexSummaryLabel_);

    triangleSummaryLabel_ = new QLabel("三角面 --");
    sb->addPermanentWidget(triangleSummaryLabel_);

    modelSizeSummaryLabel_ = new QLabel("尺寸 --");
    sb->addPermanentWidget(modelSizeSummaryLabel_);

    auto* fpsTimer = new QTimer(this);
    fpsTimer->setInterval(500);
    connect(fpsTimer, &QTimer::timeout, this, &MainWindow::updateStatusSummaries);
    fpsTimer->start();
    updatePickModeSummary(PickMode::Node);
    updateStatusSummaries();
}

void MainWindow::loadBackgroundSettings()
{
    const QColor themeTop = themeColor(currentTheme_.bgTopR, currentTheme_.bgTopG, currentTheme_.bgTopB);
    const QColor themeBottom = themeColor(currentTheme_.bgBotR, currentTheme_.bgBotG, currentTheme_.bgBotB);
    QSettings settings = makeAppSettings();
    const int mode = settings.value(QString::fromLatin1(kBackgroundModeKey),
                                    static_cast<int>(BackgroundMode::Gradient)).toInt();
    backgroundMode_ = mode == static_cast<int>(BackgroundMode::Solid)
        ? BackgroundMode::Solid
        : BackgroundMode::Gradient;

    backgroundSolidColor_ = QColor(settings.value(QString::fromLatin1(kBackgroundSolidKey),
                                                  themeBottom.name(QColor::HexRgb)).toString());
    if (!backgroundSolidColor_.isValid()) {
        backgroundSolidColor_ = themeBottom;
    }

    backgroundGradientTopColor_ = QColor(settings.value(QString::fromLatin1(kBackgroundGradientTopKey),
                                                        themeTop.name(QColor::HexRgb)).toString());
    if (!backgroundGradientTopColor_.isValid()) {
        backgroundGradientTopColor_ = themeTop;
    }

    backgroundGradientBottomColor_ = QColor(settings.value(QString::fromLatin1(kBackgroundGradientBottomKey),
                                                           themeBottom.name(QColor::HexRgb)).toString());
    if (!backgroundGradientBottomColor_.isValid()) {
        backgroundGradientBottomColor_ = themeBottom;
    }
}

void MainWindow::saveBackgroundSettings() const
{
    QSettings settings = makeAppSettings();
    settings.setValue(QString::fromLatin1(kBackgroundModeKey), static_cast<int>(backgroundMode_));
    settings.setValue(QString::fromLatin1(kBackgroundSolidKey),
                      backgroundSolidColor_.name(QColor::HexRgb));
    settings.setValue(QString::fromLatin1(kBackgroundGradientTopKey),
                      backgroundGradientTopColor_.name(QColor::HexRgb));
    settings.setValue(QString::fromLatin1(kBackgroundGradientBottomKey),
                      backgroundGradientBottomColor_.name(QColor::HexRgb));
    settings.sync();
}

void MainWindow::applyViewportBackground()
{
    if (!renderViewport_) {
        return;
    }

    Theme viewportTheme = currentTheme_;
    if (backgroundMode_ == BackgroundMode::Solid) {
        writeThemeBackgroundColor(viewportTheme, backgroundSolidColor_, backgroundSolidColor_);
    } else {
        writeThemeBackgroundColor(viewportTheme, backgroundGradientTopColor_, backgroundGradientBottomColor_);
    }
    renderViewport_->applyTheme(viewportTheme);
}

void MainWindow::showBackgroundSettingsDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle("背景色设置");
    dialog.setModal(true);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);

    auto* modeRow = new QHBoxLayout;
    auto* solidRadio = new QRadioButton("纯色");
    auto* gradientRadio = new QRadioButton("渐变色");
    solidRadio->setChecked(backgroundMode_ == BackgroundMode::Solid);
    gradientRadio->setChecked(backgroundMode_ == BackgroundMode::Gradient);
    modeRow->addWidget(solidRadio);
    modeRow->addWidget(gradientRadio);
    modeRow->addStretch();
    layout->addLayout(modeRow);

    QColor color1 = backgroundMode_ == BackgroundMode::Solid
        ? backgroundSolidColor_
        : backgroundGradientTopColor_;
    QColor color2 = backgroundGradientBottomColor_;

    auto makeColorButton = [](const QString& title, const QColor& color) {
        auto* button = new QPushButton;
        button->setMinimumWidth(150);
        auto updateButton = [button, title](const QColor& c) {
            button->setText(QString("%1  %2").arg(title, c.name(QColor::HexRgb).toUpper()));
            button->setStyleSheet(QString(
                "QPushButton {"
                "  text-align: left; padding: 6px 10px;"
                "  border: 1px solid #7c7f93; border-radius: 5px;"
                "  background: %1; color: %2; }")
                .arg(c.name(QColor::HexRgb),
                     c.lightness() < 128 ? QStringLiteral("#ffffff") : QStringLiteral("#202020")));
        };
        updateButton(color);
        return button;
    };

    auto updateColorButton = [](QPushButton* button, const QString& title, const QColor& c) {
        button->setText(QString("%1  %2").arg(title, c.name(QColor::HexRgb).toUpper()));
        button->setStyleSheet(QString(
            "QPushButton {"
            "  text-align: left; padding: 6px 10px;"
            "  border: 1px solid #7c7f93; border-radius: 5px;"
            "  background: %1; color: %2; }")
            .arg(c.name(QColor::HexRgb),
                 c.lightness() < 128 ? QStringLiteral("#ffffff") : QStringLiteral("#202020")));
    };

    auto* color1Row = new QHBoxLayout;
    color1Row->addWidget(new QLabel("颜色 1"));
    auto* color1Button = makeColorButton("颜色 1", color1);
    color1Row->addWidget(color1Button, 1);
    layout->addLayout(color1Row);

    auto* color2Row = new QHBoxLayout;
    auto* color2Label = new QLabel("颜色 2");
    color2Row->addWidget(color2Label);
    auto* color2Button = makeColorButton("颜色 2", color2);
    color2Row->addWidget(color2Button, 1);
    layout->addLayout(color2Row);

    auto updateMode = [&]() {
        const bool gradient = gradientRadio->isChecked();
        color2Label->setVisible(gradient);
        color2Button->setVisible(gradient);
    };
    updateMode();

    connect(solidRadio, &QRadioButton::toggled, &dialog, [&]() { updateMode(); });
    connect(color1Button, &QPushButton::clicked, &dialog, [&]() {
        const QColor selected = QColorDialog::getColor(color1, &dialog, "选择颜色 1");
        if (selected.isValid()) {
            color1 = selected;
            updateColorButton(color1Button, "颜色 1", color1);
        }
    });
    connect(color2Button, &QPushButton::clicked, &dialog, [&]() {
        const QColor selected = QColorDialog::getColor(color2, &dialog, "选择颜色 2");
        if (selected.isValid()) {
            color2 = selected;
            updateColorButton(color2Button, "颜色 2", color2);
        }
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Apply)->setText("应用");
    buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog, [&]() {
        if (solidRadio->isChecked()) {
            backgroundMode_ = BackgroundMode::Solid;
            backgroundSolidColor_ = color1;
        } else {
            backgroundMode_ = BackgroundMode::Gradient;
            backgroundGradientTopColor_ = color1;
            backgroundGradientBottomColor_ = color2;
        }
        saveBackgroundSettings();
        applyViewportBackground();
        if (statusLabel_) {
            statusLabel_->setText(backgroundMode_ == BackgroundMode::Solid
                ? QStringLiteral("  背景已切换为纯色")
                : QStringLiteral("  背景已切换为渐变色"));
        }
    });

    dialog.exec();
}

void MainWindow::showModelInfoDialog()
{
    const FEModel& model = activeModel();
    if (model.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("模型信息"), QStringLiteral("当前没有已加载的模型。"));
        return;
    }

    const FERenderData& rd = displayRenderData();
    const Mesh& mesh = rd.mesh;

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("模型信息"));
    dialog.resize(620, 560);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto* tree = new QTreeWidget(&dialog);
    tree->setColumnCount(2);
    tree->setHeaderLabels({QStringLiteral("项目"), QStringLiteral("值")});
    tree->header()->setStretchLastSection(true);
    tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree->setAlternatingRowColors(false);
    layout->addWidget(tree, 1);

    auto addGroup = [tree](const QString& title) {
        auto* item = new QTreeWidgetItem(tree);
        item->setText(0, title);
        QFont font = item->font(0);
        font.setBold(true);
        item->setFont(0, font);
        return item;
    };
    auto addRow = [](QTreeWidgetItem* parent, const QString& name, const QString& value) {
        auto* item = new QTreeWidgetItem(parent);
        item->setText(0, name);
        item->setText(1, value);
    };
    auto numberText = [](size_t value) {
        return QLocale().toString(static_cast<qlonglong>(value));
    };

    const QString filePath = QString::fromStdString(model.filePath);
    const QFileInfo fileInfo(filePath);
    const QString modelName = model.name.empty()
        ? (fileInfo.fileName().isEmpty() ? QStringLiteral("未命名模型") : fileInfo.fileName())
        : QString::fromStdString(model.name);
    const QString formatText = fileInfo.suffix().isEmpty()
        ? QStringLiteral("未知")
        : fileInfo.suffix().toUpper();

    QTreeWidgetItem* basic = addGroup(QStringLiteral("基本信息"));
    addRow(basic, QStringLiteral("模型名称"), modelName);
    addRow(basic, QStringLiteral("文件路径"), filePath.isEmpty() ? QStringLiteral("未知") : filePath);
    addRow(basic, QStringLiteral("文件格式"), formatText);
    addRow(basic, QStringLiteral("单位"), QStringLiteral("未知"));
    addRow(basic, QStringLiteral("当前渲染后端"),
           renderViewport_ ? QString::fromLatin1(renderBackendName(renderViewport_->activeRenderBackendKind()))
                           : QStringLiteral("未知"));

    QTreeWidgetItem* counts = addGroup(QStringLiteral("规模统计"));
    addRow(counts, QStringLiteral("节点数"), numberText(model.nodes.size()));
    addRow(counts, QStringLiteral("单元数"), numberText(model.elements.size()));
    addRow(counts, QStringLiteral("部件数"), numberText(model.parts.size()));
    addRow(counts, QStringLiteral("节点集数"), numberText(model.nodeSets.size()));
    addRow(counts, QStringLiteral("单元集数"), numberText(model.elementSets.size()));
    addRow(counts, QStringLiteral("标量场数"), numberText(model.scalarFields.size()));
    addRow(counts, QStringLiteral("矢量场数"), numberText(model.vectorFields.size()));
    addRow(counts, QStringLiteral("结果帧数"), QLocale().toString(loadedResultFrameCount_));
    addRow(counts, QStringLiteral("渲染顶点数"), numberText(mesh.vertices.size() / 6));
    addRow(counts, QStringLiteral("渲染三角面数"), numberText(mesh.indices.size() / 3));
    addRow(counts, QStringLiteral("渲染边线数"), numberText(mesh.edgeIndices.size() / 2));

    glm::vec3 bbMin(0.0f);
    glm::vec3 bbMax(0.0f);
    model.computeBoundingBox(bbMin, bbMax);
    const glm::vec3 span = bbMax - bbMin;
    QTreeWidgetItem* bounds = addGroup(QStringLiteral("空间尺寸"));
    addRow(bounds, QStringLiteral("包围盒 Min"), vec3Text(bbMin));
    addRow(bounds, QStringLiteral("包围盒 Max"), vec3Text(bbMax));
    addRow(bounds, QStringLiteral("尺寸 X/Y/Z"), vec3Text(span));
    addRow(bounds, QStringLiteral("几何中心"), vec3Text(model.computeCenter()));
    addRow(bounds, QStringLiteral("最大尺寸/对角线"), QString::number(model.computeSize(), 'g', 7));

    std::map<ElementType, int> elementTypeCounts;
    for (const auto& [id, element] : model.elements) {
        (void)id;
        ++elementTypeCounts[element.type];
    }
    QTreeWidgetItem* elementTypes = addGroup(QStringLiteral("单元类型统计"));
    if (elementTypeCounts.empty()) {
        addRow(elementTypes, QStringLiteral("无"), QStringLiteral("0"));
    } else {
        for (const auto& [type, count] : elementTypeCounts) {
            addRow(elementTypes, elementTypeText(type), QLocale().toString(count));
        }
    }

    int visiblePartCount = 0;
    int hiddenPartCount = 0;
    if (partsPanel_) {
        for (const auto& [partIndex, visible] : partsPanel_->partVisibilityStates()) {
            (void)partIndex;
            visible ? ++visiblePartCount : ++hiddenPartCount;
        }
    } else {
        visiblePartCount = static_cast<int>(model.parts.size());
    }

    QString largestPartName = QStringLiteral("无");
    size_t largestPartElements = 0;
    for (const FEPart& part : model.parts) {
        if (part.elementIds.size() > largestPartElements) {
            largestPartElements = part.elementIds.size();
            largestPartName = QString::fromStdString(part.name);
            if (largestPartName.isEmpty()) {
                largestPartName = QStringLiteral("未命名部件");
            }
        }
    }
    QTreeWidgetItem* parts = addGroup(QStringLiteral("部件摘要"));
    addRow(parts, QStringLiteral("可见部件"), QLocale().toString(visiblePartCount));
    addRow(parts, QStringLiteral("隐藏部件"), QLocale().toString(hiddenPartCount));
    addRow(parts, QStringLiteral("当前选中部件"), numberText(lastSelectedPartIndices_.size()));
    addRow(parts, QStringLiteral("最大部件"),
           QStringLiteral("%1（%2 单元）").arg(largestPartName, numberText(largestPartElements)));

    tree->expandAll();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog.setStyleSheet(QStringLiteral(
        "QDialog { background: %1; color: %2; }"
        "QTreeWidget { background: %3; color: %2; border: 1px solid %4; border-radius: 6px; }"
        "QTreeWidget::item { background: %3; color: %2; padding: 4px; }"
        "QTreeWidget::item:hover { background: %4; }"
        "QTreeWidget::item:selected { background: %5; color: %2; }"
        "QTreeWidget::branch { background: %3; }"
        "QHeaderView::section { background: %3; color: %6; border: none; padding: 5px 8px; font-weight: bold; }"
        "QPushButton { background: %6; color: %7; border: none; border-radius: 6px; padding: 6px 18px; }"
        "QPushButton:hover { background: %8; }"
    ).arg(currentTheme_.base,
          currentTheme_.text,
          currentTheme_.mantle,
          currentTheme_.surface0,
          currentTheme_.surface1,
          currentTheme_.blue,
          currentTheme_.crust,
          currentTheme_.teal));
    dialog.exec();
}

void MainWindow::applyTheme(const Theme& t) {
    // 主题按钮文字更新
    themeAction_->setText(t.name);

    // 主窗口背景
    setStyleSheet(QString(
        "QMainWindow { background: %1; }"
        "QMenuBar {"
        "  background: %2; color: %3; border-bottom: 1px solid %4;"
        "  padding: 2px 6px; font-size: 12px; }"
        "QMenuBar::item {"
        "  background: transparent; padding: 5px 10px; border-radius: 4px; }"
        "QMenuBar::item:selected { background: %4; color: %5; }"
    ).arg(t.mantle, t.crust, t.text, t.surface0, t.blue));

    // 侧边栏停靠面板
    QString dockStyle = QString(
        "QDockWidget {"
        "  color: %1; font-size: 12px; font-weight: bold;"
        "  titlebar-close-icon: none; }"
        "QDockWidget::title {"
        "  background: %2; border-bottom: 1px solid %3;"
        "  padding: 6px 10px; }"
    ).arg(t.blue, t.mantle, t.surface0);
    partsDock_->setStyleSheet(dockStyle);

    // 工具栏 — Tecplot 式紧凑图标按钮
    const QString toolBarStyle = QString(
        "QToolBar {"
        "  background: %1; border-bottom: 1px solid %2;"
        "  padding: 1px 4px; spacing: 1px; }"
        "QToolButton {"
        "  background: transparent; color: %3;"
        "  border: 1px solid transparent; border-radius: 2px;"
        "  padding: 1px; margin: 0;"
        "  min-width: 24px; min-height: 24px;"
        "  max-width: 24px; max-height: 24px; }"
        "QToolButton:hover {"
        "  background: %4;"
        "  border: 1px solid %5; }"
        "QToolButton:pressed {"
        "  background: %4; }"
        "QToolButton:checked {"
        "  background: %4; color: %5;"
        "  border: 1px solid %5; border-radius: 2px; }"
        "QToolButton:checked:hover {"
        "  background: %4;"
        "  border: 1px solid %5; }"
        "QToolBar::separator {"
        "  width: 1px; background: %2; margin: 3px 3px; }"
    ).arg(t.mantle, t.surface0, t.text, t.surface1, t.blue);
    toolbar_->setStyleSheet(toolBarStyle);
    if (postToolBar_) {
        postToolBar_->setStyleSheet(toolBarStyle);
    }

    // 主题下拉菜单
    const QString menuStyle = QString(
        "QMenu {"
        "  background: %1; border: 1px solid %2; border-radius: 6px;"
        "  padding: 6px 0; }"
        "QMenu::item {"
        "  color: %3; padding: 8px 24px; font-size: 12px; border-radius: 4px;"
        "  margin: 2px 6px; }"
        "QMenu::item:selected {"
        "  background: %4; color: %5; }"
        "QMenu::indicator {"
        "  width: 14px; height: 14px; margin-left: 8px; }"
    ).arg(t.mantle, t.surface0, t.text, t.surface1, t.blue);
    themeMenu_->setStyleSheet(menuStyle);
    if (rhiMenu_) {
        rhiMenu_->setStyleSheet(menuStyle);
    }
    for (QMenu* menu : menuBar()->findChildren<QMenu*>()) {
        menu->setStyleSheet(menuStyle);
    }

    // 标记当前主题
    auto actions = themeMenu_->actions();
    for (int i = 0; i < actions.size(); ++i)
        actions[i]->setCheckable(true);
    for (int i = 0; i < actions.size(); ++i)
        actions[i]->setChecked(i == themeIndex_);

    // 状态栏 — 更高、更宽敞
    statusBar()->setStyleSheet(QString(
        "QStatusBar {"
        "  background: %1; border-top: 1px solid %2;"
        "  font-size: 11px; padding: 2px 8px; }"
        "QStatusBar::item { border: none; }"
    ).arg(t.crust, t.surface0));

    statusLabel_->setStyleSheet(
        QString("color: %1; font-weight: bold; font-size: 11px;").arg(t.green));
    progressText_->setStyleSheet(
        QString("color: %1; font-size: 11px; padding-right: 8px;").arg(t.blue));
    const QString summaryStyle =
        QString("color: %1; font-size: 11px; padding: 0 8px;").arg(t.subtext1);
    if (pickModeSummaryLabel_) {
        pickModeSummaryLabel_->setStyleSheet(
            QString("color: %1; font-size: 11px; font-weight: 600; padding: 0 8px;").arg(t.blue));
    }
    if (fpsSummaryLabel_) fpsSummaryLabel_->setStyleSheet(summaryStyle);
    if (frameTimeSummaryLabel_) frameTimeSummaryLabel_->setStyleSheet(summaryStyle);
    if (vertexSummaryLabel_) vertexSummaryLabel_->setStyleSheet(summaryStyle);
    if (triangleSummaryLabel_) triangleSummaryLabel_->setStyleSheet(summaryStyle);
    if (modelSizeSummaryLabel_) modelSizeSummaryLabel_->setStyleSheet(summaryStyle);
    statusProgress_->setStyleSheet(QString(
        "QProgressBar {"
        "  border: 1px solid %1; border-radius: 7px;"
        "  background: %2; text-align: center;"
        "  color: %3; font-size: 10px; }"
        "QProgressBar::chunk {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 %4, stop:1 %5);"
        "  border-radius: 6px; }"
    ).arg(t.surface1, t.base, t.text, t.blue, t.teal));

    if (modelNavigatorPanel_) {
        modelNavigatorPanel_->setStyleSheet(QString(
            "QWidget { background: %1; color: %2; }"
            "QTreeWidget {"
            "  background: %3; border: 1px solid %4;"
            "  border-radius: 6px; outline: none; padding: 2px; }"
            "QTreeWidget::item {"
            "  padding: 4px 4px; border-radius: 4px; margin: 1px 0; }"
            "QTreeWidget::item:hover { background: %4; }"
            "QTreeWidget::item:selected { background: %5; }"
            "QHeaderView::section {"
            "  background: %3; border: none; border-bottom: 1px solid %4;"
            "  padding: 5px 8px; font-weight: bold; font-size: 12px; color: %6; }"
        ).arg(t.base, t.text, t.mantle, t.surface0, t.surface1, t.blue));
    }

    // 各面板
    feModelPanel_->applyTheme(t);
    partsPanel_->applyTheme(t);
    resultPanel_->applyTheme(t);
    deformationPanel_->applyTheme(t);
    thresholdPanel_->applyTheme(t);
    applyViewportBackground();

    const QString dialogStyle = QString("QDialog { background: %1; }").arg(t.base);
    if (contourDialog_) contourDialog_->setStyleSheet(dialogStyle);
    if (deformationDialog_) deformationDialog_->setStyleSheet(dialogStyle);
    if (thresholdDialog_) thresholdDialog_->setStyleSheet(dialogStyle);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    QSettings settings("FEModelViewer", "FEModelViewer");
    settings.setValue("lastModelPath", importPaths_.modelPath);
    settings.setValue("lastResultPath", importPaths_.resultPath);
    settings.setValue("lastResultPathAutoFilled", importPaths_.resultPathAutoFilled);
    QMainWindow::closeEvent(event);
}

// ════════════════════════════════════════════════════════════
// 变形 / 云图辅助
// ════════════════════════════════════════════════════════════

const FERenderData& MainWindow::activeRenderData() const
{
    return deform_.active ? deform_.renderData : feModelPanel_->currentRenderData();
}

const FEModel& MainWindow::activeModel() const
{
    return deform_.active ? deform_.model : feModelPanel_->currentModel();
}

const FERenderData& MainWindow::displayRenderData() const
{
    if (postEffect_.isActive() && postEffect_.isMeshReplacement())
        return postEffect_.filteredRD;
    return activeRenderData();
}

void MainWindow::pushRenderDataToGL(const FERenderData& rd)
{
    renderViewport_->setMesh(rd.mesh);
    renderViewport_->setTriangleToElementMap(rd.triangleToElement);
    renderViewport_->setVertexToNodeMap(rd.vertexToNode);
    renderViewport_->setTriangleToPartMap(rd.triangleToPart);
    renderViewport_->setEdgeToPartMap(rd.edgeToPart);
    syncPartVisibilityToViewport();
    if (!hiddenElementIds_.empty()) {
        renderViewport_->setElementsVisibility(
            std::vector<int>(hiddenElementIds_.begin(), hiddenElementIds_.end()),
            false);
    }
}

void MainWindow::reapplyContourIfNeeded()
{
    if (contour_.active)
        applyContour(contour_.field, contour_.title);
}

void MainWindow::beginPostEffect(PostEffectMode mode)
{
    renderViewport_->clearSliceLines();
    renderViewport_->clearIsoSurface();

    bool wasReplacement = postEffect_.isActive() && postEffect_.isMeshReplacement();
    bool willReplace = (mode == PostEffectMode::Threshold
                     || mode == PostEffectMode::ClipPlane);

    if (wasReplacement && !willReplace)
        pushRenderDataToGL(activeRenderData());

    postEffect_.clear();
    postEffect_.mode = mode;
}

void MainWindow::updateFilterPlaneBounds()
{
    const FEModel& model = activeModel();
    if (model.nodes.empty()) {
        thresholdPanel_->setPlaneBounds(glm::vec3(0.0f), glm::vec3(0.0f));
        renderViewport_->clearClipPlanePreview();
        return;
    }

    glm::vec3 bbMin, bbMax;
    model.computeBoundingBox(bbMin, bbMax);
    thresholdPanel_->setPlaneBounds(bbMin, bbMax);
}

void MainWindow::applyDeformation(float scale, bool overlayUndeformed)
{
    const FEModel& model = feModelPanel_->currentModel();
    if (model.nodes.empty()) return;

    FEVectorField disp = deformationPanel_->currentDisplacement();
    if (disp.values.empty()) return;

    FEDeformationOptions opts;
    opts.scale = scale;
    opts.overlayUndeformed = overlayUndeformed;

    deform_.model = FEDeformation::apply(model, disp, opts);
    deform_.renderData = FEMeshConverter::toRenderData(deform_.model);
    deform_.active = true;
    deform_.scale = scale;
    deform_.overlay = overlayUndeformed;

    // 主网格即将替换，清除依赖旧网格的后处理效果
    postEffect_.clear();
    renderViewport_->clearSliceLines();
    renderViewport_->clearIsoSurface();
    renderViewport_->clearClipPlanePreview();

    if (overlayUndeformed) {
        renderViewport_->setOverlayMesh(feModelPanel_->currentRenderData().mesh);
        renderViewport_->setOverlayVisible(true);
    } else {
        renderViewport_->setOverlayVisible(false);
    }

    pushRenderDataToGL(deform_.renderData);
    reapplyContourIfNeeded();

    float size = deform_.model.computeSize();
    if (size > 0.0f)
        renderViewport_->fitToModel(deform_.model.computeCenter(), size);
    updateFilterPlaneBounds();
}

void MainWindow::clearDeformation()
{
    deform_.clear();

    // 主网格即将还原，清除依赖旧网格的后处理效果
    postEffect_.clear();
    renderViewport_->clearSliceLines();
    renderViewport_->clearIsoSurface();
    renderViewport_->clearClipPlanePreview();
    renderViewport_->setOverlayVisible(false);

    pushRenderDataToGL(feModelPanel_->currentRenderData());
    reapplyContourIfNeeded();

    const FEModel& model = feModelPanel_->currentModel();
    float size = model.computeSize();
    if (size > 0.0f)
        renderViewport_->fitToModel(model.computeCenter(), size);
    updateFilterPlaneBounds();
}

void MainWindow::applyContour(const FEScalarField& field, const QString& title)
{
    contour_.field = field;
    contour_.title = title;
    contour_.active = true;

    const FEModel& model = activeModel();
    if (model.nodes.empty()) return;

    const FERenderData& rd = displayRenderData();
    int vertCount = static_cast<int>(rd.mesh.vertices.size() / 6);
    int edgeVertCount = static_cast<int>(rd.mesh.edgeVertices.size() / 3);
    if (vertCount == 0 && edgeVertCount == 0) return;

    const int numBands = 9;

    FEMappedScalars mapped = FEResultMapper::mapScalarToVertices(field, rd, model);
    if (mapped.scalars.empty() && mapped.edgeScalars.empty()) return;

    renderViewport_->setVertexScalars(mapped.scalars, mapped.minValue, mapped.maxValue, numBands);
    renderViewport_->setEdgeScalars(mapped.edgeScalars, mapped.minValue, mapped.maxValue, numBands);
    renderViewport_->setColorBarVisible(true);
    renderViewport_->setColorBarRange(mapped.minValue, mapped.maxValue);
    renderViewport_->setColorBarTitle(title);
    renderViewport_->setColorBarIdLabel(mapped.location == FieldLocation::Element ? "Ele ID" : "Node ID");
    renderViewport_->setColorBarExtremes(mapped.minId, mapped.minValue, mapped.maxId, mapped.maxValue);
}

// ── 过滤方法 ──

static int axisFromNormal(const glm::vec3& n) {
    if (std::abs(n.y) > 0.5f) return 1;
    if (std::abs(n.z) > 0.5f) return 2;
    return 0;
}

void MainWindow::applyThreshold(float minVal, float maxVal)
{
    const FERenderData& rd = activeRenderData();
    if (rd.triangleCount() == 0) return;

    FEScalarField field;
    QString title;
    if (!thresholdPanel_->currentScalarField(field, title)) return;

    FEScalarField elemField = field;
    if (field.location == FieldLocation::Node) {
        elemField.values.clear();
        elemField.location = FieldLocation::Element;
        const FEModel& model = activeModel();
        for (const auto& [eid, elem] : model.elements) {
            float sum = 0.0f;
            int n = 0;
            for (int nid : elem.nodeIds) {
                auto it = field.values.find(nid);
                if (it != field.values.end()) { sum += it->second; ++n; }
            }
            if (n > 0) elemField.values[eid] = sum / n;
        }
    }

    beginPostEffect(PostEffectMode::Threshold);

    postEffect_.threshold = { title, minVal, maxVal };
    postEffect_.filteredRD = FEPostFilter::thresholdByElementValue(rd, elemField, minVal, maxVal);
    postEffect_.applied = true;

    pushRenderDataToGL(postEffect_.filteredRD);
    reapplyContourIfNeeded();

    int total = rd.triangleCount();
    int kept = postEffect_.filteredRD.triangleCount();
    int totalElems = static_cast<int>(elemField.values.size());
    int keptElems = 0;
    {
        std::set<int> seen;
        for (int eid : postEffect_.filteredRD.triangleToElement) seen.insert(eid);
        keptElems = static_cast<int>(seen.size());
    }
    statusBar()->showMessage(
        QString("阈值 [%1, %2]: 保留 %3/%4 单元，%5/%6 三角面")
            .arg(minVal, 0, 'f', 4).arg(maxVal, 0, 'f', 4)
            .arg(keptElems).arg(totalElems)
            .arg(kept).arg(total),
        8000);
}

void MainWindow::applyClipPlane(const glm::vec3& origin, const glm::vec3& normal, bool keepPositive)
{
    const FERenderData& rd = activeRenderData();
    if (rd.triangleCount() == 0) return;

    FEPlane plane;
    plane.origin = origin;
    plane.normal = normal;

    beginPostEffect(PostEffectMode::ClipPlane);

    int axis = axisFromNormal(normal);
    postEffect_.clipPlane = { axis, origin[axis], origin, normal, keepPositive };
    postEffect_.filteredRD = FEPostFilter::clipByPlane(rd, plane, keepPositive);
    postEffect_.applied = true;

    pushRenderDataToGL(postEffect_.filteredRD);
    reapplyContourIfNeeded();
}

void MainWindow::applySlicePlane(const glm::vec3& origin, const glm::vec3& normal)
{
    beginPostEffect(PostEffectMode::Slice);

    int axis = axisFromNormal(normal);
    postEffect_.slice = { axis, origin[axis], origin, normal };
    postEffect_.applied = true;

    const FERenderData& rd = displayRenderData();
    if (rd.triangleCount() == 0) return;

    FEPlane plane;
    plane.origin = origin;
    plane.normal = normal;

    FESliceResult slice = FEPostFilter::sliceByPlane(rd, plane);
    renderViewport_->setSliceLines(slice.lineVertices);
}

void MainWindow::applyIsoSurface(float isoValue)
{
    beginPostEffect(PostEffectMode::IsoSurface);

    const FEModel& model = activeModel();
    if (model.nodes.empty()) return;

    FEScalarField field;
    QString title;
    if (!thresholdPanel_->currentScalarField(field, title)) return;

    postEffect_.iso = { title, isoValue };
    postEffect_.applied = true;

    Mesh iso = FEIsoSurface::extract(model, field, isoValue);
    renderViewport_->setIsoSurfaceMesh(iso);
}

void MainWindow::clearFilters()
{
    bool wasReplacement = postEffect_.isActive() && postEffect_.isMeshReplacement();
    postEffect_.clear();

    renderViewport_->clearSliceLines();
    renderViewport_->clearIsoSurface();
    renderViewport_->clearClipPlanePreview();

    if (wasReplacement)
        pushRenderDataToGL(activeRenderData());

    reapplyContourIfNeeded();
}
