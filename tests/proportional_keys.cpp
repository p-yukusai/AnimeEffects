// Contract tests for proportional keys (src/ctrl/proportional/prop_Keys.h):
// the authoring-only weights that let a key keep a fractional position between
// its two nearest ABSOLUTE keys of the same core::TimeLine, and the value
// reflow a live gesture runs over them.
//
// What is pinned, and why:
//   * findBounds walks the map's own frame order (negative frames included),
//     skips every proportional key, and never answers the key itself;
//   * the frame projection uses the STORED weight: squeezing and restoring a
//     bound's frame repeatedly must return the key to the same frame. The
//     alternative - reading the weight back off the rounded frame - is computed
//     below too, so the test proves the two answers differ instead of
//     re-stating the same arithmetic;
//   * each graphable component follows its own weight; a FLAT pre-edit span
//     freezes a coordinate whose key holds a different value, while a key ON the
//     flat value inherits the time fraction (the authoring order in which every
//     value is still equal when the key is marked); a key outside its bounds
//     extrapolates;
//   * bound identity: ids, not frames or values, decide when weights must be
//     rebased; a one-sided or missing bound is inert;
//   * ValueReflow capture/apply/restore over a real TimeLine, a directly edited
//     proportional key kept exactly as written while its sibling follows,
//     repeated apply being stable, a bound REPLACED mid-gesture rebased once,
//     and several types in one reflow.
//
// Headless: QApplication for the timeline's QObject/translation internals; no
// widgets, no GL, no fixture. The keys are pushed through the production
// core::TimeLine::createPusher path onto a test-owned cmnd::Stack, so the
// document is a real TimeLine holding real keys, and every component is read
// through ctrl::graph::Channel - the accessor the graph itself uses.
#include <QApplication>
#include <QtGlobal>
#include <QVector2D>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "XCAssert.h"
#include "cmnd/Stack.h"
#include "core/BlurKey.h"
#include "core/MoveKey.h"
#include "core/TimeKeyType.h"
#include "core/TimeLine.h"
#include "ctrl/graph/graph_Channel.h"
#include "ctrl/proportional/prop_Keys.h"

// core::Project.cpp (pulled in by the core library) references the GUI
// AudioPlaybackWidget::deserialize only to load a .aemus sidecar, which this
// test never has; ctrl's timeline editor references two GUI symbols that only
// matter on paths this test does not drive. Stub them (the same three stubs
// graph_curve / creation_select_keys use) so the link succeeds without pulling
// the whole gui library in.
#include "gui/AudioPlaybackWidget.h"
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

#include "gui/ObjectTreeWidget.h"
bool gui::obj::Item::isTopNode() const { return false; }
std::pair<core::TimeKey*, core::LayerMesh*> gui::ObjectTreeWidget::getAreaMeshImpl(core::ObjectNode&, int) {
    return { nullptr, nullptr };
}

