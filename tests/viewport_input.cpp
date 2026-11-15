// THE VIEWPORT'S INPUT ROUTER, DRIVEN WITHOUT A WINDOW.
//
// Two defects were reported against the live application and neither had a gate:
//
//   "the brush overlay only follows the mouse when some mouse buttons are pressed"
//       -> a move with NO gesture returned false and the tool never saw the sample
//   "when drawing it only does one tap instead of keeping drawing while lmb is down"
//       -> the drag's buttons came from the QMouseEvent's `buttons()`, which is only populated for
//          the widget Qt routed the press to
//
// This test builds a fake ViewportHost, so every call the router makes - how many samples reached the
// tool, where they were, whether a button was reported - is observable. It uses a REAL
// core::AbstractCursor, because that is what the router reads the pressed state from.
#include <QCoreApplication>
#include <QMouseEvent>
#include <QVector2D>
#include <Qt>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "core/AbstractCursor.h"
#include "core/CameraInfo.h"
#include "core/RenderInfo.h"
#include "gui/ViewportInput.h"
#include "XCAssert.h"

// libcore's Project.cpp calls the AUDIO widget's project serializer. It is irrelevant to pointer
// routing, and compiling that widget would drag its whole tool-panel dependency into a headless test,
// so the one symbol is stubbed here instead.
#include "gui/AudioPlaybackWidget.h"
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

// A failing XC_ASSERT or XC_FATAL_ERROR is a test failure, not a crash log (the stanza every test
// that links libcore needs).
namespace {
class TestAssertHandler: public XCAssertHandler {
public:
    void failure() const override {
        std::printf("FAIL: XC_ASSERT\n");
        std::exit(1);
    }
};

class TestErrorHandler: public XCErrorHandler {
public:
    void critical(const QString&, const QString& aInfo, const QString&) const override {
        std::printf("FAIL: XC_FATAL_ERROR %s\n", aInfo.toUtf8().data());
        std::exit(1);
    }
};

TestAssertHandler gAssertHandler;
TestErrorHandler gErrorHandler;
} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

namespace {

int gFailures = 0;

void check(bool aCondition, const char* aWhat) {
    if (!aCondition) {
        std::printf("FAIL %d: %s\n", __LINE__, aWhat);
        ++gFailures;
    }
}

struct Fed {
    QPointF pos;
    bool leftDown = false;
    bool tablet = false;
};

// The host: records what the router fed the tool, and nothing else.
struct FakeHost: gui::ViewportHost {
    core::RenderInfo info;
    core::AbstractCursor mCursor;
    core::CameraInfo cameraInfo;
    int updates = 0;
    int frames = 0;
    int modifierForwards = 0;
    std::vector<Fed> fed;

    core::RenderInfo& renderInfo() override { return info; }
    core::CameraInfo& camera() override { return cameraInfo; }
    core::AbstractCursor& cursor() override { return mCursor; }
    ctrl::MaskEditor* maskEditor() override { return nullptr; }
    void forwardModifiers(Qt::KeyboardModifiers) override { ++modifierForwards; }
    ctrl::ToolType toolType() const override { return mTool; }
    const ctrl::BrushParam* activeBrush() const override { return nullptr; }
    void updateCursorNow() override {
        ++updates;
        fed.push_back(Fed{mCursor.screenPos().toPointF(), mCursor.isPressedLeft(), false});
    }
    void requestFrame() override { ++frames; }
    void sampleMaskValue(float) override {}
    void beginBrushDial(const QPointF&) override {}
    void stepBrushDial(const QPointF&) override {}
    bool brushDialActive() const override { return false; }
    void endBrushDial() override {}
    QPoint mapToGlobal(const QPoint& aPos) override { return aPos; }
    void setGrabCursor(bool) override {}

