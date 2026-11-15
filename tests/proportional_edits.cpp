// Frame and value edits on channel tracks: a proportional companion follows
// its bound in its OWN channel, and one undo restores both.
#include <QApplication>
#include <cmath>
#include <cstdio>
#include <memory>

#include "cmnd/BasicCommands.h"
#include "cmnd/ScopedMacro.h"
#include "cmnd/Stack.h"
#include "core/ChannelKey.h"
#include "core/ChannelTrack.h"
#include "core/ObjectNode.h"
#include "core/Project.h"
#include "core/TimeLine.h"
#include "ctrl/TimeLineUtil.h"
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

class NullAnimator: public core::Animator {
public:
    core::Frame currentFrame() const override { return core::Frame(); }
    void stop() override {}
    void suspend() override {}
    void resume() override {}
    bool isSuspended() const override { return false; }
};

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

    // (a) A FRAME move of one channel's absolute key projects the proportional
    // companion in that channel; one undo restores both frames.
    {
        core::TimeLine line;
        cmnd::Stack stack;
        push(line, stack, 0, 0, 0.0f);
        auto* middle = push(line, stack, 2, 0, 50.0f);
        middle->setProportional(true);
        auto* right = push(line, stack, 4, 0, 100.0f);
        prop::rebase(*middle, prop::findBounds(line.channelMap(TimeKeyType_Move, 0), *middle));

        core::TimeLineEvent source;
        source.setType(core::TimeLineEvent::Type_MoveKey);

        core::TimeLineEvent::Target target;
        target.pos = core::TimeKeyPos(line, TimeKeyType_Move, 4, 0);
        target.subIndex = 4;
        source.targets().push_back(target);

        auto* command = new ctrl::TimeLineUtil::MoveFrameOfKey(source);
        int clamped = 0;
        core::TimeLineEvent mod;
        check(command->modifyMove(mod, 4, util::Range(-1000, 1000), &clamped), "the frame move is accepted");
        check(clamped == 4, "the clamped step is 4");

        {
            cmnd::ScopedMacro macro(stack, "Move a channel key");
            stack.push(command);
        }
        check(line.hasChannelKey(TimeKeyType_Move, 0, 8), "the absolute key lands at frame 8");
        check(line.hasChannelKey(TimeKeyType_Move, 0, 4), "the proportional key follows to frame 4");
        check(nearly(middle->value(), 50.0f), "the frame move writes no value");

        bool undone = false;
        stack.undo(&undone);
        check(undone, "the frame move undoes");
        check(line.hasChannelKey(TimeKeyType_Move, 0, 4), "the absolute key is back at frame 4");
        check(line.hasChannelKey(TimeKeyType_Move, 0, 2), "the proportional key is back at frame 2");
    }

    // (b) A value edit through ValueReflow follows the stored weight and
    // restores both channels independently.
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
        check(nearly(px->value(), 100.0f), "X follows its bound");
        check(nearly(py->value(), 10.0f), "Y keeps its own unchanged span");
        reflow.restore();
        check(nearly(px->value(), 50.0f), "restore brings X back");
        check(nearly(py->value(), 10.0f), "restore leaves Y where it was");
        if (failures == before) std::printf("[2] channel ValueReflow: apply and restore\n");
    }

    if (failures == 0)
        std::printf("proportional_edits: per-channel frame projection and value reflow pass\n");
    return failures == 0 ? 0 : 1;
}
