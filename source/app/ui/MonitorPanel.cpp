/**
 * @file MonitorPanel.cpp
 * @brief 实时监控面板实现
 */

#include "MonitorPanel.h"
#include "RenderViewport.h"

#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

#include "Theme.h"

namespace {

QString backendName(RenderBackendKind kind)
{
    switch (kind) {
    case RenderBackendKind::OpenGL:
        return QStringLiteral("OpenGL");
    case RenderBackendKind::Vulkan:
        return QStringLiteral("Vulkan");
    case RenderBackendKind::Metal:
        return QStringLiteral("Metal");
    }
    return QStringLiteral("--");
}

} // namespace

MonitorPanel::MonitorPanel(QWidget* parent) : QGroupBox("监控", parent) {
    // 默认主题在 MainWindow 中统一调用 applyTheme() 设置

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(4);

    // 创建各行信息标签（初始显示 "标签: --"）
    fpsLabel_       = makeRow(layout, "FPS");
    frameTimeLabel_ = makeRow(layout, "帧时间");
    backendLabel_   = makeRow(layout, "后端");
    viewportLabel_  = makeRow(layout, "视口");
    vertexLabel_    = makeRow(layout, "顶点数");
    triangleLabel_  = makeRow(layout, "三角面");
    rendererLabel_  = makeRow(layout, "GPU");
    vendorLabel_    = makeRow(layout, "厂商");
    apiVersionLabel_ = makeRow(layout, "API");
    shaderLabel_     = makeRow(layout, "Shader");
}

void MonitorPanel::bindToViewport(RenderViewport* viewport) {
    viewport_ = viewport;

    // GL 初始化完成后，一次性读取硬件信息（这些信息不会变化）
    connect(viewport_, &RenderViewport::renderInitialized, this, [this]{
        rendererLabel_->setText("GPU: "     + viewport_->glRenderer());
        vendorLabel_->setText("厂商: "      + viewport_->gpuVendor());
        apiVersionLabel_->setText("API: "      + viewport_->glVersion());
        shaderLabel_->setText("Shader: "       + viewport_->glslVersion());
    });

    // 启动定时器，每 200ms 刷新一次动态数据（FPS、网格统计）
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MonitorPanel::refresh);
    timer->start(200);
}

void MonitorPanel::refresh() {
    if (!viewport_) return;

    // 更新动态性能数据
    fpsLabel_->setText(QString("FPS: %1").arg(viewport_->currentFps(), 0, 'f', 1));
    frameTimeLabel_->setText(QString("帧时间: %1 ms").arg(viewport_->frameTimeMs(), 0, 'f', 2));
    backendLabel_->setText(QString("后端: %1").arg(backendName(viewport_->activeRenderBackendKind())));
    viewportLabel_->setText(QString("视口: %1").arg(viewport_->renderDiagnostics()));
    vertexLabel_->setText(QString("顶点数: %1").arg(viewport_->vertexCount()));
    triangleLabel_->setText(QString("三角面: %1").arg(viewport_->triangleCount()));
}

QLabel* MonitorPanel::makeRow(QVBoxLayout* layout, const QString& label) {
    // 创建等宽字体的信息标签
    auto* lbl = new QLabel(label + ": --");
    lbl->setWordWrap(true);  // 允许换行（GPU 型号可能较长）
    lbl->setStyleSheet("font-size: 11px; font-family: monospace;");
    layout->addWidget(lbl);
    return lbl;
}

void MonitorPanel::applyTheme(const Theme& t) {
    setStyleSheet(QString(
        "QGroupBox {"
        "  background: %1; border: 1px solid %2;"
        "  border-radius: 8px; margin-top: 14px; padding: 14px 10px 10px 10px;"
        "  font-weight: bold; font-size: 12px; color: %3; }"
        "QGroupBox::title {"
        "  subcontrol-origin: margin; left: 12px; padding: 0 6px;"
        "  color: %4; }"
        "QLabel { color: %5; font-size: 11px; font-family: monospace; padding: 1px 0; }"
    ).arg(t.mantle, t.surface0, t.subtext0, t.teal, t.overlay2));
}
