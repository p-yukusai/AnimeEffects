// Reproduction harness for the reported crash: posing bones with the POSE
// tool's "Pull bones" mode (ctrl::PoseEditMode_Draw -> pose::DrawBoneMode)
// crashes, while "Move bone" (ctrl::PoseEditMode_Move -> pose::TransBoneMode)
// does not.
//
// It drives the PRODUCTION route, not a private copy of it:
//   * loads a real .anie through ctrl::ProjectLoader (same as roundtrip_main),
//   * builds the real ctrl::Driver with the Pose tool selected and the real
//     PoseParam(PoseEditMode_Draw), and connects project.onTimeLineModified to
//     ctrl::Driver::updateKey exactly like gui::DriverHolder does,
//   * feeds synthetic cursor frames through core::AbstractCursor and
//     Driver::updateCursor -- the same call MainDisplayWidget makes.
//
// The click point is computed the way ctrl::bone::Focuser computes its hit
// test: camera.toScreenPos(targetMtx * bone->worldPos()), with targetMtx the
// same matrix PoseEditor::resetCurrentTarget reads out of the current
// TimeKeyExpans. Candidate bones come from the area BoneKey's tree and from the
// currently blended pose, so both the "pose key already at this frame" and the
// "create a new pose key" paths are exercised.
//
// "Erase pose" (ctrl::PoseEditMode_Erase -> pose::ErasePoseMode) has no
// focuser: its edit is a brush multiply, so a bone drag is not its hit test and
// a rest pose hides the edit even though the paint path ran. Its hit rule is
// therefore "the brush covers a bone joint AND the paint path emitted its
// TimeLineEvent"; see "the erase hit rule" in main().
//
// Usage:
//   pose_pull_crash <file.anie> [options]
//     --mode draw|move|erase   tool sub-mode (default draw)
//     --frame N                animator frame (default: the area bone key frame)
//     --node NAME              object node name to pose (default: first with a bone key)
//     --moves N                drag events per gesture (default 8)
//     --repeat N               gestures on the first bone that hits (default 1)
//     --all-bones              try every candidate bone instead of stopping at the first hit
//     --grid STEP              brute-force screen grid, STEP pixels (instead of bone positions)
//     --direct                 drive ctrl::PoseEditor directly instead of ctrl::Driver
//     --list                   only list candidate nodes/bones and exit
//
// Exit status 0 = ran to completion without crashing. A crash leaves the
// process dead at the crashing frame; run it under gdb, e.g.
//   xvfb-run -a gdb -batch -ex run -ex "bt 25" --args build/tests/Debug/pose_pull_crash <file.anie>
//
// Headless: needs a live GL context because loading a project constructs
// LayerNode/MeshTransformer (same reason roundtrip embeds assets.qrc).
#include <QApplication>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLFunctions_4_0_Core>
#include <QSurfaceFormat>
#include <QStringList>
#include <QSize>
#include <QPoint>
#include <QPointF>
#include <QFont>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "XCAssert.h"
#include "gl/Global.h"
#include "gl/DeviceInfo.h"
#include "gui/AudioPlaybackWidget.h"
#include "core/Project.h"
#include "core/Animator.h"
#include "core/ObjectNode.h"
#include "core/TimeLine.h"
#include "core/TimeKeyExpans.h"
#include "core/BoneKey.h"
#include "core/Bone2.h"
#include "core/PoseKey.h"
#include "core/AbstractCursor.h"
#include "core/CameraInfo.h"
#include "core/TimeKeyBlender.h"
#include "ctrl/ProjectLoader.h"
#include "ctrl/Driver.h"
#include "ctrl/DriverResources.h"
#include "ctrl/GraphicStyle.h"
#include "ctrl/PoseEditor.h"
#include "ctrl/UILogger.h"
#include "util/IProgressReporter.h"

// Project.cpp references the GUI AudioPlaybackWidget::deserialize (only for the
// .aemus sidecar). Stub it, same as roundtrip/creation_select_keys.
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

