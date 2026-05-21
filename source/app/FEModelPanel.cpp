/**
 * @file FEModelPanel.cpp
 * @brief 有限元模型显示面板实现
 *
 * 包含测试模型生成算法和面板 UI 逻辑。
 */

#include "FEModelPanel.h"
#include "FEParser.h"
#include "FEMeshConverter.h"
#include "FEProbe.h"
#include "Theme.h"

#include <QVBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QSettings>
#include <QDir>
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QEventLoop>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QStorageInfo>
#include <QUrl>
#include <functional>
#include <atomic>

#define SKIP_EMPTY Qt::SkipEmptyParts

namespace {

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

// ════════════════════════════════════════════════════════════
// 构造函数
// ════════════════════════════════════════════════════════════

FEModelPanel::FEModelPanel(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(140);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(6);

    layout->addWidget(createInfoGroup());
    layout->addWidget(createSelectionGroup());
    layout->addWidget(createSearchGroup());
    layout->addStretch();

    // 默认主题在 MainWindow 中统一调用 applyTheme() 设置
}

void FEModelPanel::applyTheme(const Theme& t) {
    setStyleSheet(QString(
        "QWidget { background: %1; color: %2; }"
        "QGroupBox {"
        "  background: %3; border: 1px solid %4;"
        "  border-radius: 8px; margin-top: 14px; padding: 14px 10px 10px 10px;"
        "  font-weight: bold; font-size: 12px; color: %5; }"
        "QGroupBox::title {"
        "  subcontrol-origin: margin; left: 12px; padding: 0 6px;"
        "  color: %6; }"
        "QLabel { font-size: 12px; color: %7; padding: 1px 0; }"
        "QLabel#searchHint { font-size: 10px; color: %8; padding: 2px 0; }"
        "QLineEdit {"
        "  background: %4; border: 1px solid %9; border-radius: 5px;"
        "  padding: 5px 10px; font-size: 12px; color: %2;"
        "  min-height: 24px; }"
        "QLineEdit:focus { border-color: %6; }"
        "QComboBox {"
        "  background: %4; border: 1px solid %9; border-radius: 5px;"
        "  padding: 5px 10px; font-size: 12px; color: %2;"
        "  min-height: 24px; }"
        "QComboBox:hover { border-color: %6; }"
        "QComboBox::drop-down { border: none; width: 24px; }"
        "QComboBox::down-arrow {"
        "  image: none; border-left: 5px solid transparent;"
        "  border-right: 5px solid transparent;"
        "  border-top: 6px solid %6; margin-right: 8px; }"
        "QComboBox QAbstractItemView {"
        "  background: %3; border: 1px solid %9; border-radius: 4px;"
        "  padding: 4px; selection-background-color: %9; color: %2;"
        "  outline: none; }"
        "QCheckBox { font-size: 12px; color: %7; spacing: 8px; padding: 2px 0; }"
        "QCheckBox::indicator {"
        "  width: 16px; height: 16px; border-radius: 4px;"
        "  border: 2px solid %9; background: %4; }"
        "QCheckBox::indicator:checked {"
        "  background: %6; border-color: %6; }"
        "QCheckBox::indicator:hover { border-color: %6; }"
    ).arg(t.base, t.text, t.mantle, t.surface0, t.subtext0, t.green, t.subtext1, t.overlay0, t.surface1));

    if (searchBtn_) {
        searchBtn_->setStyleSheet(QString(
            "QPushButton {"
            "  background: %1; color: %2; border: none;"
            "  border-radius: 5px; padding: 6px 10px; font-size: 12px; font-weight: bold;"
            "  min-height: 24px; }"
            "QPushButton:hover { background: %3; }"
            "QPushButton:pressed { background: %4; }"
        ).arg(t.blue, t.btnText, t.blueHover, t.bluePressed));
    }
}

// ════════════════════════════════════════════════════════════
// UI 分组创建
// ════════════════════════════════════════════════════════════

/**
 * @brief 创建模型信息分组
 *
 * 显示当前模型的统计信息。
 * 标签内容在 updateInfoLabels() 中动态更新。
 */
QGroupBox* FEModelPanel::createInfoGroup() {
    auto* group = new QGroupBox("模型信息");
    auto* layout = new QVBoxLayout(group);

    // 使用 lambda 简化标签创建
    auto makeLabel = [layout](const QString& text) -> QLabel* {
        auto* label = new QLabel(text);
        layout->addWidget(label);
        return label;
    };

    nodeCountLabel_     = makeLabel("节点数: 0");
    elementCountLabel_  = makeLabel("单元数: 0");
    triangleCountLabel_ = makeLabel("三角面数: 0");
    modelSizeLabel_     = makeLabel("尺寸: -");

    return group;
}

// ════════════════════════════════════════════════════════════
// 模型文件加载
// ════════════════════════════════════════════════════════════

void FEModelPanel::loadModelFromFile() {
    QSettings settings("FEModelViewer", "FEModelViewer");
    QString lastDir = settings.value("lastOpenDir", QString()).toString();
    if (lastDir.isEmpty() || !QDir(lastDir).exists()) {
        lastDir = QDir::homePath() + "/Desktop";
        if (!QDir(lastDir).exists()) lastDir = QDir::homePath();
    }

    QFileDialog dialog(this, "打开有限元模型", lastDir,
                       "所有支持格式 (*.inp *.bdf *.fem *.op2 *.step *.stp *.iges *.igs *.stl);;"
                       "ABAQUS Input (*.inp);;"
                       "Nastran BDF (*.bdf *.fem);;"
                       "Nastran OP2 (*.op2);;"
                       "CAD Exchange (*.step *.stp *.iges *.igs);;"
                       "STL Geometry (*.stl);;"
                       "所有文件 (*)");
    dialog.setFileMode(QFileDialog::ExistingFile);
    configureFileDialog(dialog);
    if (dialog.exec() != QDialog::Accepted) return;
    QString path = dialog.selectedFiles().first();
    if (path.isEmpty()) return;

    settings.setValue("lastOpenDir", QFileInfo(path).absolutePath());
    loadModelFromPath(path);
}

void FEModelPanel::loadModelFromPath(const QString& path) {
    if (path.isEmpty()) return;

    currentModel_.clear();
    currentRenderData_.clear();

    emit loadProgress(0, "正在解析节点数据...");

    // ── 后台线程做所有重计算，原子变量传进度 ──
    std::atomic<int>  targetVal{0};
    std::atomic<int>  phase{0};
    std::atomic<int>  elemCount{0};
    bool workerOk = false;
    FEModel           resultModel;
    FERenderData      resultRender;

    QThread* worker = QThread::create([&]() {
        phase.store(0);
        bool ok = false;
        if (path.endsWith(".inp", Qt::CaseInsensitive)) {
            ok = FEParser::parseAbaqusInp(path, resultModel, [&](int pct) {
                targetVal.store(pct * 5);
            });
        } else if (path.endsWith(".bdf", Qt::CaseInsensitive) ||
                   path.endsWith(".fem", Qt::CaseInsensitive)) {
            ok = FEParser::parseNastranBdf(path, resultModel, [&](int pct) {
                targetVal.store(pct * 5);
            });
        } else if (path.endsWith(".op2", Qt::CaseInsensitive)) {
            ok = FEParser::parseNastranOp2(path, resultModel, [&](int pct) {
                targetVal.store(pct * 5);
            });
        } else if (path.endsWith(".stl", Qt::CaseInsensitive)) {
            ok = FEParser::parseStlGeometry(path, resultModel, [&](int pct) {
                targetVal.store(pct * 5);
            });
        } else if (path.endsWith(".step", Qt::CaseInsensitive) ||
                   path.endsWith(".stp", Qt::CaseInsensitive) ||
                   path.endsWith(".iges", Qt::CaseInsensitive) ||
                   path.endsWith(".igs", Qt::CaseInsensitive)) {
            ok = FEParser::parseCadGeometry(path, resultModel, [&](int pct) {
                targetVal.store(pct * 5);
            });
        }
        workerOk = ok;
        if (!ok || resultModel.isEmpty()) {
            phase.store(2);
            return;
        }
        resultModel.name = QFileInfo(path).baseName().toStdString();
        resultModel.filePath = path.toStdString();
        elemCount.store(resultModel.elementCount());
        targetVal.store(500);

        phase.store(1);
        resultRender = FEMeshConverter::toRenderData(resultModel, [&](int pct) {
            targetVal.store(500 + pct * 450 / 100);
        });
        targetVal.store(950);
        phase.store(2);
    });

    worker->start();
    int displayed = 0;
    while (!worker->isFinished()) {
        int target = targetVal.load();
        if (displayed < target) {
            int diff = target - displayed;
            int step = qMax(1, diff / 4);
            displayed = qMin(displayed + step, target);
        }

        int pct = displayed / 10;  // 0-100
        int p = phase.load();
        if (p == 0) {
            int filePct = targetVal.load() / 5;
            emit loadProgress(pct, filePct < 50 ? "正在解析节点数据..." : "正在解析单元数据...");
        } else if (p == 1) {
            emit loadProgress(pct, QString("正在生成渲染数据（%1 个单元）...").arg(elemCount.load()));
        } else {
            emit loadProgress(pct, "正在更新显示...");
        }

        QApplication::processEvents(QEventLoop::AllEvents);
        worker->wait(16);
    }
    worker->wait();
    delete worker;

    if (!workerOk || resultModel.isEmpty()) {
        emit loadProgress(0, "");
        emit loadFinished(false,
            QString("加载失败：节点 %1，单元 %2")
            .arg(resultModel.nodeCount())
            .arg(resultModel.elementCount()));
        return;
    }

    currentModel_ = resultModel;
    currentRenderData_ = resultRender;

    // 收尾
    emit loadProgress(100, "正在更新显示...");
    QApplication::processEvents();

    updateInfoLabels();
    emit meshGenerated(currentRenderData_.mesh, currentModel_.computeCenter(),
                       currentModel_.computeSize(), currentRenderData_.triangleToElement,
                       currentRenderData_.vertexToNode);
    emit partsChanged(QString::fromStdString(currentModel_.name), currentModel_.parts,
                      currentRenderData_.triangleToPart, currentRenderData_.edgeToPart);

    emit loadProgress(0, "");
    emit loadFinished(true,
        QString("节点: %1  |  单元: %2  |  三角面: %3")
        .arg(currentModel_.nodeCount())
        .arg(currentModel_.elementCount())
        .arg(currentRenderData_.triangleCount()));
}


// ════════════════════════════════════════════════════════════
// 解析委托（实现已移至 FEParser）
// ════════════════════════════════════════════════════════════

bool FEModelPanel::parseNastranOp2Results(const QString& filePath, FEResultData& results) {
    return FEParser::parseNastranOp2Results(filePath, results);
}

bool FEModelPanel::parseUnvResults(const QString& filePath, FEResultData& results) {
    return FEParser::parseUnvResults(filePath, results);
}

// ════════════════════════════════════════════════════════════
// 清空模型
// ════════════════════════════════════════════════════════════

void FEModelPanel::clearModel() {
    currentModel_.clear();
    currentRenderData_.clear();
    updateInfoLabels();
    emit meshGenerated(Mesh{}, glm::vec3(0), 0, {}, {});
    emit partsChanged(QString(), {}, {}, {});
}


// ════════════════════════════════════════════════════════════
// 信息更新
// ════════════════════════════════════════════════════════════

/**
 * @brief 更新模型信息标签
 *
 * 从 currentModel_ 和 currentRenderData_ 中提取统计数据，
 * 显示在信息面板的各个标签上。
 */
void FEModelPanel::updateInfoLabels() {
    nodeCountLabel_->setText(
        QString("节点数: %1").arg(currentModel_.nodeCount()));
    elementCountLabel_->setText(
        QString("单元数: %1").arg(currentModel_.elementCount()));
    triangleCountLabel_->setText(
        QString("三角面数: %1").arg(currentRenderData_.triangleCount()));

    if (currentModel_.isEmpty()) {
        modelSizeLabel_->setText("尺寸: -");
    } else {
        float size = currentModel_.computeSize();
        modelSizeLabel_->setText(
            QString("尺寸: %1").arg(size, 0, 'f', 2));
    }
}

// ════════════════════════════════════════════════════════════
// 选中信息
// ════════════════════════════════════════════════════════════

QGroupBox* FEModelPanel::createSelectionGroup() {
    auto* group = new QGroupBox("选中信息");
    auto* layout = new QVBoxLayout(group);

    selModeLabel_  = new QLabel("模式: -");
    selCountLabel_ = new QLabel("数量: 0");
    selIdsLabel_   = new QLabel("ID: -");
    selIdsLabel_->setWordWrap(true);

    labelCheck_ = new QCheckBox("显示ID标签");
    labelCheck_->setChecked(false);
    connect(labelCheck_, &QCheckBox::toggled, this, &FEModelPanel::labelVisibilityChanged);

    probeValueLabel_ = new QLabel("结果值: -");
    probeValueLabel_->setWordWrap(true);

    layout->addWidget(selModeLabel_);
    layout->addWidget(selCountLabel_);
    layout->addWidget(selIdsLabel_);
    layout->addWidget(probeValueLabel_);
    layout->addWidget(labelCheck_);

    return group;
}

QGroupBox* FEModelPanel::createSearchGroup() {
    auto* group = new QGroupBox("ID 搜索");
    auto* layout = new QVBoxLayout(group);

    // 类型选择行
    auto* typeRow = new QHBoxLayout;
    auto* typeLabel = new QLabel("类型:");
    typeLabel->setFixedWidth(36);
    typeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    searchTypeCombo_ = new QComboBox;
    searchTypeCombo_->addItem("节点", static_cast<int>(PickMode::Node));
    searchTypeCombo_->addItem("单元", static_cast<int>(PickMode::Element));
    searchTypeCombo_->addItem("部件", static_cast<int>(PickMode::Part));
    searchTypeCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    typeRow->addWidget(typeLabel);
    typeRow->addWidget(searchTypeCombo_);
    layout->addLayout(typeRow);

    // 输入行
    auto* inputRow = new QHBoxLayout;
    searchInput_ = new QLineEdit;
    searchInput_->setPlaceholderText("如 1,3,5-10");
    // 只允许输入数字、逗号和短横线
    searchInput_->setValidator(new QRegularExpressionValidator(
        QRegularExpression("[0-9,\\-]+"), searchInput_));
    searchInput_->setToolTip("支持格式：\n"
                             "  单个 ID：42\n"
                             "  多个 ID：1,3,5\n"
                             "  范围：5-10\n"
                             "  混合：1,3,5-10,20");
    inputRow->addWidget(searchInput_);

    searchBtn_ = new QPushButton("定位");
    searchBtn_->setFixedWidth(42);
    inputRow->addWidget(searchBtn_);
    layout->addLayout(inputRow);

    // 格式提示
    auto* hintLabel = new QLabel("支持: 单个 42 | 多个 1,3,5 | 范围 5-10");
    hintLabel->setObjectName("searchHint");
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    connect(searchBtn_, &QPushButton::clicked, this, &FEModelPanel::onSearchTriggered);
    connect(searchInput_, &QLineEdit::returnPressed, this, &FEModelPanel::onSearchTriggered);

    return group;
}

void FEModelPanel::onSearchTriggered() {
    QString text = searchInput_->text().trimmed();
    if (text.isEmpty()) return;

    // 解析 ID：支持逗号分隔和范围（如 1,3,5-10）
    std::vector<int> ids;
    QStringList parts = text.split(',', SKIP_EMPTY);
    for (const QString& part : parts) {
        QString p = part.trimmed();
        if (p.contains('-')) {
            QStringList range = p.split('-');
            if (range.size() == 2) {
                bool ok1, ok2;
                int from = range[0].trimmed().toInt(&ok1);
                int to = range[1].trimmed().toInt(&ok2);
                if (ok1 && ok2) {
                    for (int i = qMin(from, to); i <= qMax(from, to); ++i)
                        ids.push_back(i);
                }
            }
        } else {
            bool ok;
            int id = p.toInt(&ok);
            if (ok) ids.push_back(id);
        }
    }

    if (ids.empty()) return;

    PickMode mode = static_cast<PickMode>(searchTypeCombo_->currentData().toInt());
    emit searchRequested(mode, ids);
}

void FEModelPanel::updateSelectionInfo(PickMode mode, int count, const std::vector<int>& ids) {
    QString modeName;
    switch (mode) {
        case PickMode::Node:    modeName = "节点"; break;
        case PickMode::Element: modeName = "单元"; break;
        case PickMode::Part:    modeName = "部件"; break;
    }
    selModeLabel_->setText(QString("模式: %1").arg(modeName));
    selCountLabel_->setText(QString("数量: %1").arg(count));

    if (ids.empty()) {
        selIdsLabel_->setText("ID: -");
        probeValueLabel_->setText("结果值: -");
        return;
    }

    QStringList idStrs;
    int limit = qMin(static_cast<int>(ids.size()), 20);
    for (int i = 0; i < limit; ++i)
        idStrs.append(QString::number(ids[i]));
    QString text = "ID: " + idStrs.join(", ");
    if (static_cast<int>(ids.size()) > 20)
        text += QString(" ... (+%1)").arg(ids.size() - 20);
    selIdsLabel_->setText(text);

    if (!hasActiveField_ || mode == PickMode::Part) {
        probeValueLabel_->setText("结果值: -");
        return;
    }

    // 收集所有选中实体的探针值
    std::vector<float> probeValues;
    bool locationMatch =
        (mode == PickMode::Node    && activeField_.location == FieldLocation::Node) ||
        (mode == PickMode::Element && activeField_.location == FieldLocation::Element);

    if (locationMatch) {
        for (int id : ids) {
            auto pv = FEProbe::valueAtEntity(activeField_, id);
            if (pv.valid) probeValues.push_back(pv.value);
        }
    } else if (mode == PickMode::Element && activeField_.location == FieldLocation::Node) {
        // 拾取单元，场在节点上 → 取该单元所有节点值的平均
        for (int eid : ids) {
            auto eit = currentModel_.elements.find(eid);
            if (eit == currentModel_.elements.end()) continue;
            float sum = 0.0f;
            int n = 0;
            for (int nid : eit->second.nodeIds) {
                auto pv = FEProbe::valueAtEntity(activeField_, nid);
                if (pv.valid) { sum += pv.value; ++n; }
            }
            if (n > 0) probeValues.push_back(sum / n);
        }
    } else if (mode == PickMode::Node && activeField_.location == FieldLocation::Element) {
        // 拾取节点，场在单元上 → 查找包含该节点的单元，取平均
        for (int nid : ids) {
            float sum = 0.0f;
            int n = 0;
            for (const auto& [eid, elem] : currentModel_.elements) {
                bool contains = false;
                for (int enid : elem.nodeIds) {
                    if (enid == nid) { contains = true; break; }
                }
                if (!contains) continue;
                auto pv = FEProbe::valueAtEntity(activeField_, eid);
                if (pv.valid) { sum += pv.value; ++n; }
            }
            if (n > 0) probeValues.push_back(sum / n);
        }
    }

    if (probeValues.empty()) {
        probeValueLabel_->setText("结果值: (无数据)");
    } else if (probeValues.size() == 1) {
        QString valText = QString("结果值: %1").arg(probeValues[0], 0, 'g', 6);
        if (!activeField_.unit.empty())
            valText += QString(" %1").arg(QString::fromStdString(activeField_.unit));
        probeValueLabel_->setText(valText);
    } else {
        float minV = probeValues[0], maxV = probeValues[0], sum = 0.0f;
        for (float v : probeValues) {
            if (v < minV) minV = v;
            if (v > maxV) maxV = v;
            sum += v;
        }
        float avg = sum / static_cast<float>(probeValues.size());
        QString unitStr;
        if (!activeField_.unit.empty())
            unitStr = " " + QString::fromStdString(activeField_.unit);
        probeValueLabel_->setText(
            QString("最小: %1 | 最大: %2 | 均值: %3%4")
                .arg(minV, 0, 'g', 5).arg(maxV, 0, 'g', 5)
                .arg(avg, 0, 'g', 5).arg(unitStr));
    }
}

void FEModelPanel::setActiveScalarField(const FEScalarField& field) {
    activeField_ = field;
    hasActiveField_ = true;
}

void FEModelPanel::clearActiveScalarField() {
    activeField_ = FEScalarField{};
    hasActiveField_ = false;
    probeValueLabel_->setText("结果值: -");
}
