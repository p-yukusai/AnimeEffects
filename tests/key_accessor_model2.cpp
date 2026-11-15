// Pins model 2 at the accessor layer: a property with no explicit keys is
// static (edits write the default key), and a property with any explicit key is
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
#include "core/OpaKey.h"
#include "gui/prop/prop_KeyAccessor.h"
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

const core::MoveKey::Data& moveDefault(const core::TimeLine& line) {
    return static_cast<const core::MoveKey*>(line.defaultKey(core::TimeKeyType_Move))->data();
}
const core::DepthKey::Data& depthDefault(const core::TimeLine& line) {
    return static_cast<const core::DepthKey*>(line.defaultKey(core::TimeKeyType_Depth))->data();
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
    CHECK(line.map(core::TimeKeyType_Move).isEmpty());
    CHECK(moveDefault(line).pos() == QVector2D(11.0f, 22.0f));

    accessor.assignDepthPosition(-7.0f);
    CHECK(line.map(core::TimeKeyType_Depth).isEmpty());
    CHECK(depthDefault(line).depth() == -7.0f);

    // The edit is one undo step and undo restores the default value.
    project.commandStack().undo();
    CHECK(depthDefault(line).depth() == -3.0f);
    project.commandStack().redo();
    CHECK(depthDefault(line).depth() == -7.0f);

    // ---- once an explicit key exists the property is animated ----
    accessor.knockNewMove();
    CHECK(line.map(core::TimeKeyType_Move).size() == 1);
    CHECK(line.hasTimeKey(core::TimeKeyType_Move, 10));

    accessor.assignMovePosition(QVector2D(31.0f, 44.0f));
    const auto* keyAt10 = static_cast<const core::MoveKey*>(line.timeKey(core::TimeKeyType_Move, 10));
    CHECK(keyAt10 && keyAt10->data().pos() == QVector2D(31.0f, 44.0f));
    CHECK(moveDefault(line).pos() == QVector2D(11.0f, 22.0f)); // default untouched

    // ---- an edit at another frame auto-creates a key there ----
    animator.setFrame(20);
    accessor.assignMovePosition(QVector2D(55.0f, 66.0f));
    CHECK(line.map(core::TimeKeyType_Move).size() == 2);
    const auto* keyAt20 = static_cast<const core::MoveKey*>(line.timeKey(core::TimeKeyType_Move, 20));
    CHECK(keyAt20 && keyAt20->data().pos() == QVector2D(55.0f, 66.0f));

    // The key dot is a toggle: removing the current key is one undo step and
    // leaves the default (the static value) untouched.
    accessor.removeKey(core::TimeKeyType_Move);
    CHECK(line.map(core::TimeKeyType_Move).size() == 1);
    CHECK(moveDefault(line).pos() == QVector2D(11.0f, 22.0f));
    project.commandStack().undo();
    CHECK(line.map(core::TimeKeyType_Move).size() == 2);

    delete folder;
    std::printf("OK: model 2 static/animated editing passes\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    return run();
}