namespace {

class HarnessAssertHandler: public XCAssertHandler {
public:
    void failure() const override {
        std::fprintf(stderr, "XC_ASSERT FAILURE (aborting)\n");
        std::fflush(stderr);
        std::abort();
    }
};
class HarnessErrorHandler: public XCErrorHandler {
public:
    void critical(const QString& text, const QString& info, const QString& detail) const override {
        std::fprintf(stderr, "XC_FATAL %s %s %s\n", qPrintable(text), qPrintable(info), qPrintable(detail));
        std::fflush(stderr);
        std::abort();
    }
};
HarnessAssertHandler gAssertHandler;
HarnessErrorHandler gErrorHandler;

class NullReporter: public util::IProgressReporter {
public:
    void setSection(const QString&) override {}
    void setMaximum(int) override {}
    void setProgress(int) override {}
    bool wasCanceled() const override { return false; }
};

// Animator whose frame the harness controls: PoseEditor keys off
// mProject.animator().currentFrame().
class HarnessAnimator: public core::Animator {
public:
    core::Frame mFrame;
    core::Frame currentFrame() const override { return mFrame; }
    void stop() override {}
    void suspend() override {}
    void resume() override {}
    bool isSuspended() const override { return false; }
};

class HarnessLogger: public ctrl::UILogger {
public:
    void pushLog(const QString& aMessage, ctrl::UILogType aType) override {
        std::fprintf(stderr, "LOG[%d] %s\n", int(aType), qPrintable(aMessage));
    }
};

class HarnessStyle: public ctrl::GraphicStyle {
public:
    QFont font() const override { return QFont(); }
    QRect boundingRect(const QString&) const override { return QRect(); }
    QIcon icon(const QString&) const override { return QIcon(); }
};

gl::DeviceInfo makeDeviceInfo() {
    gl::DeviceInfo dev;
    dev.vender = "harness";
    dev.renderer = "harness";
    dev.version = "5";
    dev.maxTextureSize = 16384;
    dev.maxRenderBufferSize = 16384;
    return dev;
}

void collectNodes(core::ObjectNode* aNode, std::vector<core::ObjectNode*>& aOut) {
    if (!aNode)
        return;
    aOut.push_back(aNode);
    for (auto child : aNode->children()) {
        collectNodes(child, aOut);
    }
}

// The last key of aType at or before aFrame, i.e. what TimeKeyGatherer would
// call the area key.
core::TimeKey* lastKeyAt(core::ObjectNode* aNode, core::TimeKeyType aType, int aFrame) {
    if (!aNode->timeLine())
        return nullptr;
    const core::TimeLine::MapType& map = aNode->timeLine()->map(aType);
    core::TimeKey* found = nullptr;
    for (auto itr = map.begin(); itr != map.end(); ++itr) {
        if (itr.key() <= aFrame)
            found = itr.value();
        else
            break;
    }
    return found;
}

float rotateSum(const core::PoseKey* aKey) {
    if (!aKey)
        return 0.0f;
    float sum = 0.0f;
    for (const core::Bone2* top : aKey->data().topBones()) {
        core::Bone2::ConstIterator itr(top);
        while (itr.hasNext()) {
            sum += itr.next()->rotate();
        }
    }
    return sum;
}

// One deterministic left-button gesture: press at aPress, N drag events, release.
struct Gesture {
    core::AbstractCursor cursor;
    const core::CameraInfo& camera;

    explicit Gesture(const core::CameraInfo& aCamera): cursor(), camera(aCamera) {}

