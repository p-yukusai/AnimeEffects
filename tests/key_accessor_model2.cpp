// Pins model 2 at the accessor layer: a property with no explicit keys is
// static (edits write the base value), and a property with any explicit key is
// animated (edits write the current frame, creating a key when needed). This is
// the behaviour the unified inspector relies on; a regression here silently
// turns every static field edit into an animation key.
//
// Run: QT_QPA_PLATFORM=offscreen ./key_accessor_model2
#include <QApplication>
#include <cstdio>
#include <memory>

#include "core/Project.h"
#include "core/Animator.h"
#include "core/FolderNode.h"
#include "core/MoveKey.h"
#include "core/DepthKey.h"
#include "core/OpacityKey.h"
#include "gui/prop/prop_KeyAccessor.h"
#include "ctrl/TimeLineUtil.h"
#include "XCAssert.h"
#include "XCReport.h"

// Project.cpp references this GUI symbol only for the optional .aemus sidecar.
#include "gui/AudioPlaybackWidget.h"
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

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
XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

namespace {

class TestAnimator : public core::Animator {
public:
    core::Frame currentFrame() const override { return mFrame; }
    void stop() override {}
    void suspend() override {}
    void resume() override {}
    bool isSuspended() const override { return false; }
    void setFrame(int aFrame) { mFrame = core::Frame(aFrame); }

private:
    core::Frame mFrame;
};

#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FAIL %d: %s\n", __LINE__, #cond);                                                   \
            return 1;                                                                                        \
        }                                                                                                    \
    } while (false)

QVector2D moveDefault(const core::TimeLine& line) {
    if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Move)) {
        return QVector2D(
            line.channelBaseValue(core::TimeKeyType_Move, 0),
            line.channelBaseValue(core::TimeKeyType_Move, 1)
        );
    }
    return static_cast<const core::MoveKey*>(line.baseValue(core::TimeKeyType_Move))->data().pos();
}

QVector2D moveAt(const core::TimeLine& line, int frame) {
    if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Move)) {
        const auto* x = line.channelKey(core::TimeKeyType_Move, 0, frame);
        const auto* y = line.channelKey(core::TimeKeyType_Move, 1, frame);
        return QVector2D(x ? x->value() : 0.0f, y ? y->value() : 0.0f);
    }
    const auto* key = static_cast<const core::MoveKey*>(line.timeKey(core::TimeKeyType_Move, frame));
    return key ? key->data().pos() : QVector2D();
}

int moveKeyCount(const core::TimeLine& line) {
    if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Move)) {
        const auto* track = line.channelTrack(core::TimeKeyType_Move, 0);
        return track ? track->keyCount() : 0;
    }
    return line.map(core::TimeKeyType_Move).size();
}
float depthDefault(const core::TimeLine& line) {
    if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Depth)) {
        return line.channelBaseValue(core::TimeKeyType_Depth, 0);
    }
    return static_cast<const core::DepthKey*>(line.baseValue(core::TimeKeyType_Depth))->data().depth();
}

