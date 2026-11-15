// Contract tests for the "creation selects its output" rule on the timeline.
//
// When new keys are created (any path that pushes a key onto the timeline —
// the property panel's Add-key buttons, the auto-create on editing an unkeyed
// frame, viewport push-ownership, and copy/paste), the current selection must
// be cleared and replaced with exactly the keys that were created. This is the
// selection's contract with every key-creating action.
//
// The rule lives in ctrl::TimeLineEditor::selectCreatedKeys and is driven by
// the project timeline-modified signal: the widget (TimeLineEditorWidget::onTime
// LineModified) calls it on a forward Type_PushKey / Type_CopyKey notification
// and never on an undo. This test wires the same route through a project signal
// so the real production entry points are exercised:
//   - TimeLineUtil::pushNewMoveKey    (the single-key Add path)
//   - TimeLineEditor::pasteCopiedKeys (the within-timeline copy/paste path)
// plus the routing gates (undo / non-creation events must not touch selection).
//
// What the rule WRITES is pinned too (C6): the selection is one set shared with the
// animation graph panel, and every timeline path - this rule included - writes
// KEY-LEVEL entries (component == ctrl::time::Selection::kKeyLevel: the whole key,
// every marker it owns), never the panel's per-marker ones. The set announces its
// own changes (ctrl::time::Selection::onChanged), which is what the timeline
// header's chip follows instead of polling: a creation that replaces the selection
// is announced exactly once, and a repeated or stale event is announced not at all.
//
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
#include "ctrl/TimeLineUtil.h"
#include "cmnd/Stack.h"

// Project.cpp references the GUI AudioPlaybackWidget::deserialize (only to
// load a .aemus sidecar, which this test never has). Stub it so the link
// succeeds -- same as bonekey_regression / rightclick_transient_selection.
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

// The editor the single project-signal slot routes notifications to. Set to the
// section's editor before driving a notification; avoids dangling references
// and duplicate slots across sections.
ctrl::TimeLineEditor* gRouteEditor = nullptr;

// True when the event carries exactly one target and it resolves to aKey.
bool singleTargetIs(core::TimeLineEvent& aEvent, const core::TimeKey* aKey) {
    return aEvent.targets().size() == 1 && aEvent.targets().front().pos.key() == aKey;
}

