// Round-trip gate for the 0.16 key record: a key's stable id and its
// proportional bound ids/weights must survive TimeLine::serialize ->
// deserialize, and the resolved frame/values must come back byte-for-byte.
//
// The weights are the document's authored data (not re-derived from the rounded
// positions), so losing them on save would silently turn every proportional key
// into an absolute one on the next open. The test deliberately writes a state
// whose weights do NOT match a re-derivation from its current frame (0.375
// instead of the 0.5 its position implies) - a reader that recomputed rather
// than restored would fail.
//
// Headless: no widgets, no GL; the stream is an in-memory std::stringstream.
#include <QApplication>
#include <QJsonObject>
#include <QVersionNumber>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <vector>

#include "XC.h"
#include "cmnd/Stack.h"
#include "core/Deserializer.h"
#include "core/MoveKey.h"
#include "core/Serializer.h"
#include "core/TimeKeyType.h"
#include "core/TimeLine.h"
#include "gl/DeviceInfo.h"
#include "util/IDSolver.h"
#include "util/IProgressReporter.h"
#include "util/StreamReader.h"
#include "util/StreamWriter.h"

// core::Project.cpp's audio sidecar loader and the GUI's assert sinks are not
// part of this test; provide the same stubs the other TimeLine tests use.
#include "gui/AudioPlaybackWidget.h"
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

namespace {

class HarnessAssertHandler: public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};
class HarnessErrorHandler: public XCErrorHandler {
public:
    void critical(const QString&, const QString&, const QString&) const override { std::abort(); }
};
class NullReporter: public util::IProgressReporter {
public:
    void setSection(const QString&) override {}
    void setMaximum(int) override {}
    void setProgress(int) override {}
    bool wasCanceled() const override { return false; }
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

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    core::TimeLine line;
    cmnd::Stack stack;
    core::MoveKey* left = new core::MoveKey();
    core::MoveKey* middle = new core::MoveKey();
    core::MoveKey* right = new core::MoveKey();
    stack.push(line.createPusher(core::TimeKeyType_Move, 0, left));
    stack.push(line.createPusher(core::TimeKeyType_Move, 2, middle));
    stack.push(line.createPusher(core::TimeKeyType_Move, 4, right));
    check(line.hasTimeKey(core::TimeKeyType_Move, 0), "the left absolute key is in the map");
    check(line.hasTimeKey(core::TimeKeyType_Move, 2), "the proportional key is in the map");
    check(line.hasTimeKey(core::TimeKeyType_Move, 4), "the right absolute key is in the map");

    middle->setPos(QVector2D(12.0f, -4.0f));
    core::TimeKey::Proportional state;
    state.leftId = left->id();
    state.rightId = right->id();
    state.timeValid = true;
    state.timeWeight = 0.375f; // deliberately NOT the 0.5 its frame implies
    state.valueValid[0] = true;
    state.valueWeight[0] = 0.25f;
    state.valueValid[1] = true;
    state.valueWeight[1] = 0.75f;
    middle->setProportionalState(state);

    std::ostringstream bytes;
    util::StreamWriter writer(bytes);
    core::Serializer out(writer);
    const bool serialized = line.serialize(out);
    check(serialized, "the timeline serializes");
    const std::string data = bytes.str();
    check(!data.empty(), "the stream contains the timeline block");

    std::istringstream input(data);
    util::LEStreamReader reader(input);
    util::IDSolver<void*> solver;
    NullReporter reporter;
    gl::DeviceInfo device;
    core::Deserializer in(
        reader, solver, data.size() + 1, QVersionNumber(0, 16), device, reporter, 0
    );
    core::TimeLine loaded;
    const bool deserialized = loaded.deserialize(in);
    check(deserialized, "the timeline deserializes at 0.16");

    const core::TimeLine::MapType& map = loaded.map(core::TimeKeyType_Move);
    check(map.size() == 3, "three keys survive the round trip");
    const core::MoveKey* loadedLeft = dynamic_cast<const core::MoveKey*>(map.value(0));
    const core::MoveKey* loadedMiddle = dynamic_cast<const core::MoveKey*>(map.value(2));
    const core::MoveKey* loadedRight = dynamic_cast<const core::MoveKey*>(map.value(4));
    check(loadedLeft && loadedMiddle && loadedRight, "the key frames and types survive");

    if (loadedLeft && loadedMiddle && loadedRight) {
        check(loadedLeft->id() == left->id(), "the left key's stable id survives");
        check(loadedMiddle->id() == middle->id(), "the proportional key's stable id survives");
        check(loadedRight->id() == right->id(), "the right key's stable id survives");
        check(loadedMiddle->pos() == QVector2D(12.0f, -4.0f), "the middle key's values survive");
        check(!loadedLeft->isProportional(), "an absolute key stays absolute");
        check(loadedMiddle->isProportional(), "the proportional flag survives");
        if (const core::TimeKey::Proportional* loadedState = loadedMiddle->proportionalState()) {
            check(loadedState->leftId == left->id(), "the left bound id survives");
            check(loadedState->rightId == right->id(), "the right bound id survives");
            check(loadedState->timeValid, "the time weight is still valid");
            check(std::abs(loadedState->timeWeight - 0.375f) < 1e-6f,
                  "the STORED time weight survives (not the 0.5 a re-derivation would give)");
            check(loadedState->valueValid[0] && loadedState->valueValid[1],
                  "both component weights are still valid");
            check(std::abs(loadedState->valueWeight[0] - 0.25f) < 1e-6f,
                  "the first component weight survives");
            check(std::abs(loadedState->valueWeight[1] - 0.75f) < 1e-6f,
                  "the second component weight survives");
        } else {
            check(false, "the proportional state exists after loading");
        }
    }

    if (failures == 0)
        std::printf("timekey_roundtrip: ids, proportional bounds and stored weights survive 0.16\n");
    else
        std::printf("timekey_roundtrip: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
