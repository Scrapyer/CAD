/**
 * @file FEModelPanel.cpp
 * @brief 有限元模型显示面板实现
 *
 * 包含测试模型生成算法和面板 UI 逻辑。
 */

#include "FEModelPanel.h"
#include "FEParser.h"
#include "FEMeshConverter.h"

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
#include <QStorageInfo>
#include <QUrl>
#include <functional>
#include <atomic>

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
}

void FEModelPanel::applyTheme(const Theme&) {
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
    emit meshGenerated(Mesh{}, glm::vec3(0), 0, {}, {});
    emit partsChanged(QString(), {}, {}, {});
}