    void press(QPoint aPos) {
        QMouseEvent ev(
            QEvent::MouseButtonPress, QPointF(aPos), QPointF(aPos), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier
        );
        cursor.setMousePress(&ev, camera);
    }
    void move(QPoint aPos) {
        QMouseEvent ev(QEvent::MouseMove, QPointF(aPos), QPointF(aPos), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        cursor.setMouseMove(&ev, camera);
    }
    void release(QPoint aPos) {
        QMouseEvent ev(
            QEvent::MouseButtonRelease, QPointF(aPos), QPointF(aPos), Qt::LeftButton, Qt::NoButton, Qt::NoModifier
        );
        cursor.setMouseRelease(&ev, camera);
    }
};

struct Options {
    QString path;
    QString mode = "draw";
    QString nodeName;
    int frame = INT_MIN;
    int moves = 8;
    int repeat = 1;
    int grid = 0;
    bool allBones = false;
    bool direct = false;
    bool list = false;
};

ctrl::PoseEditMode modeFrom(const QString& aName) {
    if (aName == "move")
        return ctrl::PoseEditMode_Move;
    if (aName == "erase")
        return ctrl::PoseEditMode_Erase;
    return ctrl::PoseEditMode_Draw;
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    Options opt;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        auto next = [&]() -> QString {
            if (i + 1 < argc)
                return QString::fromLocal8Bit(argv[++i]);
            return QString();
        };
        if (arg == "--mode")
            opt.mode = next();
        else if (arg == "--frame")
            opt.frame = next().toInt();
        else if (arg == "--node")
            opt.nodeName = next();
        else if (arg == "--moves")
            opt.moves = next().toInt();
        else if (arg == "--repeat")
            opt.repeat = next().toInt();
        else if (arg == "--grid")
            opt.grid = next().toInt();
        else if (arg == "--all-bones")
            opt.allBones = true;
        else if (arg == "--direct")
            opt.direct = true;
        else if (arg == "--list")
            opt.list = true;
        else if (arg.startsWith("--")) {
            std::fprintf(stderr, "unknown option %s\n", qPrintable(arg));
            return 2;
        } else if (opt.path.isEmpty())
            opt.path = arg;
    }
    if (opt.path.isEmpty()) {
        std::fprintf(stderr, "usage: %s <file.anie> [--mode draw|move|erase] [--frame N] [--node NAME]\n", argv[0]);
        return 2;
    }

    // ---- live GL context (see roundtrip_main) ----
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setVersion(gl::Global::kVersion.first, gl::Global::kVersion.second);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);
    QOpenGLWidget glWidget;
    glWidget.resize(4, 4);
    glWidget.show();
    glWidget.makeCurrent();
    if (!glWidget.context()) {
        std::fprintf(stderr, "GL context get failed\n");
        return 2;
    }
    auto funcs = QOpenGLVersionFunctionsFactory::get<gl::Global::Functions>(glWidget.context());
    if (!funcs || !funcs->initializeOpenGLFunctions()) {
        std::fprintf(stderr, "GL functions init failed\n");
        return 2;
    }
    gl::Global::setContext(glWidget);
    gl::Global::setFunctions(*funcs);

    gl::DeviceInfo dev = makeDeviceInfo();
    gl::DeviceInfo::setInstance(&dev);

    HarnessAnimator animator;
    NullReporter reporter;
    HarnessLogger logger;
    HarnessStyle style;

    core::Project project(opt.path, animator, nullptr);
    {
        ctrl::ProjectLoader loader;
        if (!loader.load(opt.path, project, dev, reporter)) {
            std::fprintf(stderr, "LOAD FAILED: %s\n", qPrintable(opt.path));
            for (const QString& l : loader.log())
                std::fprintf(stderr, "  %s\n", qPrintable(l));
            return 2;
        }
    }
    std::printf("loaded %s\n", qPrintable(opt.path));
    std::fflush(stdout);

    // ---- pick a target node with a usable area bone key ----
    std::vector<core::ObjectNode*> nodes;
    collectNodes(project.objectTree().topNode(), nodes);

    std::vector<core::ObjectNode*> candidates;
    for (auto* node : nodes) {
        if (!node->timeLine() || node->timeLine()->isEmpty(core::TimeKeyType_Bone))
            continue;
        if (!opt.nodeName.isEmpty() && node->name() != opt.nodeName)
            continue;
        candidates.push_back(node);
    }
    std::printf("nodes with a BoneKey: %d\n", int(candidates.size()));
    if (candidates.empty()) {
        std::fprintf(stderr, "no node carries a BoneKey\n");
        return 2;
    }

