// The Transform tool's key policy, headless.
//
// The tool used to own a key for every channel missing at the playhead and push it
// on the first drag: a static object grew a Move key for no reason, and unchecking
// all three "Create ... key" boxes changed nothing. The policy now matches the
// inspector's model-2: a channel with an explicit key elsewhere auto-keys at the
// playhead, a static channel edits the base value, and a checked box forces an
// explicit key. This gate drives the real SRTEditor/MoveMode with a synthetic
// cursor, so it pins the actual key maps and the undo command.
#include <QApplication>
#include <QMouseEvent>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "cmnd/BasicCommands.h"
#include "core/AbstractCursor.h"
#include "core/CameraInfo.h"
#include "core/FolderNode.h"
#include "core/MoveKey.h"
#include "core/Project.h"
#include "core/TimeKeyExpans.h"
#include "core/ChannelKey.h"
#include "core/RotateKey.h"
#include "core/Animator.h"
#include "ctrl/SRTEditor.h"
#include "ctrl/TimeLineUtil.h"
#include "ctrl/UILogger.h"
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

int failures = 0;
void check(bool aOk, const char* aWhat) {
    std::printf("%s  %s\n", aOk ? "PASS" : "FAIL", aWhat);
    if (!aOk) ++failures;
}

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

class TestLogger : public ctrl::UILogger {
public:
    void pushLog(const QString&, ctrl::UILogType) override {}
};

struct Fixture {
    TestAnimator animator;
    core::Project project;
    core::FolderNode* folder = nullptr;
    ctrl::SRTEditor* editor = nullptr;
    TestLogger logger;
    core::CameraInfo camera;
    core::AbstractCursor cursor;

    explicit Fixture(int aFrame = 10): project(QStringLiteral("srt_keys.anie"), animator, nullptr) {
        animator.setFrame(aFrame);
        camera.reset(QSize(800, 600), 1.0, QSize(800, 600), QPoint(0, 0));
        folder = new core::FolderNode(QStringLiteral("folder"));
        folder->setDefaultPosture(QVector2D(100.0f, 200.0f));
        editor = new ctrl::SRTEditor(project, logger);
    }
    ~Fixture() {
        delete editor;
        delete folder;
    }

    ctrl::SRTParam param(bool aMove = false, bool aRotate = false, bool aScale = false) {
        ctrl::SRTParam p;
        p.necessarilyMove = aMove;
        p.necessarilyRotate = aRotate;
        p.necessarilyScale = aScale;
        return p;
    }

    void target(const ctrl::SRTParam& aParam) {
        editor->updateParam(aParam);
        editor->setTarget(folder);
    }

