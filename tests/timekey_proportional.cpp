// Contract tests for proportional keys at the core::TimeLine/TimeKey layer:
// stable key ids, the reserveId counter, the proportional state toggle, and the
// atomic multi-key move the frame projection drives.
//
// What is pinned, and why:
//   1 every constructed key gets a positive, unique id (the id is how a
//     proportional key names its bounds, so duplicates would silently bind to
//     the wrong key);
//   2 reserveId raises the counter above a stored id and ignores ids <= 0
//     (older files store none, and a 0 keeps meaning "no bound");
//   3 setProportional(true) is idempotent and preserves the assigned state;
//     false clears; setProportionalState enables and copies every field;
//   4 TimeLine::moveKeys validates the WHOLE batch first: a single move, a
//     swap, a destination vacated by the same batch, negative frames, and the
//     refusals (occupied destination, duplicate source/destination, missing
//     source) which must leave every key at its frame.
//
// Headless: QApplication for the timeline's QObject/translation internals; the
// keys are pushed through the production createPusher path onto a test-owned
// cmnd::Stack, so the document is a real TimeLine holding real keys.
#include <QApplication>
#include <QPair>
#include <QVector>
#include <cstdio>
#include <initializer_list>

#include "cmnd/Stack.h"
#include "core/DepthKey.h"
#include "core/MoveKey.h"
#include "core/OpaKey.h"
#include "core/TimeKey.h"
#include "core/TimeKeyType.h"
#include "core/TimeLine.h"
#include "XC.h"

// core::Project.cpp references the GUI's audio sidecar loader; this test never
// has a sidecar, so stub it (the same stub the other TimeLine tests use).
#include "gui/AudioPlaybackWidget.h"
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

