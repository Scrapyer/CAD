/**
 * @file MainWindow.cpp
 * @brief 主窗口实现
 *
 * Tecplot 风格布局：
 *   ┌─────────────────────────────────────────────────────────┐
 *   │ Tecplot 式菜单栏 + 紧凑快捷工具栏                        │
 *   ├────────────┬──────────────────────────────┬─────────────┤
 *   │ 模型结构   │      RenderViewport           │ 属性/控制   │
 *   │ 项目树/部件│                              │ 选择/显示   │
 *   │            ├──────────────────────────────┤             │
 *   │            │  底部工作流 Tabs              │             │
 *   ├────────────┴──────────────────────────────┴─────────────┤
 *   │ 状态栏                                                   │
 *   └─────────────────────────────────────────────────────────┘
 */

#include "MainWindow.h"
#include "RenderViewport.h"
#include "FEModelPanel.h"
#include "PartsPanel.h"
#include "ResultPanel.h"
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
#include <set>
#include <utility>
#include <vector>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QToolBar>
#include <QAction>
#include <QActionGroup>
#include <QGroupBox>
#include <QHeaderView>
#include <QMenuBar>
#include <QStatusBar>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QTimer>
#include <QStyle>
#include <QKeySequence>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QSettings>
#include <QDir>
#include <QCloseEvent>
#include <QStorageInfo>
#include <QUrl>

namespace {

QIcon toolbarIcon(const QString& name)
{
    return QIcon(QStringLiteral(":/icons/toolbar/") + name + QStringLiteral(".svg"));
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
    feModelPanel_ = new FEModelPanel;
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

    // ── 左右侧组合面板 ──
    modelNavigatorPanel_ = createModelNavigatorPanel();
    inspectorPanel_ = createInspectorPanel();

    // ── 后处理小弹窗 ──
    contourDialog_ = createPostDialog("云图显示", resultPanel_);
    deformationDialog_ = createPostDialog("变形显示", deformationPanel_);
    thresholdDialog_ = createPostDialog("阈值设置", thresholdPanel_);

    // ── 中央区域：渲染视口 ──
    setCentralWidget(renderViewport_);

    // ── 左侧停靠：模型结构 ──
    partsDock_ = new QDockWidget("模型结构", this);
    partsDock_->setWidget(modelNavigatorPanel_);
    partsDock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::LeftDockWidgetArea, partsDock_);

