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
            if (action && action->text() == QStringLiteral("模型信息...")) {
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
        if (action && action->menu()) {
            if (QAction* nested = findAction(action->menu(), text)) {
                return nested;
            }
        }
    }
    return nullptr;
}

QMenu* findSubMenu(QMenu* menu, const QString& text)
{
    if (!menu) {
        return nullptr;
    }
    for (QAction* action : menu->actions()) {
        if (action && action->menu() && action->text() == text) {
            return action->menu();
        }
    }
    return nullptr;
}

void assertVisibilityActions(QApplication& app,
                             QWidget& parent,
                             ViewportContextMenu& contextMenu,
                             bool hasModel,
                             bool hasSelection,
                             PickMode selectionMode,
                             bool selectionHasVisibleParts,
                             bool selectionHasHiddenParts,
                             bool hasHiddenItems,
                             bool hasHiddenElements,
                             bool hasHiddenParts,
                             bool modelInfoEnabled,
                             bool hideEnabled,
                             bool isolateElementsEnabled,
                             bool isolatePartsEnabled,
                             bool showAllElementsEnabled,
                             bool showAllPartsEnabled,
                             bool showAllEnabled)
{
    contextMenu.setModelVisibilityState(hasModel,
                                        hasSelection,
                                        selectionMode,
                                        selectionHasVisibleParts,
                                        selectionHasHiddenParts,
                                        hasHiddenItems,
                                        hasHiddenElements,
                                        hasHiddenParts);
    contextMenu.popup(&parent, parent.mapToGlobal(QPoint(24, 24)));
    app.processEvents();

    QMenu* menu = findViewportMenu(parent);
    assert(menu);
    QAction* modelInfoAction = findAction(menu, QStringLiteral("模型信息..."));
    QAction* hideAction = findAction(menu, QStringLiteral("隐藏选中"));
    QMenu* isolateMenu = findSubMenu(menu, QStringLiteral("仅显示"));
    QMenu* hideMenu = findSubMenu(menu, QStringLiteral("隐藏"));
    QMenu* showMenu = findSubMenu(menu, QStringLiteral("显示"));
    QAction* isolateNodesAction = findAction(isolateMenu, QStringLiteral("选中节点"));
    QAction* isolateElementsAction = findAction(isolateMenu, QStringLiteral("选中单元"));
    QAction* isolatePartsAction = findAction(isolateMenu, QStringLiteral("选中部件"));
    QAction* hideAllNodesAction = findAction(hideMenu, QStringLiteral("所有节点"));
    QAction* hideAllElementsAction = findAction(hideMenu, QStringLiteral("所有单元"));
    QAction* hideAllPartsAction = findAction(hideMenu, QStringLiteral("所有部件"));
    QAction* hideAllAction = findAction(hideMenu, QStringLiteral("全部对象"));
    QAction* showAllNodesAction = findAction(showMenu, QStringLiteral("所有节点"));
    QAction* showAllElementsAction = findAction(showMenu, QStringLiteral("所有单元"));
    QAction* showAllPartsAction = findAction(showMenu, QStringLiteral("所有部件"));
    QAction* showAllAction = findAction(showMenu, QStringLiteral("全部对象"));
    assert(modelInfoAction && hideAction && isolateMenu && hideMenu && showMenu &&
           isolateNodesAction && isolateElementsAction && isolatePartsAction &&
           hideAllNodesAction && hideAllElementsAction && hideAllPartsAction && hideAllAction &&
           showAllNodesAction && showAllElementsAction && showAllPartsAction && showAllAction);
    assert(modelInfoAction->isEnabled() == modelInfoEnabled);
    assert(hideAction->isEnabled() == hideEnabled);
    assert(!isolateNodesAction->isEnabled());
    assert(isolateElementsAction->isEnabled() == isolateElementsEnabled);
    assert(isolatePartsAction->isEnabled() == isolatePartsEnabled);
    assert(!hideAllNodesAction->isEnabled());
    assert(hideAllElementsAction->isEnabled() == hasModel);
    assert(hideAllPartsAction->isEnabled() == hasModel);
    assert(hideAllAction->isEnabled() == hasModel);
    assert(!showAllNodesAction->isEnabled());
    assert(showAllElementsAction->isEnabled() == showAllElementsEnabled);
    assert(showAllPartsAction->isEnabled() == showAllPartsEnabled);
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
                            false, false, PickMode::Node,
                            false, false, false, false, false,
                            false, false, false, false, false, false, false);
    assertVisibilityActions(app, parent, contextMenu,
                            true, true, PickMode::Part,
                            true, false, false, false, false,
                            true, true, false, true, false, false, false);
    assertVisibilityActions(app, parent, contextMenu,
                            true, true, PickMode::Element,
                            false, true, true, true, false,
                            true, false, true, false, true, false, true);
    assertVisibilityActions(app, parent, contextMenu,
                            true, true, PickMode::Part,
                            true, true, true, true, true,
                            true, true, false, true, true, true, true);

    printf("ViewportContextMenu visibility action state test passed.\\n");
    return 0;
}
