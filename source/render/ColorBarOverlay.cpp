/**
 * @file ColorBarOverlay.cpp
 * @brief 色标覆盖层控件实现
 */

#include "ColorBarOverlay.h"

#include <QPainter>
#include <QFontMetrics>
#include <algorithm>

ColorBarOverlay::ColorBarOverlay(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    setVisible(false);
}

void ColorBarOverlay::setRange(float mn, float mx) { min_ = mn; max_ = mx; update(); }
void ColorBarOverlay::setTitle(const QString& t) { title_ = t; update(); }
void ColorBarOverlay::setTextColor(const QColor& c) { textColor_ = c; update(); }

void ColorBarOverlay::setExtremes(int minId, float minVal, int maxId, float maxVal) {
    minId_ = minId; minVal_ = minVal;
    maxId_ = maxId; maxVal_ = maxVal;
    hasExtremes_ = true;
    update();
}

void ColorBarOverlay::setIdLabel(const QString& label) { idLabel_ = label; update(); }

void ColorBarOverlay::paintEvent(QPaintEvent*) {
    const int segCount = 9;
    const int barW = 20;
    const int segH = 28;
    const int barH = segCount * segH;
    const int margin = 14;
    const int barLabelGap = 8;

    auto jetColor = [](float t) -> QColor {
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        float r = 0, g = 0, b = 0;
        if (t < 0.125f)      { r = 0;   g = 0;   b = 0.5f + t / 0.125f * 0.5f; }
        else if (t < 0.375f) { r = 0;   g = (t - 0.125f) / 0.25f; b = 1.0f; }
        else if (t < 0.625f) { r = (t - 0.375f) / 0.25f; g = 1.0f; b = 1.0f - (t - 0.375f) / 0.25f; }
        else if (t < 0.875f) { r = 1.0f; g = 1.0f - (t - 0.625f) / 0.25f; b = 0; }
        else                 { r = 1.0f - (t - 0.875f) / 0.125f * 0.5f; g = 0; b = 0; }
        return QColor(static_cast<int>(r * 255), static_cast<int>(g * 255), static_cast<int>(b * 255));
    };

    auto formatValue = [](float val) -> QString {
        return QString::number(static_cast<double>(val), 'E', 3);
    };

    QFont labelFont("Consolas", 0);
    labelFont.setPixelSize(14);
    QFontMetrics fm(labelFont);
    int labelTextH = fm.height();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setFont(labelFont);

    // 绘制分段色块
    for (int i = 0; i < segCount; ++i) {
        float t = (i + 0.5f) / segCount;
        int y = margin + barH - (i + 1) * segH;
        painter.setPen(Qt::NoPen);
        painter.setBrush(jetColor(t));
        painter.drawRect(margin, y, barW, segH);
    }

    // 段界横线 + 数值标签
    int labelX = margin + barW + barLabelGap;
    for (int i = 0; i <= segCount; ++i) {
        float t = 1.0f - i / static_cast<float>(segCount);
        float val = min_ + (max_ - min_) * t;
        int y = margin + i * segH;

        painter.setPen(QPen(QColor(0, 0, 0), 1));
        painter.drawLine(margin, y, margin + barW, y);

        painter.setPen(textColor_);
        QRectF labelRect(labelX, y - labelTextH / 2, 120, labelTextH);
        painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, formatValue(val));
    }

    // 色标下方显示最大/最小值及其 ID
    if (hasExtremes_) {
        int infoY = margin + barH + 10;
        QFont infoFont("Consolas", 0);
        infoFont.setPixelSize(12);
        painter.setFont(infoFont);
        painter.setPen(textColor_);

        QString maxLine = QString("Max: %1 (%2: %3)").arg(formatValue(maxVal_)).arg(idLabel_).arg(maxId_);
        QString minLine = QString("Min: %1 (%2: %3)").arg(formatValue(minVal_)).arg(idLabel_).arg(minId_);

        // 按实际文本宽度计算 rect，避免 5 位以上节点 ID 被截断
        QFontMetrics infoFm(infoFont);
        int textW = std::max(infoFm.horizontalAdvance(maxLine),
                              infoFm.horizontalAdvance(minLine));
        textW += 4;  // 留点余量
        painter.drawText(margin, infoY, textW, 16, Qt::AlignLeft | Qt::AlignVCenter, maxLine);
        painter.drawText(margin, infoY + 18, textW, 16, Qt::AlignLeft | Qt::AlignVCenter, minLine);
    }

    painter.end();
}