    ctrl::ToolType mTool = ctrl::ToolType_SRT;
};

gui::PointerSample sample(QPointF aPos, Qt::MouseButton aButton, Qt::MouseButtons aButtons) {
    gui::PointerSample s;
    s.pos = aPos;
    s.button = aButton;
    s.buttons = aButtons;
    return s;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    // The smoother's default comes from AE_POINTER_SMOOTH; the checks below assert RAW positions,
    // so the default is pinned here and the smoothing block turns its window on explicitly.
    qputenv("AE_POINTER_SMOOTH", "0");

    // ---- 1. HOVER: a move with no gesture still reaches the tool -----------------------------
    {
        FakeHost host;
        host.mTool = ctrl::ToolType_Mask;
        gui::ViewportInput input(host);

        const bool taken = input.move(sample(QPointF(120, 90), Qt::NoButton, Qt::NoButton));
        check(taken, "a hover move is handled by the router");
        check(host.fed.size() == 1, "the tool was fed for a hover (the brush ring follows the pointer)");
        check(!host.fed.empty() && !host.fed.back().leftDown, "the hover reported no button");
        check(host.frames >= 1, "the hover asked for a frame");
    }

    // ---- 2. A BRUSH STROKE IS EVERY SAMPLE, WHILE THE BUTTON IS DOWN ------------------------
    {
        FakeHost host;
        host.mTool = ctrl::ToolType_Mask;
        host.cameraInfo.setScreenSize(QSize(640, 480));
        host.cameraInfo.setImageSize(QSize(640, 480));
        host.cameraInfo.setScale(1.0f);
        gui::ViewportInput input(host);

        // the router presses the cursor itself (the test must not do it twice)
        input.press(sample(QPointF(100, 100), Qt::LeftButton, Qt::LeftButton));

        const size_t afterPress = host.fed.size();
        check(afterPress == 1, "the press fed the tool once");

        // five moves: EVERY one must reach the tool, and the cursor must report the left button -
        // even though the samples carry `buttons` empty, which is what the event looked like when
        // the press had been consumed elsewhere (the reported "one tap" defect)
        for (int i = 1; i <= 5; ++i)
            input.move(sample(QPointF(100.0 + i * 7.0, 100.0), Qt::NoButton, Qt::NoButton));

        check(host.fed.size() == afterPress + 5, "each drag sample reached the tool (not one tap)");
        check(!host.fed.empty() && host.fed.back().leftDown,
              "the drag was fed with the cursor's left button down");
        check(!host.fed.empty() && host.fed.back().pos == QPointF(135.0, 100.0),
              "the last fed sample is the last pointer position");

        input.release(sample(QPointF(135, 100), Qt::LeftButton, Qt::NoButton));
        check(input.isBusy() == false, "the release ended the gesture");
    }

    // ---- 3. A STATE-ONLY GESTURE IS ONE SAMPLE PER FRAME ------------------------------------
    {
        FakeHost host;
        host.mTool = ctrl::ToolType_SRT;
        host.cameraInfo.setScreenSize(QSize(640, 480));
        host.cameraInfo.setImageSize(QSize(640, 480));
        host.cameraInfo.setScale(1.0f);
        gui::ViewportInput input(host);

        input.press(sample(QPointF(50, 50), Qt::LeftButton, Qt::LeftButton));
        const size_t afterPress = host.fed.size();

        for (int i = 1; i <= 5; ++i)
            input.move(sample(QPointF(50.0 + i * 10.0, 50.0), Qt::NoButton, Qt::LeftButton));
        check(host.fed.size() == afterPress, "an SRT drag is HELD, not applied per sample");

        check(input.frameTick(), "the frame fed the held sample");
        check(host.fed.size() == afterPress + 1, "exactly one sample per frame");
        check(!host.fed.empty() && host.fed.back().pos == QPointF(100.0, 50.0),
              "the frame applies the LATEST pointer position");
    }

    // ---- 4. A PEN IS ALWAYS `Every` ---------------------------------------------------------
    {
        FakeHost host;
        host.mTool = ctrl::ToolType_Mask;
        gui::ViewportInput input(host);
        gui::PointerSample pen = sample(QPointF(10, 10), Qt::LeftButton, Qt::LeftButton);
        pen.precise = true;
        input.press(pen);
        check(input.sampling() == gui::Sampling::Every, "a pen press is Every, whatever the tool");
    }

    // ---- 5. THE SMOOTHER: a path, its trail, and the anchor on release ----------------------
    {
        FakeHost host;
        host.mTool = ctrl::ToolType_Mask;
        host.cameraInfo.setScreenSize(QSize(640, 480));
        host.cameraInfo.setImageSize(QSize(640, 480));
        host.cameraInfo.setScale(1.0f);
        gui::ViewportInput input(host);
        input.smoothing().setWindowPx(12.0);
        check(input.smoothing().lagPx() == 6.0, "the trail is half the window");

        input.press(sample(QPointF(100, 100), Qt::LeftButton, Qt::LeftButton));
        // a straight drag of 40 px in 1 px steps: the mean trails by 6 px once the window is full
        for (int i = 1; i <= 40; ++i)
            input.move(sample(QPointF(100.0 + i, 100.0), Qt::NoButton, Qt::NoButton));

        const QPointF tip = host.fed.back().pos;
        check(std::abs(tip.x() - 134.0) < 0.75 && std::abs(tip.y() - 100.0) < 0.001,
              "the smoothed tip trails the pointer by the window's half length");
        check(!host.fed.empty() && host.fed.front().pos == QPointF(100.0, 100.0),
              "the stroke starts exactly under the pen");

        // the release lands the RAW position, so the stroke ends under the nib
        input.release(sample(QPointF(140, 100), Qt::LeftButton, Qt::NoButton));
        check(host.fed.back().pos == QPointF(140.0, 100.0),
              "the release lands the raw pointer position after the smoothed tip");

        // a hover is never smoothed: the ring follows the pointer exactly
        const size_t before = host.fed.size();
        input.move(sample(QPointF(300, 200), Qt::NoButton, Qt::NoButton));
        check(host.fed.size() == before + 1 && host.fed.back().pos == QPointF(300.0, 200.0),
              "a hover is fed unsmoothed");
    }

    // ---- 5b. THE FRACTION SURVIVES THE CURSOR ----------------------------------------------
    // The tools read `cursor.screenPos()`. That position used to be the event's QPoint - a whole
    // pixel - so a smoothed sample was snapped back to the device grid before any tool saw it, and
    // a stroke at 0.5 zoom landed on a 2-content-pixel grid (reported as "quantized" while zoomed
    // out). The cursor now keeps the event's QPointF; this pins it.
    {
        FakeHost host;
        host.mTool = ctrl::ToolType_Mask;
        host.cameraInfo.setScreenSize(QSize(640, 480));
        host.cameraInfo.setImageSize(QSize(640, 480));
        host.cameraInfo.setScale(0.5f);
        gui::ViewportInput input(host);
        input.smoothing().setWindowPx(12.0);

        input.press(sample(QPointF(100, 100), Qt::LeftButton, Qt::LeftButton));
        // steps of (2,1): arc length sqrt(5), so a 12 px window ends between samples
        for (int i = 1; i <= 12; ++i)
            input.move(sample(QPointF(100.0 + i * 2.0, 100.0 + i * 1.0),
                              Qt::NoButton, Qt::NoButton));

        const QVector2D p = host.mCursor.screenPos();
        std::printf("MEASURED smoothed screen pos %.3f,%.3f\n", p.x(), p.y());
        check(std::abs(p.x() - std::round(p.x())) > 0.05
                  || std::abs(p.y() - std::round(p.y())) > 0.05,
              "the cursor keeps the sub-pixel sample");
        const QVector2D w = host.mCursor.worldPos();
        check(std::abs(w.x() - host.cameraInfo.toWorldPos(p).x()) < 1e-3,
              "and the world position is that sample through the camera, not its rounding");
    }

    // ---- 5c. ZOOMED OUT, THE PATH IS NOT ON A CONTENT-PIXEL GRID ----------------------------
    // What the hand sees at fit zoom (0.2): one SCREEN pixel is five CONTENT pixels. Before the
    // cursor kept the event's QPointF, every sample - smoothed or not - was rounded to a whole
    // screen pixel first, so the stamps fell on that 5-px grid and the stroke looked quantized.
    {
        FakeHost host;
        host.mTool = ctrl::ToolType_Mask;
        host.cameraInfo.setScreenSize(QSize(640, 480));
        host.cameraInfo.setImageSize(QSize(640, 480));
        host.cameraInfo.setScale(0.2f);
        gui::ViewportInput input(host);
        input.smoothing().setWindowPx(12.0);

        std::vector<double> worlds;
        input.press(sample(QPointF(300, 300), Qt::LeftButton, Qt::LeftButton));
        for (int i = 1; i <= 40; ++i) {
            // a 1-px staircase, the pointer quantisation the user draws with
            input.move(sample(QPointF(300.0 + i, 300.0 + i * 3.0), Qt::NoButton, Qt::NoButton));
            worlds.push_back(host.mCursor.worldPos().x());
        }
        // count how many land on the 5-px content grid the old rounding produced
        int onGrid = 0;
        for (double w : worlds)
            if (std::abs(w - std::round(w / 5.0) * 5.0) < 1e-3)
                ++onGrid;
        std::printf("MEASURED zoom 0.2: smoothed %d of %d samples on the 5-px grid\n",
                    onGrid, int(worlds.size()));
        check(onGrid < int(worlds.size()),
              "the smoothed path is not snapped to the zoom's content-pixel grid");

        // THE CONTROL: the same drag with the smoother off IS on that grid - which is what the
        // user saw, and what makes the check above mean something.
        input.release(sample(QPointF(340, 420), Qt::LeftButton, Qt::NoButton));
        input.smoothing().setWindowPx(0.0);
        std::vector<double> rawWorlds;
        input.press(sample(QPointF(300, 300), Qt::LeftButton, Qt::LeftButton));
        for (int i = 1; i <= 40; ++i) {
            input.move(sample(QPointF(300.0 + i, 300.0 + i * 3.0), Qt::NoButton, Qt::NoButton));
            rawWorlds.push_back(host.mCursor.worldPos().x());
        }
        int rawOnGrid = 0;
        for (double w : rawWorlds)
            if (std::abs(w - std::round(w / 5.0) * 5.0) < 1e-3)
                ++rawOnGrid;
        std::printf("MEASURED zoom 0.2: unsmoothed %d of %d samples on the 5-px grid\n",
                    rawOnGrid, int(rawWorlds.size()));
        check(rawOnGrid == int(rawWorlds.size()),
              "and without the smoother every sample IS on that grid (the control)");
    }

    // ---- 6. A DISABLED SMOOTHER IS THE IDENTITY --------------------------------------------
    {
        FakeHost host;
        host.mTool = ctrl::ToolType_Mask;
        host.cameraInfo.setScreenSize(QSize(640, 480));
        host.cameraInfo.setImageSize(QSize(640, 480));
        host.cameraInfo.setScale(1.0f);
        gui::ViewportInput input(host);
        input.smoothing().setWindowPx(0.0);
        input.press(sample(QPointF(10, 10), Qt::LeftButton, Qt::LeftButton));
        input.move(sample(QPointF(17, 10), Qt::NoButton, Qt::NoButton));
        check(host.fed.back().pos == QPointF(17.0, 10.0), "window 0 passes the sample through");
    }

    if (gFailures == 0)
        std::printf("viewport_input: hover feeds, a stroke is every sample, a drag is one per frame\n");
    return gFailures == 0 ? 0 : 1;
}
