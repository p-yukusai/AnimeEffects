// Contract tests for right-click targeting and selection survival on the
// timeline.
//
// Right-click model (ctrl::TimeLineEditor::selectKeysAt):
//  - Right-clicking an UNSELECTED key clears the current selection and lends
//    the clicked key a temporary selection for the lifetime of the context
//    menu: it shows the highlight and every menu action consumes it. The
//    previous selection is NOT restored; endTemporarySelection() drops the
//    temporary selection (leaving nothing selected) once the menu closes.
//  - Right-clicking an ALREADY-selected key operates on the whole selection
//    and lends nothing.
//
// Selection survival (ctrl::TimeLineEditor::updateKey -> Selection::refresh):
// timeline notifications must not cancel the selection. Data-only edits
// (easing type/range, HSV, ...) keep every entry; moved keys are followed;
// removed keys drop out.
//
// Plus the context-menu coordinate contract: rows live in model space and
// customContextMenuRequested delivers surface coordinates, so the widget
// maps through CameraInfo::toWorldPos before calling selectKeysAt (the
// original bug fed raw surface coordinates -- every key silently missed
// whenever the view was scrolled).
//
// Drives the real ctrl::TimeLineEditor gesture path: a synthetic left click
// (AbstractCursor fed QMouseEvents) establishes a persistent selection.
// Headless: needs QApplication for QSettings/QMap internals, no GL.
#include <QApplication>
#include <QMouseEvent>
#include <cstdio>
#include "XCAssert.h"
#include "gui/AudioPlaybackWidget.h"
#include "core/Project.h"
#include "core/ObjectNode.h"
#include "core/TimeLine.h"
#include "core/MoveKey.h"
#include "core/AbstractCursor.h"
#include "util/Easing.h"
#include "ctrl/TimeLineEditor.h"
#include "cmnd/Stack.h"

// Project.cpp references the GUI AudioPlaybackWidget::deserialize (only to
// load a .aemus sidecar, which this test never has). Stub it so the link
// succeeds -- same as bonekey_regression.
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

// ctrl::TimeLineEditor's clipboard-paste helpers reference two GUI symbols
// that only matter on the paste path this test never exercises. Stub them
// (same pattern) instead of linking the whole gui library.
#include "gui/ObjectTreeWidget.h"
bool gui::obj::Item::isTopNode() const { return false; }
std::pair<core::TimeKey*, core::LayerMesh*> gui::ObjectTreeWidget::getAreaMeshImpl(core::ObjectNode&, int) {
    return { nullptr, nullptr };
}

namespace {

class NullAnimator : public core::Animator {
public:
    core::Frame currentFrame() const override { return core::Frame(); }
    void stop() override {}
    void suspend() override {}
    void resume() override {}
    bool isSuspended() const override { return false; }
};

class HarnessAssertHandler : public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};
class HarnessErrorHandler : public XCErrorHandler {
public:
    void critical(const QString&, const QString&, const QString&) const override { std::abort(); }
};
HarnessAssertHandler gAssertHandler;
HarnessErrorHandler gErrorHandler;

