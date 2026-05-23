#include "GLWidget.h"
#include "Theme.h"
#include "ColorBarOverlay.h"
#include "OpenGLRenderBackend.h"
#include "ScreenSpacePicking.h"
#include "ViewportGridMetrics.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

namespace {
constexpr int kAxesLabelSize = 30;
constexpr int kAxesMargin = 8;
constexpr int kAxesViewportSize = 152;
constexpr float kAxesClickPadding = 10.0f;
}

void GLWidget::render2DOverlays(const glm::mat4& mvp) {
    QPainter painter(this);
    painter.beginNativePainting();
    painter.endNativePainting();
    painter.setRenderHint(QPainter::Antialiasing);
    drawAxesLabels(painter);
    if (showLabels_ && selection_.hasSelection())
        drawIdLabels(painter, mvp);
    painter.end();
}


glm::mat4 GLWidget::axesIndicatorMvp() const
{
    glm::mat3 rot = glm::mat3(cam_.viewMatrix());
    glm::vec3 axesEye = glm::vec3(rot[0][2], rot[1][2], rot[2][2]) * 2.5f;
    glm::mat4 axesView = glm::lookAt(axesEye, glm::vec3(0), glm::vec3(0, 1, 0));
    glm::mat4 axesProj = glm::ortho(-1.3f, 1.3f, -1.3f, 1.3f, 0.01f, 10.0f);
    return axesProj * axesView;
}


void GLWidget::drawAxesIndicator() {
    if (!axesGeometry_) return;

    const int axesSize = kAxesViewportSize;
    const int margin = kAxesMargin;
    const int dpr = devicePixelRatio();

    // 仅旋转的 view 矩阵（固定距离，跟随相机朝向）
    glm::mat4 axesMVP = axesIndicatorMvp();

    // ── 左下角小视口绘制 ──
    auto* glBackend = openGLBackend();
    glBackend->setViewport(margin * dpr, margin * dpr, axesSize * dpr, axesSize * dpr);
    glBackend->clearDepthBuffer();

    axesShader_->bind();
    glBackend->setMvpUniform(*axesShader_,
                             QMatrix4x4(glm::value_ptr(glm::transpose(axesMVP))));

    // 绘制实心几何体（圆柱轴杆 + 圆锥箭头 + 球心）
    if (axesLineCount_ > 0) {
        ScenePassState linePassState;
        linePassState.applyLineWidth = true;
        linePassState.lineWidth = 2.5f;
        linePassState.restoredLineWidth = 1.0f;
        linePassState.applyDepthTest = true;
        linePassState.depthTestEnabled = true;
        linePassState.restoredDepthTestEnabled = true;
        glBackend->drawArraysPass(*axesGeometry_, ScenePrimitive::Lines, 0, axesLineCount_, linePassState);
    }
    ScenePassState triPassState;
    triPassState.applyDepthTest = true;
    triPassState.depthTestEnabled = true;
    triPassState.restoredDepthTestEnabled = true;
    glBackend->drawArraysPass(*axesGeometry_,
                              ScenePrimitive::Triangles,
                              axesLineCount_,
                              axesTriCount_,
                              triPassState);

    axesShader_->release();

    // 恢复主视口
    glBackend->setViewport(0, 0, width() * dpr, height() * dpr);

    // 保存投影参数，供 drawAxesLabels() 使用
    axesMVP_ = axesMVP;
}


