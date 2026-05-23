#include "ViewportContextMenu.h"

#include <QApplication>
#include <QMenu>
#include <QWidget>

#include <cassert>
#include <cstdio>

namespace {
QMenu* findViewportMenu(QWidget& parent)
{
    const QList<QMenu*> menus = parent.findChildren<QMenu*>();
    for (QMenu* menu : menus) {
        if (!menu) {
            continue;
        }
        for (QAction* action : menu->actions()) {
            if (action && action->text() == QStringLiteral("隐藏选中")) {
                return menu;
            }
        }
    }
    return nullptr;
}

QAction* findAction(QMenu* menu, const QString& text)
{
    if (!menu) {
        return nullptr;
    }
    for (QAction* action : menu->actions()) {
        if (action && action->text() == text) {
            return action;
        }
    }
    return nullptr;
}

void assertVisibilityActions(QApplication& app,
                             QWidget& parent,
                             ViewportContextMenu& contextMenu,
                             bool hasModel,
                             bool hasSelection,
                             bool selectionHasVisibleParts,
                             bool selectionHasHiddenParts,
                             bool hasHiddenParts,
                             bool modelInfoEnabled,
                             bool hideEnabled,
                             bool showEnabled,
                             bool isolateEnabled,
                             bool showAllEnabled)
{
    contextMenu.setModelVisibilityState(hasModel,
                                        hasSelection,
                                        selectionHasVisibleParts,
                                        selectionHasHiddenParts,
                                        hasHiddenParts);
    contextMenu.popup(&parent, parent.mapToGlobal(QPoint(24, 24)));
    app.processEvents();

    QMenu* menu = findViewportMenu(parent);
    assert(menu);
    QAction* modelInfoAction = findAction(menu, QStringLiteral("模型信息..."));
    QAction* hideAction = findAction(menu, QStringLiteral("隐藏选中"));
    QAction* showAction = findAction(menu, QStringLiteral("显示选中"));
    QAction* isolateAction = findAction(menu, QStringLiteral("仅显示选中"));
    QAction* showAllAction = findAction(menu, QStringLiteral("显示全部"));
    assert(modelInfoAction && hideAction && showAction && isolateAction && showAllAction);
    assert(modelInfoAction->isEnabled() == modelInfoEnabled);
    assert(hideAction->isEnabled() == hideEnabled);
    assert(showAction->isEnabled() == showEnabled);
    assert(isolateAction->isEnabled() == isolateEnabled);
    assert(showAllAction->isEnabled() == showAllEnabled);

    menu->close();
    app.processEvents();
}
} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    QWidget parent;
    parent.resize(160, 120);
    parent.show();
    app.processEvents();

    ViewportContextMenu contextMenu;

    assertVisibilityActions(app, parent, contextMenu,
                            false, false, false, false, false,
                            false, false, false, false, false);
    assertVisibilityActions(app, parent, contextMenu,
                            true, true, true, false, false,
                            true, true, false, true, false);
    assertVisibilityActions(app, parent, contextMenu,
                            true, true, false, true, true,
                            true, false, true, true, true);
    assertVisibilityActions(app, parent, contextMenu,
                            true, true, true, true, true,
                            true, true, true, true, true);

    printf("ViewportContextMenu visibility action state test passed.\\n");
    return 0;
}
