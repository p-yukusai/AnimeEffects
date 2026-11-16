// Tests the "observable project attribute" contract: setting an attribute value
// drives onProjectAttributeModified (with a matching event type), an unchanged
// set does NOT, and both forward (exec) and undo of a command that sets the
// attribute notify through that same path. This is the decoupling that lets a
// settings dialog apply a value live (set -> notify -> timeline reacts) while
// undo is a separate, single command grain.
//
// Run: QT_QPA_PLATFORM=offscreen ./attribute_observable
#include <QApplication>
#include <QVector>
#include <cstdio>
#include "core/Project.h"
#include "core/Animator.h"
#include "cmnd/Stack.h"
#include "cmnd/BasicCommands.h"
#include "XCAssert.h"
#include "XCReport.h"

// Project.cpp references the GUI AudioPlaybackWidget::deserialize (only to load
// a .aemus sidecar, which this test never has). Stub it so the link succeeds.
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

class NullAnimator : public core::Animator {
public:
    core::Frame currentFrame() const override { return core::Frame(); }
    void stop() override {}
    void suspend() override {}
    void resume() override {}
    bool isSuspended() const override { return false; }
};

#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FAIL %d: %s\n", __LINE__, #cond);                                                   \
            return 1;                                                                                        \
        }                                                                                                    \
    } while (false)

int run() {
    NullAnimator animator;
    core::Project project("attribute_observable.anie", animator, nullptr);

    QVector<core::ProjectEvent::Type> seen;
    const util::SlotId id =
        project.onProjectAttributeModified.connect([&](core::ProjectEvent& aEvent, bool) { seen << aEvent.type(); });

    // ---- setting a value notifies with the matching event type ----
    project.attribute().setImageSize(QSize(1920, 1080));
    CHECK(seen.size() == 1 && seen.last() == core::ProjectEvent::Type_ChangeImageSize);

    project.attribute().setFrameRange(util::Range(1, 100));
    CHECK(seen.size() == 2 && seen.last() == core::ProjectEvent::Type_ChangeFrameRange);

    project.attribute().setFps(30);
    CHECK(seen.size() == 3 && seen.last() == core::ProjectEvent::Type_ChangeFps);

    project.attribute().setLoop(true);
    CHECK(seen.size() == 4 && seen.last() == core::ProjectEvent::Type_ChangeLoop);

    // ---- an unchanged set does NOT notify ----
    project.attribute().setFrameRange(util::Range(1, 100));
    CHECK(seen.size() == 4);
    project.attribute().setFps(30);
    CHECK(seen.size() == 4);

    // ---- the optional viewport background notifies on set and on clear ----
    project.attribute().setViewportBackground(QColor(10, 20, 30));
    CHECK(seen.size() == 5 && seen.last() == core::ProjectEvent::Type_ChangeViewportBackground);
    CHECK(project.attribute().hasViewportBackground());
    CHECK(project.attribute().viewportBackground() == QColor(10, 20, 30));

    // setViewportBackground is observable as unset -> set, so an equal colour
    // over an already-set override does NOT notify
    project.attribute().setViewportBackground(QColor(10, 20, 30));
    CHECK(seen.size() == 5);

    project.attribute().clearViewportBackground();
    CHECK(seen.size() == 6 && seen.last() == core::ProjectEvent::Type_ChangeViewportBackground);
    CHECK(!project.attribute().hasViewportBackground());

    // clearing an already-cleared attribute does NOT notify
    project.attribute().clearViewportBackground();
    CHECK(seen.size() == 6);

    // ---- command exec and undo both notify through the setter ----
    cmnd::Stack stack;
    core::Project* projectPtr = &project;
    const util::Range kRangeA(1, 100);
    const util::Range kRangeB(1, 200);
    stack.push(new cmnd::Delegatable(
        [=]() { projectPtr->attribute().setFrameRange(kRangeB); },
        [=]() { projectPtr->attribute().setFrameRange(kRangeA); }));

    // push() executes immediately (forward).
    CHECK(seen.last() == core::ProjectEvent::Type_ChangeFrameRange);
    CHECK(project.attribute().frameRange() == kRangeB);

    stack.undo();
    CHECK(seen.last() == core::ProjectEvent::Type_ChangeFrameRange); // undo re-notifies
    CHECK(project.attribute().frameRange() == kRangeA);

    stack.redo();
    CHECK(seen.last() == core::ProjectEvent::Type_ChangeFrameRange); // redo re-notifies
    CHECK(project.attribute().frameRange() == kRangeB);

    project.onProjectAttributeModified.disconnect(id);
    std::printf("OK: attribute observability passes\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    return run();
}
