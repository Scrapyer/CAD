/**
 * @file main.cpp
 * @brief 程序入口
 *
 * 职责：
 *   1. 配置 OpenGL 上下文格式（版本、Profile、深度缓冲、多重采样）
 *   2. 创建 QApplication 和主窗口
 *   3. 进入 Qt 事件循环
 *
 * CLI 模式：
 *   --parse <file.op2>   仅解析 OP2 文件并打印结果，不启动 GUI
 */

#include <QApplication>
#include <QCoreApplication>
#include <QSurfaceFormat>
#include <QFileInfo>
#include <QDebug>
#include <cstring>

#include "MainWindow.h"
#include "FEParser.h"
#include "FEResultData.h"

/**
 * @brief CLI 模式：解析 OP2 文件并输出统计信息
 * @return 0 成功，1 失败
 */
static int runParseMode(const QString& filePath) {
    QFileInfo fi(filePath);
    if (!fi.exists()) {
        qWarning("文件不存在: %s", qPrintable(filePath));
        fprintf(stderr, "file does not exist: %s\n", qPrintable(filePath));
        return 1;
    }

    qDebug("file: %s (%lld bytes)", qPrintable(fi.fileName()), fi.size());

    // 解析几何
    FEModel model;
    bool geomOk = FEParser::parseNastranOp2(filePath, model);

    qDebug("nodes=%d elements=%d parts=%d",
           (int)model.nodes.size(), (int)model.elements.size(), (int)model.parts.size());

    // 解析结果
    FEResultData results;
    bool resultOk = FEParser::parseNastranOp2Results(filePath, results);

    if (resultOk) {
        qDebug("%d subcases loaded", (int)results.subcases.size());
        for (const auto& sc : results.subcases) {
            qDebug("  subcase %d '%s': %d result types",
                   sc.id, sc.name.c_str(), (int)sc.resultTypes.size());
            for (const auto& rt : sc.resultTypes) {
                qDebug("    %s: %d components", rt.name.c_str(), (int)rt.components.size());
                for (const auto& comp : rt.components) {
                    qDebug("      %s: %d values", comp.name.c_str(), (int)comp.field.values.size());
                }
            }
        }
    }

    // 输出汇总行（供批量脚本解析）
    fprintf(stdout, "RESULT: geom=%s nodes=%d elems=%d parts=%d results=%s subcases=%d\n",
            geomOk ? "OK" : "FAIL",
            (int)model.nodes.size(), (int)model.elements.size(), (int)model.parts.size(),
            resultOk ? "OK" : "FAIL",
            (int)results.subcases.size());

    return (geomOk || resultOk) ? 0 : 1;
}

int main(int argc, char* argv[]) {
    QCoreApplication::setOrganizationName("FEModelViewer");
    QCoreApplication::setApplicationName("FEModelViewer");

    // ── 检查 CLI 模式 ──
    bool parseMode = false;
    QString parseFile;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--parse") == 0 && i + 1 < argc) {
            parseMode = true;
            parseFile = argv[i + 1];
            break;
        }
    }

    if (parseMode) {
        // CLI 模式：只使用解析层，不初始化 GUI 平台插件
        QCoreApplication app(argc, argv);
        return runParseMode(parseFile);
    }

    // ── 配置 OpenGL 上下文 ──
    QSurfaceFormat fmt;
    fmt.setVersion(4, 1);                      // OpenGL 4.1（macOS 支持的最高核心版本）
    fmt.setProfile(QSurfaceFormat::CoreProfile); // Core Profile（不包含已废弃的固定管线功能）
    fmt.setDepthBufferSize(32);                // 32 位深度缓冲（提高薄壳模型深度精度）
    fmt.setSamples(8);                         // 8x MSAA 多重采样抗锯齿
    QSurfaceFormat::setDefaultFormat(fmt);     // 设为全局默认格式

    // ── 启动应用 ──
    QApplication app(argc, argv);

    MainWindow window;
    window.show();

    // 进入 Qt 事件循环（阻塞直到窗口关闭）
    return app.exec();
}
