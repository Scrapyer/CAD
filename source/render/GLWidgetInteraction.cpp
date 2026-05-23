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

void GLWidget::resizeGL(int w, int h) {
    auto* glBackend = openGLBackend();
    glBackend->setViewport(0, 0, w, h);

    // 重建拾取 framebuffer（尺寸需与视口一致）
    // 注意：resizeGL 由 Qt 调用时 GL 上下文已 current，无需手动 makeCurrent/doneCurrent
    int dpr = devicePixelRatio();
    if (!pickFramebuffer_)
        pickFramebuffer_ = std::make_unique<OpenGLFramebuffer>();
    glBackend->resizeFramebuffer(*pickFramebuffer_, w * dpr, h * dpr);

    // 色标覆盖层跟随窗口大小
    if (colorBarOverlay_)
        colorBarOverlay_->resize(size());
}

// ============================================================
// 鼠标与键盘事件
// ============================================================


void GLWidget::mousePressEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();
    pressPos_ = pos;
    lastPos_ = pos;
    isDragging_ = false;
    isBoxSelecting_ = false;
    isBoxDeselecting_ = false;

    bool hasMod = (e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier));
    const bool pickTool = interactionMode_ == ViewportInteractionMode::Pick;

    if (!hasMod && e->button() == Qt::LeftButton) {
        StandardView view = StandardView::Front;
        if (standardViewFromAxesClick(pos, &view)) {
            setStandardView(view);
            e->accept();
            return;
        }
    }

    if (hasMod || pickTool) {
        if (e->button() == Qt::LeftButton) {
            // 拾取工具或 Ctrl/Shift + 左键 → 框选/点选
            isBoxSelecting_ = true;
            boxOrigin_ = pos;
            if (!rubberBand_)
                rubberBand_ = new QRubberBand(QRubberBand::Rectangle, this);
            rubberBand_->setGeometry(QRect(boxOrigin_, QSize()));
            rubberBand_->show();
        } else if (hasMod && e->button() == Qt::RightButton) {
            // Ctrl/Shift + 右键 → 框选/点选（取消选中）
            isBoxDeselecting_ = true;
            boxOrigin_ = pos;
            if (!rubberBand_)
                rubberBand_ = new QRubberBand(QRubberBand::Rectangle, this);
            rubberBand_->setGeometry(QRect(boxOrigin_, QSize()));
            rubberBand_->show();
        }
    }
}


void GLWidget::mouseMoveEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();

    // 框选模式（选中/取消）：更新矩形
    if ((isBoxSelecting_ || isBoxDeselecting_) && rubberBand_) {
        rubberBand_->setGeometry(QRect(boxOrigin_, pos).normalized());
        return;
    }

    float dx = pos.x() - lastPos_.x();
    float dy = pos.y() - lastPos_.y();
    lastPos_ = pos;

    // 判断是否已经开始拖拽（超过 5 像素阈值）
    if (!isDragging_) {
        QPoint diff = pos - pressPos_;
        if (diff.manhattanLength() > 5)
            isDragging_ = true;
        else
            return;
    }

    // 拾取工具和 Ctrl/Shift 手势用于选取，不做视图导航
    if ((e->buttons() & Qt::LeftButton) &&
        !(e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
        if (interactionMode_ == ViewportInteractionMode::Rotate) {
            cam_.rotate(dx, dy);
        } else if (interactionMode_ == ViewportInteractionMode::Pan) {
            cam_.pan(dx, dy);
        } else if (interactionMode_ == ViewportInteractionMode::Zoom) {
            cam_.zoom(-dy / 120.0f);
        }
    }
    // Ctrl/Shift + 右键用于取消拾取，不平移
    if ((e->buttons() & (Qt::RightButton | Qt::MiddleButton)) &&
        !(e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)))
        cam_.pan(dx, dy);

    // 部件模式轮廓边依赖视角，相机变化时需要刷新
    if (pickMode_ == PickMode::Part && selection_.hasSelection())
        silhouetteDirty_ = true;

    update();
}


void GLWidget::mouseReleaseEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();

    // ── Ctrl/Shift + 左键：添加选中（点选/框选） ──
    if (e->button() == Qt::LeftButton && isBoxSelecting_ && rubberBand_) {
        rubberBand_->hide();
        isBoxSelecting_ = false;

        QRect rect = QRect(boxOrigin_, pos).normalized();
        if (rect.width() > 3 && rect.height() > 3) {
            // 框选
            pickRectPending_ = true;
            pendingPickRect_ = rect;
            update();
        } else {
            // 范围太小视为点选
            pickPointPending_ = true;
            pendingPickPos_ = pos;
            pendingPickCtrl_ = (e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier));
            update();
        }
    }

    // ── Ctrl/Shift + 右键：取消选中（点选/框选） ──
    if (e->button() == Qt::RightButton && isBoxDeselecting_ && rubberBand_) {
        rubberBand_->hide();
        isBoxDeselecting_ = false;

        QRect rect = QRect(boxOrigin_, pos).normalized();
        if (rect.width() > 3 && rect.height() > 3) {
            // 框选取消
            deselectRectPending_ = true;
            pendingDeselectRect_ = rect;
            update();
        } else {
            // 范围太小视为点选取消
            deselectPointPending_ = true;
            pendingDeselectPos_ = pos;
            update();
        }
    }

    if (e->button() == Qt::RightButton &&
        !isDragging_ &&
        !isBoxDeselecting_ &&
        !(e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
        isDragging_ = false;
        emit contextMenuRequested(mapToGlobal(pos));
        e->accept();
        return;
    }

    isDragging_ = false;
}


void GLWidget::wheelEvent(QWheelEvent* e) {
    // 按住中键或右键拖动时忽略滚轮，防止平移与缩放同时触发
    if (e->buttons() & (Qt::MiddleButton | Qt::RightButton)) return;
    cam_.zoom(e->angleDelta().y() / 120.0f);
    if (pickMode_ == PickMode::Part && selection_.hasSelection())
        silhouetteDirty_ = true;
    update();
}


void GLWidget::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        if (selection_.hasSelection()) {
            selection_.clear();
            selectionDirty_ = true;
            partEdgeCacheValid_ = false;
            selEdgeVertCount_ = 0;
            emit selectionChanged(pickMode_, 0, {});
            update();
        } else {
            window()->close();
        }
    } else {
        QOpenGLWidget::keyPressEvent(e);
    }
}

// ============================================================
// 拾取功能
// ============================================================


void GLWidget::setInteractionMode(ViewportInteractionMode mode)
{
    interactionMode_ = mode;
    isDragging_ = false;
    isBoxSelecting_ = false;
    isBoxDeselecting_ = false;
    if (rubberBand_) {
        rubberBand_->hide();
    }
}


void GLWidget::setPickMode(PickMode mode) {
    if (mode == pickMode_) return;
    pickMode_ = mode;

    // 切换拾取模式时清除之前的选中状态
    if (selection_.hasSelection()) {
        selection_.clear();
        selectionDirty_ = true;
        partEdgeCacheValid_ = false;
        selEdgeVertCount_ = 0;
        emit selectionChanged(pickMode_, 0, {});
        if (mode == PickMode::Part)
            emit partsPicked({});
        update();
    }
}