void GLWidget::drawAxesLabels(QPainter& painter) {
    const int axesSize = kAxesViewportSize;
    const int margin = kAxesMargin;

    auto project = [&](glm::vec3 pt) -> QPointF {
        glm::vec4 clip = axesMVP_ * glm::vec4(pt, 1.0f);
        float sx = margin + (clip.x / clip.w * 0.5f + 0.5f) * axesSize;
        float sy = height() - margin - (clip.y / clip.w * 0.5f + 0.5f) * axesSize;
        return QPointF(sx, sy);
    };

    struct AxisLabel { glm::vec3 dir; QString name; QColor color; };
    AxisLabel labels[] = {
        {{1,0,0}, "X", QColor(240, 80, 80)},
        {{0,1,0}, "Y", QColor(90, 220, 90)},
        {{0,0,1}, "Z", QColor(90, 140, 255)},
    };

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(17);
    painter.setFont(font);

    for (const auto& l : labels) {
        QPointF pos = project(l.dir * 1.15f);
        painter.setPen(l.color);
        painter.drawText(QRectF(pos.x() - kAxesLabelSize / 2.0f,
                                pos.y() - kAxesLabelSize / 2.0f,
                                kAxesLabelSize,
                                kAxesLabelSize),
                         Qt::AlignCenter, l.name);
    }
}


bool GLWidget::standardViewFromAxesClick(const QPoint& pos, StandardView* view) const
{
    if (!view || width() <= kAxesMargin * 2 || height() <= kAxesMargin * 2) {
        return false;
    }

    const glm::mat4 axesMVP = axesIndicatorMvp();
    auto project = [&](glm::vec3 pt) -> QPointF {
        glm::vec4 clip = axesMVP * glm::vec4(pt, 1.0f);
        if (std::abs(clip.w) <= 1.0e-6f) {
            return QPointF(-10000.0, -10000.0);
        }
        const float sx = kAxesMargin + (clip.x / clip.w * 0.5f + 0.5f) * kAxesViewportSize;
        const float sy = height() - kAxesMargin - (clip.y / clip.w * 0.5f + 0.5f) * kAxesViewportSize;
        return QPointF(sx, sy);
    };

    const QPointF p(pos);
    const QPointF origin = project(glm::vec3(0.0f));
    const std::array<glm::vec3, 3> axisDirs = {
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f)
    };
    const std::array<StandardView, 3> axisViews = {
        StandardView::Right,
        StandardView::Top,
        StandardView::Front
    };

    int bestAxis = -1;
    float bestDist2 = 1.0e30f;
    const float lineThreshold2 = 13.0f * 13.0f;
    for (size_t i = 0; i < axisDirs.size(); ++i) {
        const QPointF end = project(axisDirs[i] * 1.15f);
        const QRectF labelRect(end.x() - kAxesLabelSize / 2.0f - kAxesClickPadding,
                               end.y() - kAxesLabelSize / 2.0f - kAxesClickPadding,
                               kAxesLabelSize + kAxesClickPadding * 2.0f,
                               kAxesLabelSize + kAxesClickPadding * 2.0f);
        if (labelRect.contains(p)) {
            *view = axisViews[i];
            return true;
        }

        const float dist2 = ScreenSpacePicking::distanceSquaredToSegment(p, origin, end);
        if (dist2 <= lineThreshold2 && dist2 < bestDist2) {
            bestAxis = static_cast<int>(i);
            bestDist2 = dist2;
        }
    }

    if (bestAxis >= 0) {
        *view = axisViews[static_cast<size_t>(bestAxis)];
        return true;
    }
    return false;
}


