// Proportional keys on CHANNEL tracks: bounds, rebasing, projection and the
// per-channel ValueReflow. The old bundle-map version of this gate lived on
// Move/Blur/HSV; those properties now store one key per channel, so the same
// rules are pinned on ChannelKey maps directly.
#include <QApplication>
#include <cmath>
#include <cstdio>
#include <memory>

#include "cmnd/BasicCommands.h"
#include "cmnd/Stack.h"
#include "core/ChannelKey.h"
#include "core/ChannelTrack.h"
#include "core/Project.h"
#include "core/TimeLine.h"
#include "ctrl/proportional/prop_Keys.h"
#include "XCAssert.h"
#include "XCReport.h"

#include "gui/AudioPlaybackWidget.h"
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

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
XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

namespace prop = ctrl::proportional;
using core::TimeKeyType_Move;

int failures = 0;
void check(bool aOk, const char* aWhat) {
    if (!aOk) {
        std::printf("FAIL  %s\n", aWhat);
        ++failures;
    }
}
bool nearly(float a, float b, float aEps = 1e-4f) { return std::fabs(a - b) <= aEps; }

core::ChannelKey* push(core::TimeLine& aLine, cmnd::Stack& aStack, int aFrame, int aComponent, float aValue) {
    auto* key = new core::ChannelKey(TimeKeyType_Move, aComponent);
    key->setFrame(aFrame);
    key->setValue(aValue);
    aStack.push(new cmnd::GrabNewObject<core::ChannelKey>(key));
    aStack.push(aLine.createChannelPusher(TimeKeyType_Move, aComponent, aFrame, key));
    return key;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // 1. findBounds: frame order, proportional keys skipped, negative frames.
    {
        const int before = failures;
        core::TimeLine line;
        cmnd::Stack stack;
        auto* a = push(line, stack, -20, 0, 0.0f);
        auto* p1 = push(line, stack, -10, 0, 1.0f);
        p1->setProportional(true);
        auto* p2 = push(line, stack, -5, 0, 2.0f);
        p2->setProportional(true);
        auto* b = push(line, stack, 0, 0, 10.0f);
        auto* c = push(line, stack, 5, 0, 20.0f);

        const auto& map = line.channelMap(TimeKeyType_Move, 0);
        check(map.size() == 5, "the five channel keys are in the map");
        const prop::Bounds bounds = prop::findBounds(map, *p1);
        check(bounds.left == a && bounds.right == b, "p1's bounds skip proportional keys");
        check(prop::findBounds(map, *p2).left == a, "p2's left bound is the absolute key");
        check(prop::findBounds(map, *a).right == b, "the first key's right bound skips proportional keys");
        check(prop::findBounds(map, *c).left == b, "the last key's left bound is the previous absolute key");
        if (failures == before) std::printf("[1] findBounds: channel map order and proportional skipping\n");
    }

    // 2. rebase and projection on one channel component.
    {
        const int before = failures;
        core::TimeLine line;
        cmnd::Stack stack;
        push(line, stack, 0, 0, 0.0f);
        auto* p = push(line, stack, 5, 0, 50.0f);
        p->setProportional(true);
        auto* b = push(line, stack, 10, 0, 100.0f);
        const auto& map = line.channelMap(TimeKeyType_Move, 0);
        const prop::Bounds bounds = prop::findBounds(map, *p);
        prop::rebase(*p, bounds);
        check(nearly(p->proportionalState()->valueWeight[0], 0.5f), "X weight is 0.5");
        check(nearly(p->proportionalState()->timeWeight, 0.5f), "time weight is 0.5");

        b->setValue(200.0f);
        float projected = 0.0f;
        check(prop::projectedValue(*p, bounds, 0, &projected), "the proportional X projects");
        check(nearly(projected, 100.0f), "X follows the bound with its stored weight");

        int frame = 0;
        check(prop::projectedFrame(*p, bounds, &frame) && frame == 5, "the frame projection is the identity");
        if (failures == before) std::printf("[2] rebase/projection: a channel key stores its own weight\n");
    }

    // 3. Each channel freezes or follows independently.
    {
        const int before = failures;
        core::TimeLine line;
        cmnd::Stack stack;
        push(line, stack, 0, 0, 5.0f);
        auto* px = push(line, stack, 5, 0, 999.0f);
        px->setProportional(true);
        auto* bx = push(line, stack, 10, 0, 5.0f);
        push(line, stack, 0, 1, 0.0f);
        auto* py = push(line, stack, 5, 1, 5.0f);
        py->setProportional(true);
        auto* by = push(line, stack, 10, 1, 10.0f);

        prop::rebase(*px, prop::findBounds(line.channelMap(TimeKeyType_Move, 0), *px));
        prop::rebase(*py, prop::findBounds(line.channelMap(TimeKeyType_Move, 1), *py));
        check(!px->proportionalState()->valueValid[0], "a flat X span freezes X");
        check(py->proportionalState()->valueValid[1], "an independent Y span gets a weight");

        bx->setValue(300.0f);
        by->setValue(20.0f);
        prop::projectValues(*px, prop::findBounds(line.channelMap(TimeKeyType_Move, 0), *px));
        prop::projectValues(*py, prop::findBounds(line.channelMap(TimeKeyType_Move, 1), *py));
        check(nearly(px->value(), 999.0f), "the frozen X is untouched");
        check(nearly(py->value(), 10.0f), "Y follows its own bound");
        if (failures == before) std::printf("[3] per-channel flat-span rules\n");
    }

    // 4. ValueReflow captures and restores each channel on its own.
    {
        const int before = failures;
        core::TimeLine line;
        cmnd::Stack stack;
        push(line, stack, 0, 0, 0.0f);
        auto* px = push(line, stack, 2, 0, 50.0f);
        px->setProportional(true);
        auto* bx = push(line, stack, 4, 0, 100.0f);
        push(line, stack, 0, 1, 0.0f);
        auto* py = push(line, stack, 2, 1, 10.0f);
        py->setProportional(true);
        push(line, stack, 4, 1, 20.0f);
        prop::rebase(*px, prop::findBounds(line.channelMap(TimeKeyType_Move, 0), *px));
        prop::rebase(*py, prop::findBounds(line.channelMap(TimeKeyType_Move, 1), *py));

        prop::ValueReflow reflow;
        reflow.capture(line, TimeKeyType_Move, {});
        bx->setValue(200.0f);
        reflow.apply();
        check(nearly(px->value(), 100.0f), "channel X follows its own bound");
        check(nearly(py->value(), 10.0f), "channel Y keeps its own unchanged span");
        reflow.restore();
        check(nearly(px->value(), 50.0f), "restore brings X back");
        check(nearly(py->value(), 10.0f), "restore leaves Y where it was");
        if (failures == before) std::printf("[4] ValueReflow: per-channel capture/apply/restore\n");
    }

    if (failures == 0)
        std::printf("proportional_keys: channel bounds, weights and per-channel reflow pass\n");
    return failures == 0 ? 0 : 1;
}