#define CHECK(cond)                                                                                    \
    do {                                                                                               \
        if (!(cond)) {                                                                                 \
            std::printf("FAIL %d: %s\n", __LINE__, #cond);                                             \
            return 1;                                                                                  \
        }                                                                                              \
    } while (false)

// True when the event carries exactly one target and it resolves to aKey.
bool singleTargetIs(core::TimeLineEvent& aEvent, const core::TimeKey* aKey) {
    return aEvent.targets().size() == 1 && aEvent.targets().front().pos.key() == aKey;
}

// What the widget does on every timeline-modified notification.
void notifyTimelineModified(ctrl::TimeLineEditor& aEditor) { aEditor.updateKey(); }

// What the context-menu easing action does to a key's data, minus the
// command plumbing: a pure data-only edit.
void editEasingLikeTheMenuWould(core::TimeKey* aKey) {
    static_cast<core::MoveKey*>(aKey)->data().easing().type = util::Easing::Type_Linear;
}

// A plain click through the real gesture path (press + release at one point).
void click(ctrl::TimeLineEditor& aEditor, const QPoint& aPos) {
    core::CameraInfo camera;
    core::AbstractCursor cursor;
    QMouseEvent press(
        QEvent::MouseButtonPress, aPos, aPos, aPos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier
    );
    if (cursor.setMousePress(&press, camera))
        aEditor.updateCursor(cursor, Qt::NoModifier);
    QMouseEvent release(
        QEvent::MouseButtonRelease, aPos, aPos, aPos, Qt::LeftButton, Qt::NoButton, Qt::NoModifier
    );
    if (cursor.setMouseRelease(&release, camera))
        aEditor.updateCursor(cursor, Qt::NoModifier);
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

namespace core {
// Minimal ObjectNode subclass owning a real TimeLine (ObjectNode::timeLine()
// is virtual and null by default). No meshes, no resources -- just key maps.
class TestNode : public ObjectNode {
    TimeLine mTimeLine;
public:
    ObjectType type() const override { return ObjectType_Layer; }
    void setName(const QString&) override {}
    const QString& name() const override { static QString s; return s; }
    float initialDepth() const override { return 0; }
    void setVisibility(bool) override {}
    bool isVisible() const override { return true; }
    void setSlimDown(bool) override {}
    bool isSlimmedDown() const override { return false; }
    bool canHoldChild() const override { return false; }
    void setInitialRect(const QRect&) override {}
    QRect initialRect() const override { return QRect(); }
    bool serialize(Serializer&) const override { return true; }
    bool deserialize(Deserializer&) override { return true; }
    TimeLine* timeLine() override { return &mTimeLine; }
    const TimeLine* timeLine() const override { return &mTimeLine; }
};
} // namespace core

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    NullAnimator animator;
    core::Project project("rightclick_selection.anie", animator, nullptr);
    cmnd::Stack stack;

    core::TestNode node;
    auto* keyA = new core::MoveKey();
    auto* keyB = new core::MoveKey();
    stack.push(node.timeLine()->createPusher(core::TimeKeyType_Move, 5, keyA));
    stack.push(node.timeLine()->createPusher(core::TimeKeyType_Move, 20, keyB));
    CHECK(node.timeLine()->validTypeCount() == 1);

    ctrl::TimeLineEditor editor;
    editor.setProject(&project);
    // One row below the ruler strip (y >= kHeaderHeight); a single valid key
    // type renders one lane centered vertically in the row rect. The row is
    // pushed at model y = 100..160 -- as updateLinesRecursive does once the
    // view is scrolled down.
    editor.pushRow(&node, util::Range(100, 160), false);

    const int margin = ctrl::TimeLineEditor::kTimeLineMargin;
    const int laneY = 100 + (160 - 100) / 2;
    auto frameX = [&](int aFrame) { return margin + editor.timeScale().pixelWidth(aFrame); };
    const QPoint posA(frameX(5), laneY);
    const QPoint posB(frameX(20), laneY);
    const QPoint posEmpty(frameX(300), laneY);

    // --- S1: nothing selected + right-click a key = a temporary selection on
    // that key for the menu. Afterwards: back to nothing.
    {
        core::TimeLineEvent targets;
        CHECK(editor.selectKeysAt(targets, posA));
        CHECK(singleTargetIs(targets, keyA)); // menu actions consume the loan
        CHECK(keyA->isFocused());             // the highlight shows...

        // menu closes: the temporary selection is dropped, nothing selected
        editor.endTemporarySelection();
        core::TimeLineEvent after; // fresh: retrieveSelectionTargets appends
        CHECK(!editor.retrieveSelectionTargets(after));
        CHECK(!keyA->isFocused());
        CHECK(!keyB->isFocused());
    }

    // --- S2: right-clicking an unselected key clears the previous selection
    // and lends the clicked key a temporary selection for the menu; the
    // previous selection is NOT restored once the menu closes -- even though
    // a menu action ran on the loaned key in between.
    {
        click(editor, posB); // persistent selection {keyB}
        core::TimeLineEvent selected;
        CHECK(editor.retrieveSelectionTargets(selected));
        CHECK(singleTargetIs(selected, keyB));

        core::TimeLineEvent targets; // fresh, like the widget's mTargets reset
        CHECK(editor.selectKeysAt(targets, posA)); // right-click unselected keyA
        CHECK(singleTargetIs(targets, keyA));      // the menu operates on keyA
        CHECK(keyA->isFocused());                  // ...which alone carries the highlight
        CHECK(!keyB->isFocused());

        // a menu action fires on the loaned target and notifies the timeline:
        // the data-only edit keeps the loan alive through updateKey()
        editEasingLikeTheMenuWould(keyA);
        CHECK(keyA->data().easing().type == util::Easing::Type_Linear);
        notifyTimelineModified(editor);
        core::TimeLineEvent during;
        CHECK(editor.retrieveSelectionTargets(during));
        CHECK(singleTargetIs(during, keyA));

        editor.endTemporarySelection(); // menu closed
        // the previous selection ({keyB}) is NOT restored; nothing is left selected
        core::TimeLineEvent restored;
        CHECK(!editor.retrieveSelectionTargets(restored));
        CHECK(!keyA->isFocused());
        CHECK(!keyB->isFocused());
    }

    // --- S3: right-clicking an already-selected key operates on the whole
    // selection and lends nothing -- endTemporarySelection() is a no-op.
    {
        click(editor, posB); // selection {keyB}
        core::TimeLineEvent targets;
        CHECK(editor.selectKeysAt(targets, posB));
        CHECK(singleTargetIs(targets, keyB));

        editor.endTemporarySelection(); // already-selected key lends nothing
        core::TimeLineEvent after;
        CHECK(editor.retrieveSelectionTargets(after));
        CHECK(singleTargetIs(after, keyB));
        CHECK(keyB->isFocused());
    }

    // --- S4: missing the keys reports false and leaves the selection alone.
    {
        core::TimeLineEvent targets;
        CHECK(!editor.selectKeysAt(targets, posEmpty));

        editor.endTemporarySelection(); // no temporary loan to drop
        core::TimeLineEvent after;
        CHECK(editor.retrieveSelectionTargets(after));
        CHECK(singleTargetIs(after, keyB));
    }

    // --- S5: the context-menu coordinate space. The timeline camera is
    // configured exactly like TimeLineWidget::updateCamera (scale 1,
    // leftTopPos = viewportTransform, negative when scrolled); the widget
    // maps the surface-coordinate right-click through toWorldPos before
    // calling selectKeysAt. A scrolled view must still hit the key under
    // the mouse -- the raw surface coordinate would not.
    {
        core::CameraInfo camera;
        camera.setScreenWidth(800);
        camera.setScreenHeight(300);
        camera.setScale(1.0f);
        camera.setLeftTopPos(QVector2D(-120, -80)); // scrolled right + down

        const QPoint screenClick = camera.toScreenPos(QVector2D(posA)).toPoint();
        const QPoint mapped = camera.toWorldPos(QPointF(screenClick)).toPoint();
        CHECK(mapped == posA);

        core::TimeLineEvent targets;
        CHECK(editor.selectKeysAt(targets, mapped));
        CHECK(singleTargetIs(targets, keyA));

        // the unmapped surface coordinate demonstrably misses (the original bug)
        core::TimeLineEvent missed;
        CHECK(!editor.selectKeysAt(missed, screenClick));
        editor.endTemporarySelection(); // close S5's temporary loan so nothing leaks into S6
    }

    // --- S6: a TRUE selection survives timeline notifications: data-only
    // edits keep it, moves are followed, removals drop out.
    {
        click(editor, posA); // persistent selection {keyA}
        core::TimeLineEvent selected;
        CHECK(editor.retrieveSelectionTargets(selected));
        CHECK(singleTargetIs(selected, keyA));

        // benign notification: nothing changed, nothing lost
        notifyTimelineModified(editor);
        core::TimeLineEvent kept;
        CHECK(editor.retrieveSelectionTargets(kept));
        CHECK(singleTargetIs(kept, keyA));
        CHECK(keyA->isFocused());

        // data-only edit (an easing change): the selection must be kept
        editEasingLikeTheMenuWould(keyA);
        notifyTimelineModified(editor);
        CHECK(editor.retrieveSelectionTargets(kept = core::TimeLineEvent()));
        CHECK(singleTargetIs(kept, keyA));
        CHECK(keyA->isFocused());

        // a move: the selection follows the key to its new frame
        CHECK(node.timeLine()->overwriteMove(core::TimeKeyType_Move, 5, 8));
        notifyTimelineModified(editor);
        CHECK(editor.retrieveSelectionTargets(kept = core::TimeLineEvent()));
        CHECK(singleTargetIs(kept, keyA));
        CHECK(kept.targets().front().pos.index() == 8);

        // removing another key must not disturb the selection
        stack.push(node.timeLine()->createRemover(core::TimeKeyType_Move, 20, false));
        notifyTimelineModified(editor);
        CHECK(editor.retrieveSelectionTargets(kept = core::TimeLineEvent()));
        CHECK(singleTargetIs(kept, keyA));

        // removing the selected key drops it from the selection
        stack.push(node.timeLine()->createRemover(core::TimeKeyType_Move, 8, false));
        notifyTimelineModified(editor);
        core::TimeLineEvent gone;
        CHECK(!editor.retrieveSelectionTargets(gone)); // the drop is the contract
    }

    std::printf("rightclick_transient_selection: all checks passed\n");
    return 0;
}
