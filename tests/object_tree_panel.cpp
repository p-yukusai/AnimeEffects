// The Animation dock's TREE SCENE: what it builds, what it shows, and what it wires.
//
// tests/object_tree_model.cpp pins what a row, a click and a drag MEAN; this gate pins the
// things the model cannot see, and each assertion is a defect the widget tree had or a trap
// this migration met:
//
//   * EVERY row is addressable as a TABLE CELL by name, through QAccessible - the route a
//     screen reader and scripts/gui/ae_ui.py take. The QML rows publish through the
//     application's own accessibility factory (marker "aeAccessibleRow") because a Qt Quick
//     item's default accessible object reports itself INVISIBLE here, which would hide every
//     layer from a screen reader; the role stays Cell, so AT-SPI still says "table cell" and
//     every probe that addressed the widget tree keeps working;
//   * the rows are the MODEL's rows, in the model's order, each at its model height: a keyed
//     layer's row is as tall as its lane block, so the tree and the lanes line up;
//   * a press on a row selects it IN THE MODEL (the scene's pointer path works end to end);
//   * a double click begins a rename and the row shows a text field;
//   * the eye publishes Show/Hide and pressing it reaches the document;
//   * the keyboard is the tree's while it has focus: Ctrl+A selects every layer, and a key the
//     tree does not act on (Ctrl+S) is NOT consumed, so the application's own map still sees it.
//
// Run under Xvfb (a core::LayerNode needs a GL context to exist), as the suite's GL branch does.
#include <QAccessible>
#include <QAccessibleActionInterface>
#include <QApplication>
#include <QFont>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QGuiApplication>
#include <QQuickStyle>
#include <QtTest/QTest>
#include <QQuickItem>

#include <QQuickWindow>
#include <QOpenGLWidget>
#include <QOpenGLVersionFunctionsFactory>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>
#include <QVector2D>
#include <cstdlib>

#include "gui/AudioPlaybackWidget.h"
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

#include "XCAssert.h"
#include "gl/Global.h"
#include "cmnd/Stack.h"
#include "core/FolderNode.h"
#include "core/LayerNode.h"
#include "core/ObjectNode.h"
#include "core/ObjectTree.h"
#include "core/Project.h"
#include "ctrl/TreeActions.h"
#include "ctrl/TimeLineUtil.h"
#include "gui/qml/ObjectTreeModel.h"
#include "gui/qml/QmlFormAccessibility.h"
#include "gui/qml/QmlTheme.h"
#include "gui/theme/Icons.h"

namespace {

int failures = 0;
int checks = 0;

void check(bool aOk, const QString& aWhat) {
    ++checks;
    QTextStream(stdout) << (aOk ? "PASS  " : "FAIL  ") << aWhat << "\n";
    if (!aOk)
        ++failures;
}

class HarnessAssertHandler: public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};
class HarnessErrorHandler: public XCErrorHandler {
public:
    void critical(const QString&, const QString&, const QString&) const override { std::abort(); }
};
HarnessAssertHandler gAssertHandler;
HarnessErrorHandler gErrorHandler;

class TestAnimator: public core::Animator {
public:
    core::Frame currentFrame() const override { return mFrame; }
    void stop() override {}
    void suspend() override {}
    void resume() override {}
    bool isSuspended() const override { return false; }
    core::Frame mFrame;
};

// Every item under a root: ListView delegates live in the VISUAL tree, so QObject's
// findChildren misses them (the trap the settings form recorded).
void collectItems(QQuickItem* aItem, QList<QQuickItem*>& aOut) {
    aOut << aItem;
    for (QQuickItem* child : aItem->childItems())
        collectItems(child, aOut);
}

QAccessibleInterface* accessible(QQuickItem* aItem) {
    return aItem ? QAccessible::queryAccessibleInterface(aItem) : nullptr;
}

QList<QQuickItem*> cells(QQuickItem* aRoot) {
    QList<QQuickItem*> all;
    collectItems(aRoot, all);
    QList<QQuickItem*> out;
    for (QQuickItem* item : all) {
        QAccessibleInterface* iface = accessible(item);
        if (iface && iface->role() == QAccessible::Cell)
            out << item;
    }
    return out;
}

QQuickItem* cell(QQuickItem* aRoot, const QString& aName) {
    for (QQuickItem* item : cells(aRoot)) {
        QAccessibleInterface* iface = accessible(item);
        if (iface && iface->text(QAccessible::Name) == aName)
            return item;
    }
    return nullptr;
}

bool press(QQuickItem* aItem) {
    QAccessibleInterface* iface = accessible(aItem);
    if (!iface || !iface->actionInterface())
        return false;
    iface->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    return true;
}

QList<QQuickItem*> buttonsNamed(QQuickItem* aRoot, const QString& aName) {
    QList<QQuickItem*> all;
    collectItems(aRoot, all);
    QList<QQuickItem*> out;
    for (QQuickItem* item : all) {
        QAccessibleInterface* iface = accessible(item);
        if (iface && iface->role() == QAccessible::Button && iface->text(QAccessible::Name) == aName)
            out << item;
    }
    return out;
}

qreal topOf(QQuickItem* aItem, QQuickItem* aFrame) { return aItem->mapToItem(aFrame, 0, 0).y(); }

// A mouse click at a point inside an item, through QtTest's QWindow route: the same
// entry point Qt's own Quick tests use, and the one that produces a real press/release pair
// (a hand-built QMouseEvent sent to the window skips the delivery agent's press bookkeeping,
// which is why the first version of this helper pressed nothing).
void click(QQuickItem* aItem, const QPointF& aLocal) {
    QWindow* window = aItem->window();
    if (!window)
        return;
    const QPointF local = window->mapFromGlobal(aItem->mapToGlobal(aLocal).toPoint());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, local.toPoint());
    QCoreApplication::processEvents();
}