    // ── 右侧停靠：属性 / 控制 ──
    modelInfoDock_ = new QDockWidget("属性 / 控制", this);
    modelInfoDock_->setWidget(inspectorPanel_);
    modelInfoDock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, modelInfoDock_);
    resizeDocks({partsDock_, modelInfoDock_}, {240, 280}, Qt::Horizontal);

    // ── 状态栏 ──
    setupStatusBar();
    updateProjectTreeSummary();

    // ── Tecplot 式菜单栏：主功能从菜单下拉进入 ──
    setupMenuBar();

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
        feModelPanel_->clearActiveScalarField();
        resultPanel_->clearResults();
        deformationPanel_->clearResults();
        thresholdPanel_->clearResults();
        if (animController_) animController_->setFrameCount(0);
        loadedResultFrameCount_ = 0;

        renderViewport_->setMesh(mesh);
        renderViewport_->setTriangleToElementMap(triToElem);
        renderViewport_->setVertexToNodeMap(vertexToNode);
        renderViewport_->setObjectColor(glm::vec3(0.55f, 0.75f, 0.73f));
        if (size > 0) {
            renderViewport_->fitToModel(center, size);
        }
        updateProjectTreeSummary();
        updateStatusSummaries();
        updateFilterPlaneBounds();
    });

    connect(renderViewport_, &RenderViewport::selectionChanged,
            feModelPanel_, &FEModelPanel::updateSelectionInfo);

    connect(feModelPanel_, &FEModelPanel::partsChanged,
            this, [this](const QString& modelName, const std::vector<FEPart>& parts,
                         const std::vector<int>& triToPart, const std::vector<int>& edgeToPart) {
        renderViewport_->setTriangleToPartMap(triToPart);
        renderViewport_->setEdgeToPartMap(edgeToPart);
        partsPanel_->setParts(modelName, parts, renderViewport_->partColors());
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
            renderViewport_, &RenderViewport::setPartVisibility);

    connect(partsPanel_, &PartsPanel::partSelectionChanged,
            renderViewport_, &RenderViewport::highlightParts);

    // 部件拾取 → 同步模型树选中状态
    connect(renderViewport_, &RenderViewport::partsPicked,
            partsPanel_, &PartsPanel::selectParts);

    // ID 标签显隐 → 渲染视口
    connect(feModelPanel_, &FEModelPanel::labelVisibilityChanged,
            renderViewport_, &RenderViewport::setShowLabels);

    // ID 搜索 → 渲染视口选中高亮
    connect(feModelPanel_, &FEModelPanel::searchRequested,
            renderViewport_, &RenderViewport::selectByIds);

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
        feModelPanel_->setActiveScalarField(field);
    });

    // 清除云图 → 恢复部件颜色
    connect(resultPanel_, &ResultPanel::clearResult,
            this, [this]() {
        contour_.clear();
        renderViewport_->setUseVertexColor(false);
        renderViewport_->setColorBarVisible(false);
        renderViewport_->refresh();
        feModelPanel_->clearActiveScalarField();
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

    // ── 初始主题（默认深色）──
    currentTheme_ = Theme::dark();
    applyTheme(currentTheme_);
}

void MainWindow::browseModelFile() {
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
    if (dialog.exec() != QDialog::Accepted) return;
    QString path = dialog.selectedFiles().first();

    if (!path.isEmpty()) {
        importPaths_.selectModelFile(path);
        settings.setValue("lastOpenDir", QFileInfo(path).absolutePath());
    }
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

    projectTree_ = new QTreeWidget;
    projectTree_->setHeaderLabel("Project");
    projectTree_->setIndentation(16);
    projectTree_->setUniformRowHeights(true);
    projectTree_->setAnimated(true);
    projectTree_->setMinimumHeight(130);

    auto* root = new QTreeWidgetItem(projectTree_, {"FEModelViewer"});
    root->setFlags(root->flags() | Qt::ItemIsEnabled);
    QFont rootFont = root->font(0);
    rootFont.setBold(true);
    root->setFont(0, rootFont);

    const QStringList nodes = {"Model", "Parts", "Fields", "Results"};
    for (const QString& name : nodes) {
        auto* item = new QTreeWidgetItem(root, {name});
        item->setFlags(item->flags() | Qt::ItemIsEnabled);
    }
    projectTree_->expandAll();

    layout->addWidget(projectTree_, 0);
    layout->addWidget(partsPanel_, 1);
    return panel;
}

QWidget* MainWindow::createInspectorPanel() {
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    displayControlPanel_ = createDisplayControlPanel();
    layout->addWidget(feModelPanel_, 1);
    layout->addWidget(displayControlPanel_, 0);
    return panel;
}

QWidget* MainWindow::createDisplayControlPanel() {
    auto* group = new QGroupBox("Display");
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(10, 16, 10, 10);
    layout->setSpacing(6);

    const QStringList actions = {"颜色", "透明度", "显隐"};
    for (const QString& text : actions) {
        auto* button = new QPushButton(text);
        connect(button, &QPushButton::clicked, this, [this, text]() {
            statusLabel_->setText(QString("  %1入口已预留").arg(text));
        });
        layout->addWidget(button);
    }
    return group;
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
    connect(newLayoutAction, &QAction::triggered, this, [this]() {
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
        feModelPanel_->clearActiveScalarField();
        resultPanel_->clearResults();
        deformationPanel_->clearResults();
        thresholdPanel_->clearResults();
        if (animController_) animController_->setFrameCount(0);
        loadedResultFrameCount_ = 0;
        feModelPanel_->clearModel();
        updateProjectTreeSummary();
        updateStatusSummaries();
        if (statusLabel_) statusLabel_->setText("  New layout");
    });

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
        browseModelFile();
        applyFiles();
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
        browseModelFile();
        applyFiles();
    });
    auto* loadResultAction = dataMenu->addAction("Load Result Data...");
    connect(loadResultAction, &QAction::triggered, this, [this]() {
        browseResultFile();
        applyFiles();
    });
    dataMenu->addSeparator();
    auto* applyDataAction = dataMenu->addAction("Apply Current Paths");
    connect(applyDataAction, &QAction::triggered, this, &MainWindow::applyFiles);

    auto* frameMenu = menuBar()->addMenu("Frame");
    auto* leftPanelAction = frameMenu->addAction("Model Structure");
    leftPanelAction->setCheckable(true);
    leftPanelAction->setChecked(true);
    connect(leftPanelAction, &QAction::toggled, this, [this](bool visible) {
        if (partsDock_) partsDock_->setVisible(visible);
    });
    auto* rightPanelAction = frameMenu->addAction("Property Panel");
    rightPanelAction->setCheckable(true);
    rightPanelAction->setChecked(true);
    connect(rightPanelAction, &QAction::toggled, this, [this](bool visible) {
        if (modelInfoDock_) modelInfoDock_->setVisible(visible);
    });
    auto* optionsMenu = menuBar()->addMenu("Options");
    if (themeMenu_) optionsMenu->addMenu(themeMenu_);
    if (rhiMenu_) optionsMenu->addMenu(rhiMenu_);

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

    auto* newLayoutAction = toolbar_->addAction(toolbarIcon("new-layout"), "新建");
    newLayoutAction->setToolTip("新建布局 / 清空当前模型");

    auto* openAction = toolbar_->addAction(toolbarIcon("open"), "打开");
    openAction->setToolTip("打开模型文件");
    connect(openAction, &QAction::triggered, this, [this]() {
        browseModelFile();
        applyFiles();
    });

    auto* saveAction = toolbar_->addAction(toolbarIcon("save"), "保存");
    saveAction->setToolTip("保存入口预留");
    connect(saveAction, &QAction::triggered, this, [this]() {
        statusLabel_->setText("  保存入口已预留");
    });

    auto* importAction = toolbar_->addAction(toolbarIcon("import"), "导入");
    importAction->setToolTip("导入 STEP / IGES / STL 几何文件");
    connect(importAction, &QAction::triggered, this, [this]() {
        browseImportFile();
        applyFiles();
    });

    auto* importResultAction = toolbar_->addAction(toolbarIcon("import-result"), "导入结果");
    importResultAction->setToolTip("导入 OP2 / UNV 结果文件");
    connect(importResultAction, &QAction::triggered, this, [this]() {
        browseResultFile();
        applyFiles();
    });

    auto* printAction = toolbar_->addAction(toolbarIcon("print"), "打印");
    printAction->setToolTip("打印入口预留");
    connect(printAction, &QAction::triggered, this, [this]() {
        if (statusLabel_) statusLabel_->setText("  Print 入口已预留");
    });

    toolbar_->addSeparator();

    const std::vector<std::pair<QString, QString>> viewActions = {
        {"选择", "select"},
        {"平移", "pan"},
        {"旋转", "rotate"},
        {"缩放", "zoom"},
        {"适配", "fit"}
    };
    for (const auto& actionSpec : viewActions) {
        const QString text = actionSpec.first;
        const QString icon = actionSpec.second;
        auto* action = toolbar_->addAction(toolbarIcon(icon), text);
        action->setToolTip(QString("%1视图入口").arg(text));
        connect(action, &QAction::triggered, this, [this, text]() {
            if (text == "适配" && renderViewport_) {
                renderViewport_->refresh();
            }
            statusLabel_->setText(QString("  %1入口已预留").arg(text));
        });
    }

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
    connect(newLayoutAction, &QAction::triggered, clearAction, &QAction::trigger);
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
        RenderBackendKind::Vulkan
    };
    for (RenderBackendKind kind : rhiKinds) {
        QAction* act = rhiMenu_->addAction(QString::fromLatin1(renderBackendName(kind)));
        act->setCheckable(true);
        act->setEnabled(isRenderBackendAvailable(kind));
        act->setChecked(kind == preferredRhi);
        act->setData(static_cast<int>(kind));
        rhiGroup_->addAction(act);
    }
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
        } else {
            statusLabel_->setText(QStringLiteral("  RHI 首选已保存为 %1；当前仍使用 %2，重启后生效")
                                      .arg(QString::fromLatin1(requestedName), QString::fromLatin1(activeName)));
        }
    });

    // ── 主题切换（下拉菜单） ──
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
        });
    }
    themeAction_ = new QAction(style()->standardIcon(QStyle::SP_FileDialogContentsView), "Theme", this);
    themeAction_->setToolTip("切换主题风格");
    themeAction_->setMenu(themeMenu_);

    // ── 连接 ──
    connect(clearAction, &QAction::triggered, this, [this]() {
        // 清除全部后处理状态
        deform_.clear();
        postEffect_.clear();
        contour_.clear();
        renderViewport_->clearSliceLines();
        renderViewport_->clearIsoSurface();
        renderViewport_->clearClipPlanePreview();
        renderViewport_->setOverlayVisible(false);
        renderViewport_->setUseVertexColor(false);
        renderViewport_->setColorBarVisible(false);
        feModelPanel_->clearActiveScalarField();
        resultPanel_->clearResults();
        deformationPanel_->clearResults();
        thresholdPanel_->clearResults();
        if (animController_) animController_->setFrameCount(0);
        loadedResultFrameCount_ = 0;

        feModelPanel_->clearModel();
        statusProgress_->setVisible(false);
        progressText_->setVisible(false);
        statusLabel_->setVisible(true);
        statusLabel_->setText("  就绪");
        statusLabel_->setStyleSheet(
            QString("color: %1; font-weight: bold;").arg(currentTheme_.green));
        updateProjectTreeSummary();
        updateStatusSummaries();
    });

    connect(pickGroup_, &QActionGroup::triggered, this, [this](QAction* action) {
        int mode = action->data().toInt();
        renderViewport_->setPickMode(static_cast<PickMode>(mode));
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
            action->setChecked(action->data().toInt() == static_cast<int>(requested));
        }
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

    fpsSummaryLabel_ = new QLabel("FPS --");
    sb->addPermanentWidget(fpsSummaryLabel_);

    frameTimeSummaryLabel_ = new QLabel("帧时间 -- ms");
    sb->addPermanentWidget(frameTimeSummaryLabel_);

    vertexSummaryLabel_ = new QLabel("顶点数 --");
    sb->addPermanentWidget(vertexSummaryLabel_);

    triangleSummaryLabel_ = new QLabel("三角面 --");
    sb->addPermanentWidget(triangleSummaryLabel_);

    auto* fpsTimer = new QTimer(this);
    fpsTimer->setInterval(500);
    connect(fpsTimer, &QTimer::timeout, this, &MainWindow::updateStatusSummaries);
    fpsTimer->start();
    updateStatusSummaries();
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
    modelInfoDock_->setStyleSheet(dockStyle);

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
        "  background: %2; }"
        "QToolButton:pressed {"
        "  background: %4; }"
        "QToolButton:checked {"
        "  background: %4; color: %5;"
        "  border: 1px solid %5; border-radius: 2px; }"
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
    if (fpsSummaryLabel_) fpsSummaryLabel_->setStyleSheet(summaryStyle);
    if (frameTimeSummaryLabel_) frameTimeSummaryLabel_->setStyleSheet(summaryStyle);
    if (vertexSummaryLabel_) vertexSummaryLabel_->setStyleSheet(summaryStyle);
    if (triangleSummaryLabel_) triangleSummaryLabel_->setStyleSheet(summaryStyle);
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

    if (inspectorPanel_) {
        inspectorPanel_->setStyleSheet(QString(
            "QWidget { background: %1; color: %2; }"
        ).arg(t.base, t.text));
    }

    if (displayControlPanel_) {
        displayControlPanel_->setStyleSheet(QString(
            "QGroupBox {"
            "  background: %1; border: 1px solid %2;"
            "  border-radius: 8px; margin: 10px 8px 8px 8px;"
            "  padding: 14px 10px 10px 10px;"
            "  font-weight: bold; font-size: 12px; color: %3; }"
            "QGroupBox::title {"
            "  subcontrol-origin: margin; left: 12px; padding: 0 6px;"
            "  color: %4; }"
            "QPushButton {"
            "  background: %2; color: %5; border: 1px solid %6;"
            "  border-radius: 5px; padding: 5px 10px; font-size: 12px; }"
            "QPushButton:hover { background: %6; border-color: %4; }"
            "QPushButton:pressed { background: %7; }"
        ).arg(t.mantle, t.surface0, t.subtext0, t.green, t.text, t.surface1, t.surface2));
    }

    // 各面板
    feModelPanel_->applyTheme(t);
    partsPanel_->applyTheme(t);
    resultPanel_->applyTheme(t);
    deformationPanel_->applyTheme(t);
    thresholdPanel_->applyTheme(t);
    renderViewport_->applyTheme(t);

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
    if (vertCount == 0) return;

    const int numBands = 9;

    FEMappedScalars mapped = FEResultMapper::mapScalarToVertices(field, rd, model);

    renderViewport_->setVertexScalars(mapped.scalars, mapped.minValue, mapped.maxValue, numBands);
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