    core::ObjectNode* target = nullptr;
    int frame = 0;
    core::BoneKey* areaKey = nullptr;
    for (auto* node : candidates) {
        core::TimeKey* lastBone = nullptr;
        for (auto itr = node->timeLine()->map(core::TimeKeyType_Bone).begin();
             itr != node->timeLine()->map(core::TimeKeyType_Bone).end();
             ++itr) {
            lastBone = itr.value();
        }
        const int f = (opt.frame != INT_MIN) ? opt.frame : lastBone->frame();

        animator.mFrame = core::Frame(f);
        core::TimeKeyBlender blender(project.objectTree());
        blender.updateCurrents(project.objectTree().topNode(), project.currentTimeInfo());

        auto* key = node->timeLine()->current().bone().areaKey();
        std::printf(
            "candidate node '%s' frame %d areaKey %p poseParent %p\n",
            qPrintable(node->name()),
            f,
            (void*)key,
            (void*)node->timeLine()->current().poseParent()
        );
        if (key) {
            target = node;
            frame = f;
            areaKey = key;
            break;
        }
    }
    if (!target) {
        std::fprintf(stderr, "no node has a live area bone key at its bone key frame\n");
        return 2;
    }

    // ---- list the bone trees we can click on ----
    struct BoneRef {
        const core::Bone2* bone;
        const char* source;
    };
    std::vector<BoneRef> bones;
    for (const core::Bone2* top : areaKey->data().topBones()) {
        core::Bone2::ConstIterator itr(top);
        while (itr.hasNext()) {
            const core::Bone2* bone = itr.next();
            if (bone->parent())
                bones.push_back({bone, "areaKey"});
        }
    }
    {
        core::TimeKey* poseKey = lastKeyAt(target, core::TimeKeyType_Pose, frame);
        if (poseKey) {
            for (const core::Bone2* top : static_cast<core::PoseKey*>(poseKey)->data().topBones()) {
                core::Bone2::ConstIterator itr(top);
                while (itr.hasNext()) {
                    const core::Bone2* bone = itr.next();
                    if (bone->parent())
                        bones.push_back({bone, "pose"});
                }
            }
        }
    }

    core::TimeKeyExpans& expans = target->timeLine()->current();
    const QMatrix4x4 targetMtx =
        expans.bone().isAffectedByBinding() ? expans.bone().worldCSRTMatrix() : expans.srt().worldCSRTMatrix();

    core::CameraInfo camera;
    camera.reset(QSize(1280, 720), 1.0, project.attribute().imageSize(), QPoint(0, 0));

    auto toScreen = [&](const core::Bone2* aBone) {
        return camera.toScreenPos(targetMtx.map(QVector3D(aBone->worldPos()))).toVector2D().toPoint();
    };

    std::printf(
        "target '%s' frame %d mode %s bones %d affectedByBinding %d\n",
        qPrintable(target->name()),
        frame,
        qPrintable(opt.mode),
        int(bones.size()),
        int(expans.bone().isAffectedByBinding())
    );
    for (size_t i = 0; i < bones.size(); ++i) {
        const QPoint p = toScreen(bones[i].bone);
        std::printf(
            "  bone[%d] %s worldPos(%.2f,%.2f) -> screen(%d,%d)\n",
            int(i),
            bones[i].source,
            bones[i].bone->worldPos().x(),
            bones[i].bone->worldPos().y(),
            p.x(),
            p.y()
        );
    }
    std::fflush(stdout);
    if (opt.list)
        return 0;

    const ctrl::PoseEditMode mode = modeFrom(opt.mode);
    core::TimeKey* poseBefore = lastKeyAt(target, core::TimeKeyType_Pose, frame);

    // ---- production driver ----
    ctrl::DriverResources resources;
    ctrl::Driver driver(project, resources, style, logger);
    util::SlotId timeLineSlot;

    ctrl::PoseEditor directEditor(project, logger);

    ctrl::PoseParam param;
    param.mode = mode;

    if (opt.direct) {
        // One level lower: the PoseEditor itself, with the same blending
        // notification gui::DriverHolder would run.
        directEditor.updateParam(param);
        const bool ok = directEditor.setTarget(target);
        timeLineSlot = project.onTimeLineModified.connect([&](core::TimeLineEvent& aEvent, bool aUndo) {
            (void)aUndo;
            core::TimeKeyBlender blender(project.objectTree());
            blender.clearCaches(aEvent);
            blender.updateCurrents(project.objectTree().topNode(), project.currentTimeInfo());
        });
        std::printf("direct PoseEditor::setTarget -> %d\n", int(ok));
    } else {
        timeLineSlot = project.onTimeLineModified.connect(&driver, &ctrl::Driver::updateKey);
        driver.setTool(ctrl::ToolType_Pose);
        driver.setTarget(target);
        driver.updateParam(param);

        // Sanity check through a throwaway PoseEditor: does this target/mode
        // give a live pose key at this frame? (It creates and frees its own
        // key; the timeline is untouched unless a gesture pushes it.)
        ctrl::PoseEditor probe(project, logger);
        probe.updateParam(param);
        std::printf("PoseEditor::setTarget -> %d\n", int(probe.setTarget(target)));
    }
    std::fflush(stdout);