void GLWidget::drawIdLabels(QPainter& painter, const glm::mat4& mvp) {
    int w = width();
    int h = height();

    // 世界坐标 → 屏幕坐标
    auto project = [&](const glm::vec3& pos) -> QPointF {
        glm::vec4 clip = mvp * glm::vec4(pos, 1.0f);
        if (clip.w <= 0.0f) return QPointF(-1, -1);
        float nx = clip.x / clip.w;
        float ny = clip.y / clip.w;
        float sx = (nx * 0.5f + 0.5f) * w;
        float sy = (1.0f - (ny * 0.5f + 0.5f)) * h;
        return QPointF(sx, sy);
    };

    QFont font = painter.font();
    font.setPixelSize(11);
    font.setBold(true);
    painter.setFont(font);

    // 描边文字：深色轮廓 + 亮色正文（避免 drawRect 导致 GL 状态崩溃）
    QColor outlineColor(0, 0, 0, 220);
    QColor textColor(255, 200, 0);
    const int offsetY = -14;  // 标签偏移到实体上方

    // 绘制带描边的文字（4方向偏移描边 + 正文叠加）
    auto drawOutlinedText = [&](int x, int y, const QString& text) {
        painter.setPen(outlineColor);
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                if (dx != 0 || dy != 0)
                    painter.drawText(x + dx, y + dy, text);
        painter.setPen(textColor);
        painter.drawText(x, y, text);
    };

    QFontMetrics fm(font);

    if (pickMode_ == PickMode::Node) {
        // ── 节点标签 ──
        if (!selection_.selectedNodes.empty() && !vertexToNode_.empty()) {
            std::unordered_map<int, int> nodeToVert;
            for (int i = 0; i < static_cast<int>(vertexToNode_.size()); ++i) {
                int nid = vertexToNode_[i];
                if (nid >= 0 && nodeToVert.find(nid) == nodeToVert.end())
                    nodeToVert[nid] = i;
            }

            for (int nid : selection_.selectedNodes) {
                auto it = nodeToVert.find(nid);
                if (it == nodeToVert.end()) continue;
                int vi = it->second;
                if (vi * 6 + 2 >= static_cast<int>(mesh_.vertices.size())) continue;

                glm::vec3 pos(mesh_.vertices[vi * 6],
                              mesh_.vertices[vi * 6 + 1],
                              mesh_.vertices[vi * 6 + 2]);
                QPointF sp = project(pos);
                if (sp.x() < 0) continue;

                QString text = QString::number(nid);
                int tx = static_cast<int>(sp.x()) - fm.horizontalAdvance(text) / 2;
                int ty = static_cast<int>(sp.y()) + offsetY;
                drawOutlinedText(tx, ty, text);
            }
        }

    } else if (pickMode_ == PickMode::Part) {
        // ── 部件标签（在部件重心位置显示部件索引） ──
        if (!selection_.selectedElements.empty() && !triToElem_.empty() && !triToPart_.empty()) {
            // 收集选中的部件索引
            std::set<int> selectedParts;
            for (int pi = 0; pi < static_cast<int>(partElementIds_.size()); ++pi) {
                if (isPartFullySelected(pi))
                    selectedParts.insert(pi);
            }

            // 计算每个选中部件的重心
            for (int pi : selectedParts) {
                if (pi < 0 || pi >= static_cast<int>(partTriangles_.size())) continue;
                float sx = 0, sy = 0, sz = 0;
                int count = 0;
                for (int t : partTriangles_[pi]) {
                    if (t * 3 + 2 >= static_cast<int>(mesh_.indices.size())) continue;
                    for (int k = 0; k < 3; ++k) {
                        unsigned int vi = mesh_.indices[t * 3 + k];
                        if (vi * 6 + 2 < mesh_.vertices.size()) {
                            sx += mesh_.vertices[vi * 6];
                            sy += mesh_.vertices[vi * 6 + 1];
                            sz += mesh_.vertices[vi * 6 + 2];
                            count++;
                        }
                    }
                }
                if (count == 0) continue;
                glm::vec3 center(sx / count, sy / count, sz / count);
                QPointF sp = project(center);
                if (sp.x() < 0) continue;

                QString text = QString("Part %1").arg(pi + 1);
                int tx = static_cast<int>(sp.x()) - fm.horizontalAdvance(text) / 2;
                int ty = static_cast<int>(sp.y()) + offsetY;
                drawOutlinedText(tx, ty, text);
            }
        }

    } else {
        // ── 单元标签（在单元重心位置显示） ──
        if (!selection_.selectedElements.empty() && !triToElem_.empty()) {
            struct ElemAccum { float sx = 0, sy = 0, sz = 0; int count = 0; };
            std::unordered_map<int, ElemAccum> elemCentroids;

            int triCount = static_cast<int>(triToElem_.size());
            int idxCount = static_cast<int>(mesh_.indices.size());
            for (int t = 0; t < triCount; ++t) {
                if (t * 3 + 2 >= idxCount) break;
                int eid = triToElem_[t];
                if (selection_.selectedElements.count(eid) == 0) continue;
                auto& acc = elemCentroids[eid];
                for (int k = 0; k < 3; ++k) {
                    unsigned int vi = mesh_.indices[t * 3 + k];
                    if (vi * 6 + 2 < mesh_.vertices.size()) {
                        acc.sx += mesh_.vertices[vi * 6];
                        acc.sy += mesh_.vertices[vi * 6 + 1];
                        acc.sz += mesh_.vertices[vi * 6 + 2];
                        acc.count++;
                    }
                }
            }

            for (const auto& [eid, acc] : elemCentroids) {
                if (acc.count == 0) continue;
                glm::vec3 center(acc.sx / acc.count, acc.sy / acc.count, acc.sz / acc.count);
                QPointF sp = project(center);
                if (sp.x() < 0) continue;

                QString text = QString::number(eid);
                int tx = static_cast<int>(sp.x()) - fm.horizontalAdvance(text) / 2;
                int ty = static_cast<int>(sp.y()) + offsetY;
                drawOutlinedText(tx, ty, text);
            }
        }
    }
}