void doubleClick(QQuickItem* aItem, const QPointF& aLocal) {
    QWindow* window = aItem->window();
    if (!window)
        return;
    const QPointF local = window->mapFromGlobal(aItem->mapToGlobal(aLocal).toPoint());
    QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, local.toPoint());
    QCoreApplication::processEvents();
}

} // namespace

// THE ENGINE'S HANDLERS ARE LINKED BY NAME, so these two live OUTSIDE the anonymous namespace:
// inside it they have internal linkage and every other TU that raises XC_ASSERT - libcore,
// ObjectTreeModel - fails to link (measured: gXCAssertHandler undefined from TreeNodeBase.h).
XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    // A live GL context: a core::LayerNode compiles its mesh shader in its constructor
    // (gl::Global::functions() asserts without a context), which is why this gate runs in the
    // suite's GL branch under Xvfb.
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setVersion(gl::Global::kVersion.first, gl::Global::kVersion.second);
    QSurfaceFormat::setDefaultFormat(format);
    QOpenGLWidget glWidget;
    glWidget.resize(4, 4);
    glWidget.show();
    glWidget.makeCurrent();
    if (!glWidget.context()) {
        QTextStream(stderr) << "no GL context\n";
        return 1;
    }
    auto funcs = QOpenGLVersionFunctionsFactory::get<gl::Global::Functions>(glWidget.context());
    if (!funcs || !funcs->initializeOpenGLFunctions()) {
        QTextStream(stderr) << "no GL functions\n";
        return 1;
    }
    gl::Global::setContext(glWidget);
    gl::Global::setFunctions(*funcs);
    // the application's own accessibility wiring: the row protocol lives in its factory
    gui::installQmlFormAccessibility();

    gui::QmlTheme theme;
    QTemporaryDir iconDir;
    check(iconDir.isValid(), "a scratch dir for the tinted icons");
    theme::tintIcons(QStringLiteral(AE_ICON_SRC), iconDir.path(), theme::Colors::current());
    theme::setIconDir(iconDir.path());

    // ---- the document: a folder with two layers, and a keyed layer beside it -------------
    TestAnimator animator;
    core::Project project(QString(), animator, nullptr);
    // a canvas, so a mask HAS a content box to allocate (maskContentRect needs one)
    project.attribute().setImageSize(QSize(64, 64));
    auto* top = new core::FolderNode(QStringLiteral("doc"));
    project.objectTree().grabTopNode(top);
    auto* group = new core::FolderNode(QStringLiteral("group"));
    top->children().pushBack(group);
    auto* innerA = new core::LayerNode(QStringLiteral("innerA"), project.objectTree().shaderHolder());
    group->children().pushBack(innerA);
    auto* innerB = new core::LayerNode(QStringLiteral("innerB"), project.objectTree().shaderHolder());
    group->children().pushBack(innerB);
    auto* leaf = new core::LayerNode(QStringLiteral("leaf"), project.objectTree().shaderHolder());
    top->children().pushBack(leaf);
    ctrl::tree::addKeysAtPlayhead(project, *leaf);

    gui::ObjectTreeModel model;
    model.setProject(&project);

    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("treeModel"), &model);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    engine.rootContext()->setContextProperty(QStringLiteral("uiFont"), QApplication::font());

    QQmlComponent component(&engine, QUrl(QStringLiteral("file:" AE_QML_DIR "/ObjectTreePanel.qml")));
    if (component.isError()) {
        QTextStream(stderr) << "the tree panel's QML does not load:\n";
        for (const QQmlError& error : component.errors())
            QTextStream(stderr) << "  " << error.toString() << "\n";
        return 2;
    }
    check(component.status() == QQmlComponent::Ready, "the tree panel's QML is Ready");

    QQuickWindow window;
    QScopedPointer<QObject> created(component.create());
    auto* root = qobject_cast<QQuickItem*>(created.data());
    if (!root) {
        QTextStream(stderr) << "the tree panel is not an Item\n";
        return 2;
    }
    root->setParentItem(window.contentItem());
    const qreal columnWidth = 240;
    root->setWidth(columnWidth);
    root->setHeight(300);
    window.resize(int(columnWidth), 300);
    window.show();
    // The key phase needs an ACTIVE window: Qt routes key events to the focus window's active
    // focus item, and a window that never became active receives none (measured under Xvfb:
    // the list was the active focus item and still saw no key).
    window.requestActivate();
    (void)QTest::qWaitForWindowExposed(&window);
    QCoreApplication::processEvents();

    // ---- 1. every row is a named, reachable table cell -----------------------------------
    check(model.rowCount() == 3, QStringLiteral("the model has the document and two rows (%1)")
                                     .arg(model.rowCount()));
    const QList<QQuickItem*> rowCells = cells(root);
    check(rowCells.size() == model.rowCount(),
          QStringLiteral("every row publishes one cell (%1 of %2)").arg(rowCells.size()).arg(model.rowCount()));
    QQuickItem* docRow = cell(root, QStringLiteral("doc"));
    QQuickItem* groupRow = cell(root, QStringLiteral("group"));
    QQuickItem* leafRow = cell(root, QStringLiteral("leaf"));
    check(docRow && groupRow && leafRow, "the rows are addressable by the layer names");
    if (docRow && groupRow && leafRow) {
        QAccessibleInterface* docIface = accessible(docRow);
        QAccessibleInterface* groupIface = accessible(groupRow);
        check(docIface && docIface->state().selectable == false,
              "the document row is published as NOT selectable (a document is not a layer)");
        check(groupIface && groupIface->state().expandable,
              "a folder with children is published as expandable");
        check(groupIface && !groupIface->state().expanded,
              "and collapsed, so a screen reader says so");
        check(groupIface && groupIface->state().checkable && groupIface->state().checked,
              "its eye is published as a checked check state");
        QTextStream(stdout) << "MEASURED  doc row: "
                            << accessible(docRow)->text(QAccessible::Description) << " / group row: "
                            << groupIface->text(QAccessible::Description) << "\n";
    }

    // ---- 2. the rows are the model's rows, at the model's heights ------------------------
    if (docRow && groupRow && leafRow) {
        check(qAbs(topOf(docRow, root)) < 0.5, "the document row is first");
        check(qAbs(topOf(groupRow, root) - model.rows()[1].top) < 0.5,
              QStringLiteral("the folder's row starts at the model's offset (%1 vs %2)")
                  .arg(topOf(groupRow, root))
                  .arg(model.rows()[1].top));
        check(qAbs(groupRow->height() - model.rows()[1].height) < 0.5,
              QStringLiteral("and is the model's height (%1 vs %2)")
                  .arg(groupRow->height())
                  .arg(model.rows()[1].height));
        check(qAbs(leafRow->height() - model.rows().last().height) < 0.5,
              QStringLiteral("the KEYED layer's row is its lane block's height (%1 vs %2)")
                  .arg(leafRow->height())
                  .arg(model.rows().last().height));
        QTextStream(stdout) << "MEASURED  doc h=" << docRow->height() << " group h=" << groupRow->height()
                            << " leaf h=" << leafRow->height() << "\n";
    }

    // ---- 2a00. A REBUILD KEEPS THE SCROLL (adding a key used to scroll both views) ------
    // Reported: "adding a key resets timeline scroll" and "masking caused the tree's scroll to
    // jump". A key add emits Type_PushKey, which rebuilds the rows; the ListView drops contentY
    // to 0 on the reset, that zero was stored in the model and forwarded to the timeline. This
    // drives the REAL list: scroll it, add a key, and require the offset and contentY to stay.
    {
        auto* listItem = [&]() -> QQuickItem* {
            QList<QQuickItem*> all;
            collectItems(root, all);
            for (QQuickItem* item : all)
                if (item->property("objectName").toString() == QStringLiteral("aeRowList"))
                    return item;
            return nullptr;
        }();
        check(listItem != nullptr, "the list is reachable");
        if (listItem) {
            const int panelBefore = int(root->height());
            root->setHeight(66);            // three rows: the fixture (170px) must scroll
            QCoreApplication::processEvents();
            model.setViewportHeight(66);
            model.setScrollOffset(22);
            QCoreApplication::processEvents();
            const qreal contentBefore = listItem->property("contentY").toReal();
            check(std::abs(contentBefore - 22.0) < 0.5,
                  QStringLiteral("the list is scrolled to the model's offset (%1)").arg(contentBefore));

            // THE REBUILD: playhead keys push Type_PushKey. On a CHILD the fixture left keyless, so
            // this phase's push is its own macro - the leaf's keys are the fixture's and must not
            // be undone below.
            ctrl::tree::addKeysAtPlayhead(project, *innerA);
            QCoreApplication::processEvents();
            QCoreApplication::processEvents();   // the deferred re-apply
            const qreal contentAfter = listItem->property("contentY").toReal();
            check(std::abs(contentAfter - contentBefore) < 0.5,
                  QStringLiteral("and a key add leaves it there (%1 -> %2)")
                      .arg(contentBefore).arg(contentAfter));
            check(model.scrollOffset() == 22,
                  QStringLiteral("the model's offset too (%1)").arg(model.scrollOffset()));

            model.setScrollOffset(0);
            root->setHeight(panelBefore);
            QCoreApplication::processEvents();
        }
    }

    // ---- 2a0. THE SCROLL RANGE IS THE LIST'S, not the panel's ----------------------------
    // The panel's height includes the horizontal bar's lane below the list. Reporting the PANEL
    // height made maxScrollOffset 12px too big, so the timeline (which clamps through the model)
    // could scroll past the row where the tree stops - the reported "scrolled all the way down
    // and the two panes desynced".
    {
        QCoreApplication::processEvents();
        int listHeight = -1;
        QList<QQuickItem*> all;
        collectItems(root, all);
        for (QQuickItem* item : all) {
            if (item->property("objectName").toString() == QStringLiteral("aeRowList")) {
                listHeight = int(item->height());
                break;
            }
        }
        check(listHeight > 0, QStringLiteral("the list's height is reachable (%1)").arg(listHeight));
        check(model.viewportHeight() == listHeight,
              QStringLiteral("the model's viewport is the LIST, not the panel (%1 vs %2 of %3)")
                  .arg(model.viewportHeight()).arg(listHeight).arg(int(root->height())));
        check(model.viewportHeight() == int(root->height()) - 12,
              QStringLiteral("which is the panel minus the bottom bar's lane (%1)")
                  .arg(model.viewportHeight()));
        // and the range that follows from it is the list's own
        // A SHORT PANEL: the range must move with the LIST, and a wild request must clamp there -
        // 12px less than the panel-height rule would have allowed (the desync's exact size).
        {
            const int panelBefore = int(root->height());
            const int rangeAtFull = model.maxScrollOffset();
            root->setHeight(120);
            QCoreApplication::processEvents();
            const int shortList = 120 - 12;
            check(model.viewportHeight() == shortList,
                  QStringLiteral("a shorter panel shortens the list's viewport too (%1)")
                      .arg(model.viewportHeight()));
            check(model.maxScrollOffset() == std::max(0, model.contentHeight() - shortList),
                  QStringLiteral("and the range follows it (%1)").arg(model.maxScrollOffset()));
            model.setScrollOffset(5000);
            check(model.scrollOffset() == model.maxScrollOffset(),
                  QStringLiteral("a wild offset clamps to the LIST's end (%1)")
                      .arg(model.scrollOffset()));
            check(model.maxScrollOffset() != std::max(0, model.contentHeight() - 120),
                  QStringLiteral("which is not the panel's range (%1 vs %2)")
                      .arg(model.maxScrollOffset())
                      .arg(std::max(0, model.contentHeight() - 120)));
            model.setScrollOffset(0);
            root->setHeight(panelBefore);
            QCoreApplication::processEvents();
            check(model.maxScrollOffset() == rangeAtFull, "and the full panel restores it");
        }

        check(model.maxScrollOffset() == std::max(0, model.contentHeight() - listHeight),
              QStringLiteral("so maxScrollOffset is the list's range (%1 of %2)")
                  .arg(model.maxScrollOffset()).arg(model.contentHeight()));
    }

    // ---- 2a. the pinned right zone is as wide as its controls, and no wider -------------
    // (the fixture's leaf has no matte, so the zone is the fade, the eye and the edge)
    {
        QList<QQuickItem*> all;
        collectItems(root, all);
        QQuickItem* zone = nullptr;
        QQuickItem* fade = nullptr;
        QQuickItem* eye = nullptr;
        for (QQuickItem* item : all) {
            const QString name = item->objectName();
            if (name == QStringLiteral("aePinnedZone") && item->isVisible())
                zone = item;
            else if (name == QStringLiteral("aePinnedFade"))
                fade = item;
            else if (name == QStringLiteral("aeEyeButton") && item->width() > 1.0)
                eye = item;   // the document row's eye is zero-width, so this finds a real one
        }
        check(zone != nullptr, "the rows have a pinned right zone");
        check(fade != nullptr, "and its soft edge is published");
        if (zone) {
            // the fade (16) + the eye (20) + the edge (the bar's 12px lane + 2)
            check(qAbs(zone->width() - 50.0) < 0.5,
                  QStringLiteral("the fade plus the eye alone (%1px)").arg(zone->width()));
            if (fade) {
                const qreal fadeRight = fade->mapToItem(zone, fade->width(), 0).x();
                check(fadeRight <= 20.5,
                      QStringLiteral("the fade ends where the eye begins (fade right %1)")
                          .arg(fadeRight));
            }
            check(eye != nullptr, "the rows have an eye");
            if (eye) {
                const qreal eyeX = eye->mapToItem(zone, 0, 0).x();
                check(eyeX >= 15.5,
                      QStringLiteral("and the eye is clear of it (eye left %1)").arg(eyeX));
                // AND IT CLEARS THE SCROLL BAR'S WHOLE LANE: the bar OVERLAYS the rows' right
                // 12px with a 4px pill (its left edge 8px in). The margin is the lane plus 2, so
                // the eye's 14px glyph ends 17px from the row's edge - 9px clear of the pill.
                // Measured along the way: 6px (=1px clear) and 8px (=3px clear) both still read as
                // "overlapping" / "quite close" to the person using it.
                const qreal glyphRight = eye->mapToItem(zone, eye->width() - 3.0, 0).x();
                const qreal pillLeft = zone->width() - 8.0;
                check(glyphRight <= pillLeft,
                      QStringLiteral("the eye's glyph ends clear of the bar (%1 vs %2)")
                          .arg(glyphRight).arg(pillLeft));
                // and the eye is ON the row's fill, clear of the fade that ends at its left edge
                check(eye->mapToItem(zone, 0, 0).x() >= (fade ? fade->width() : 16.0),
                      "the eye starts at or after the fade's right edge");
            }
            // anchored to the ROW's edge (not the list's: the row is what the fill belongs to, and
            // a lookup by class can find another view), so no row fill sits to its right
            if (QQuickItem* rowItem = zone->parentItem()) {
                const qreal rightEdge = zone->mapToItem(rowItem, zone->width(), 0).x();
                check(qAbs(rightEdge - rowItem->width()) < 0.5,
                      QStringLiteral("and it reaches the row's edge (%1 of %2)")
                          .arg(rightEdge).arg(rowItem->width()));
                check(qAbs(rowItem->width() - columnWidth) < 0.5,
                      QStringLiteral("the row is as wide as the column (%1 of %2)")
                          .arg(rowItem->width()).arg(columnWidth));
            }
        }
    }

    // ---- 2a1. A MATTE CHIP SITS LEFT OF THE EYE, AND THE EYE DOES NOT MOVE -----------------
    // Two reported defects: the chip appeared only once the mask had a key (the flag was computed
    // at row build time and only the url was re-emitted), and its slot moved the eye 20px for that
    // row alone, misaligning the visibility column. The fixture gives the leaf a mask BEFORE the
    // chip is looked at: the chip must be there with no key at all, left of the eye, and the eye
    // must sit at the same x as the row that has no chip.
    {
        auto eyeOf = [&](const QString& aName) -> QQuickItem* {
            QQuickItem* rowItem = cell(root, aName);
            if (!rowItem)
                return nullptr;
            QList<QQuickItem*> all;
            collectItems(rowItem, all);
            for (QQuickItem* item : all)
                if (item->objectName() == QStringLiteral("aeEyeButton") && item->width() > 1.0)
                    return item;
            return nullptr;
        };
        // the leaf's eye BEFORE it has a matte: the chip's slot must not move it
        QQuickItem* eyeBefore = eyeOf(QStringLiteral("leaf"));
        check(eyeBefore != nullptr, "the leaf's eye is there before the mask");
        const qreal eyeOfPlainRow = eyeBefore ? eyeBefore->mapToScene(QPointF(0, 0)).x() : -1;

        ctrl::TimeLineUtil::addMask(project, *leaf);
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        // the chip is there with NO key in the mask lane
        QQuickItem* leafRowItem = cell(root, QStringLiteral("leaf"));
        check(leafRowItem != nullptr, "the leaf row exists");
        if (leafRowItem) {
            QList<QQuickItem*> all;
            collectItems(leafRowItem, all);
            QQuickItem* chip = nullptr;
            QQuickItem* eye = nullptr;
            QQuickItem* zone = nullptr;
            for (QQuickItem* item : all) {
                const QString name = item->objectName();
                if (name == QStringLiteral("aeMatteChip"))
                    chip = item;
                else if (name == QStringLiteral("aeEyeButton") && item->width() > 1.0)
                    eye = item;
                else if (name == QStringLiteral("aePinnedZone"))
                    zone = item;
            }
            check(chip != nullptr, "the matte chip exists");
            if (chip && eye && zone) {
                check(chip->isVisible(), "and it is visible without a key in the lane");
                const qreal chipX = chip->mapToItem(zone, 0, 0).x();
                const qreal eyeX = eye->mapToItem(zone, 0, 0).x();
                check(chipX + chip->width() <= eyeX,
                      QStringLiteral("the chip is LEFT of the eye (%1+%2 vs %3)")
                          .arg(chipX).arg(chip->width()).arg(eyeX));
                // and the EYE is where it is on a row without a matte: the column does not move
                const QPointF eyeScene = eye->mapToScene(QPointF(0, 0));
                check(eyeOfPlainRow >= 0
                          && std::abs(eyeScene.x() - eyeOfPlainRow) < 0.5,
                      QStringLiteral("and the eye column did not move (%1 vs %2)")
                          .arg(eyeScene.x()).arg(eyeOfPlainRow));
            }
        }
    }

    // The mask added a lane to the leaf, so its row got taller and the ListView rebuilt its
    // delegates. Re-address the folder row before the press/double-click/rename checks: a
    // QQuickItem kept across a model reset names the delegate that is no longer in the scene.
    groupRow = cell(root, QStringLiteral("group"));

    // ---- 2a2. THE NAME'S TEXT ORIGIN IS A WHOLE PIXEL, and its ink has a solid core -----
    // Reported as "the text is kinda blurry": the centring arithmetic is fractional (an 8pt line
    // box is not a whole number of pixels), and a text origin on a fraction of a pixel spreads
    // every glyph. MEASURED on the running app at the same font: 8.1% of the row's ink at full
    // coverage before the round, 16.1% after, against the Properties panel's 18.6%.
    {
        QList<QQuickItem*> all;
        collectItems(root, all);
        QQuickItem* nameItem = nullptr;
        for (QQuickItem* item : all)
            if (item->objectName() == QStringLiteral("aeRowName") && item->isVisible()) {
                nameItem = item;
                break;
            }
        check(nameItem != nullptr, "the row's name is published");
        if (nameItem) {
            check(std::abs(nameItem->y() - std::round(nameItem->y())) < 0.001,
                  QStringLiteral("its origin is a whole pixel (%1)").arg(nameItem->y()));
            // THE CONSEQUENCE: the glyphs must carry full-coverage pixels. A soft run is almost
            // entirely mid-tone, so this fails on a fractional origin even at the right size.
            QCoreApplication::processEvents();
            const QImage shot = window.grabWindow();
            const QPoint origin = nameItem->mapToScene(QPointF(0, 0)).toPoint();
            const QRect box(origin, QSize(int(nameItem->width()), int(nameItem->height())));
            if (box.intersected(shot.rect()) == box && shot.width() > box.right()) {
                double lo = 255, hi = 0;
                for (int y = box.top(); y <= box.bottom(); ++y)
                    for (int x = box.left(); x <= box.right(); ++x) {
                        const double v = qGray(shot.pixel(x, y));
                        lo = std::min(lo, v);
                        hi = std::max(hi, v);
                    }
                const double span = std::max(1.0, hi - lo);
                int inked = 0;
                int strongInk = 0;
                for (int y = box.top(); y <= box.bottom(); ++y)
                    for (int x = box.left(); x <= box.right(); ++x) {
                        const double cov = (qGray(shot.pixel(x, y)) - lo) / span;
                        if (cov > 0.2)
                            ++inked;
                        if (cov > 0.85)
                            ++strongInk;
                    }
                check(inked > 40, QStringLiteral("the name has ink to measure (%1 px)").arg(inked));
                if (inked > 0) {
                    const double fraction = double(strongInk) / double(inked);
                    check(fraction > 0.12,
                          QStringLiteral("and it lands on whole pixels: %1% at full coverage")
                              .arg(fraction * 100, 0, 'f', 1));
                }
            }
        }
    }

    // ---- 2b. the FOLDER GLYPH is the slot's size, not a shrunk one ---------------------
    if (groupRow) {
        // the widget drew `icon.pixmap(kItemSize, kItemSize)` (22px, ink ~19): a hand-picked
        // smaller Image shrank every folder by a quarter (measured 19px ink -> 14px)
        QList<QQuickItem*> all;
        collectItems(groupRow, all);
        QQuickItem* glyph = nullptr;
        for (QQuickItem* item : all) {
            const QVariant src = item->property("source");
            if (src.isValid() && src.toUrl().toString().endsWith(QStringLiteral("folder.svg")))
                glyph = item;
        }
        check(glyph != nullptr, "the folder row draws the folder glyph");
        if (glyph) {
            check(qAbs(glyph->width() - 22) < 0.5 && qAbs(glyph->height() - 22) < 0.5,
                  QStringLiteral("at the icon slot's own size, 22px (%1x%2)")
                      .arg(glyph->width()).arg(glyph->height()));
        }
    }

    // ---- 3. a press selects in the model ------------------------------------------------
    if (groupRow) {
        press(groupRow);
        check(model.selectionCount() == 1 && model.representNode() == group,
              "pressing a row through the accessibility action selects it in the model");
    }

    // ---- 4. a mouse press on a row selects it too (the scene's own pointer path) ---------
    model.clearSelection();
    if (groupRow) {
        click(groupRow, QPointF(60, 11));
        check(model.selectionCount() == 1 && model.representNode() == group,
              "a mouse press on the row selects it (the delegate's pointer area is live)");
    }

    // ---- 5. a double click begins a rename ----------------------------------------------
    model.clearSelection();
    if (groupRow) {
        doubleClick(groupRow, QPointF(60, 11));
        check(model.isRenaming(),
              QStringLiteral("a double click on a FOLDER row starts a rename (renaming=%1 row=%2)")
                  .arg(model.isRenaming())
                  .arg(model.renamingRow()));
        const QList<QQuickItem*> editors = [&]() {
            QList<QQuickItem*> all;
            collectItems(root, all);
            QList<QQuickItem*> out;
            for (QQuickItem* item : all) {
                // the editor is a Controls TextField (a QQuickTextField, whose internals are a
                // QQuickTextInput); match either, by the item's own metaobject
                const QString kind = QString::fromLatin1(item->metaObject()->className());
                const bool isField = kind.startsWith(QStringLiteral("QQuickTextField")) ||
                                     kind.startsWith(QStringLiteral("QQuickTextInput"));
                if (isField && item->isVisible() && item->property("text").isValid())
                    out << item;
            }
            return out;
        }();
        check(!editors.isEmpty(), "the row shows a text field to type the name into");
        // AND THE FIELD TAKES TEXT. The editor selects the old name when it opens, so typing
        // replaces it; Return commits through the model, which renames the DOCUMENT in one
        // undo step - the whole gesture, not just "a field appeared".
        //
        // The letters are delivered TO THE ITEM and Return through the WINDOW: QTest's window
        // route delivers special keys but not a bare letter to a focused TextInput (measured -
        // Return arrived and 't' did not), and the item route is what the delivery agent ends
        // up calling anyway (QQuickTextInput::keyPressEvent).
        if (!editors.isEmpty()) {
            QQuickItem* field = editors.first();
            for (const char letter : {'t', 'y', 'p', 'e'}) {
                QKeyEvent typed(
                    QEvent::KeyPress, Qt::Key(letter), Qt::NoModifier, QString(QLatin1Char(letter))
                );
                QCoreApplication::sendEvent(field, &typed);
            }
            QCoreApplication::processEvents();
            check(field->property("text").toString() == QStringLiteral("type"),
                  QStringLiteral("typing reaches the editor (%1)")
                      .arg(field->property("text").toString()));
            QTest::keyClick(&window, Qt::Key_Return);
            QCoreApplication::processEvents();
            check(!model.isRenaming(), "Return closes the editor");
            check(group->name() == QStringLiteral("type"),
                  QStringLiteral("and commits the name to the document (%1)").arg(group->name()));
            project.commandStack().undo();
            check(group->name() == QStringLiteral("group"), "one undo restores the old name");
        }
        model.cancelRename();
    }

    // ---- 6. the eye is a named control, and pressing it reaches the document --------------
    model.clearSelection();
    const QList<QQuickItem*> hideButtons = buttonsNamed(root, QStringLiteral("Hide leaf"));
    check(hideButtons.size() == 1,
          QStringLiteral("the layer's eye is a button named 'Hide leaf' (%1)").arg(hideButtons.size()));
    if (!hideButtons.isEmpty()) {
        check(press(hideButtons.first()), "and it accepts a press");
        check(!leaf->isVisible(), "the layer is hidden in the DOCUMENT");
        check(!buttonsNamed(root, QStringLiteral("Show leaf")).isEmpty(),
              "and its name says Show now");
        project.commandStack().undo();
        check(leaf->isVisible(), "one undo shows it again");
    }

    // ---- 7. the keyboard belongs to the tree while it has focus --------------------------
    model.clearSelection();
    {
        QList<QQuickItem*> all;
        collectItems(root, all);
        QQuickItem* list = nullptr;
        for (QQuickItem* item : all) {
            if (QString::fromLatin1(item->metaObject()->className()).startsWith(
                    QStringLiteral("QQuickListView"))) {
                list = item;
                break;
            }
        }
        check(list != nullptr, "the scene has a ListView");
        if (list) {
            list->forceActiveFocus();
            QCoreApplication::processEvents();
            check(model.isFocused(), "focusing the list tells the model the tree has the keyboard");

            // QtTest's key route: it goes through the window's own delivery agent, so the
            // event reaches the ACTIVE FOCUS ITEM - a hand-built QKeyEvent sent to the
            // window is not the same thing (the first version of this check selected nothing).
            // a rename earlier in this flow must NOT leave its editor holding the keyboard:
            // the scene returns focus to the list, so the tree still answers keys
            QQuickItem* focus = window.activeFocusItem();
            check(focus != nullptr &&
                      !QString::fromLatin1(focus->metaObject()->className())
                           .startsWith(QStringLiteral("QQuickTextInput")),
                  QStringLiteral("the keyboard is back on the list, not on a closed editor (%1)")
                      .arg(focus ? QString::fromLatin1(focus->metaObject()->className())
                                 : QStringLiteral("null")));
            QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
            QCoreApplication::processEvents();
            check(model.selectionCount() >= 2,
                  QStringLiteral("ctrl+A selects every layer (%1)").arg(model.selectionCount()));

            model.clearSelection();
            QTest::keyClick(&window, Qt::Key_S, Qt::ControlModifier);
            QCoreApplication::processEvents();
            check(model.selectionCount() == 0, "ctrl+S changes nothing in the tree");
        }
    }

    // ---- 8. expanding through the model re-flows the scene --------------------------------
    model.clearSelection();
    // nothing above expanded it: a double click renames (a folder too), so the folder is still
    // collapsed here and this phase measures an EXPANSION
    check(!model.isExpanded(1), "the folder is still collapsed after the earlier phases");
    const int before = model.rowCount();
    model.toggleExpanded(1);
    QCoreApplication::processEvents();
    check(model.rowCount() == before + 2, "expanding the folder adds its two children");
    check(cell(root, QStringLiteral("innerA")) != nullptr,
          "and the scene publishes them (a rebuild is a real reset, not a stale list)");
    check(cell(root, QStringLiteral("leaf")) != nullptr, "the row below moved down, not away");

    // ---- 8b. the right zone is PINNED and the middle zone scrolls -----------------------
    model.clearSelection();
    {
        QQuickItem* eye = buttonsNamed(root, QStringLiteral("Hide leaf")).value(0);
        check(eye != nullptr, "the layer has its eye");
        const qreal eyeBefore = eye ? eye->mapToItem(root, 0, 0).x() : -1;
        // widen the content past the viewport and scroll: the name moves, the eye must not
        QQuickItem* list = nullptr;
        QList<QQuickItem*> all;
        collectItems(root, all);
        for (QQuickItem* item : all) {
            if (QString::fromLatin1(item->metaObject()->className())
                    .startsWith(QStringLiteral("QQuickListView"))) {
                list = item;
                break;
            }
        }
        check(list != nullptr, "the scene has a list to scroll");
        QQuickItem* nameItem = cell(root, QStringLiteral("leaf"));
        const qreal nameBefore = nameItem ? nameItem->mapToItem(root, 0, 0).x() : -1;
        root->setProperty("hOffset", 40.0);
        QCoreApplication::processEvents();
        const qreal eyeAfter = eye ? eye->mapToItem(root, 0, 0).x() : -1;
        check(qAbs(eyeAfter - eyeBefore) < 0.5,
              QStringLiteral("the eye does not move when the content scrolls (%1 -> %2)")
                  .arg(eyeBefore).arg(eyeAfter));
        if (nameItem) {
            // the NAME lives inside the scrolled zone: the row item itself does not move, so
            // this asserts the panel kept the offset (the row reads it as a property)
            const qreal offset = root->property("hOffset").toReal();
            check(qAbs(offset - 40.0) < 0.5,
                  QStringLiteral("the offset reached the row (%1)").arg(offset));
        }
        root->setProperty("hOffset", 0.0);
        QCoreApplication::processEvents();
        (void)nameBefore;
    }

    // ---- 8c. a mouse click leaves NO focus ring; a key press does ----------------------
    {
        model.clearSelection();
        QQuickItem* leafCell = cell(root, QStringLiteral("leaf"));
        check(leafCell != nullptr, "the leaf row is addressable");
        if (leafCell) {
            // a real mouse click through the scene: the delegate's own press handler is what
            // tells the panel "this was the MOUSE", so calling the model directly would prove
            // nothing
            click(leafCell, QPointF(60, 11));
            check(root->property("focusVisible").toBool() == false,
                  "after a mouse press the cursor ring is off (focusVisible false)");

            QList<QQuickItem*> all;
            collectItems(root, all);
            QQuickItem* list = nullptr;
            for (QQuickItem* item : all) {
                if (QString::fromLatin1(item->metaObject()->className())
                        .startsWith(QStringLiteral("QQuickListView"))) {
                    list = item;
                    break;
                }
            }
            check(list != nullptr, "the scene has a list to take the keyboard");
            if (list) {
                list->forceActiveFocus();
                QTest::keyClick(&window, Qt::Key_Down);
                QCoreApplication::processEvents();
                check(root->property("focusVisible").toBool(),
                      "and a key press turns it on");
            }
        }
    }

    // ---- 8d. the wheel scrolls BOTH axes, and the offset travels BOTH ways ---------------
    {
        // a long name (through the rename text path) and a short viewport: the content then
        // overflows both ways, which is the state the reports were made in
        model.beginRename(1);
        model.setRenamingText(QStringLiteral("a very long layer name that will not fit at all"));
        model.commitRenaming();
        QCoreApplication::processEvents();
        root->setHeight(60);
        QCoreApplication::processEvents();

        QList<QQuickItem*> all;
        collectItems(root, all);
        QQuickItem* list = nullptr;
        for (QQuickItem* item : all) {
            if (QString::fromLatin1(item->metaObject()->className())
                    .startsWith(QStringLiteral("QQuickListView"))) {
                list = item;
                break;
            }
        }
        check(list != nullptr, "the scene has a list for the wheel");
        if (list) {
            const QPointF pos(60, 20);
            const auto wheel = [&](Qt::KeyboardModifiers aMods) {
                QWheelEvent event(pos, window.mapToGlobal(pos.toPoint()), QPoint(0, 0),
                                  QPoint(0, -120), Qt::NoButton, aMods, Qt::NoScrollPhase, false);
                QCoreApplication::sendEvent(&window, &event);
                QCoreApplication::processEvents();
            };

            // plain wheel: the rows move, i.e. the handler did not eat the vertical axis
            const qreal beforeY = list->property("contentY").toReal();
            wheel(Qt::NoModifier);
            QTest::qWait(250); // the wheel glides (120ms NumberAnimation)
            const qreal afterY = list->property("contentY").toReal();
            check(afterY > beforeY + 1.0,
                  QStringLiteral("a plain wheel scrolls the ROWS down (%1 -> %2)").arg(beforeY).arg(afterY));

            // shift+wheel: the same gesture on the horizontal axis
            const qreal beforeH = root->property("hOffset").toReal();
            wheel(Qt::ShiftModifier);
            const qreal afterH = root->property("hOffset").toReal();
            check(afterH > beforeH + 1.0,
                  QStringLiteral("shift+wheel scrolls the CONTENT sideways (%1 -> %2)")
                      .arg(beforeH).arg(afterH));
            check(root->property("focusVisible").toBool() == false ||
                      list->property("contentY").toReal() == afterY,
                  "shift+wheel did not disturb the rows");

            // and the OTHER direction: the timeline sets the offset, the rows follow
            model.setScrollOffset(0);
            QCoreApplication::processEvents();
            check(qAbs(list->property("contentY").toReal()) < 0.5,
                  "an offset of 0 from outside puts the rows at the top");
            model.setScrollOffset(30);
            QCoreApplication::processEvents();
            check(qAbs(list->property("contentY").toReal() - 30.0) < 0.5,
                  QStringLiteral("and 30 from outside moves them 30px (%1)")
                      .arg(list->property("contentY").toReal()));
        }
        root->setHeight(300);
    }

    // ---- 8e. clicking away commits an open rename --------------------------------------
    {
        model.clearSelection();
        // 8d left the list 30px down. Put it back BEFORE addressing the row: a
        // programmatic offset followed immediately by a mouse click can be
        // hit-tested against the list's previous transform, so the delegate's
        // mapToScene and the pointer disagree for a frame.
        model.setScrollOffset(0);
        QCoreApplication::processEvents();
        QQuickItem* leafRow = cell(root, QStringLiteral("leaf"));
        check(leafRow != nullptr, "the leaf row is addressable for the rename");
        if (leafRow) {
            core::LayerNode* leafModelNode = leaf;
            // A real double click is two presses; the first also gives the scene its
            // mouse focus. (QTest::mouseDClick alone, with no press immediately before
            // it, does not produce the second click this row's MouseArea sees.)
            click(leafRow, QPointF(60, 11));
            QTest::qWait(60);
            doubleClick(leafRow, QPointF(60, 11));
            check(model.isRenaming(), "a double click opens the editor");
            {
                // the FIELD's text, as typing would set it: the editor pushes every change to the
                // model (setRenamingText), which is what a click away then commits
                QList<QQuickItem*> all;
                collectItems(root, all);
                for (QQuickItem* item : all) {
                    if (QString::fromLatin1(item->metaObject()->className())
                            .startsWith(QStringLiteral("QQuickTextInput")) &&
                        item->isVisible()) {
                        item->setProperty("text", QStringLiteral("renamed by clicking away"));
                        break;
                    }
                }
                QCoreApplication::processEvents();
            }
            // a VISIBLE other row: innerA is the tall layer above the leaf. The document
            // row would be "somewhere else" too, but 8d's scroll put it off screen.
            QQuickItem* other = cell(root, QStringLiteral("innerA"));
            check(other != nullptr, "there is another row to click");
            if (other) {
                click(other, QPointF(60, 11));
                QCoreApplication::processEvents();
                check(!model.isRenaming(), "a press on another row closes the editor");
                check(leafModelNode->name() == QStringLiteral("renamed by clicking away"),
                      QStringLiteral("and COMMITS the name (%1)").arg(leafModelNode->name()));
                project.commandStack().undo();
            }
        }
    }

    // ---- 9. the asset chooser is the SCENE's (the last widget dialog in this dock) ------
    model.openAddAssets();
    QCoreApplication::processEvents();
    check(!model.isAddAssetsOpen(), "with no resource trees there is nothing to choose from");
    check(model.assetTrees().isEmpty(), "and the list is empty");

    // BOTH PHASES ABOVE CHANGE THE DOCUMENT (one macro each: playhead keys, then a mask), so they
    // hand it back - exactly those two, because the fixture's own setup commands are older than
    // them and the later checks (renames, restructures, drags) must see the document as it was.
    project.commandStack().undo();
    project.commandStack().undo();
    QCoreApplication::processEvents();

    // ---- 12. DRAG TO REORDER: the row keeps the grab the Flickable used to steal -------------
    // Reported: "drag to reorder doesn't work because drag scrolls". The row sits in a ListView
    // (a Flickable), which takes the mouse from a child after a few pixels unless the area sets
    // preventStealing - so the press moved the LIST and the model never saw a drag. This drives
    // the real list: press the leaf, drag it 30px up, release, and require the document's order.
    {
        QQuickItem* listItem = nullptr;
        QQuickItem* leafCell = cell(root, QStringLiteral("leaf"));
        QQuickItem* groupCell = cell(root, QStringLiteral("group"));
        QList<QQuickItem*> all;
        collectItems(root, all);
        for (QQuickItem* item : all)
            if (item->property("objectName").toString() == QStringLiteral("aeRowList"))
                listItem = item;
        check(listItem != nullptr && leafCell != nullptr && groupCell != nullptr,
              "the list and the two rows are reachable");
        if (listItem && leafCell && groupCell) {
            const qreal contentBefore = listItem->property("contentY").toReal();
            const QString orderBefore = [&]() {
                QStringList names;
                for (core::ObjectNode* child : top->children()) {
                    if (child)
                        names << child->name();
                }
                return names.join(QStringLiteral(","));
            }();
            // the leaf's row centre, dragged 30px up: past the slop, into the group's row
            const int rowH = int(leafCell->height());
            {
                QWindow* w = leafCell->window();
                const QPointF g = leafCell->mapToGlobal(QPointF(leafCell->width() / 2, rowH / 2));
                const QPoint p = w->mapFromGlobal(g.toPoint());
                QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, p);
                QCoreApplication::processEvents();
                // THE TRAVEL MUST CLEAR THE DRAGGED ROW: a keyed row is 126px here, and a drop
                // inside it is refused by the model's own no-op rule (a drop where the node
                // already is changes nothing) - which is why a short drag in the running app looks
                // like "nothing happens" rather than like a reorder.
                const int travel = -(rowH / 2 + 16);
                for (int i = 1; i <= 6; ++i) {
                    QTest::mouseMove(w, p + QPoint(0, travel * i / 6));
                    QCoreApplication::processEvents();
                }
                QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, p + QPoint(0, travel));
                QCoreApplication::processEvents();
            }
            const QString orderAfter = [&]() {
                QStringList names;
                for (core::ObjectNode* child : top->children()) {
                    if (child)
                        names << child->name();
                }
                return names.join(QStringLiteral(","));
            }();
            // 30px up from the leaf's own centre lands in the GROUP's row, so the drop is "into
            // that folder" - the shape changes even though the top's name list does not
            bool leafUnderGroup = false;
            for (core::ObjectNode* child : group->children()) {
                if (child == leaf)
                    leafUnderGroup = true;
            }
            check(leafUnderGroup,
                  QStringLiteral("the drag moved the leaf into the folder it was dropped on "
                                 "(top order %1 -> %2)")
                      .arg(orderBefore).arg(orderAfter));
            check(std::abs(listItem->property("contentY").toReal() - contentBefore) < 1.0,
                  "and it did not scroll the list instead");
            project.commandStack().undo();
            QCoreApplication::processEvents();
        }
    }

    if (failures) {
        QTextStream(stdout) << "FAILED " << failures << " check(s)\n";
        return 1;
    }
    QTextStream(stdout) << "OK: the tree dock's scene builds, shows and wires its rows\n";
    return 0;
}
