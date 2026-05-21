/**
 * @file ColorBarOverlay.h
 * @brief 色标覆盖层控件
 *
 * 独立于 GL 的色标覆盖层，作为 GLWidget 的子控件，
 * 使用 Qt raster 绘图引擎绘制，不涉及 OpenGL 状态。
 */

#pragma once

#include <QWidget>
#include <QColor>
#include <QString>

class ColorBarOverlay : public QWidget {
public:
    explicit ColorBarOverlay(QWidget* parent = nullptr);

    void setRange(float mn, float mx);
    void setTitle(const QString& t);
    void setTextColor(const QColor& c);
    void setExtremes(int minId, float minVal, int maxId, float maxVal);
    void setIdLabel(const QString& label);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    float min_ = 0.0f, max_ = 1.0f;
    QString title_ = "Result";
    QColor textColor_{30, 30, 30};
    bool hasExtremes_ = false;
    int minId_ = -1, maxId_ = -1;
    float minVal_ = 0.0f, maxVal_ = 0.0f;
    QString idLabel_ = "ID";
};
