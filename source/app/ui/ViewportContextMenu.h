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
    void setModelVisibilityState(bool hasModel,
                                 bool hasSelection,
                                 bool selectionHasVisibleParts,
                                 bool selectionHasHiddenParts,
                                 bool hasHiddenParts);
    void popup(QWidget* parent, const QPoint& globalPos);

signals:
    void fitRequested();
    void standardViewRequested(StandardView view);
    void displayModeRequested(ModelDisplayMode mode);
    void modelInfoRequested();
    void hideSelectedRequested();
    void showSelectedRequested();
    void isolateSelectedRequested();
    void showAllRequested();
    void backgroundSettingsRequested();
    void gridVisibleChanged(bool visible);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QPointer<QMenu> activeMenu_;
    QPointer<QWidget> activeParent_;
    ModelDisplayMode displayMode_ = ModelDisplayMode::SolidWireframe;
    bool gridVisible_ = true;
    bool hasModel_ = false;
    bool hasSelection_ = false;
    bool selectionHasVisibleParts_ = false;
    bool selectionHasHiddenParts_ = false;
    bool hasHiddenParts_ = false;
};