    // ---- the erase hit rule ----
    // ErasePoseMode has no bone::Focuser, so a bone drag means nothing to it: its
    // edit is a brush multiply (src/ctrl/pose/pose_ErasePoseMode.cpp,
    // updatePaint). The brush centre is the cursor's world position (set in
    // updateCursor) and every bone joint of the walked tree whose world position
    // is inside Circle(eiRadius) gets rotate() *= eraseRate, eraseRate =
    // 1 - eiPressure. With the PoseParam defaults the harness passes (eiRadius
    // 200, eiPressure 1.0, src/ctrl/PoseParam.cpp:5) that eraseRate is 0, so on a
    // rest pose the edit multiplies zeros: a real pass through updatePaint that
    // moves no key value, which the pose-key delta the other modes use can never
    // report. Erase therefore gets its own rule:
    //  (1) the brush covers at least one joint of the tree updatePaint walks,
    //      tested exactly like the mode tests it (cursor world position from the
    //      same CameraInfo, targetMtx * bone->worldPos(), PoseParam::eiRadius),
    //  (2) the paint path really ran, observed as the TimeLineEvents the erase
    //      command emits: the first commit's macro fires the grabbed
    //      TimeLineUtil::Notifier when it closes and each coalesced commit
    //      notifies explicitly, so every updatePaint leaves one event. Both
    //      halves of the gesture are required -- an event from the PRESS (erase
    //      paints on press; draw and move only select a bone there, so an event
    //      at the press can only come from erase) and an event from a DRAG, i.e.
    //      updatePaint re-entered while the gate mGesture.continues() decides
    //      whether the drag coalesces into the open erase command.
    int eraseEvents = 0;
    const util::SlotId eraseSlot = project.onTimeLineModified.connect([&](core::TimeLineEvent&, bool) {
        ++eraseEvents;
    });

    auto eraseBrushJointCount = [&](QPoint aPress) {
        const QVector2D center = camera.toWorldPos(QVector2D(aPress));
        const float radius = param.eiRadius;
        int covered = 0;
        auto countTree = [&](const auto& aTopBones) {
            for (const core::Bone2* top : aTopBones) {
                core::Bone2::ConstIterator itr(top);
                while (itr.hasNext()) {
                    const QVector2D joint = targetMtx.map(QVector3D(itr.next()->worldPos())).toVector2D();
                    if ((joint - center).lengthSquared() <= radius * radius)
                        ++covered;
                }
            }
        };
        // the tree updatePaint walks: the pose key at this frame when one exists
        // (PoseEditor::initializeKey), else the area key a new key is built from
        if (core::TimeKey* poseKey = lastKeyAt(target, core::TimeKeyType_Pose, frame))
            countTree(static_cast<core::PoseKey*>(poseKey)->data().topBones());
        else
            countTree(areaKey->data().topBones());
        return covered;
    };

    // (2) of the erase hit rule: the press painted and a drag re-entered paint.
    auto erasePaintReached = [&](int aEventsBefore, int aEventsAtPress) {
        return aEventsAtPress > aEventsBefore && eraseEvents > aEventsAtPress;
    };

    Gesture gesture(camera);
    auto dispatch = [&](Gesture& g) {
        // Driver::updateCursor is what MainDisplayWidget calls; --direct drives
        // the PoseEditor one level lower.
        if (opt.direct)
            (void)directEditor.updateCursor(camera, g.cursor);
        else
            (void)driver.updateCursor(g.cursor, camera);
    };

