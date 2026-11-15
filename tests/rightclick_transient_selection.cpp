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
// The set announces its own changes (S7): ctrl::time::Selection::onChanged fires
// on every mutation that changes the set and on no other, which is what the
// timeline header's chip follows instead of the 200ms reconcile timer it used to
// run. The entries these paths write are KEY-LEVEL (component ==
// ctrl::time::Selection::kKeyLevel): this view selects KEYS, and such an entry
// covers every marker its key owns - the graph panel's finer, per-component
// entries are what the same set carries when the panel writes it.
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
#include <QClipboard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <cstdio>
#include "XCAssert.h"
#include "gui/AudioPlaybackWidget.h"
#include "core/Project.h"
#include "core/ObjectNode.h"
#include "core/TimeLine.h"
#include "core/MoveKey.h"
#include "core/PoseKey.h"
#include "core/FFDKey.h"
#include "core/KeyEasing.h"
#include "ctrl/graph/graph_Curve.h"
#include "core/AbstractCursor.h"
#include "util/Easing.h"
#include "ctrl/TimeLineEditor.h"
#include "cmnd/Stack.h"

// Project.cpp references the GUI AudioPlaybackWidget::deserialize (only to
// load a .aemus sidecar, which this test never has). Stub it so the link
// succeeds -- same as bonekey_regression.
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

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