void GLWidget::setColorBarVisible(bool visible) {
    colorBarVisible_ = visible;
    if (colorBarOverlay_) {
        colorBarOverlay_->setVisible(visible);
        colorBarOverlay_->resize(size());
    }
    update();
}


void GLWidget::setColorBarRange(float min, float max) {
    colorBarMin_ = min;
    colorBarMax_ = max;
    if (colorBarOverlay_) colorBarOverlay_->setRange(min, max);
    update();
}


void GLWidget::setColorBarTitle(const QString& title) {
    colorBarTitle_ = title;
    if (colorBarOverlay_) colorBarOverlay_->setTitle(title);
    update();
}


void GLWidget::setColorBarExtremes(int minId, float minVal, int maxId, float maxVal) {
    if (colorBarOverlay_) colorBarOverlay_->setExtremes(minId, minVal, maxId, maxVal);
    update();
}


void GLWidget::setColorBarIdLabel(const QString& label) {
    if (colorBarOverlay_) colorBarOverlay_->setIdLabel(label);
    update();
}


void GLWidget::applyTheme(const Theme& theme) {
    // 更新色标文字颜色
    barTextColor_ = QColor(theme.barTextR, theme.barTextG, theme.barTextB);
    if (colorBarOverlay_) colorBarOverlay_->setTextColor(barTextColor_);

    // 存储背景颜色（initializeGL 会使用）
    bgTopColor_[0] = theme.bgTopR; bgTopColor_[1] = theme.bgTopG; bgTopColor_[2] = theme.bgTopB;
    bgBotColor_[0] = theme.bgBotR; bgBotColor_[1] = theme.bgBotG; bgBotColor_[2] = theme.bgBotB;

    // 更新渐变背景 VBO（仅在 GL 已初始化时）
    if (backgroundGeometry_) {
        float bgData[] = {
            -1, -1,  theme.bgBotR, theme.bgBotG, theme.bgBotB,
             1, -1,  theme.bgBotR, theme.bgBotG, theme.bgBotB,
             1,  1,  theme.bgTopR, theme.bgTopG, theme.bgTopB,
            -1, -1,  theme.bgBotR, theme.bgBotG, theme.bgBotB,
             1,  1,  theme.bgTopR, theme.bgTopG, theme.bgTopB,
            -1,  1,  theme.bgTopR, theme.bgTopG, theme.bgTopB,
        };
        makeCurrent();
        openGLBackend()->updatePositionColorGeometry(*backgroundGeometry_,
                                                     bgData,
                                                     sizeof(bgData));
        doneCurrent();
    }
    update();
}


