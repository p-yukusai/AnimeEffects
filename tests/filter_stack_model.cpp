// Filter stack at the command layer: add (which also keys the filter), remove
// (which removes that lane's explicit keys), reorder, and the eye toggle. Each
// operation must be one undo step, including the key the add creates.
//
// Run: QT_QPA_PLATFORM=offscreen ./filter_stack_model
#include <QApplication>
#include <cstdio>

#include "core/Project.h"
#include "core/Animator.h"
#include "core/FolderNode.h"
#include "core/TimeLine.h"
#include "core/HsvKey.h"
#include "core/TimeKeyExpans.h"
#include "ctrl/TimeLineUtil.h"
#include "gui/prop/prop_KeyAccessor.h"
#include "XCAssert.h"
#include "XCReport.h"

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

QVector<core::TimeKeyType> types(const core::TimeLine& line) {
    QVector<core::TimeKeyType> out;
    for (const core::FilterEntry& entry : line.filters())
        out.append(entry.type);
    return out;
}

int run() {
    TestAnimator animator;
    animator.setFrame(7);
    core::Project project("filter_stack_model.anie", animator, nullptr);
    core::FolderNode folder(QStringLiteral("folder"));
    folder.setDefaultPosture(QVector2D(0, 0));
    core::TimeLine& line = *folder.timeLine();

    // A filter starts STATIC: its entry and default key are one undo step, and
    // the timeline gains no explicit key until the row's key dot is pressed.
    ctrl::TimeLineUtil::addFilter(project, folder, core::TimeKeyType_HSV);
    CHECK(types(line) == QVector<core::TimeKeyType>{core::TimeKeyType_HSV});
    CHECK(line.defaultKey(core::TimeKeyType_HSV) != nullptr);
    CHECK(line.map(core::TimeKeyType_HSV).isEmpty());

    // Duplicate add is a no-op: the stack and its default stay exactly as they were.
    ctrl::TimeLineUtil::addFilter(project, folder, core::TimeKeyType_HSV);
    CHECK(types(line) == QVector<core::TimeKeyType>{core::TimeKeyType_HSV});
    CHECK(line.map(core::TimeKeyType_HSV).isEmpty());

    ctrl::TimeLineUtil::addFilter(project, folder, core::TimeKeyType_Blur);
    CHECK(types(line) == (QVector<core::TimeKeyType>{core::TimeKeyType_HSV, core::TimeKeyType_Blur}));
    CHECK(line.defaultKey(core::TimeKeyType_Blur) != nullptr);
    CHECK(line.map(core::TimeKeyType_Blur).isEmpty());

    // Undo removes the stack row. The default key is structural and stays (a
    // hidden value reused if the filter is added again), so the visible state
    // is what we assert.
    project.commandStack().undo();
    CHECK(types(line) == QVector<core::TimeKeyType>{core::TimeKeyType_HSV});
    CHECK(line.map(core::TimeKeyType_Blur).isEmpty());
    project.commandStack().redo();
    CHECK(types(line) == (QVector<core::TimeKeyType>{core::TimeKeyType_HSV, core::TimeKeyType_Blur}));
    CHECK(line.defaultKey(core::TimeKeyType_Blur) != nullptr);

    // Model 2: a static filter edit writes its default, not a key; pressing
    // the dot is what adds the explicit key; the dot toggles it back off.
    gui::prop::KeyAccessor accessor;
    accessor.setProject(&project);
    accessor.setTarget(&folder);
    accessor.assignHSV(42, "hue");
    CHECK(line.map(core::TimeKeyType_HSV).isEmpty());
    CHECK(static_cast<const core::HSVKey*>(line.defaultKey(core::TimeKeyType_HSV))->data().hsv().at(0) == 42);
    accessor.knockNewHSV();
    CHECK(line.hasTimeKey(core::TimeKeyType_HSV, 7));
    accessor.removeKey(core::TimeKeyType_HSV);
    CHECK(!line.hasTimeKey(core::TimeKeyType_HSV, 7));
    accessor.knockNewHSV();
    CHECK(line.hasTimeKey(core::TimeKeyType_HSV, 7));

    // Reorder is one undoable command.
    ctrl::TimeLineUtil::moveFilter(project, folder, core::TimeKeyType_Blur, 0);
    CHECK(types(line) == (QVector<core::TimeKeyType>{core::TimeKeyType_Blur, core::TimeKeyType_HSV}));
    project.commandStack().undo();
    CHECK(types(line) == (QVector<core::TimeKeyType>{core::TimeKeyType_HSV, core::TimeKeyType_Blur}));
    project.commandStack().redo();

    // The eye toggle is stack state, not key state.
    auto* blurKey = new core::BlurKey();
    blurKey->setBlurX(line.current().blurX());
    blurKey->setBlurY(line.current().blurY());
    blurKey->setAngleDeg(line.current().angleDeg());
    ctrl::TimeLineUtil::pushNewBlurKey(project, folder, 7, blurKey);
    ctrl::TimeLineUtil::setFilterEnabled(project, folder, core::TimeKeyType_Blur, false);
    CHECK(!line.filterEnabled(core::TimeKeyType_Blur));
    CHECK(line.hasTimeKey(core::TimeKeyType_Blur, 7));
    project.commandStack().undo();
    CHECK(line.filterEnabled(core::TimeKeyType_Blur));

    // Removing a filter removes its lane's explicit keys, in one undo step.
    ctrl::TimeLineUtil::removeFilter(project, folder, core::TimeKeyType_HSV);
    CHECK(types(line) == QVector<core::TimeKeyType>{core::TimeKeyType_Blur});
    CHECK(!line.hasTimeKey(core::TimeKeyType_HSV, 7));
    project.commandStack().undo();
    CHECK(types(line) == (QVector<core::TimeKeyType>{core::TimeKeyType_Blur, core::TimeKeyType_HSV}));
    CHECK(line.hasTimeKey(core::TimeKeyType_HSV, 7));

    std::printf("OK: filter stack add/remove/reorder/enable and undo pass\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    return run();
}
