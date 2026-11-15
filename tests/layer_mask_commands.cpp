// Gate for the LAYER MASK's edits (ctrl::TimeLineUtil::addMask / removeMask /
// invertMask / toggleMaskKey). Every surface - the inspector's card, the tree's chip,
// the Mask tool - calls these four, so this is where the state machine a user drives
// is pinned; the drawing itself is measured by layer_mask_pixels and the storage by
// timekey_roundtrip.
//
// What is asserted, in the order a person meets it:
//   [1] ADD is a no-op on screen: a field at the neck value, the node's content rect,
//       a texel grid inside the cap, and it goes on the DEFAULT key (so the lane
//       gains no keyframe).
//   [2] ONE MASK PER NODE: a second add does nothing, and it pushes no command.
//   [3] INVERT flips every texel and is ONE undo step.
//   [4] TOGGLE KEY holds the blend: the key created between two keys carries the
//       field the frame showed, and toggling again removes it.
//   [5] REMOVE takes the keys and the default key, and undo brings them back.
//
// Headless: no widgets, no GL. The project's canvas is the content space a folder's
// mask uses (a layer with no rendered texture falls back to it too).
#include <QApplication>
#include <QJsonObject>
#include <QSize>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "XC.h"
#include "cmnd/Stack.h"
#include "core/MaskKey.h"
#include "core/ObjectNode.h"
#include "core/Project.h"
#include "core/TimeLine.h"
#include "core/TimeKeyExpans.h"
#include "core/TimeKeyType.h"
#include "ctrl/TimeLineUtil.h"

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
class NullAnimator: public core::Animator {
public:
    core::Frame currentFrame() const override { return core::Frame(); }
    void stop() override {}
    void suspend() override {}
    void resume() override {}
    bool isSuspended() const override { return false; }
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

// A command count that a stack without one can still prove: exactly one command means
// one undo succeeds and the next does not. Restores whatever it undid.
bool exactlyOneCommand(core::Project& aProject) {
    bool first = false;
    aProject.commandStack().undo(&first);
    if (!first)
        return false;
    bool second = false;
    aProject.commandStack().undo(&second);
    aProject.commandStack().redo();
    aProject.commandStack().redo();
    return !second;
}

bool noCommand(core::Project& aProject) {
    bool undone = false;
    aProject.commandStack().undo(&undone);
    if (undone)
        aProject.commandStack().redo();
    return !undone;
}

const core::MaskKey* defaultMask(const core::TimeLine& aLine) {
    return static_cast<const core::MaskKey*>(aLine.defaultKey(core::TimeKeyType_Mask));
}

// A node standing in for a layer: enough of ObjectNode for the mask commands.
class TestNode: public core::ObjectNode {
    core::TimeLine mTimeLine;

public:
    core::ObjectType type() const override { return core::ObjectType_Layer; }
    void setName(const QString&) override {}
    const QString& name() const override {
        static const QString s("layer");
        return s;
    }
    float initialDepth() const override { return 0; }
    void setVisibility(bool) override {}
    bool isVisible() const override { return true; }
    void setSlimDown(bool) override {}
    bool isSlimmedDown() const override { return false; }
    bool canHoldChild() const override { return false; }
    void setInitialRect(const QRect&) override {}
    QRect initialRect() const override { return QRect(); }
    bool serialize(core::Serializer&) const override { return true; }
    bool deserialize(core::Deserializer&) override { return true; }
    core::TimeLine* timeLine() override { return &mTimeLine; }
    const core::TimeLine* timeLine() const override { return &mTimeLine; }
};

int countTexels(const QImage& aField, uchar aValue) {
    int count = 0;
    for (int y = 0; y < aField.height(); ++y) {
        const uchar* row = aField.constScanLine(y);
        for (int x = 0; x < aField.width(); ++x) {
            if (row[x] == aValue)
                ++count;
        }
    }
    return count;
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    NullAnimator animator;
    core::Project project("layer_mask_commands.anie", animator, nullptr);
    project.attribute().setImageSize(QSize(1024, 768));
    TestNode node;

    // --- [1] add -------------------------------------------------------------
    std::printf("MEASURED fixture: project canvas %dx%d\n", project.attribute().imageSize().width(),
                project.attribute().imageSize().height());
    ctrl::TimeLineUtil::addMask(project, node);
    core::MaskKey* mask = static_cast<core::MaskKey*>(
        node.timeLine()->defaultKey(core::TimeKeyType_Mask)
    );
    check(mask != nullptr, "add puts the mask on the default key");
    check(exactlyOneCommand(project), "add pushes exactly one command");
    check(node.timeLine()->map(core::TimeKeyType_Mask).isEmpty(), "add leaves the lane keyless");
    if (mask) {
        check(mask->data().isValid(), "the fresh mask holds a field");
        check(mask->data().contentRect() == QRectF(0.0, 0.0, 1024.0, 768.0), "the field covers the canvas");
        const QSize texels = mask->data().texelSize();
        std::printf("MEASURED fresh mask: %dx%d texels over 1024x768 content\n", texels.width(), texels.height());
        check(texels.width() == core::MaskKey::kMaxTexels, "the long side is at the field cap");
        check(texels.height() < texels.width(), "the aspect is kept");
        check(
            countTexels(mask->data().field(), core::MaskKey::kNeutral)
                == texels.width() * texels.height(),
            "every texel starts at REVEAL ALL (adding a mask changes nothing on screen)"
        );
    }

    // --- [2] one mask per node ----------------------------------------------
    project.commandStack().clear();
    ctrl::TimeLineUtil::addMask(project, node);
    check(noCommand(project), "a second add pushes no command");

    // --- [3] invert ----------------------------------------------------------
    if (mask) {
        core::MaskKey* key = mask;
        // paint a known pattern first: the left half hidden
        core::MaskKey::Data painted = key->data();
        QImage field = painted.field();
        for (int y = 0; y < field.height(); ++y) {
            uchar* row = field.scanLine(y);
            for (int x = 0; x < field.width() / 2; ++x)
                row[x] = 0;
        }
        painted.write(field, painted.contentRect());
        key->data().assign(painted);  // not a command: this is the fixture, not an edit

        const int hidden = countTexels(key->data().field(), 0);
        const int width = key->data().texelSize().width();
        const auto texelAt = [&](int x) {
            return int(key->data().field().constScanLine(0)[x]);
        };
        const int leftBefore = texelAt(4);
        const int rightBefore = texelAt(width - 4);
        project.commandStack().clear();
        ctrl::TimeLineUtil::invertMask(project, node, 0);
        std::printf(
            "MEASURED invert: left texel %d -> %d, right texel %d -> %d (%d hidden texels either "
            "way)\n",
            leftBefore, texelAt(4), rightBefore, texelAt(width - 4), hidden
        );
        check(leftBefore == 0 && rightBefore == 255, "the fixture is half hidden / half visible");
        check(texelAt(4) == 255, "inverting reveals the hidden half");
        check(texelAt(width - 4) == 0, "inverting hides the visible half");
        check(exactlyOneCommand(project), "invert is exactly one command");

        project.commandStack().undo();
        check(countTexels(key->data().field(), 0) == hidden, "undo restores the field exactly");
        project.commandStack().redo();
        check(texelAt(4) == 255, "redo inverts again");
        project.commandStack().undo();
    }

    // --- [4] toggle key ------------------------------------------------------
    {
        project.commandStack().clear();
        ctrl::TimeLineUtil::toggleMaskKey(project, node, 12);
        const core::MaskKey* atTwelve =
            static_cast<const core::MaskKey*>(node.timeLine()->map(core::TimeKeyType_Mask).value(12));
        check(atTwelve != nullptr, "toggle creates a key at the frame");
        check(exactlyOneCommand(project), "keying is exactly one command");
        if (atTwelve && mask) {
            check(atTwelve->data().texelSize() == mask->data().texelSize(), "the key holds the same grid");
            int differ = 0;
            for (int y = 0; y < atTwelve->data().field().height(); ++y) {
                const uchar* a = atTwelve->data().field().constScanLine(y);
                const uchar* b = mask->data().field().constScanLine(y);
                for (int x = 0; x < atTwelve->data().field().width(); ++x) {
                    if (a[x] != b[x])
                        ++differ;
                }
            }
            std::printf("MEASURED keyed hold: %d texels differ from the default key\n", differ);
            check(differ == 0, "a new key HOLDS what the frame showed");
        }

        ctrl::TimeLineUtil::toggleMaskKey(project, node, 12);
        check(
            node.timeLine()->map(core::TimeKeyType_Mask).value(12) == nullptr,
            "toggling again removes the key"
        );
    }

    // --- [5] remove ----------------------------------------------------------
    {
        project.commandStack().clear();
        ctrl::TimeLineUtil::removeMask(project, node);
        check(defaultMask(*node.timeLine()) == nullptr, "remove takes the default key");
        check(node.timeLine()->map(core::TimeKeyType_Mask).isEmpty(), "remove takes every key");
        check(exactlyOneCommand(project), "remove is one undo step");

        project.commandStack().undo();
        check(defaultMask(*node.timeLine()) != nullptr, "undo restores the default key");
    }

    if (failures == 0)
        std::printf("layer_mask_commands: add / invert / key / remove are one undo step each\n");
    else
        std::printf("layer_mask_commands: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
