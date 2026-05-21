/**
 * @file MonitorPanel.h
 * @brief 实时监控面板声明
 *
 * 显示实时性能数据和硬件信息：
 *   - FPS 帧率、每帧耗时
 *   - 当前网格的顶点数、三角面数
 *   - GPU 型号、厂商、图形 API 版本、Shader 版本
 *
 * 通过 bindToViewport() 绑定到 RenderViewport 后，使用 QTimer 定时刷新数据。
 */

#pragma once

#include <QGroupBox>

class QLabel;
class QVBoxLayout;
class RenderViewport;
struct Theme;

class MonitorPanel : public QGroupBox {
    Q_OBJECT

public:
    explicit MonitorPanel(QWidget* parent = nullptr);

    /**
     * @brief 绑定到 RenderViewport 并启动定时刷新
     * @param viewport 要监控的渲染视口
     *
     * 连接 RenderViewport::renderInitialized 信号以获取硬件信息，
     * 并启动 200ms 间隔的定时器刷新 FPS 等动态数据。
     */
    void bindToViewport(RenderViewport* viewport);

    /** @brief 应用主题 */
    void applyTheme(const Theme& theme);

private slots:
    /** @brief 定时刷新性能统计数据 */
    void refresh();

private:
    /**
     * @brief 创建一行信息标签
     * @param layout 父布局
     * @param label 标签前缀文字（如 "FPS"）
     * @return 创建的 QLabel 指针，后续用于更新显示文本
     */
    static QLabel* makeRow(QVBoxLayout* layout, const QString& label);

    RenderViewport* viewport_ = nullptr;  // 绑定的渲染视口（用于查询数据）

    // ── 各项数据的显示标签 ──
    QLabel* fpsLabel_ = nullptr;         // FPS 帧率
    QLabel* frameTimeLabel_ = nullptr;   // 每帧耗时
    QLabel* vertexLabel_ = nullptr;      // 顶点数
    QLabel* triangleLabel_ = nullptr;    // 三角面数
    QLabel* rendererLabel_ = nullptr;    // GPU 型号
    QLabel* vendorLabel_ = nullptr;      // GPU 厂商
    QLabel* apiVersionLabel_ = nullptr;  // 图形 API 版本
    QLabel* shaderLabel_ = nullptr;      // Shader 版本
};