    // The symbol's translation handle is the node's own origin. A press on it
    // focuses Translation; the move supplies a world velocity.
    void dragTranslation(float aDx, const QVector2D& aWorldCenter = QVector2D(100.0f, 200.0f)) {
        const QPointF c = camera.toScreenPos(aWorldCenter).toPointF();
        const QPointF moved = c + QPointF(aDx, 0.0);
        QMouseEvent press(QEvent::MouseButtonPress, c, c, c, Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        cursor.setMousePress(&press, camera);
        editor->updateCursor(camera, cursor);

        QMouseEvent move(QEvent::MouseMove, moved, moved, moved, Qt::NoButton, Qt::LeftButton,
                         Qt::NoModifier);
        cursor.setMouseMove(&move, camera);
        editor->updateCursor(camera, cursor);

        QMouseEvent release(QEvent::MouseButtonRelease, moved, moved, moved, Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        cursor.setMouseRelease(&release, camera);
        editor->updateCursor(camera, cursor);
    }

    // The rotation handle is the symbol's p[2] (top-right): 100 world px from
    // the origin, scaled by the camera's symbol size. Press there and drag the
    // point around the centre; the mode turns the angle into a rotation.
    void dragRotation(float aDegrees) {
        const float symbolScale =
            std::clamp((camera.screenSize().width() + camera.screenSize().height()) / 2500.0f, 0.01f, 100.0f)
            / camera.scale();
        const QPointF center = camera.toScreenPos(QPointF(100.0f, 200.0f));
        const QPointF handle = camera.toScreenPos(
            QPointF(100.0f + 100.0f * symbolScale, 200.0f + 100.0f * symbolScale)
        );
        const QVector2D radius(handle - center);
        const float radians = aDegrees * 3.14159265f / 180.0f;
        const QPointF moved = center + QPointF(
            radius.x() * std::cos(radians) - radius.y() * std::sin(radians),
            radius.x() * std::sin(radians) + radius.y() * std::cos(radians)
        );

        QMouseEvent press(QEvent::MouseButtonPress, handle, handle, handle,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        cursor.setMousePress(&press, camera);
        editor->updateCursor(camera, cursor);

        QMouseEvent move(QEvent::MouseMove, moved, moved, moved,
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        cursor.setMouseMove(&move, camera);
        editor->updateCursor(camera, cursor);

        QMouseEvent release(QEvent::MouseButtonRelease, moved, moved, moved,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        cursor.setMouseRelease(&release, camera);
        editor->updateCursor(camera, cursor);
    }

    QVector2D moveBasePos() const {
        return QVector2D(
            folder->timeLine()->channelBaseValue(core::TimeKeyType_Move, 0),
            folder->timeLine()->channelBaseValue(core::TimeKeyType_Move, 1)
        );
    }
    QVector2D movePosAt(int aFrame) const {
        const auto* x = folder->timeLine()->channelKey(core::TimeKeyType_Move, 0, aFrame);
        const auto* y = folder->timeLine()->channelKey(core::TimeKeyType_Move, 1, aFrame);
        return QVector2D(x ? x->value() : 0.0f, y ? y->value() : 0.0f);
    }
    int moveKeyCount() const {
        const auto* track = folder->timeLine()->channelTrack(core::TimeKeyType_Move, 0);
        return track ? track->keyCount() : 0;
    }
    void pushMove(int aFrame, const QVector2D& aPos, const QVector2D& aCentroid = QVector2D()) {
        const float values[4] = { aPos.x(), aPos.y(), aCentroid.x(), aCentroid.y() };
        for (int c = 0; c < 4; ++c) {
            auto* key = new core::ChannelKey(core::TimeKeyType_Move, c);
            key->setFrame(aFrame);
            key->setValue(values[c]);
            project.commandStack().push(new cmnd::GrabNewObject<core::ChannelKey>(key));
            project.commandStack().push(
                folder->timeLine()->createChannelPusher(core::TimeKeyType_Move, c, aFrame, key)
            );
        }
    }

    core::TimeLine& line() { return *folder->timeLine(); }
};

void testStaticEditsDefault() {
    Fixture f;
    f.target(f.param());
    const QVector2D before = f.moveBasePos();
    f.dragTranslation(40.0f);
    check(f.moveKeyCount() == 0, "static: drag creates no explicit Move key");
    const QVector2D after = f.moveBasePos();
    check(after != before, "static: the base Move value is what the drag edits");
    f.project.commandStack().undo();
    const QVector2D undone = f.moveBasePos();
    check(undone == before, "static: one undo restores the base value");
    f.project.commandStack().redo();
}

void testExistingKeyEditsInPlace() {
    Fixture f;
    f.pushMove(10, QVector2D(100.0f, 200.0f));
    const int sizeBefore = f.moveKeyCount();
    f.editor->updateParam(f.param());
    f.editor->setTarget(f.folder);
    const QVector2D before = f.movePosAt(10);
    f.dragTranslation(30.0f);
    check(f.moveKeyCount() == sizeBefore,
          "existing key: drag does not add a second key");
    const QVector2D after = f.movePosAt(10);
    check(after != before, "existing key: the key at the playhead is edited");
}

void testAnimatedChannelAutoKeys() {
    Fixture f(10);
    f.pushMove(20, QVector2D(300.0f, 200.0f));
    f.editor->updateParam(f.param());
    f.editor->setTarget(f.folder);
    f.dragTranslation(30.0f, f.line().current().srt().pos());
    check(f.moveKeyCount() == 2,
          "animated: a drag at another frame creates a key there");
    check(f.line().hasChannelKey(core::TimeKeyType_Move, 0, 10),
          "animated: and the new key is at the playhead");
}

void testCheckboxForcesAKey() {
    Fixture f;
    f.target(f.param(true));
    f.dragTranslation(30.0f, f.line().current().srt().pos());
    check(f.moveKeyCount() == 1,
          "checked Move: the drag creates the explicit key");
    check(f.line().hasChannelKey(core::TimeKeyType_Move, 0, 10),
          "checked Move: at the playhead");
}

void testOtherChannelCheckboxCreatesOnlyThatKey() {
    Fixture f;
    f.target(f.param(false, true));
    f.dragTranslation(30.0f);
    check(f.moveKeyCount() == 0,
          "checked Rotate: the move stays static");
    if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Rotate)) {
        check(f.line().map(core::TimeKeyType_Rotate).isEmpty(),
              "checked Rotate: the legacy bundle map stays empty");
        check(f.line().hasChannelKey(core::TimeKeyType_Rotate, 0, 10),
              "checked Rotate: a Rotate channel key is created");
        check(f.line().channelKey(core::TimeKeyType_Rotate, 0, 10) != nullptr,
              "checked Rotate: at the playhead");
    } else {
        check(f.line().map(core::TimeKeyType_Rotate).size() == 1,
              "checked Rotate: a Rotate key is created");
        check(f.line().timeKey(core::TimeKeyType_Rotate, 10) != nullptr,
              "checked Rotate: at the playhead");
    }
}

void testStaticRotateEditsBase() {
    Fixture f;
    f.target(f.param());
    const float before = f.line().channelBaseValue(core::TimeKeyType_Rotate, 0);
    f.dragRotation(30.0f);
    check(f.line().channelTrack(core::TimeKeyType_Rotate, 0)->keyCount() == 0,
          "static Rotate: a drag creates no explicit key");
    const float after = f.line().channelBaseValue(core::TimeKeyType_Rotate, 0);
    check(after != before, "static Rotate: the base rotation is what the drag edits");
    f.project.commandStack().undo();
    check(f.line().channelBaseValue(core::TimeKeyType_Rotate, 0) == before,
          "static Rotate: one undo restores the base value");
}

void testExistingRotateKeyEditsInPlace() {
    Fixture f;
    {
        auto* key = new core::ChannelKey(core::TimeKeyType_Rotate, 0);
        key->setFrame(10);
        key->setValue(0.0f);
        f.project.commandStack().push(new cmnd::GrabNewObject<core::ChannelKey>(key));
        f.project.commandStack().push(
            f.line().createChannelPusher(core::TimeKeyType_Rotate, 0, 10, key)
        );
    }
    f.target(f.param(false, true));
    const float before = f.line().channelKey(core::TimeKeyType_Rotate, 0, 10)->value();
    f.dragRotation(30.0f);
    check(f.line().channelTrack(core::TimeKeyType_Rotate, 0)->keyCount() == 1,
          "existing Rotate key: the drag does not add a second key");
    check(f.line().channelKey(core::TimeKeyType_Rotate, 0, 10)->value() != before,
          "existing Rotate key: the key at the playhead is edited");
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    testStaticEditsDefault();
    testExistingKeyEditsInPlace();
    testAnimatedChannelAutoKeys();
    testCheckboxForcesAKey();
    testOtherChannelCheckboxCreatesOnlyThatKey();
    testStaticRotateEditsBase();
    testExistingRotateKeyEditsInPlace();
    if (failures) {
        std::printf("FAILED %d check(s)\n", failures);
        return 1;
    }
    std::printf("OK: transform keys follow static/animated/forced policy\n");
    return 0;
}