// The timeline's XC_ASSERT macros go through these two sink pointers; the
// library leaves them undefined, so a test that drives core::TimeLine has to
// provide them (the same definition graph_curve uses, aborting the run).
namespace {

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

namespace prop = ctrl::proportional;
using core::TimeKeyType;
using core::TimeKeyType_Blur;
using core::TimeKeyType_Move;

int failures = 0;

void check(bool aOk, const char* aWhat) {
    if (!aOk) {
        std::printf("FAIL  %s\n", aWhat);
        ++failures;
    }
}

bool nearly(float a, float b, float aEps = 1e-4f) { return std::fabs(a - b) <= aEps; }

// One component of one key, through the graph's one accessor.
float valueOf(const core::TimeKey& aKey, TimeKeyType aType, int aComponent) {
    return ctrl::graph::Channel(aType, aComponent).value(aKey);
}

// Push one key onto the timeline through the production push path
// (TimeLine::createPusher), which is where a real edit installs keys.
core::MoveKey* pushMove(core::TimeLine& aLine, cmnd::Stack& aStack, int aFrame, const QVector2D& aPos) {
    auto* key = new core::MoveKey();
    key->setPos(aPos);
    aStack.push(aLine.createPusher(TimeKeyType_Move, aFrame, key));
    return key;
}

core::BlurKey* pushBlur(core::TimeLine& aLine, cmnd::Stack& aStack, int aFrame, float aBlurX) {
    auto* key = new core::BlurKey();
    key->setBlurX(aBlurX);
    aStack.push(aLine.createPusher(TimeKeyType_Blur, aFrame, key));
    return key;
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // --- 1. findBounds: frame order, proportional keys skipped, negative frames ---
    {
        const int before = failures;
        core::TimeLine line;
        cmnd::Stack stack;

        core::MoveKey* a = pushMove(line, stack, -20, QVector2D(0, 0));
        core::MoveKey* p1 = pushMove(line, stack, -10, QVector2D(1, 1));
        p1->setProportional(true);
        core::MoveKey* p2 = pushMove(line, stack, -5, QVector2D(2, 2));
        p2->setProportional(true);
        core::MoveKey* b = pushMove(line, stack, 0, QVector2D(10, 10));
        core::MoveKey* c = pushMove(line, stack, 5, QVector2D(20, 20));

        const core::TimeLine::MapType& map = line.map(TimeKeyType_Move);
        check(map.size() == 5, "the five pushed keys are in the Move map");
        check(a->id() > 0 && b->id() > 0 && c->id() > 0, "key ids are positive");
        check(a->id() != b->id() && b->id() != c->id() && a->id() != c->id(), "key ids are distinct");

        // Both proportional keys resolve to the same absolute pair; the other
        // proportional key is skipped in both directions.
        prop::Bounds bounds = prop::findBounds(map, *p1);
        check(bounds.left == a, "p1's left bound skips nothing before it");
        check(bounds.right == b, "p1's right bound skips the proportional p2");

        bounds = prop::findBounds(map, *p2);
        check(bounds.left == a, "p2's left bound skips the proportional p1");
        check(bounds.right == b, "p2's right bound is the first absolute key after it");

        // An absolute key is never returned as its own bound.
        bounds = prop::findBounds(map, *b);
        check(bounds.left == a, "b's left bound skips both proportional keys");
        check(bounds.right == c, "b is not its own right bound");

        // Outermost keys see one missing side.
        bounds = prop::findBounds(map, *a);
        check(bounds.left == nullptr, "the first key has no left bound");
        check(bounds.right == b, "the first key's right bound skips the proportional keys");

        bounds = prop::findBounds(map, *c);
        check(bounds.left == b, "the last key's left bound is the previous absolute key");
        check(bounds.right == nullptr, "the last key has no right bound");

        // A key that is NOT in the map still resolves its neighbours by frame,
        // negative frames included.
        core::MoveKey stray;
        stray.setFrame(-7);
        bounds = prop::findBounds(map, stray);
        check(bounds.left == a, "a missing-key frame resolves the earlier absolute key");
        check(bounds.right == b, "and the next absolute key");

        stray.setFrame(-30);
        bounds = prop::findBounds(map, stray);
        check(bounds.left == nullptr, "a frame before every key finds no left bound");
        check(bounds.right == a, "and the first absolute key on the right");

        if (failures == before)
            std::printf("[1] findBounds: frame order, proportional keys skipped, negative frames\n");
    }

    // --- 2. Frame projection and the drift property ---
    // Bounds 0..3 with the proportional key at 1 store wT = 1/3. Squeezing the
    // right bound to 2 projects to round(2/3) = 1; reading the weight back off
    // that rounded frame would store 1/2 and answer 2 once the span is back to
    // 3. The stored weight answers 1 every time, and stays 1/3.
    {
        const int before = failures;
        core::TimeLine line;
        cmnd::Stack stack;

        core::MoveKey* a = pushMove(line, stack, 0, QVector2D(0, 0));
        core::MoveKey* p = pushMove(line, stack, 1, QVector2D(0, 0));
        p->setProportional(true);
        core::MoveKey* b = pushMove(line, stack, 3, QVector2D(0, 0));

        const core::TimeLine::MapType& map = line.map(TimeKeyType_Move);
        const prop::Bounds bounds = prop::findBounds(map, *p);
        check(bounds.left == a && bounds.right == b, "the drift case starts with bounds 0 and 3");
        check(prop::boundsChanged(*p, bounds), "a fresh proportional key has no weights yet");
        prop::rebase(*p, bounds);

        const core::TimeKey::Proportional* state = p->proportionalState();
        check(state != nullptr && state->timeValid, "rebase stores a valid time weight");
        check(nearly(state->timeWeight, 1.0f / 3.0f, 1e-6f), "the time weight is 1/3");

        int frame = -1;
        check(prop::projectedFrame(*p, bounds, &frame), "the frame projects");
        check(frame == 1, "the projected frame is the key's own frame");

        // The key is squeezed and restored five times. Only the span changes;
        // the bound KEY is the same, so no rebase may happen.
        for (int i = 0; i < 5; ++i) {
            check(line.move(TimeKeyType_Move, 3, 2), "the right bound moves to frame 2");
            const prop::Bounds squeezed = prop::findBounds(map, *p);
            check(squeezed.left == a && squeezed.right == b, "the same keys are the bounds after the move");
            check(!prop::boundsChanged(*p, squeezed), "moving a bound's frame is not an identity change");

            frame = -1;
            check(prop::projectedFrame(*p, squeezed, &frame), "the squeezed bounds project");
            check(frame == 1, "1/3 of the 0..2 span still rounds to frame 1");

            // This is the drift the stored weight exists to prevent: an
            // implementation re-deriving from the rounded frame would answer 2.
            check(qRound((1.0f / 2.0f) * 3.0f) == 2, "the re-derived alternative really is 2");

            check(line.move(TimeKeyType_Move, 2, 3), "the right bound returns to frame 3");
            const prop::Bounds restored = prop::findBounds(map, *p);
            check(!prop::boundsChanged(*p, restored), "the restored pair is still the same identity");
            frame = -1;
            check(prop::projectedFrame(*p, restored, &frame), "the restored bounds project");
            check(frame == 1, "the stored weight returns to the original frame");
        }
        check(nearly(p->proportionalState()->timeWeight, 1.0f / 3.0f, 1e-6f),
              "the stored time weight never moved");

        // A second, wider pair: 0 / 3 / 10.
        core::TimeLine wide;
        cmnd::Stack wideStack;
        core::MoveKey* wa = pushMove(wide, wideStack, 0, QVector2D(0, 0));
        core::MoveKey* wp = pushMove(wide, wideStack, 3, QVector2D(0, 0));
        wp->setProportional(true);
        core::MoveKey* wb = pushMove(wide, wideStack, 10, QVector2D(0, 0));

        const core::TimeLine::MapType& wideMap = wide.map(TimeKeyType_Move);
        const prop::Bounds wideBounds = prop::findBounds(wideMap, *wp);
        check(wideBounds.left == wa && wideBounds.right == wb, "the wide drift case starts with bounds 0 and 10");
        prop::rebase(*wp, wideBounds);
        check(nearly(wp->proportionalState()->timeWeight, 0.3f, 1e-6f), "the wide time weight is 0.3");
        frame = -1;
        check(prop::projectedFrame(*wp, wideBounds, &frame), "the wide bounds project");
        check(frame == 3, "the projection lands on the key's authored frame");

        for (int i = 0; i < 3; ++i) {
            check(wide.move(TimeKeyType_Move, 10, 8), "the wide right bound moves to frame 8");
            const prop::Bounds moved = prop::findBounds(wideMap, *wp);
            frame = -1;
            check(prop::projectedFrame(*wp, moved, &frame), "the moved wide bounds project");
            check(frame == 2, "0.3 * 8 rounds to frame 2");

            check(wide.move(TimeKeyType_Move, 8, 10), "the wide right bound returns to frame 10");
            const prop::Bounds back = prop::findBounds(wideMap, *wp);
            frame = -1;
            check(prop::projectedFrame(*wp, back, &frame), "the restored wide bounds project");
            check(frame == 3, "the stored weight still returns to frame 3");
        }

        if (failures == before)
            std::printf("[2] frame projection: stored weight is drift-free across bound moves\n");
    }

    // --- 3. Component projections: per-component weights, the flat-span rules,
    //        extrapolation, and the inert cases ---
    {
        const int before = failures;
        core::TimeLine line;
        cmnd::Stack stack;

        core::MoveKey* a = pushMove(line, stack, 0, QVector2D(0, 10));
        core::MoveKey* p = pushMove(line, stack, 5, QVector2D(50, 20));
        p->setProportional(true);
        core::MoveKey* b = pushMove(line, stack, 10, QVector2D(100, 40));

        const core::TimeLine::MapType& map = line.map(TimeKeyType_Move);
        const prop::Bounds bounds = prop::findBounds(map, *p);
        check(bounds.left == a && bounds.right == b, "the value case has both bounds");
        prop::rebase(*p, bounds);

        const core::TimeKey::Proportional* state = p->proportionalState();
        check(state->valueValid[0] && state->valueValid[1], "both Move components got a weight");
        check(!state->valueValid[2], "Move has no third component");
        check(nearly(state->valueWeight[0], 0.5f, 1e-6f), "the X weight is 0.5");
        check(nearly(state->valueWeight[1], 1.0f / 3.0f, 1e-6f), "the Y weight is 1/3");

        // Projecting at the captured positions is the identity.
        prop::projectValues(*p, bounds);
        check(nearly(valueOf(*p, TimeKeyType_Move, 0), 50.0f), "projecting at capture is the identity on X");
        check(nearly(valueOf(*p, TimeKeyType_Move, 1), 20.0f), "projecting at capture is the identity on Y");

        // Each component reads its own weight against the bound's CURRENT value.
        b->setPos(QVector2D(200, 70));
        prop::projectValues(*p, bounds);
        check(nearly(valueOf(*p, TimeKeyType_Move, 0), 100.0f), "X follows: 0 + 0.5 * 200");
        check(nearly(valueOf(*p, TimeKeyType_Move, 1), 30.0f), "Y follows its own weight: 10 + 1/3 * 60");
        check(nearly(b->pos().x(), 200.0f), "an absolute key is not projected onto");

        // Zero pre-edit span freezes that coordinate only.
        core::TimeLine flat;
        cmnd::Stack flatStack;
        core::MoveKey* fa = pushMove(flat, flatStack, 0, QVector2D(5, 0));
        core::MoveKey* fp = pushMove(flat, flatStack, 5, QVector2D(999, 5));
        fp->setProportional(true);
        core::MoveKey* fb = pushMove(flat, flatStack, 10, QVector2D(5, 10));

        const core::TimeLine::MapType& flatMap = flat.map(TimeKeyType_Move);
        const prop::Bounds flatBounds = prop::findBounds(flatMap, *fp);
        prop::rebase(*fp, flatBounds);
        check(!fp->proportionalState()->valueValid[0], "a zero X span freezes X");
        check(fp->proportionalState()->valueValid[1], "an independent Y span still gets a weight");

        fa->setPos(QVector2D(100, 0));
        fb->setPos(QVector2D(300, 20));
        prop::projectValues(*fp, flatBounds);
        check(nearly(valueOf(*fp, TimeKeyType_Move, 0), 999.0f), "the frozen X is left exactly where it was");
        check(nearly(valueOf(*fp, TimeKeyType_Move, 1), 10.0f), "Y still follows: 0 + 0.5 * 20");

        // Extrapolation: a key outside its bounds keeps the stored fraction, so
        // the projection runs past the bounds instead of clamping onto them.
        core::MoveKey leftEdge;
        leftEdge.setFrame(0);
        leftEdge.setPos(QVector2D(0, 10));
        core::MoveKey rightEdge;
        rightEdge.setFrame(10);
        rightEdge.setPos(QVector2D(100, 40));
        const prop::Bounds edges { &leftEdge, &rightEdge };

        core::MoveKey stray;
        stray.setFrame(20);
        stray.setPos(QVector2D(-100, 30));
        stray.setProportionalState(prop::rebaseState(stray, edges));

        int strayFrame = 0;
        check(prop::projectedFrame(stray, edges, &strayFrame), "the outside key's frame projects");
        check(strayFrame == 20, "wT = 2 extrapolates to frame 20");

        float extrapolated = 0.0f;
        check(prop::projectedValue(stray, edges, 0, &extrapolated), "the outside key's X projects");
        check(nearly(extrapolated, -100.0f), "wV = -1 extrapolates before the left bound");

        check(prop::projectedValue(stray, edges, 1, &extrapolated), "the outside key's Y projects");
        check(nearly(extrapolated, 30.0f), "wV = 2/3 lands inside the Y span");

        rightEdge.setPos(QVector2D(200, 40));
        check(prop::projectedValue(stray, edges, 0, &extrapolated), "the moved span still projects");
        check(nearly(extrapolated, -200.0f), "the same weight, a longer span");

        // A component the type does not have, and a key without both bounds,
        // are both inert (nothing written to the out parameter).
        float untouched = 123.0f;
        check(!prop::projectedValue(stray, edges, 2, &untouched), "a non-existent component does not project");
        check(untouched == 123.0f, "and the out parameter is untouched");

        const prop::Bounds none;
        check(!prop::projectedFrame(stray, none, &strayFrame), "no bounds means no frame projection");
        check(!prop::projectedValue(stray, none, 0, &untouched), "no bounds means no value projection");
        prop::projectValues(stray, none);
        check(nearly(valueOf(stray, TimeKeyType_Move, 0), -100.0f), "no bounds leaves the value alone");

        if (failures == before)
            std::printf("[3] value projection: per-component weights, flat-span rules, extrapolation\n");
    }

    // --- 4. Bound identity: ids, not frames or values, decide rebasing ---
    {
        const int before = failures;
        core::TimeLine line;
        cmnd::Stack stack;

        core::MoveKey* a = pushMove(line, stack, 0, QVector2D(0, 0));
        core::MoveKey* p = pushMove(line, stack, 2, QVector2D(10, 10));
        p->setProportional(true);
        core::MoveKey* b = pushMove(line, stack, 10, QVector2D(20, 20));

        const core::TimeLine::MapType& map = line.map(TimeKeyType_Move);
        const prop::Bounds bounds = prop::findBounds(map, *p);
        check(bounds.left == a && bounds.right == b, "the identity case has bounds a and b");
        check(prop::boundsChanged(*p, bounds), "a fresh proportional key needs a rebase");

        prop::rebase(*p, bounds);
        check(!prop::boundsChanged(*p, bounds), "after the rebase the identity matches");
        check(p->proportionalState()->leftId == a->id(), "the left bound id is stored");
        check(p->proportionalState()->rightId == b->id(), "the right bound id is stored");

        // An absolute key has no state, so it is always "changed".
        check(prop::boundsChanged(*a, bounds), "an absolute key has no state and always needs a rebase");

        // A new key between p and b becomes p's right bound: identity changed.
        core::MoveKey* c = pushMove(line, stack, 5, QVector2D(30, 30));
        const prop::Bounds replaced = prop::findBounds(map, *p);
        check(replaced.left == a && replaced.right == c, "the new key is p's right bound");
        check(prop::boundsChanged(*p, replaced), "a different bound key is an identity change");
        prop::rebase(*p, replaced);
        check(!prop::boundsChanged(*p, replaced), "the rebase adopts the new pair");
        check(p->proportionalState()->rightId == c->id(), "the new right bound id is stored");

        // Moving that bound's FRAME keeps its id: not a rebase trigger.
        check(line.move(TimeKeyType_Move, 5, 6), "the right bound moves to frame 6");
        const prop::Bounds moved = prop::findBounds(map, *p);
        check(moved.left == a && moved.right == c, "the same keys are still the bounds");
        check(!prop::boundsChanged(*p, moved), "moving a bound's frame is not an identity change");

        // Missing bounds: inert, and an empty identity (ids 0/0) is stable.
        core::MoveKey lone;
        lone.setFrame(0);
        const prop::Bounds noBounds;
        lone.setProportionalState(prop::rebaseState(lone, noBounds));
        check(lone.proportionalState() != nullptr, "an empty state still exists");
        check(!lone.proportionalState()->timeValid, "no bounds means no time weight");
        check(!lone.proportionalState()->valueValid[0], "no bounds means no value weight");
        check(!prop::boundsChanged(lone, noBounds), "the empty identity matches no bounds");

        // One-sided bound: the present id is recorded, the key is still inert.
        const prop::Bounds leftOnly { a, nullptr };
        const core::TimeKey::Proportional oneSided = prop::rebaseState(lone, leftOnly);
        check(oneSided.leftId == a->id(), "a one-sided bound records its id");
        check(oneSided.rightId == 0, "the missing side records no id");
        check(!oneSided.timeValid, "a one-sided bound stores no time weight");

        int frame = 0;
        check(!prop::projectedFrame(lone, noBounds, &frame), "an inert key does not project a frame");
        check(frame == 0, "and the out parameter is untouched");

        if (failures == before)
            std::printf("[4] bound identity: ids decide rebasing, missing bounds are inert\n");
    }

    // --- 5. ValueReflow: capture before the edit, apply from fixed weights,
    //        restore the exact pre-edit values and state ---
    {
        const int before = failures;
        core::TimeLine line;
        cmnd::Stack stack;

        core::MoveKey* a = pushMove(line, stack, 0, QVector2D(10, 10));
        core::MoveKey* p = pushMove(line, stack, 5, QVector2D(60, 60));
        p->setProportional(true);
        core::MoveKey* b = pushMove(line, stack, 10, QVector2D(110, 110));

        // The captured pre-edit state is what restore() must answer: p has a
        // proportional marker but no weights yet.
        const core::TimeKey::Proportional baseState = *p->proportionalState();
        check(!baseState.timeValid, "the pre-edit state has no weights");

        prop::ValueReflow reflow;
        check(reflow.empty(), "a fresh reflow is empty");
        reflow.capture(line, TimeKeyType_Move, QVector<core::TimeKey*> { a, b });
        check(!reflow.empty(), "capture finds the proportional key");

        // Capture does not mutate the document; it only fixes the weights it
        // will apply. The key is still unweighted until apply().
        check(!p->proportionalState()->timeValid, "capture does not install the weights");

        // The caller's edit: both bounds move outward.
        a->setPos(QVector2D(20, 20));
        b->setPos(QVector2D(220, 220));

        reflow.apply();
        check(nearly(valueOf(*p, TimeKeyType_Move, 0), 120.0f), "X follows the edited bounds: 20 + 0.5 * 200");
        check(nearly(valueOf(*p, TimeKeyType_Move, 1), 120.0f), "Y follows the edited bounds");
        check(nearly(p->proportionalState()->timeWeight, 0.5f, 1e-6f), "the time weight is the captured 0.5");
        check(p->proportionalState()->leftId == a->id(), "apply adopts the captured pair's ids");

        // Applying the same document state again answers the same values.
        reflow.apply();
        check(nearly(valueOf(*p, TimeKeyType_Move, 0), 120.0f), "a repeated apply is stable on X");
        check(nearly(valueOf(*p, TimeKeyType_Move, 1), 120.0f), "a repeated apply is stable on Y");

        reflow.modify(); // modify() is apply()
        check(nearly(valueOf(*p, TimeKeyType_Move, 0), 120.0f), "modify() is apply()");

        reflow.restore();
        check(nearly(valueOf(*p, TimeKeyType_Move, 0), 60.0f), "restore brings X back");
        check(nearly(valueOf(*p, TimeKeyType_Move, 1), 60.0f), "restore brings Y back");
        check(!p->proportionalState()->timeValid, "restore reinstates the exact base state");
        check(p->proportionalState()->leftId == baseState.leftId, "the base left id is reinstated");
        check(p->proportionalState()->rightId == baseState.rightId, "the base right id is reinstated");

        // The caller's own edits are the caller's to undo; the reflow restores
        // only what it captured.
        check(nearly(a->pos().x(), 20.0f), "restore leaves the caller's bound edit in place");
        check(nearly(b->pos().x(), 220.0f), "restore leaves the other bound edit in place");

        if (failures == before)
            std::printf("[5] ValueReflow: apply projects from fixed weights, restore is exact\n");
    }

    // --- 6. A directly edited proportional key keeps exactly the value it was
    //        given while its proportional sibling follows the edited bounds ---
    {
        const int before = failures;
        core::TimeLine line;
        cmnd::Stack stack;

        core::MoveKey* a = pushMove(line, stack, 0, QVector2D(0, 0));
        core::MoveKey* p1 = pushMove(line, stack, 3, QVector2D(30, 30));
        p1->setProportional(true);
        core::MoveKey* p2 = pushMove(line, stack, 6, QVector2D(60, 60));
        p2->setProportional(true);
        core::MoveKey* b = pushMove(line, stack, 10, QVector2D(100, 100));

        prop::ValueReflow reflow;
        reflow.capture(line, TimeKeyType_Move, QVector<core::TimeKey*> { p1, b });

        // The caller writes p1 directly (it is being dragged) and extends b.
        p1->setPos(QVector2D(35, 35));
        b->setPos(QVector2D(200, 200));

        reflow.apply();
        check(nearly(valueOf(*p1, TimeKeyType_Move, 0), 35.0f), "the edited proportional key keeps its X");
        check(nearly(valueOf(*p1, TimeKeyType_Move, 1), 35.0f), "the edited proportional key keeps its Y");
        check(nearly(valueOf(*p2, TimeKeyType_Move, 0), 120.0f), "its sibling follows: 0 + 0.6 * 200");
        check(nearly(valueOf(*p2, TimeKeyType_Move, 1), 120.0f), "its sibling follows on Y too");

        // The edited key's weight is re-derived from what the caller wrote,
        // not from an earlier projection: 35 / 200.
        check(nearly(p1->proportionalState()->valueWeight[0], 0.175f, 1e-6f),
              "the edited key's weight comes from its new value");
        check(p2->proportionalState()->leftId == a->id(), "the sibling keeps the captured left id");
        check(p2->proportionalState()->rightId == b->id(), "the sibling keeps the captured right id");
        check(nearly(p2->proportionalState()->valueWeight[0], 0.6f, 1e-6f),
              "the sibling keeps the captured weight");

        // Repeated applies are stable for both.
        for (int i = 0; i < 3; ++i) {
            reflow.apply();
            check(nearly(valueOf(*p1, TimeKeyType_Move, 0), 35.0f), "the edited key stays put across applies");
            check(nearly(valueOf(*p2, TimeKeyType_Move, 0), 120.0f), "the sibling stays put across applies");
        }
        check(nearly(p1->proportionalState()->valueWeight[0], 0.175f, 1e-6f),
              "the edited key's weight does not drift either");

        reflow.restore();
        check(nearly(valueOf(*p1, TimeKeyType_Move, 0), 30.0f), "restore brings the edited key back");
        check(nearly(valueOf(*p2, TimeKeyType_Move, 0), 60.0f), "restore brings its sibling back");

        if (failures == before)
            std::printf("[6] ValueReflow: an edited proportional key is preserved, its sibling follows\n");
    }

    // --- 7. A bound REPLACED during the gesture is rebased once from the
    //        key's current position; later applies use that fixed state ---
    {
        const int before = failures;
        core::TimeLine line;
        cmnd::Stack stack;

        core::MoveKey* a = pushMove(line, stack, 0, QVector2D(0, 0));
        core::MoveKey* p = pushMove(line, stack, 5, QVector2D(50, 50));
        p->setProportional(true);
        core::MoveKey* b = pushMove(line, stack, 10, QVector2D(100, 100));

        const core::TimeLine::MapType& map = line.map(TimeKeyType_Move);
        const prop::Bounds preEdit = prop::findBounds(map, *p);
        check(preEdit.left == a && preEdit.right == b, "the replaced-bound case starts with a and b");

        prop::ValueReflow reflow;
        reflow.capture(line, TimeKeyType_Move, QVector<core::TimeKey*> { b });

        // A new absolute key lands between a and p: it becomes p's left bound.
        core::MoveKey* c = pushMove(line, stack, 3, QVector2D(20, 20));

        reflow.apply();
        // Rebased once from p's current position (50) against c..b, so the key
        // stays exactly where it is; its new weight is (50-20)/(100-20).
        check(nearly(valueOf(*p, TimeKeyType_Move, 0), 50.0f), "the replaced-bound key keeps its position");
        check(p->proportionalState()->leftId == c->id(), "the replaced bound's id is adopted");
        check(p->proportionalState()->rightId == b->id(), "the unchanged bound's id is kept");
        check(nearly(p->proportionalState()->valueWeight[0], 0.375f, 1e-6f),
              "the weight was re-derived once from the current position");

        // The bound's value now moves. The stored weight projects p forward; a
        // per-apply re-derivation from p's own position would answer 50 instead.
        b->setPos(QVector2D(300, 300));
        reflow.apply();
        check(nearly(valueOf(*p, TimeKeyType_Move, 0), 125.0f), "the stored weight projects to 20 + 0.375 * 280");
        check(nearly(valueOf(*p, TimeKeyType_Move, 1), 125.0f), "the stored Y weight projects the same way");

        reflow.apply();
        check(nearly(valueOf(*p, TimeKeyType_Move, 0), 125.0f), "a second apply does not re-derive the weight");
        check(nearly(p->proportionalState()->valueWeight[0], 0.375f, 1e-6f),
              "the one-time rebase is what stays stored");

        if (failures == before)
            std::printf("[7] ValueReflow: a replaced bound is rebased once, then the weight is fixed\n");
    }

    // --- 8. Several types in one reflow, and the default key excluded ---
    {
        const int before = failures;
        core::TimeLine line;
        cmnd::Stack stack;

        core::MoveKey* ma = pushMove(line, stack, 0, QVector2D(0, 0));
        core::MoveKey* mp = pushMove(line, stack, 5, QVector2D(50, 50));
        mp->setProportional(true);
        core::MoveKey* mb = pushMove(line, stack, 10, QVector2D(100, 100));

        core::BlurKey* ba = pushBlur(line, stack, 0, 0.0f);
        core::BlurKey* bp = pushBlur(line, stack, 5, 50.0f);
        bp->setProportional(true);
        core::BlurKey* bb = pushBlur(line, stack, 10, 100.0f);

        prop::ValueReflow reflow;
        reflow.capture(line, TimeKeyType_Move, QVector<core::TimeKey*> { ma, mb });
        reflow.capture(line, TimeKeyType_Blur, QVector<core::TimeKey*> { ba, bb });
        check(!reflow.empty(), "one reflow holds both types");

        // Blur has three components. Its X span is non-zero; Y and angle are
        // flat, and the key sits ON those flat values, so they inherit the TIME
        // fraction (1/2) and can follow the first edit of either bound. A key
        // with a DISTINCT value on a flat span still freezes - section 3's 999.
        check(ctrl::graph::Channel::componentCount(TimeKeyType_Blur) == 3, "Blur has three components");
        check(!bp->proportionalState()->timeValid, "capture does not install the weights");
        mb->setPos(QVector2D(200, 200));
        bb->setBlurX(200.0f);

        reflow.apply();
        check(nearly(valueOf(*mp, TimeKeyType_Move, 0), 100.0f), "the Move group follows its bounds");
        check(nearly(valueOf(*bp, TimeKeyType_Blur, 0), 100.0f), "the Blur group follows its bounds");
        check(bp->proportionalState()->valueValid[0], "Blur X has a weight");
        check(bp->proportionalState()->valueValid[1], "Blur Y sits on a flat value and inherits the time fraction");
        check(bp->proportionalState()->valueValid[2], "Blur angle does the same");
        check(nearly(bp->proportionalState()->valueWeight[1], 0.5f, 1e-6f), "the inherited Y fraction is 1/2");
        check(nearly(bp->proportionalState()->valueWeight[2], 0.5f, 1e-6f), "the inherited angle fraction is 1/2");

        reflow.restore();
        check(nearly(valueOf(*mp, TimeKeyType_Move, 0), 50.0f), "restore returns the Move group");
        check(nearly(valueOf(*bp, TimeKeyType_Blur, 0), 50.0f), "restore returns the Blur group");

        // The default key is not an authored position: a proportional default
        // key is never captured.
        core::TimeLine defaultLine;
        cmnd::Stack defaultStack;
        pushMove(defaultLine, defaultStack, 0, QVector2D(0, 0));
        pushMove(defaultLine, defaultStack, 10, QVector2D(10, 10));
        auto* defaultKey = new core::MoveKey();
        defaultKey->setProportional(true);
        defaultLine.grabDefaultKey(TimeKeyType_Move, defaultKey);
        check(defaultLine.defaultKey(TimeKeyType_Move) == defaultKey, "the default key is installed");
        check(defaultKey->isDefaultKey(), "and carries the default frame marker");

        prop::ValueReflow defaultReflow;
        defaultReflow.capture(defaultLine, TimeKeyType_Move, QVector<core::TimeKey*> {});
        check(defaultReflow.empty(), "a proportional default key is not captured");

        if (failures == before)
            std::printf("[8] ValueReflow: multiple type groups, default key excluded\n");
    }

    // --- 9. A key that left the map during the gesture is skipped ---
    {
        const int before = failures;
        core::TimeLine line;
        cmnd::Stack stack;

        core::MoveKey* a = pushMove(line, stack, 0, QVector2D(0, 0));
        core::MoveKey* p = pushMove(line, stack, 5, QVector2D(50, 50));
        p->setProportional(true);
        core::MoveKey* b = pushMove(line, stack, 10, QVector2D(100, 100));

        prop::ValueReflow reflow;
        reflow.capture(line, TimeKeyType_Move, QVector<core::TimeKey*> { a, b });

        // The key at that frame is replaced by another key (a move/copy path):
        // the captured pointer is no longer the map's entry at its frame.
        auto* replacement = new core::MoveKey();
        replacement->setPos(QVector2D(70, 70));
        stack.push(line.createPusher(TimeKeyType_Move, 5, replacement));
        check(line.timeKey(TimeKeyType_Move, 5) == replacement, "the replacement owns the frame");

        b->setPos(QVector2D(200, 200));
        reflow.apply();
        check(nearly(valueOf(*replacement, TimeKeyType_Move, 0), 70.0f), "the replacement is not projected");
        check(nearly(valueOf(*p, TimeKeyType_Move, 0), 50.0f), "the removed key is not projected");

        reflow.restore();
        check(nearly(valueOf(*replacement, TimeKeyType_Move, 0), 70.0f), "restore skips the removed key");

        if (failures == before)
            std::printf("[9] ValueReflow: keys that left the map are skipped\n");
    }

    if (failures == 0)
        std::printf("proportional_keys: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