// True when the event carries exactly the given set of targets (by key pointer),
// in any order, and nothing else.
bool targetSetIs(core::TimeLineEvent& aEvent, std::initializer_list<const core::TimeKey*> aKeys) {
    // aKeys.size() is std::initializer_list's size_type (unsigned long) while
    // QVector::size() is qsizetype (signed). Compare in the container's own
    // count type: an initializer list's count always fits in qsizetype, so
    // the conversion is exact and neither side can be negative here.
    const qsizetype keyCount = static_cast<qsizetype>(aKeys.size());
    if (aEvent.targets().size() != keyCount)
        return false;
    for (const core::TimeKey* key : aKeys) {
        bool found = false;
        for (core::TimeLineEvent::Target& t : aEvent.targets()) {
            if (t.pos.key() == key) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

// Exactly what gui::TimeLineEditorWidget::onTimeLineModified does with the
// project's timeline-modified signal, minus the widget repaint: reconcile the
// existing selection, then apply the creation rule to a forward push/copy.
void applyCreationRule(ctrl::TimeLineEditor& aEditor, core::TimeLineEvent& aEvent, bool aUndo) {
    aEditor.updateKey();
    if (!aUndo &&
        (aEvent.type() == core::TimeLineEvent::Type_PushKey || aEvent.type() == core::TimeLineEvent::Type_CopyKey)) {
        aEditor.selectCreatedKeys(aEvent);
    }
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
    core::Project project("creation_selection.anie", animator, nullptr);
    cmnd::Stack stack;

    const int margin = ctrl::TimeLineEditor::kTimeLineMargin;

    // A single project-signal slot that mirrors the widget's routing, driven at
    // whichever editor gRouteEditor points at. One slot, no dangling captures.
    project.onTimeLineModified.connect([](core::TimeLineEvent& ev, bool undo) {
        if (gRouteEditor)
            applyCreationRule(*gRouteEditor, ev, undo);
    });

    // --- C1: selectCreatedKeys replaces the whole selection with the created
    // key, and only it carries the highlight.
    {
        core::TestNode node;
        auto* keyB = new core::MoveKey();
        stack.push(node.timeLine()->createPusher(core::TimeKeyType_Move, 20, keyB));

        ctrl::TimeLineEditor editor;
        editor.setProject(&project);
        editor.pushRow(&node, util::Range(100, 160), false);
        const int laneY = 100 + (160 - 100) / 2;
        const QPoint posB(margin + editor.timeScale().pixelWidth(20), laneY);

        click(editor, posB); // persistent selection {keyB}
        core::TimeLineEvent selected;
        CHECK(editor.retrieveSelectionTargets(selected));
        CHECK(singleTargetIs(selected, keyB));

        auto* newKey = new core::MoveKey();
        stack.push(node.timeLine()->createPusher(core::TimeKeyType_Move, 5, newKey));
        core::TimeLineEvent created;
        created.setType(core::TimeLineEvent::Type_PushKey);
        created.pushTarget(node, core::TimeKeyType_Move, 5);
        editor.selectCreatedKeys(created);

        core::TimeLineEvent after;
        CHECK(editor.retrieveSelectionTargets(after));
        CHECK(singleTargetIs(after, newKey));
        CHECK(newKey->isFocused());
        CHECK(!keyB->isFocused());
    }

    // --- C2: a stale index (an event naming a frame with no key) is filtered
    // out; the current selection is left untouched rather than injecting a
    // null-key entry. A default-key-only payload likewise selects nothing.
    {
        core::TestNode node;
        auto* keyB = new core::MoveKey();
        stack.push(node.timeLine()->createPusher(core::TimeKeyType_Move, 20, keyB));

        ctrl::TimeLineEditor editor;
        editor.setProject(&project);
        editor.pushRow(&node, util::Range(100, 160), false);
        const int laneY = 100 + (160 - 100) / 2;
        const QPoint posB(margin + editor.timeScale().pixelWidth(20), laneY);

        click(editor, posB); // selection {keyB}
        core::TimeLineEvent created;
        created.setType(core::TimeLineEvent::Type_PushKey);
        created.pushTarget(node, core::TimeKeyType_Move, 999); // no key here
        editor.selectCreatedKeys(created);
        core::TimeLineEvent after;
        CHECK(editor.retrieveSelectionTargets(after));
        CHECK(singleTargetIs(after, keyB)); // untouched
        CHECK(keyB->isFocused());

        core::TimeLineEvent defaultOnly;
        defaultOnly.setType(core::TimeLineEvent::Type_PushKey);
        defaultOnly.pushDefaultTarget(node, core::TimeKeyType_Move);
        editor.selectCreatedKeys(defaultOnly);
        CHECK(editor.retrieveSelectionTargets(after = core::TimeLineEvent()));
        CHECK(singleTargetIs(after, keyB)); // still untouched
    }

    // --- C3: the widget's routing gates. A forward push selects the created
    // key; an undo of a push must NOT select; a value-change event must not
    // touch the selection.
    {
        core::TestNode node;
        auto* keyB = new core::MoveKey();
        stack.push(node.timeLine()->createPusher(core::TimeKeyType_Move, 20, keyB));

        ctrl::TimeLineEditor editor;
        gRouteEditor = &editor;
        editor.setProject(&project);
        editor.pushRow(&node, util::Range(100, 160), false);
        const int laneY = 100 + (160 - 100) / 2;
        const QPoint posB(margin + editor.timeScale().pixelWidth(20), laneY);
        click(editor, posB); // selection {keyB}

        // C3a: forward push selects the created key.
        auto* newKey = new core::MoveKey();
        stack.push(node.timeLine()->createPusher(core::TimeKeyType_Move, 5, newKey));
        core::TimeLineEvent push;
        push.setType(core::TimeLineEvent::Type_PushKey);
        push.pushTarget(node, core::TimeKeyType_Move, 5);
        project.onTimeLineModified(push, false);
        core::TimeLineEvent after;
        CHECK(editor.retrieveSelectionTargets(after));
        CHECK(singleTargetIs(after, newKey));

        // C3b: undo of a push must not re-select the pushed key.
        click(editor, posB); // re-establish a stable selection {keyB}
        project.onTimeLineModified(push, true); // undo gate: no selectCreatedKeys
        CHECK(editor.retrieveSelectionTargets(after = core::TimeLineEvent()));
        CHECK(singleTargetIs(after, keyB)); // selection left alone (not the pushed key)

        // C3c: a value-change event must not touch the selection.
        core::TimeLineEvent valueChange;
        valueChange.setType(core::TimeLineEvent::Type_ChangeKeyValue);
        valueChange.pushTarget(node, core::TimeKeyType_Move, 20);
        project.onTimeLineModified(valueChange, false);
        CHECK(editor.retrieveSelectionTargets(after = core::TimeLineEvent()));
        CHECK(singleTargetIs(after, keyB));

        gRouteEditor = nullptr;
    }

    // --- C4: the real single-key creation entry point
    // (TimeLineUtil::pushNewMoveKey — what the property panel's Add key button
    // uses) selects the created key via the production signal route.
    {
        core::TestNode node;
        auto* keyB = new core::MoveKey();
        stack.push(node.timeLine()->createPusher(core::TimeKeyType_Move, 20, keyB));

        ctrl::TimeLineEditor editor;
        gRouteEditor = &editor;
        editor.setProject(&project);
        editor.pushRow(&node, util::Range(100, 160), false);
        const int laneY = 100 + (160 - 100) / 2;
        const QPoint posB(margin + editor.timeScale().pixelWidth(20), laneY);
        click(editor, posB); // selection {keyB}

        auto* newKey = new core::MoveKey();
        ctrl::TimeLineUtil::pushNewMoveKey(project, node, 5, newKey);
        core::TimeLineEvent after;
        CHECK(editor.retrieveSelectionTargets(after));
        CHECK(singleTargetIs(after, newKey));
        CHECK(newKey->isFocused());
        CHECK(!keyB->isFocused());

        gRouteEditor = nullptr;
    }

    // --- C5: within-timeline copy/paste (pasteCopiedKeys) selects exactly the
    // created keys at their new frames, replacing a prior selection.
    {
        core::TestNode node;
        auto* keyA = new core::MoveKey();
        auto* keyB = new core::MoveKey();
        stack.push(node.timeLine()->createPusher(core::TimeKeyType_Move, 5, keyA));
        stack.push(node.timeLine()->createPusher(core::TimeKeyType_Move, 20, keyB));

        ctrl::TimeLineEditor editor;
        gRouteEditor = &editor;
        editor.setProject(&project);
        editor.pushRow(&node, util::Range(100, 160), false);
        const int laneY = 100 + (160 - 100) / 2;
        const QPoint posB(margin + editor.timeScale().pixelWidth(20), laneY);
        click(editor, posB); // prior selection {keyB}

        // copy keyA (frame 5) and keyB (frame 20), paste at frame 30
        core::TimeLineEvent copies;
        copies.pushTarget(node, core::TimeKeyType_Move, 5);
        copies.pushTarget(node, core::TimeKeyType_Move, 20);
        const QPoint pastePos(margin + editor.timeScale().pixelWidth(30), laneY);
        CHECK(editor.pasteCopiedKeys(copies, pastePos));

        core::TimeLineEvent after;
        CHECK(editor.retrieveSelectionTargets(after));
        const core::TimeKey* pastedA = node.timeLine()->timeKey(core::TimeKeyType_Move, 30);
        const core::TimeKey* pastedB = node.timeLine()->timeKey(core::TimeKeyType_Move, 45);
        CHECK(pastedA && pastedA != keyA); // a fresh clone landed at frame 30
        CHECK(pastedB && pastedB != keyB); // a fresh clone landed at frame 45
        CHECK(targetSetIs(after, { pastedA, pastedB }));
        CHECK(pastedA->isFocused());
        CHECK(pastedB->isFocused());
        CHECK(!keyA->isFocused());
        CHECK(!keyB->isFocused()); // the prior selection was replaced

        gRouteEditor = nullptr;
    }

    // --- C6: WHAT THE RULE WRITES, AND WHAT IT ANNOUNCES. The selection is ONE set
    // shared with the animation graph panel (ctrl::time::Selection), and the two
    // views name different things in it: the panel names a MARKER - one COMPONENT of
    // one key - while every timeline path, this rule included, names the WHOLE KEY
    // (component == ctrl::time::Selection::kKeyLevel, which covers every marker its
    // key owns). The set also announces its own changes, which is what the timeline
    // header's chip follows instead of the reconcile timer it used to run: creating
    // keys REPLACES the selection, so it is announced exactly once - and a second
    // application of the same event, the set already being exactly that, is
    // announced not at all.
    {
        core::TestNode node;
        ctrl::TimeLineEditor editor;
        editor.setProject(&project);
        editor.pushRow(&node, util::Range(100, 160), false);
        const int laneY = 100 + (160 - 100) / 2;
        const QPoint posB(margin + editor.timeScale().pixelWidth(20), laneY);

        int changes = 0;
        editor.selection().onChanged.connect([&changes]() { ++changes; });

        auto* keyB = new core::MoveKey();
        stack.push(node.timeLine()->createPusher(core::TimeKeyType_Move, 20, keyB));
        click(editor, posB); // a plain click selects keyB, and says so
        const int afterClick = changes;
        CHECK(afterClick > 0);
        CHECK(editor.selection().items().size() == 1);
        CHECK(editor.selection().items().front().component == ctrl::time::Selection::kKeyLevel);

        // The creation rule replaces the selection with the created key: ONE change,
        // announced once, and the entry it writes is key-level like every other
        // timeline path's.
        auto* newKey = new core::MoveKey();
        stack.push(node.timeLine()->createPusher(core::TimeKeyType_Move, 5, newKey));
        core::TimeLineEvent created;
        created.setType(core::TimeLineEvent::Type_PushKey);
        created.pushTarget(node, core::TimeKeyType_Move, 5);
        editor.selectCreatedKeys(created);
        CHECK(changes == afterClick + 1);
        CHECK(editor.selection().items().size() == 1);
        CHECK(editor.selection().items().front().component == ctrl::time::Selection::kKeyLevel);
        core::TimeLineEvent after;
        CHECK(editor.retrieveSelectionTargets(after));
        CHECK(singleTargetIs(after, newKey));

        // The same event again is not a change - the set already IS that one key - and
        // a stale event (a frame that holds no key) leaves the selection alone and
        // silent, exactly as the rule above requires.
        editor.selectCreatedKeys(created);
        CHECK(changes == afterClick + 1);
        core::TimeLineEvent stale;
        stale.setType(core::TimeLineEvent::Type_PushKey);
        stale.pushTarget(node, core::TimeKeyType_Move, 999);
        editor.selectCreatedKeys(stale);
        CHECK(changes == afterClick + 1);
        CHECK(editor.retrieveSelectionTargets(after = core::TimeLineEvent()));
        CHECK(singleTargetIs(after, newKey));
    }

    // --- C7: A ROW REBUILD PRUNES ONLY WHAT IT NO LONGER SHOWS. Expand/collapse is a
    // view change: the selection's identity is (TimeLine map, type, frame), which a
    // row rebuild does not touch, so it must survive. A collapsed folder's summary lane
    // still draws its descendants' keys - those stay selected - while rows that vanish
    // completely (the root collapsed) take their keys with them.
    {
        core::TestNode parent;
        core::TestNode child;
        core::TestNode leaf;
        parent.children().pushBack(&child);

        auto* childKey = new core::MoveKey();
        stack.push(child.timeLine()->createPusher(core::TimeKeyType_Move, 10, childKey));
        auto* leafKey = new core::MoveKey();
        stack.push(leaf.timeLine()->createPusher(core::TimeKeyType_Move, 20, leafKey));

        ctrl::TimeLineEditor editor;
        editor.setProject(&project);

        core::TimeLineEvent selected;
        selected.pushTarget(child, core::TimeKeyType_Move, 10);
        selected.pushTarget(leaf, core::TimeKeyType_Move, 20);
        CHECK(editor.selection().set(selected));

        // The folder is collapsed, but its summary row represents `child`; `leaf` has
        // its own row. Both entries stay, and neither key is re-resolved.
        editor.pushRow(&parent, util::Range(100, 160), true);
        editor.pushRow(&leaf, util::Range(160, 200), false);
        CHECK(!editor.retainSelectionInRows());
        CHECK(editor.selection().items().size() == 2);
        CHECK(editor.selection().items().front().key == childKey);
        CHECK(editor.selection().items().back().key == leafKey);

        // The folder's row is gone (the root collapsed): only the leaf's key survives.
        editor.resetRows();
        editor.pushRow(&leaf, util::Range(160, 200), false);
        CHECK(editor.retainSelectionInRows());
        CHECK(editor.selection().items().size() == 1);
        CHECK(editor.selection().items().front().key == leafKey);

        // Nothing visible: the set empties, and the notification fires.
        editor.resetRows();
        CHECK(editor.retainSelectionInRows());
        CHECK(editor.selection().empty());
    }

    std::printf("creation_select_keys: all checks passed\n");
    return 0;
}
