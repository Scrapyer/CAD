#pragma once

#include "RenderBackend.h"

#include <QObject>
#include <QPoint>
#include <QPointer>

class QMenu;
class QWidget;

class ViewportContextMenu : public QObject {
    Q_OBJECT

public:
    explicit ViewportContextMenu(QObject* parent = nullptr);

    void setDisplayMode(ModelDisplayMode mode);
    void setGridVisible(bool visible);
    void popup(QWidget* parent, const QPoint& globalPos);

signals:
    void fitRequested();
    void standardViewRequested(StandardView view);
    void displayModeRequested(ModelDisplayMode mode);
    void backgroundSettingsRequested();
    void gridVisibleChanged(bool visible);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QPointer<QMenu> activeMenu_;
    QPointer<QWidget> activeParent_;
    ModelDisplayMode displayMode_ = ModelDisplayMode::SolidWireframe;
    bool gridVisible_ = true;
};