// A pure data-only edit of a key's easing (the header's easing picker is the
// route that writes this now; the key menus carry no easing entry), minus the
// command plumbing. The selection must survive it.
void editKeyEasingDataOnly(core::TimeKey* aKey) {
    static_cast<core::MoveKey*>(aKey)->data().easing(0).type = util::Easing::Type_Linear;
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

        // a data-only edit, like the one a menu action would fire on the loaned
        // target, notifies the timeline: the loan survives updateKey()
        editKeyEasingDataOnly(keyA);
        CHECK(keyA->data().easing(0).type == util::Easing::Type_Linear);
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
        editKeyEasingDataOnly(keyA);
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

    // --- S7: THE SELECTION ANNOUNCES ITS OWN CHANGES. The set is the one thing
    // EVERY view of the timeline writes (the timeline's own gestures, and the
    // animation graph panel's markers - see ctrl::time::Selection), so a change is
    // NOTICED rather than polled: every mutation that changes the set fires
    // onChanged, and one that changes nothing stays silent. Both halves are the
    // contract the timeline header's chip runs on (it used to poll the set on a
    // 200ms timer), and they are pinned here. The entries this view's paths write
    // are KEY-LEVEL - a timeline selection is a KEY, whatever the panel selects of
    // its components - which is what makes one key one thing on this side.
    {
        int changes = 0;
        // The slot captures this block's local, and the editor is main-scope, so it
        // is dropped again before the block ends (see the disconnect below).
        const util::SlotId changeSlot =
            editor.selection().onChanged.connect([&changes]() { ++changes; });

        // The lane this section drives: one fresh key at frame 12 on the node the
        // row already names (S6 removed both of the original pair).
        auto* keyC = new core::MoveKey();
        stack.push(node.timeLine()->createPusher(core::TimeKeyType_Move, 12, keyC));
        const QPoint posC(frameX(12), laneY);
        editor.endTemporarySelection();
        editor.selection().clear();
        const int afterClear = changes;

        // A right-click on an unselected key lends the selection: a change, and it
        // says so. The entry it wrote is a WHOLE KEY: no component is named, which
        // is the timeline's own form of a selection (the panel's marker entries name
        // one component - see ctrl::time::Selection::kKeyLevel).
        core::TimeLineEvent targets;
        CHECK(editor.selectKeysAt(targets, posC));
        CHECK(singleTargetIs(targets, keyC));
        CHECK(changes > afterClear);
        CHECK(editor.selection().items().size() == 1);
        CHECK(editor.selection().items().front().component == ctrl::time::Selection::kKeyLevel);
        CHECK(editor.selection().items().front().target.pos.index() == 12);

        // A NO-OP is not a change: lending the same key again leaves the set exactly
        // as it was (Selection::set answers false and announces nothing).
        const int afterLend = changes;
        CHECK(editor.selectKeysAt(targets = core::TimeLineEvent(), posC));
        CHECK(changes == afterLend);

        // ...and neither is a timeline notification that changes nothing, which is
        // what updateKey() runs on every data-only edit: refresh() finds every entry
        // exactly where it was.
        editKeyEasingDataOnly(keyC);
        notifyTimelineModified(editor);
        CHECK(changes == afterLend);

        // A key that MOVED is a change: the entry follows it to its new frame.
        CHECK(node.timeLine()->overwriteMove(core::TimeKeyType_Move, 12, 16));
        notifyTimelineModified(editor);
        CHECK(changes > afterLend);
        core::TimeLineEvent followed;
        CHECK(editor.retrieveSelectionTargets(followed));
        CHECK(singleTargetIs(followed, keyC));
        CHECK(followed.targets().front().pos.index() == 16);

        // ...and a key that is GONE is a change too: the entry is dropped.
        const int afterFollow = changes;
        stack.push(node.timeLine()->createRemover(core::TimeKeyType_Move, 16, false));
        notifyTimelineModified(editor);
        CHECK(changes > afterFollow);
        core::TimeLineEvent gone;
        CHECK(!editor.retrieveSelectionTargets(gone));

        // The set answers whether a call changed it, which is the same fact from the
        // caller's side: clearing an empty set changes nothing and says so.
        CHECK(!editor.selection().clear());
        CHECK(editor.selection().empty());

        editor.selection().onChanged.disconnect(changeSlot);
    }

    // ---- ONE CHANNEL'S EASING EDIT NEVER WRITES ITS SIBLINGS ----
    //
    // The picker's commit is ctrl::TimeLineEditor::assignSelectionEasing, and it
    // writes only the COMPONENT the selection names: a graph-panel press selects
    // Move Y, so Move X's param must come out byte-identical. (A whole-key entry -
    // every timeline gesture - is the one case that writes them all, because it
    // names them all.) Undo restores exactly the written component.
    {
        auto* key = new core::MoveKey();
        project.commandStack().push(node.timeLine()->createPusher(core::TimeKeyType_Move, 30, key));

        util::Easing::Param x;
        x.type = util::Easing::Type_Cubic;
        x.range = util::Easing::Range_In;
        x.weight = 0.3f;
        util::Easing::Param y;
        y.type = util::Easing::Type_Elastic;
        y.range = util::Easing::Range_Out;
        y.weight = 0.8f;
        key->data().easing(0) = x;
        key->data().easing(1) = y;

        core::TimeLineEvent::Target target;
        target.node = &node;
        target.pos = core::TimeKeyPos(*node.timeLine(), core::TimeKeyType_Move, 30);
        CHECK(editor.selection().setMarker(target, 1)); // Move Y, not the whole key
        CHECK(editor.selection().items().front().component == 1);

        util::Easing::Param next;
        next.type = util::Easing::Type_Sine;
        next.range = util::Easing::Range_InOut;
        next.weight = 1.0f;
        CHECK(editor.assignSelectionEasing([next](const util::Easing::Param&) { return next; }));

        // THE OTHER CHANNEL IS BYTE-IDENTICAL, all fields included.
        CHECK(key->data().easing(0) == x);
        CHECK(key->data().easing(1) == next);

        // ...and undo restores only what was written.
        bool undone = false;
        project.commandStack().undo(&undone);
        CHECK(undone);
        CHECK(key->data().easing(0) == x);
        CHECK(key->data().easing(1) == y);

        // WHAT THE SELECTION NAMES - the question the picker opens on. A marker
        // answers its own component; several markers answer the sorted union; a
        // whole-key entry answers every component the type stores. (This is what
        // lets openEasingPicker open on Move Y instead of collapsing to component 0,
        // and what tells the chip that the selection spans more than the curve it
        // paints.)
        const QVector<int> onlyY = editor.selection().componentsOf(target, core::MoveKey::kComponents);
        CHECK(onlyY.size() == 1 && onlyY.first() == 1);
        CHECK(editor.selection().addMarker(target, 0));
        const QVector<int> both = editor.selection().componentsOf(target, core::MoveKey::kComponents);
        CHECK(both.size() == 2 && both.first() == 0 && both.at(1) == 1);
        core::TimeLineEvent whole;
        whole.pushTarget(node, core::TimeKeyType_Move, 30);
        editor.selection().set(whole);
        const QVector<int> wholeKey = editor.selection().componentsOf(target, core::MoveKey::kComponents);
        CHECK(wholeKey.size() == 2 && wholeKey.first() == 0 && wholeKey.at(1) == 1);
        // A type with one stored component (Rotate) answers exactly one, and an
        // out-of-range stored count answers none.
        CHECK(editor.selection().componentsOf(target, 1).size() == 1);
        CHECK(editor.selection().componentsOf(target, 0).isEmpty());
        editor.selection().clear();
    }

    // ---- THE QUICK EASINGS REACH POSE/FFD (one shared easing, no graph channel) ----
    //
    // The header's four quick buttons used to go through the ANIMATION GRAPH's
    // action, which walks graph channels: Pose and FFD publish none (their count is
    // 0 there), so the buttons could neither act on those keys nor light for them -
    // and a key with an easing that no button can set is the regression. The route
    // is now the same selection edit as the picker's (assignSelectionEasing), which
    // asks the key type's STORED component count, and the read side answers the
    // shared param through the one bridge. Both halves are pinned here.
    {
        auto* pose = new core::PoseKey();
        auto* ffd = new core::FFDKey();
        project.commandStack().push(node.timeLine()->createPusher(core::TimeKeyType_Pose, 40, pose));
        project.commandStack().push(node.timeLine()->createPusher(core::TimeKeyType_FFD, 41, ffd));

        // The graph channel count is 0 for both (they are not graphable)...
        CHECK(core::easingComponentCount(core::TimeKeyType_Pose) == 0);
        CHECK(core::easingComponentCount(core::TimeKeyType_FFD) == 0);
        // ...but they STORE one shared param, which is what the timeline edits...
        CHECK(core::easingStorageCount(core::TimeKeyType_Pose) == 1);
        CHECK(core::easingStorageCount(core::TimeKeyType_FFD) == 1);
        // ...and the header's read (Curve::easingOf) resolves it, where it used to
        // answer null and leave the quick buttons disabled for these keys.
        CHECK(ctrl::graph::Curve::easingOf(*pose, core::TimeKeyType_Pose, 0) == &pose->data().easing(0));
        CHECK(ctrl::graph::Curve::easingOf(*ffd, core::TimeKeyType_FFD, 0) == &ffd->data().easing(0));

        core::TimeLineEvent both;
        both.pushTarget(node, core::TimeKeyType_Pose, 40);
        both.pushTarget(node, core::TimeKeyType_FFD, 41);
        editor.selection().set(both);

        // A quick TYPE button: every selected component takes the type...
        CHECK(editor.assignSelectionEasing([](const util::Easing::Param& aCurrent) {
            util::Easing::Param next = aCurrent;
            next.type = util::Easing::Type_Quad;
            return next;
        }));
        CHECK(pose->data().easing(0).type == util::Easing::Type_Quad);
        CHECK(ffd->data().easing(0).type == util::Easing::Type_Quad);

        // ...and the Auto button writes the PRESET (Custom with both sides Auto).
        CHECK(editor.assignSelectionEasing([](const util::Easing::Param& aCurrent) {
            util::Easing::Param next = aCurrent;
            next.type = util::Easing::Type_Custom;
            next.handleIn = util::Easing::HandleMode_Auto;
            next.handleOut = util::Easing::HandleMode_Auto;
            return next;
        }));
        for (const util::Easing::Param* param : { &pose->data().easing(0), &ffd->data().easing(0) }) {
            CHECK(param->type == util::Easing::Type_Custom);
            CHECK(param->handleIn == util::Easing::HandleMode_Auto);
            CHECK(param->handleOut == util::Easing::HandleMode_Auto);
        }
        editor.selection().clear();
    }

    // ---- THE CLIPBOARD'S EASING PAYLOAD (ctrl::TimeLineEditor::easingToJson /
    // easingsFromJson, and the paste reader that consumes them) ----
    //
    // A copied key used to carry ONE easing: eType/eRange/eWeight from component 0,
    // so paste broadcast that one curve over every component and a Custom curve's
    // handle MODES (the only place its shape lives now) were lost entirely. The
    // payload is per component, and each entry carries both modes plus the stored
    // cubic. Drive the real reader (getTypesFromCb) with a payload built by the
    // real writer-side codec, and assert the components come back DISTINCT and
    // bit-identical.
    {
        // `util::Easing` is a CLASS, not a namespace, and block-scope `using` of a
        // class member is ill-formed (measured: "using declaration cannot refer to
        // class member"), so every enumerator below is qualified.

        util::Easing::Param x;
        x.type = util::Easing::Type_Custom;
        x.range = util::Easing::Range_Out;
        x.weight = 0.4f;
        x.handleIn = util::Easing::HandleMode_Auto;      // the TO key's arriving side
        x.handleOut = util::Easing::HandleMode_Free;     // the FROM key's leaving side
        x.cubicBezier = { 0.1f, 0.2f, 0.7f, 0.8f };
        util::Easing::Param y;
        y.type = util::Easing::Type_Custom;
        y.range = util::Easing::Range_In;
        y.weight = 0.9f;
        y.handleIn = util::Easing::HandleMode_AutoClamped;
        y.handleOut = util::Easing::HandleMode_Vector;
        y.cubicBezier = { 0.35f, -0.1f, 0.65f, 1.1f };
        CHECK(!(x == y));

        QJsonArray array;
        array.append(ctrl::TimeLineEditor::easingToJson(x));
        array.append(ctrl::TimeLineEditor::easingToJson(y));
        // The codec alone round-trips every field, per component.
        {
            QJsonObject payload;
            payload["eEasings"] = array;
            const QVector<util::Easing::Param> back = ctrl::TimeLineEditor::easingsFromJson(payload, 2);
            CHECK(back.size() == 2);
            CHECK(back.at(0) == x);
            CHECK(back.at(1) == y);
        }

        // ...and through the PASTE READER, which is what a user's copy/paste hits.
        QJsonObject move;
        move["Type"] = QStringLiteral("Move");
        move["Frame"] = 5;
        move["PosX"] = 0.0;
        move["PosY"] = 0.0;
        move["CentreX"] = 0.0;
        move["CentreY"] = 0.0;
        move["Spline"] = QStringLiteral("Linear");
        move["eType"] = QStringLiteral("Custom"); // component 0's legacy view
        move["eRange"] = QStringLiteral("Out");
        move["eWeight"] = 0.4;
        move["eEasings"] = array;
        QJsonObject payload;
        payload["TargetsSize"] = 1;
        payload["Keys"] = QJsonArray{ move };
        QGuiApplication::clipboard()->setText(
            QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));

        QList<core::TimeKey*> pasted = ctrl::TimeLineEditor::getTypesFromCb(project.pointee(), &node);
        CHECK(pasted.size() == 1);
        if (!pasted.isEmpty()) {
            auto* pastedMove = static_cast<core::MoveKey*>(pasted.front());
            CHECK(pastedMove->data().easing(0) == x); // modes, range, weight, cubic
            CHECK(pastedMove->data().easing(1) == y); // the OTHER component is not x
        }
        qDeleteAll(pasted);

        // A LEGACY payload (no per-component array) still pastes: its one easing is
        // broadcast, and its "Auto" name is the RETIRED Auto TYPE - both sides
        // CLAMPED. (The preset's own clamped pair also writes "Auto" for a legacy
        // reader; unclamped Auto remains a deliberate per-side pick.)
        QJsonObject legacy;
        legacy["Type"] = QStringLiteral("Move");
        legacy["Frame"] = 9;
        legacy["PosX"] = 0.0;
        legacy["PosY"] = 0.0;
        legacy["CentreX"] = 0.0;
        legacy["CentreY"] = 0.0;
        legacy["Spline"] = QStringLiteral("Linear");
        legacy["eType"] = QStringLiteral("Auto");
        legacy["eRange"] = QStringLiteral("All");
        legacy["eWeight"] = 1.0;
        QJsonObject legacyPayload;
        legacyPayload["TargetsSize"] = 1;
        legacyPayload["Keys"] = QJsonArray{ legacy };
        QGuiApplication::clipboard()->setText(
            QString::fromUtf8(QJsonDocument(legacyPayload).toJson(QJsonDocument::Compact)));

        QList<core::TimeKey*> legacyPasted = ctrl::TimeLineEditor::getTypesFromCb(project.pointee(), &node);
        CHECK(legacyPasted.size() == 1);
        if (!legacyPasted.isEmpty()) {
            auto* pastedMove = static_cast<core::MoveKey*>(legacyPasted.front());
            for (int component = 0; component < core::MoveKey::kComponents; ++component) {
                const util::Easing::Param& got = pastedMove->data().easing(component);
                CHECK(got.type == util::Easing::Type_Custom);
                CHECK(got.handleIn == util::Easing::kDefaultAutomaticHandleMode);
                CHECK(got.handleOut == util::Easing::kDefaultAutomaticHandleMode);
            }
        }
        qDeleteAll(legacyPasted);
    }

    std::printf("rightclick_transient_selection: all checks passed\n");
    return 0;
}