namespace {

// The libraries leave the assert sinks undefined; a test that drives the real
// TimeLine supplies them (the same pair proportional_keys.cpp uses).
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

int failures = 0;

void check(bool aOk, const char* aWhat) {
    if (!aOk) {
        std::printf("FAIL  %s\n", aWhat);
        ++failures;
    }
}

// Push one key onto the timeline through the production push path.
core::MoveKey* pushMove(core::TimeLine& aLine, cmnd::Stack& aStack, int aFrame) {
    auto* key = new core::MoveKey();
    aStack.push(aLine.createPusher(core::TimeKeyType_Move, aFrame, key));
    return key;
}

// True when one type's map holds exactly aFrame -> aKey and every key's stored
// frame agrees with its map frame.
bool mappingIs(core::TimeLine& aLine, core::TimeKeyType aType,
    std::initializer_list<QPair<int, core::TimeKey*>> aExpected) {
    const core::TimeLine::MapType& map = aLine.map(aType);
    if (map.size() != static_cast<int>(aExpected.size())) {
        return false;
    }
    for (const auto& entry : aExpected) {
        if (map.value(entry.first) != entry.second) {
            return false;
        }
        if (entry.second->frame() != entry.first) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// 1. ids: positive and unique across constructed keys.
// ---------------------------------------------------------------------------
void sectionIds() {
    core::MoveKey a;
    core::MoveKey b;
    core::OpaKey c;
    core::DepthKey d;

    check(a.id() > 0 && b.id() > 0 && c.id() > 0 && d.id() > 0, "every constructed key gets a positive id");
    check(a.id() != b.id(), "two keys of one type get different ids");
    check(a.id() != c.id() && a.id() != d.id() && c.id() != d.id(), "ids are unique across key types");

    std::printf("[1] ids: move %d/%d, opa %d, depth %d (all positive)\n", a.id(), b.id(), c.id(), d.id());
}

// ---------------------------------------------------------------------------
// 2. reserveId raises the counter, ignores non-positive ids.
// ---------------------------------------------------------------------------
void sectionReserveId() {
    core::MoveKey probe;
    const int reserved = probe.id() + 1000;

    core::TimeKey::reserveId(reserved);
    core::MoveKey after;
    check(after.id() > reserved, "a key constructed after reserveId is greater than the reserved id");

    // A non-positive value is not a stored id, so it must not move the counter.
    core::TimeKey::reserveId(0);
    core::TimeKey::reserveId(-7);
    core::MoveKey next;
    check(next.id() == after.id() + 1, "reserveId(0) and reserveId(-7) leave the counter where it was");

    // A lower id must not lower the counter either.
    core::TimeKey::reserveId(1);
    core::MoveKey last;
    check(last.id() == next.id() + 1, "reserveId with a lower id leaves the counter where it was");

    std::printf("[2] reserveId: reserved %d -> next %d, then %d, %d\n", reserved, after.id(), next.id(),
        last.id());
}

// ---------------------------------------------------------------------------
// 3. the proportional toggle and state copy.
// ---------------------------------------------------------------------------
void sectionProportionalState() {
    core::MoveKey key;
    check(!key.isProportional(), "a fresh key is absolute");
    check(key.proportionalState() == nullptr, "an absolute key stores no proportional state");

    key.setProportional(true);
    check(key.isProportional(), "setProportional(true) enables proportional");
    check(key.proportionalState() != nullptr, "an enabled key stores a state");

    core::TimeKey::Proportional* state = key.proportionalState();
    state->leftId = 41;
    state->rightId = 42;
    state->timeValid = true;
    state->timeWeight = 0.25f;
    state->valueValid[0] = true;
    state->valueWeight[0] = 0.5f;
    state->valueValid[2] = true;
    state->valueWeight[2] = 0.75f;

    key.setProportional(true);
    state = key.proportionalState();
    check(key.isProportional(), "setProportional(true) twice stays enabled");
    check(state != nullptr && state->leftId == 41 && state->rightId == 42, "enabling twice preserves the bound ids");
    check(state != nullptr && state->timeValid && state->timeWeight == 0.25f, "enabling twice preserves the time weight");
    check(state != nullptr && state->valueValid[0] && state->valueWeight[0] == 0.5f, "enabling twice preserves value 0");
    check(state != nullptr && state->valueValid[2] && state->valueWeight[2] == 0.75f, "enabling twice preserves value 2");

    key.setProportional(false);
    check(!key.isProportional(), "setProportional(false) disables proportional");
    check(key.proportionalState() == nullptr, "disabling clears the stored state");

    core::TimeKey::Proportional copy;
    copy.leftId = 11;
    copy.rightId = 12;
    copy.timeValid = true;
    copy.timeWeight = 0.6f;
    copy.valueValid[1] = true;
    copy.valueWeight[1] = 0.4f;

    key.setProportionalState(copy);
    check(key.isProportional(), "setProportionalState enables proportional");
    state = key.proportionalState();
    check(state != nullptr && state->leftId == 11 && state->rightId == 12, "setProportionalState copies the bound ids");
    check(state != nullptr && state->timeValid && state->timeWeight == 0.6f, "setProportionalState copies the time weight");
    check(state != nullptr && !state->valueValid[0] && state->valueValid[1] && state->valueWeight[1] == 0.4f,
        "setProportionalState copies the per-value flags and weights");
    check(state != nullptr && !state->valueValid[2], "setProportionalState copies the cleared value slots");

    std::printf("[3] state: toggle idempotent, disable clears, setState copies\n");
}

// ---------------------------------------------------------------------------
// 4. moveKeys: accepted batches.
// ---------------------------------------------------------------------------
void sectionMoveKeysAccepted() {
    {
        core::TimeLine line;
        cmnd::Stack stack;
        core::MoveKey* a = pushMove(line, stack, 0);
        core::MoveKey* b = pushMove(line, stack, 10);

        check(line.moveKeys(core::TimeKeyType_Move, { { 0, 5 } }), "a single move is accepted");
        check(mappingIs(line, core::TimeKeyType_Move, { { 5, a }, { 10, b } }),
            "the moved key lands at its destination and stores that frame");
    }

    {
        core::TimeLine line;
        cmnd::Stack stack;
        core::MoveKey* a = pushMove(line, stack, 0);
        core::MoveKey* b = pushMove(line, stack, 10);

        check(line.moveKeys(core::TimeKeyType_Move, { { 0, 10 }, { 10, 0 } }), "a swap of two frames is accepted");
        check(mappingIs(line, core::TimeKeyType_Move, { { 0, b }, { 10, a } }),
            "a swap exchanges the two keys and their stored frames");
    }

    {
        core::TimeLine line;
        cmnd::Stack stack;
        core::MoveKey* a = pushMove(line, stack, 0);
        core::MoveKey* b = pushMove(line, stack, 10);

        check(line.moveKeys(core::TimeKeyType_Move, { { 0, 10 }, { 10, 20 } }),
            "a destination vacated by the same batch is accepted");
        check(mappingIs(line, core::TimeKeyType_Move, { { 10, a }, { 20, b } }),
            "chained moves land at their destinations regardless of listing order");
    }

    {
        core::TimeLine line;
        cmnd::Stack stack;
        core::MoveKey* a = pushMove(line, stack, -10);
        core::MoveKey* b = pushMove(line, stack, -5);

        check(line.moveKeys(core::TimeKeyType_Move, { { -10, -20 }, { -5, -10 } }),
            "negative frames are ordinary frames");
        check(mappingIs(line, core::TimeKeyType_Move, { { -20, a }, { -10, b } }),
            "a negative destination below the existing keys is accepted");
    }

    {
        core::TimeLine line;
        cmnd::Stack stack;
        core::MoveKey* move = pushMove(line, stack, 0);
        auto* opa = new core::OpaKey();
        stack.push(line.createPusher(core::TimeKeyType_Opa, 10, opa));

        check(line.moveKeys(core::TimeKeyType_Move, { { 0, 10 } }),
            "another type's key does not occupy a move destination");
        check(mappingIs(line, core::TimeKeyType_Move, { { 10, move } }), "the move landed on the shared frame");
        check(line.timeKey(core::TimeKeyType_Opa, 10) == opa, "the other type's key was not disturbed");
    }

    std::printf("[4] moveKeys: single, swap, chained, negative, and type isolation accepted\n");
}

// ---------------------------------------------------------------------------
// 5. moveKeys: every refusal is atomic.
// ---------------------------------------------------------------------------
void sectionMoveKeysRefused() {
    {
        core::TimeLine line;
        cmnd::Stack stack;
        core::MoveKey* a = pushMove(line, stack, 0);
        core::MoveKey* b = pushMove(line, stack, 10);

        check(!line.moveKeys(core::TimeKeyType_Move, { { 0, 10 } }),
            "a destination occupied by a non-moving key is refused");
        check(mappingIs(line, core::TimeKeyType_Move, { { 0, a }, { 10, b } }),
            "a refused single move leaves every key at its frame");
    }

    {
        core::TimeLine line;
        cmnd::Stack stack;
        core::MoveKey* a = pushMove(line, stack, 0);
        core::MoveKey* b = pushMove(line, stack, 10);
        core::MoveKey* c = pushMove(line, stack, 20);

        check(!line.moveKeys(core::TimeKeyType_Move, { { 0, 5 }, { 10, 20 } }),
            "one occupied destination refuses the whole batch");
        check(mappingIs(line, core::TimeKeyType_Move, { { 0, a }, { 10, b }, { 20, c } }),
            "a refused batch leaves the free destination untouched too");
    }

    {
        core::TimeLine line;
        cmnd::Stack stack;
        core::MoveKey* a = pushMove(line, stack, 0);
        core::MoveKey* b = pushMove(line, stack, 10);

        check(!line.moveKeys(core::TimeKeyType_Move, { { 0, 5 }, { 0, 6 } }),
            "two moves sharing a source are refused");
        check(mappingIs(line, core::TimeKeyType_Move, { { 0, a }, { 10, b } }),
            "a duplicate source leaves every key at its frame");
    }

    {
        core::TimeLine line;
        cmnd::Stack stack;
        core::MoveKey* a = pushMove(line, stack, 0);
        core::MoveKey* b = pushMove(line, stack, 10);

        check(!line.moveKeys(core::TimeKeyType_Move, { { 0, 5 }, { 10, 5 } }),
            "two moves sharing a destination are refused");
        check(mappingIs(line, core::TimeKeyType_Move, { { 0, a }, { 10, b } }),
            "a duplicate destination leaves every key at its frame");
    }

    {
        core::TimeLine line;
        cmnd::Stack stack;
        core::MoveKey* a = pushMove(line, stack, 0);
        core::MoveKey* b = pushMove(line, stack, 10);

        check(!line.moveKeys(core::TimeKeyType_Move, { { 0, 5 }, { 7, 8 } }),
            "a batch naming a missing source is refused");
        check(mappingIs(line, core::TimeKeyType_Move, { { 0, a }, { 10, b } }),
            "a missing source leaves every key at its frame");
    }

    std::printf("[5] moveKeys: occupied, duplicate source/destination, and missing source are atomic refusals\n");
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    // QApplication, not QCoreApplication: the timeline and the key types are
    // QObject-based and QObject::tr is what the channel labels go through.
    QApplication app(argc, argv);

    sectionIds();
    sectionReserveId();
    sectionProportionalState();
    sectionMoveKeysAccepted();
    sectionMoveKeysRefused();

    if (failures == 0) {
        std::printf("timekey_proportional: ids, reserveId, the proportional toggle and atomic moveKeys hold\n");
    } else {
        std::printf("timekey_proportional: %d FAILED\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