    // One deterministic left-button gesture. Returns the erase-event count right
    // after the press: ErasePoseMode paints (and so notifies) on the press itself
    // (updateCursor), while draw/move only select a bone there, so a non-zero
    // delta at that point is erase-specific evidence (always 0 in the other two).
    auto runGesture = [&](QPoint press, int moves) {
        const float angle = 0.6f;
        const QPoint dir(int(60.0f * std::cos(angle)), int(60.0f * std::sin(angle)));
        gesture.press(press);
        dispatch(gesture);
        const int eventsAtPress = eraseEvents;
        for (int i = 1; i <= moves; ++i) {
            const QPoint p = press + (dir * i) / moves;
            gesture.move(p);
            dispatch(gesture);
        }
        gesture.release(press + dir);
        dispatch(gesture);
        return eventsAtPress;
    };

    int hits = 0;
    if (opt.grid > 0) {
        std::printf("grid scan step %d over %dx%d\n", opt.grid, camera.screenWidth(), camera.screenHeight());
        std::fflush(stdout);
        const core::TimeLine::MapType& poseMap = target->timeLine()->map(core::TimeKeyType_Pose);
        for (int y = 0; y < camera.screenHeight() && !hits; y += opt.grid) {
            for (int x = 0; x < camera.screenWidth() && !hits; x += opt.grid) {
                const int countBefore = int(poseMap.size());
                const float rotBefore = rotateSum(static_cast<core::PoseKey*>(poseBefore));
                const int jointsCovered =
                    mode == ctrl::PoseEditMode_Erase ? eraseBrushJointCount(QPoint(x, y)) : 0;
                const int eventsBefore = eraseEvents;
                const int eventsAtPress = runGesture(QPoint(x, y), opt.moves);
                const bool changed = int(poseMap.size()) != countBefore ||
                    rotateSum(static_cast<core::PoseKey*>(lastKeyAt(target, core::TimeKeyType_Pose, frame))) != rotBefore ||
                    (mode == ctrl::PoseEditMode_Erase && jointsCovered > 0 &&
                     erasePaintReached(eventsBefore, eventsAtPress));
                if (changed) {
                    std::printf("HIT at screen(%d,%d)\n", x, y);
                    std::fflush(stdout);
                    ++hits;
                    poseBefore = lastKeyAt(target, core::TimeKeyType_Pose, frame);
                }
            }
        }
    } else {
        for (size_t i = 0; i < bones.size(); ++i) {
            const QPoint press = toScreen(bones[i].bone);
            for (int rep = 0; rep < opt.repeat; ++rep) {
                const int countBefore = int(target->timeLine()->map(core::TimeKeyType_Pose).size());
                const float rotBefore = rotateSum(static_cast<core::PoseKey*>(poseBefore));
                const int jointsCovered = mode == ctrl::PoseEditMode_Erase ? eraseBrushJointCount(press) : 0;
                const int eventsBefore = eraseEvents;
                const int eventsAtPress = runGesture(press, opt.moves);
                const float rotAfter =
                    rotateSum(static_cast<core::PoseKey*>(lastKeyAt(target, core::TimeKeyType_Pose, frame)));
                // erase hit rule above: brush covers a joint and the paint path ran
                const bool eraseHit = mode == ctrl::PoseEditMode_Erase && jointsCovered > 0 &&
                    erasePaintReached(eventsBefore, eventsAtPress);
                const bool changed = int(target->timeLine()->map(core::TimeKeyType_Pose).size()) != countBefore ||
                    rotAfter != rotBefore || eraseHit;
                std::printf(
                    "gesture bone[%d] %s at (%d,%d) rep %d -> %s\n",
                    int(i),
                    bones[i].source,
                    press.x(),
                    press.y(),
                    rep,
                    changed ? "HIT" : "miss"
                );
                std::fflush(stdout);
                if (changed) {
                    ++hits;
                    poseBefore = lastKeyAt(target, core::TimeKeyType_Pose, frame);
                    if (!opt.allBones)
                        break;
                }
            }
            if (hits && !opt.allBones)
                break;
        }
    }

    project.onTimeLineModified.disconnect(timeLineSlot);
    project.onTimeLineModified.disconnect(eraseSlot);
    std::printf("DONE hits=%d (no crash)\n", hits);
    std::fflush(stdout);
    return hits > 0 ? 0 : 1;
}