int run() {
    TestAnimator animator;
    animator.setFrame(10);
    core::Project project("key_accessor_model2.anie", animator, nullptr);

    auto* folder = new core::FolderNode(QStringLiteral("folder"));
    folder->setDefaultPosture(QVector2D(100.0f, 200.0f));
    folder->setDefaultDepth(-3.0f);
    folder->setDefaultOpacity(0.75f);
    core::TimeLine& line = *folder->timeLine();

    gui::prop::KeyAccessor accessor;
    accessor.setProject(&project);
    accessor.setTarget(folder);

    // ---- static Move: an edit updates the default and creates NO key ----
    accessor.assignMovePosition(QVector2D(11.0f, 22.0f));
    CHECK(moveKeyCount(line) == 0);
    CHECK(moveDefault(line) == QVector2D(11.0f, 22.0f));

    accessor.assignDepthPosition(-7.0f);
    CHECK(line.hasChannelKeys(core::TimeKeyType_Depth, 0) == false);
    CHECK(depthDefault(line) == -7.0f);

    // The edit is one undo step and undo restores the default value.
    project.commandStack().undo();
    CHECK(depthDefault(line) == -3.0f);
    project.commandStack().redo();
    CHECK(depthDefault(line) == -7.0f);

    // ---- once an explicit key exists the property is animated ----
    accessor.knockNewMove();
    CHECK(moveKeyCount(line) == 1);
    CHECK(line.hasChannelKey(core::TimeKeyType_Move, 0, 10));

    accessor.assignMovePosition(QVector2D(31.0f, 44.0f));
    CHECK(moveAt(line, 10) == QVector2D(31.0f, 44.0f));
    CHECK(moveDefault(line) == QVector2D(11.0f, 22.0f)); // default untouched

    // ---- an edit at another frame auto-creates a key there ----
    animator.setFrame(20);
    accessor.assignMovePosition(QVector2D(55.0f, 66.0f));
    CHECK(moveKeyCount(line) == 2);
    CHECK(moveAt(line, 20) == QVector2D(55.0f, 66.0f));

    // ---- a linked keyframe edits every channel; a split one edits only the
    // channel the caller names. A split property also keeps NEW field edits on
    // the named channel, which is what makes the separation durable.
    CHECK(line.isFrameLinked(core::TimeKeyType_Move, 10));
    CHECK(line.isFrameLinked(core::TimeKeyType_Move, 20));
    CHECK(line.setFrameLinked(core::TimeKeyType_Move, 20, false));
    CHECK(!line.isFrameLinked(core::TimeKeyType_Move, 20));
    accessor.assignMovePosition(QVector2D(77.0f, 66.0f), 0);
    CHECK(moveAt(line, 20) == QVector2D(77.0f, 66.0f)); // Y stays where it was

    animator.setFrame(10);
    accessor.assignMovePosition(QVector2D(31.0f, 99.0f), 0);
    CHECK(moveAt(line, 10) == QVector2D(31.0f, 99.0f)); // linked frame writes both

    animator.setFrame(30);
    accessor.assignMovePosition(QVector2D(5.0f, 6.0f), 0);
    CHECK(line.hasChannelKey(core::TimeKeyType_Move, 0, 30));
    CHECK(!line.hasChannelKey(core::TimeKeyType_Move, 1, 30)); // no silent sibling key
    CHECK(!line.channelKey(core::TimeKeyType_Move, 0, 30)->linked());

    // Centroid fields follow the same split rule: component 2 writes only
    // centroid X on a split frame; a linked frame writes both centroid channels.
    animator.setFrame(20);
    accessor.assignMoveCentroid(QVector2D(70.0f, 80.0f), 2);
    CHECK(line.hasChannelKey(core::TimeKeyType_Move, 2, 20));
    CHECK(!line.hasChannelKey(core::TimeKeyType_Move, 3, 20));
    CHECK(line.channelKey(core::TimeKeyType_Move, 2, 20)->value() == 70.0f);

    animator.setFrame(10);
    accessor.assignMoveCentroid(QVector2D(91.0f, 92.0f), 2);
    CHECK(line.channelKey(core::TimeKeyType_Move, 2, 10)->value() == 91.0f);
    CHECK(line.channelKey(core::TimeKeyType_Move, 3, 10)->value() == 92.0f);

    // Image offsets are channels beside the Image state key. With no image
    // key at all (the GL-only ImageKey is not constructed here), the static edit
    // seeds the base; once one offset key exists, a field edit keys only that
    // component.
    animator.setFrame(20);
    accessor.assignImageOffset(QVector2D(12.0f, 34.0f), 0);
    CHECK(line.channelBaseValue(core::TimeKeyType_Image, 0) == 12.0f);
    CHECK(line.channelBaseValue(core::TimeKeyType_Image, 1) == 0.0f);
    line.channelTrack(core::TimeKeyType_Image, 0)->insertKey(20, 50.0f);
    accessor.assignImageOffset(QVector2D(60.0f, 34.0f), 0);
    CHECK(line.channelKey(core::TimeKeyType_Image, 0, 20)->value() == 60.0f);
    CHECK(!line.hasChannelKey(core::TimeKeyType_Image, 1, 20));

    // The timeline menu's Link/Separate helper is one undo step over the frame.
    CHECK(!line.isFrameLinked(core::TimeKeyType_Move, 20));
    ctrl::TimeLineUtil::setFrameLinked(
        project, *folder, core::TimeKeyType_Move, 20, true
    );
    CHECK(line.isFrameLinked(core::TimeKeyType_Move, 20));
    project.commandStack().undo();
    CHECK(!line.isFrameLinked(core::TimeKeyType_Move, 20));

    // The key dot is a toggle: removing the current key is one undo step and
    // leaves the default (the static value) untouched.
    accessor.removeKey(core::TimeKeyType_Move);
    CHECK(moveKeyCount(line) == 2);
    CHECK(moveDefault(line) == QVector2D(11.0f, 22.0f));
    project.commandStack().undo();
    CHECK(moveKeyCount(line) == 3);

    delete folder;
    std::printf("OK: model 2 static/animated editing passes\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    return run();
}
