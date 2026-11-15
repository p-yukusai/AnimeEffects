#include "ctrl/GraphEditor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

#include <QObject>
#include <memory>

#include "cmnd/ScopedMacro.h"
#include "core/BlurKey.h"
#include "core/DepthKey.h"
#include "core/HsvKey.h"
#include "core/ImageKey.h"
#include "core/MoveKey.h"
#include "core/ObjectNode.h"
#include "core/OpaKey.h"
#include "core/Project.h"
#include "core/RotateKey.h"
#include "core/ScaleKey.h"
#include "core/TimeLine.h"
#include "ctrl/CmndName.h"
#include "ctrl/TimeLineUtil.h"
#include "ctrl/graph/graph_Geometry.h"

namespace {

// The panel has no geometry constants of its own any more. It used to be a HUD
// band with an inset and a clamped height; it is now the whole viewport, so
// panelRect() needs neither - and the band constants that only fed that
// geometry (the inset, the height ratio, the min/max band height and the band
// ceiling) went with it.

// The value axis' wheel sensitivity: one physical notch (Qt's 120 units of
// angleDelta) scales the window by 1.1, which is the canvas mover's and the
// timeline scaler's own per-notch zoom factor - so the wheel feels the same over
// the panel and over the viewport. The DIRECTION is theirs too: a notch Qt
// reports as positive zooms OUT (see GraphEditor::updateWheel), which is why the
// value zoom negates the delta before it reaches this factor.
constexpr double kValueZoomPerNotch = 1.1;
constexpr double kWheelNotch = 120.0;

// How many points a segment is walked through when the value axis is fitted.
// The fit cannot read the key values alone: a Custom easing's cubic bezier (and
// Back/Elastic) reaches values beyond BOTH of the keys it sits between, and a
// window fitted to the key values would map that overshoot outside the plot,
// where the panel's plot clip removes it. 16 is the order of
// Curve::buildPolyline's drawing rate, so the fit sees what is drawn.
constexpr int kFitSamplesPerSegment = 16;

double notchZoomFactor(int aWheelDelta) {
    return std::pow(kValueZoomPerNotch, static_cast<double>(aWheelDelta) / kWheelNotch);
}

double pixelDistance(const QPointF& aLhs, const QPointF& aRhs) {
    return std::hypot(aLhs.x() - aRhs.x(), aLhs.y() - aRhs.y());
}

// The inverse of graph::handlePoint for one drag position. aFlatY is what the y
// component falls back to when the segment's value span is zero: a constant
// segment has no value axis to normalize across, so the vertical component of
// the drag has nothing to write and the handle keeps the y it had.
QPointF
handleNormalized(const ctrl::graph::Scaler& aScale, const ctrl::graph::Curve::Segment& aSegment, const QPoint& aPos,
    double aFlatY) {
    // Keys are unique per frame, so a segment always spans at least one frame;
    // a degenerate span could not be inverted anyway.
    const double frameSpan = static_cast<double>(aSegment.to->frame() - aSegment.from->frame());
    if (!(frameSpan > 0.0))
        return QPointF(0.0, aFlatY);

    const double nx = (aScale.frameAtPixel(aPos.x()) - static_cast<double>(aSegment.from->frame())) / frameSpan;

    const double valueSpan = static_cast<double>(aSegment.toValue - aSegment.fromValue);
    if (valueSpan == 0.0) {
        return QPointF(nx, aFlatY);
    }
    const double ny =
        (static_cast<double>(aScale.valueAtPixel(static_cast<double>(aPos.y()))) - aSegment.fromValue) / valueSpan;
    return QPointF(nx, ny);
}

// The value the graph draws for one component of one key type at aFrame: what
// Curve::sample() returns for that (type, component) channel, i.e. the number
// playback evaluates there through the `from` key's easing.
//
// The insert seeds EVERY component of the new key through this, not just the
// one the user's curve shows: a key holds all of its type's components at once,
// so adding a key from a Move X curve must carry the evaluated Y as well or the
// insert would change the animation it is meant to leave alone.
float sampledChannelValue(const core::TimeLine& aLine, core::TimeKeyType aType, int aComponent, core::Frame aFrame) {
    return ctrl::graph::Curve(aLine, ctrl::graph::Channel(aType, aComponent)).sample(aFrame);
}

} // namespace

namespace ctrl {

    //-------------------------------------------------------------------------------------------------
    GraphEditor::Hit::Hit(): hitCurve(false), hitHandle(false), item(-1), segment(-1), handle(-1), keyFrame() {}

    //-------------------------------------------------------------------------------------------------
    GraphEditor::GraphEditor(core::Project& aProject, UILogger& aLogger):
        mProject(&aProject),
        mLogger(aLogger),
        mTargets(),
        mHiddenChannels(),
        mOwnSelection(),
        mSelection(&mOwnSelection),
        mScale(),
        mVisible(false),
        mFrameWindowShared(false),
        mFrameWindow(),
        mAppliedFrameWindow(),
        mAppliedPlotWidth(0), // nothing mapped yet: the first sync always maps
        mAppliedFramePpf(0),
        mScrollFrames(),
        mZoomFrames(),
        mModifiers(Qt::NoModifier),
        mFocusNode(nullptr),
        mFocusChannel(),
        mValueBasis(),
        mDragging(false),
        mKeyMoveArmed(false),
        mMarqueeArmed(false),
        mPanning(false),
        mDragKey(),
        mDragItem(-1),
        mDragSegment(-1),
        mDragHandle(-1),
        mGesture(aProject.commandStack()),
        mMarqueeOrigin(),
        mMarqueeCurrent(),
        mAddToSelection(false),
        mPanPointer(),
        mMoveLine(nullptr),
        mMoveNode(nullptr),
        mMoveType(core::TimeKeyType_TERM),
        mMoveComponent(0),
        mMoveFrame(),
        mMovePointerFrame(),
        mMovePressPos(),
        mMoveBaseValue(0.0f),
        mMoveCommand(nullptr),
        mValueCommand(nullptr),
        mAxisFps(0) {
        // The graph starts HIDDEN. The Driver routes every cursor and wheel
        // event to the tools while isVisible() is false, so a panel nobody asked
        // for cannot take the cursor on the first frame; a user shows it and the
        // state below comes back with it.
        syncFrameAxis();
    }

    GraphEditor::~GraphEditor() {
        // Nothing here is owned, and an edit that was never committed pushes
        // nothing (cmnd::Gesture's contract), so teardown needs neither a commit
        // nor a rollback.
    }

    //-------------------------------------------------------------------------------------------------
    void GraphEditor::setProject(core::Project* aProject) {
        // Everything this editor holds belongs to the previous document: the
        // target pointers, the hidden-channel keys and the selection are all
        // pointers into it (or copies naming it), and an open gesture belongs to
        // its command stack.
        endDrag();
        mTargets.clear();
        mHiddenChannels.clear();
        clearSelection();
        mProject = aProject;

        if (aProject) {
            // mGesture holds its command stack by reference, so a different
            // document needs a different stack and the reference cannot be
            // reassigned. The gesture is inert at this point (endDrag() above),
            // so it is reconstructed in place - the only way to move it - and an
            // edit can never be pushed into a stack that no longer belongs to the
            // document being edited.
            mGesture.~Gesture();
            ::new (static_cast<void*>(&mGesture))
                cmnd::Gesture<cmnd::ModifiableAssign<util::Easing::Param> >(aProject->commandStack());
            syncFrameAxis();
        }

        // A different document is different data: the remembered basis, and any
        // focus pointing into the old document, belong to it. setProject cleared
        // the targets above, so the next sync either has nothing to fit (window
        // kept) or new data to fit, and it drops the (now absent) focus.
        syncValueAxis();
    }

    void GraphEditor::setTargets(const QVector<core::ObjectNode*>& aTargets) {
        // Stored as handed in: the target list is the object tree selection's
        // own currency, and nothing is resolved here - the entries are rebuilt
        // from the document on every use, so a target whose timeline changes
        // later is still drawn correctly.
        mTargets = aTargets;

        // A new selection is a new set of shown curves, so it is a new basis and
        // the axis is fitted to it (and a focus that was on a channel the new
        // selection no longer shows is dropped by the same sync). Setting the
        // SAME selection again changes nothing and so refits nothing, which keeps
        // a wheel zoom through a redundant re-selection.
        syncValueAxis();
    }

    void GraphEditor::setVisible(bool aVisible) {
        if (mVisible == aVisible)
            return;

        mVisible = aVisible;

        if (!aVisible) {
            // Hiding drops a drag in flight. The panel is gone, so the pixels a
            // further move would be measured against are gone too, and a gesture
            // left open would keep coalescing the next edit into a command whose
            // geometry nobody can see. The selection, the scale and the hidden
            // channels are kept, so showing the panel again restores it as it
            // was.
            endDrag();
        } else {
            // Becoming visible SHOWS the data, and the application toggles the
            // panel on without a cursor event ever reaching it - so the fit
            // cannot be left to updateCursor alone: this is the one path that
            // must fit on its own, or the first paint of an opened panel would
            // draw the seed window around a layer whose values live elsewhere.
            syncValueAxis();
        }
    }

    //-------------------------------------------------------------------------------------------------
    QRect GraphEditor::panelRect(const QSize& aViewportSize) {
        // The panel is a fullscreen overlay drawn over the animation with no
        // background of its own: the WHOLE viewport, with no band, no inset and
        // no clamped height. It cannot own the pointer by rectangle for exactly
        // that reason - a rect that contains everything would swallow every
        // event the viewport tools need - so ownership is hit-based instead, see
        // ownsCursor. Still a pure function of the size: no camera, no layout
        // pass.
        return QRect(QPoint(0, 0), aViewportSize);
    }

    QRect GraphEditor::currentPanelRect(const core::CameraInfo& aCamera) const {
        // Pure: the panel is pinned over the canvas, so it never depends on the
        // camera's transform, only on the viewport it covers (the whole of it).
        // The callers that are allowed to write (updateCursor/updateWheel) copy
        // this into mScale; ownsCursor re-derives it into a local copy, because a
        // hit query may not mutate the editor.
        return panelRect(aCamera.screenSize());
    }

    //-------------------------------------------------------------------------------------------------
    // The frame axis' tick ladder belongs to the timeline's scaler, so the graph
    // hands it the same pair ctrl::TimeLineEditor does: the project's fps and the
    // end of its frame range. Both setters rebuild the ladder, so they are called
    // only when the attribute actually moved - this runs on every cursor event,
    // and the project's attribute can change while the panel is open.
    void GraphEditor::syncFrameAxis() {
        if (!mProject)
            return;

        const core::Project::Attribute& attribute = mProject->attribute();
        const int fps = attribute.fps();
        const int frameEnd = attribute.frameRange().max();

        ctrl::time::Scaler& axis = mScale.frameScale();
        if (mAxisFps == fps && axis.frameEnd() == frameEnd)
            return;

        axis.setFps(fps);
        axis.setFrameEnd(frameEnd);
        mAxisFps = fps;
    }

    //-------------------------------------------------------------------------------------------------
    // The shared frame window (see the header). The timeline hands in the frames
    // it is showing; this maps them onto the panel's plot.
    void GraphEditor::setFrameWindow(const util::Range& aFrames) {
        mFrameWindowShared = true;
        mFrameWindow = aFrames;
        syncFrameWindow();
    }

    void GraphEditor::setFrameGestureDelegates(FrameScrollFunc aScrollFrames, FrameZoomFunc aZoomFrames) {
        mScrollFrames = aScrollFrames;
        mZoomFrames = aZoomFrames;
    }

    void GraphEditor::syncFrameWindowFor(const QSize& aViewportSize) {
        if (!mFrameWindowShared)
            return; // the frame axis is the panel's own: nothing to derive

        // The same rect the cursor path writes (a pure function of the viewport), so
        // the geometry the panel is DRAWN with and the geometry it is PICKED through
        // are one, and the mapping below is derived from the rect the drawing layer
        // will copy.
        mScale.setRect(panelRect(aViewportSize));
        syncFrameWindow();
    }

    void GraphEditor::clearFrameAxis() {
        mFrameWindowShared = false;
        mFrameWindow = util::Range();
        mAppliedFrameWindow = util::Range();
        mAppliedPlotWidth = 0; // nothing mapped: a later attach maps afresh
        mAppliedFramePpf = 0;
        mScrollFrames = nullptr;
        mZoomFrames = nullptr;
    }

    void GraphEditor::syncFrameWindow() {
        if (!mFrameWindowShared)
            return;

        const QRect plot = mScale.plotRect();
        if (plot.width() <= 0)
            return; // no plot to map into (an unlaid-out scale): keep the window

        // Already mapped for this range, this plot and this rung: the write below
        // is not a fixed point (see the members), so it must not run twice on the
        // same inputs.
        const int plotWidth = plot.width();
        const int currentPpf = mScale.frameScale().pixelsPerFrame();
        if (mFrameWindow == mAppliedFrameWindow && plotWidth == mAppliedPlotWidth
            && currentPpf == mAppliedFramePpf) {
            return;
        }

        // The window is [lo, hi] in frames; a span of at least one frame, so a
        // degenerate timeline viewport (a one-frame window) still has a positive
        // divisor and does not ask the ladder for infinite pixels per frame.
        const double lo = static_cast<double>(mFrameWindow.min());
        const double hi = static_cast<double>(mFrameWindow.max());
        const double span = std::max(1.0, hi - lo);

        // The rung: the largest zoom the ladder has that still fits the pushed span
        // in the plot's INSET width (kFrameWindowMargin held at each end, see the
        // header), which is graph::Scaler::fitFrameAxis's rule with that margin in
        // place of its pad. The inset is not decoration: the origin below is written
        // through the scaler's integer pixel origin, so an end can land a pixel
        // closer to its edge than this arithmetic asks - and a key AT the window's
        // first or last frame must be drawn and picked WHOLE, not half clipped by the
        // plot's edge. update() is the only way to address the rung (whole notches of
        // 120) and clamps to the ladder's own floor and ceiling, so a range too wide
        // for 1 px/frame is shown as much of it as fits - the same limit the fit has.
        constexpr int kWheelNotch = 120;
        const int wanted = std::max(1, (plotWidth - 2 * kFrameWindowMargin) / static_cast<int>(span));
        mScale.frameScale().update((currentPpf - wanted) * kWheelNotch);

        // The origin: frame `lo` at plotRect().left() + kFrameWindowMargin, so the
        // plot shows exactly [lo, hi] inset by that margin at both ends (to the pixel
        // the mapping rounds to) and neither end's key is clipped. mFrameOffset - the
        // content pixel of frame 0, relative to the plot's left edge - is written
        // through panFrame, one of the only two writers of it (the other is an
        // anchored zoom): pixelX(0) reads it back as plot.left() + round(offset), so
        // the delta is derived from the same mapping the drawing layer uses.
        const double ppf = static_cast<double>(mScale.frameScale().pixelsPerFrame());
        const int currentOrigin = mScale.pixelX(core::Frame(0)) - plot.left();
        mScale.panFrame(
            static_cast<int>(std::lround(kFrameWindowMargin - ppf * lo)) - currentOrigin
        );

        mAppliedFrameWindow = mFrameWindow;
        mAppliedPlotWidth = plotWidth;
        mAppliedFramePpf = mScale.frameScale().pixelsPerFrame();
    }

    //-------------------------------------------------------------------------------------------------
    // The value axis' fit. The window is where the user's data is, and the only
    // other writer is the wheel: with no fit the axis keeps its seed 0..1, so a
    // channel whose values are 0..90 (a rotate curve) maps its whole curve and
    // its keys outside plotRect(), where the panel's plot clip removes them and the
    // panel draws an empty grid.
    void GraphEditor::fitValueAxis() {
        const QVector<Entry> entries = buildEntries();
        applyValueFit(entries);
        // The basis this fit ran on, so the next syncValueAxis() compares against
        // it instead of repeating the fit - which would also overrule a window a
        // caller set right after fitting.
        mValueBasis = basisOf(entries);
    }

    void GraphEditor::fitFrameAxis() {
        // The frame window is the TIMELINE's while one owns it (see
        // setFrameWindow): fitting it here would be this panel deciding alone
        // which frames are on screen, which is exactly the disagreement the
        // shared window prevents. Nothing else changes - the value fit beside it
        // still runs.
        if (mFrameWindowShared)
            return;

        // The frame window follows the shown key FRAMES, the same way the value
        // window follows the shown key values. Collected here rather than inside
        // graph::Scaler because "which frames are shown" is this class' question;
        // the scaler is only handed the interval.
        bool any = false;
        core::Frame lo;
        core::Frame hi;

        for (const Entry& entry : buildEntries()) {
            for (const core::TimeKey* key : entry.curve.keys()) {
                const core::Frame frame(key->frame());
                if (!any) {
                    lo = frame;
                    hi = frame;
                    any = true;
                } else {
                    lo = std::min(lo, frame);
                    hi = std::max(hi, frame);
                }
            }
        }

        // No shown key: exactly the empty-panel rule the value fit follows - the
        // frame window the user set is left alone rather than collapsed.
        if (!any)
            return;

        // A MARGIN, so the content is never drawn flush against the axis. A fitted
        // range that ENDS on a key puts that key's chip at the plot's edge, where the
        // plot's clip halves it and a press at its own centre misses it - measured
        // 2026-10-14 on a single Rotate key at frame 0, drawn clamped against the left
        // edge. A tenth of the span on each side, and at least one frame, keeps the
        // keys inside the plot the way a graph editor should; the explicit fits get
        // the same courtesy, and the scaler still clamps to the project's frame range.
        const int span = int(hi.get()) - int(lo.get());
        const int margin = std::max(1, span / 10);
        mScale.fitFrameAxis(core::Frame(int(lo.get()) - margin), core::Frame(int(hi.get()) + margin));
    }

    void GraphEditor::fitAll() {
        // Both axes at once, which is what the panel's double-click does. The
        // value side is the union of every shown curve; the frame side covers
        // every shown key.
        const QVector<Entry> entries = buildEntries();
        applyValueFit(entries);
        mValueBasis = basisOf(entries);
        fitFrameAxis();
    }

    void GraphEditor::syncValueAxis() {
        const QVector<Entry> entries = buildEntries();

        // Sticky-focus pruning: a focused channel that is no longer among the
        // shown entries cannot be what the axis follows, so it is dropped here.
        // setTargets and setProject reach this through their own call, and a
        // channel the user hides drops its focus the same way. Whenever this
        // fires the shown SET changed, so the basis test below refits the window
        // (to the union, there being no focus left) rather than leaving it on a
        // channel that is gone.
        if (mFocusNode && !focusIsShown(entries)) {
            mFocusNode = nullptr;
            mFocusChannel = graph::Channel();
        }

        const QVector<QPair<core::ObjectNode*, graph::Channel> > basis = basisOf(entries);

        // The basis is the set of (layer, channel) pairs on screen, and that is
        // exactly what a refit is FOR: a selection change, or a channel whose
        // first key appeared, changes the data the window has to contain. Nothing
        // else does - a wheel zoom, a handle drag and a paint all leave this list
        // alone, so the window they were looking at is not re-fitted from under
        // them. Comparing the list (and not the values) is what makes the policy
        // "the fit follows the SHOWN CURVES, the user owns the window".
        if (basis == mValueBasis)
            return;

        mValueBasis = basis;
        applyValueFit(entries);
        // AND THE FRAME AXIS WITH IT, for the same reason and at the same moment. The
        // frame window is fitted nowhere else, so a channel whose first key sits at
        // frame 0 drew that key flush against the plot's left edge - its chip clamped
        // by the axis and unclickable at its own centre (measured 2026-10-14, the
        // pinned project's single Rotate key). fitFrameAxis pads what it fits, so the
        // content is never flush; the policy is unchanged - the fit still follows the
        // SHOWN CURVES and the user still owns a window they have wheeled or panned.
        // With a timeline attached the call is the no-op fitFrameAxis documents: the
        // frames on screen are then the timeline's, not this fit's.
        fitFrameAxis();
    }

    bool GraphEditor::focusIsShown(const QVector<Entry>& aItems) const {
        for (const Entry& entry : aItems) {
            if (entry.node == mFocusNode && entry.channel == mFocusChannel)
                return true;
        }
        return false;
    }

    QVector<QPair<core::ObjectNode*, graph::Channel> > GraphEditor::basisOf(const QVector<Entry>& aItems) {
        QVector<QPair<core::ObjectNode*, graph::Channel> > basis;
        basis.reserve(aItems.size());
        for (const Entry& entry : aItems) {
            basis.append(QPair<core::ObjectNode*, graph::Channel>(entry.node, entry.channel));
        }
        return basis;
    }

    //-------------------------------------------------------------------------------------------------
    void GraphEditor::applyValueFit(const QVector<Entry>& aItems) {
        // The union of EVERY shown curve. The focus deliberately does not narrow
        // this: it is emphasis only (see DELIBERATE FIT in the header), and a
        // window that answered a click would move the key the click was on.
        bool any = false;
        float lo = 0.0f;
        float hi = 0.0f;

        for (const Entry& entry : aItems) {
            const graph::Curve& curve = entry.curve;

            const QVector<const core::TimeKey*> keys = curve.keys();
            if (keys.isEmpty())
                continue; // no keys: the curve has no values of its own to fit

            // The key values first, so a channel whose keys never interpolate
            // (one key, or a step-held channel) still bounds the window.
            for (const core::TimeKey* key : keys) {
                const float value = curve.valueOf(*key);
                lo = any ? std::min(lo, value) : value;
                hi = any ? std::max(hi, value) : value;
                any = true;
            }

            // Then the overshoot between them. The samples go through
            // Curve::sample(), so the fit sees the value PLAYBACK reaches inside
            // the segment - the Custom bezier and the Move spline included - and
            // the segment endpoints are samples too (i = 0 and i = n), so a curve
            // is never fitted to a range narrower than the polyline drawn from it.
            for (int s = 0; s < curve.segmentCount(); ++s) {
                const graph::Curve::Segment segment = curve.segment(s);
                const float first = static_cast<float>(segment.from->frame());
                const float last = static_cast<float>(segment.to->frame());
                for (int i = 0; i <= kFitSamplesPerSegment; ++i) {
                    const float frame = first + (last - first) * (static_cast<float>(i) / kFitSamplesPerSegment);
                    const float value = curve.sample(core::Frame::fromDecimal(frame));
                    lo = std::min(lo, value);
                    hi = std::max(hi, value);
                }
            }
        }

        // No shown curve has a key: the panel is empty (or every channel shown is
        // keyless), and blanking the axis here would throw away a window the user
        // zoomed. Leaving it is the only harmless answer.
        if (!any)
            return;

        mScale.fitValueWindow(lo, hi);
    }

    //-------------------------------------------------------------------------------------------------
    bool GraphEditor::ownsCursor(const core::CameraInfo& aCamera, const core::AbstractCursor& aCursor) const {
        // A gesture keeps the cursor even when the pointer is nowhere near an
        // item: the button is still down on something this editor is moving, and
        // the release that ends it (and every move in between) may land anywhere
        // in the viewport.
        if (mDragging)
            return true;

        graph::Scaler scale = mScale;
        scale.setRect(currentPanelRect(aCamera));

        const QVector<Entry> entries = buildEntries();
        const QPoint point = aCursor.screenPoint();

        // The MIDDLE button is the panel's while it is up: the middle drag pans
        // the graph, not the canvas camera (see updateCursor). Tested before the
        // hits so a middle press is the pan even when it lands on a key.
        if (aCursor.eventButton() == core::AbstractCursor::Button_Middle
            && (aCursor.emitsPressedEvent() || aCursor.isPressedMiddle())
            && currentPanelRect(aCamera).contains(point)) {
            return true;
        }

        // A key or a tangent handle: the pointer on something the graph can act
        // on, wherever it is (a handle may sit outside the plot). The hit-test
        // runs on a LOCAL copy of the scale pinned to the current panel rect, so
        // this stays const and never mutates the live view.
        const Hit hit = hitTest(entries, scale, point);
        if (hit.hitCurve && 0 <= hit.item && hit.item < entries.size())
            return true;

        // A LEFT PRESS inside the plot is where this editor's own plot gestures
        // begin: on a key it selects (and arms the move), on a curve's band with
        // Ctrl it inserts a key, and anywhere else it opens the MARQUEE. The plot
        // is most of the fullscreen panel, so this IS the panel taking those
        // presses from the viewport tools while it is up - the deliberate cost of
        // a rubber band that can start anywhere in the data area, see the
        // ownership note in the header. Only presses are claimed: a plain hover
        // and everything outside the plot pass through.
        if (aCursor.emitsLeftPressedEvent() && scale.plotRect().contains(point))
            return true;

        return false;
    }

    bool GraphEditor::updateCursor(const core::CameraInfo& aCamera, const core::AbstractCursor& aCursor) {
        // The Driver calls this before the paint it may trigger, with the camera
        // that paint will use, so syncing here is what keeps the hit-test and the
        // drawing on one geometry.
        mScale.setRect(currentPanelRect(aCamera));
        syncFrameAxis();
        // The panel rect this pass just wrote is what the shared frame window is
        // mapped into, so the timeline's frames are re-mapped onto it here: a
        // resize (or the first pass after the window was pushed) would otherwise
        // show a window that no longer matches the plot it is drawn in. A no-op
        // while the frame axis is the panel's own.
        syncFrameWindow();
        // A channel appearing in (or leaving) buildEntries() is a change of the
        // shown curves, so the window is fitted again here - BEFORE the hit-test
        // below, so the pixels tested and the pixels painted agree. A cursor pass
        // whose curves are the ones the last fit saw (the common case, and every
        // pass after a wheel zoom) leaves the user's window exactly as it is.
        syncValueAxis();

        const QPoint point = aCursor.screenPoint();

        // --- the MIDDLE button: pan the GRAPH, not the canvas camera ---
        // The panel is up, so it is the panel's window the user is working in and
        // the middle drag moves it. ctrl::MainDisplayWidget no longer hands a
        // middle drag to ctrl::CanvasMover while this panel is visible, so the
        // two can never pan at once.
        if (aCursor.eventType() == core::AbstractCursor::Event_Press
            && aCursor.eventButton() == core::AbstractCursor::Button_Middle) {
            endDrag();
            if (currentPanelRect(aCamera).contains(point)) {
                beginPan(point);
            }
            return true;
        }
        if (aCursor.eventType() == core::AbstractCursor::Event_Move && aCursor.isPressedMiddle()) {
            if (mPanning) {
                updatePan(point);
                return true;
            }
            return false; // a middle drag this editor did not begin (it began hidden)
        }
        if (aCursor.eventType() == core::AbstractCursor::Event_Release
            && aCursor.eventButton() == core::AbstractCursor::Button_Middle) {
            if (mPanning) {
                endPan();
                return true;
            }
            return false;
        }

        if (aCursor.emitsLeftPressedEvent()) {
            // A press closes whatever edit was open. BOTH kinds, which is why
            // this is endDrag() and not just mGesture.end(): a gesture still
            // continuing would coalesce the next drag into the previous undo
            // step, and a key-move command left open would be rewritten by the
            // next drag - updateKeyMove asks isModifiable, and it would still be
            // the stack's modifiable command, now aiming at the key this press
            // armed instead.
            endDrag();

            const QVector<Entry> entries = buildEntries();
            const Hit hit = hitTest(entries, mScale, point);
            const bool validItem = 0 <= hit.item && hit.item < entries.size();
            const bool inPlot = mScale.plotRect().contains(point);

            // The branches are exactly the graph's press meanings - a handle, a
            // key, the Ctrl-insert on a band, and the marquee - and
            // `&& validItem` is the routing decision the whole ownership model
            // rests on, so it must not be relaxed to `||` (a hit with an invalid
            // item would index past entries and grab a handle that is not there).
            if (hit.hitHandle && validItem) {
                // A handle is a point ON a curve, so grabbing it both focuses the
                // channel it belongs to and starts the drag.
                setFocusedChannel(*entries[hit.item].node, entries[hit.item].channel);
                beginDrag(hit, entries);
            } else if (hit.hitCurve && validItem) {
                // Selecting the key IS the whole update: the graph keeps its own
                // selection (the two views can show different layers), and it is
                // held in the timeline's currency so no second type is needed.
                // The modifier chooses between replacing, extending and toggling.
                const graph::Channel channel = entries[hit.item].channel;
                if (mModifiers.testFlag(Qt::ShiftModifier)) {
                    // Shift EXTENDS: an entry that is already there is left alone
                    // (extending is not toggling).
                    modifySelection(*entries[hit.item].node, channel, hit.keyFrame, true);
                } else if (mModifiers.testFlag(Qt::ControlModifier)) {
                    // Ctrl TOGGLES: in when the key is out, out when it is in.
                    const bool in = selectionIndexOf(*entries[hit.item].node, channel, hit.keyFrame) >= 0;
                    modifySelection(*entries[hit.item].node, channel, hit.keyFrame, !in);
                } else {
                    selectKey(*entries[hit.item].node, channel, hit.keyFrame);
                }
                // The same hit makes this entry the focused channel, which is
                // what the panel emphasises.
                setFocusedChannel(*entries[hit.item].node, channel);
                // ...and ARMS the move: a drag from this press changes the key's
                // frame and value. Nothing is committed by the press itself, so a
                // click that only selects leaves the history untouched.
                beginKeyMove(hit, entries, point);
            } else if (inPlot) {
                // ONE plot, two gestures, decided by what is under the pointer:
                // a CTRL-press on a curve's band INSERTS the key that curve
                // already passes through (see insertKeyAt's ADD note), and
                // everything else - a plain press on the band included, and a
                // Ctrl-press off every band - opens the MARQUEE. They cannot
                // collide: a point on a band that carries a key is caught by the
                // key branch above, where Ctrl means "toggle it in the
                // selection". insertKeyAt answers whether it inserted, so this
                // needs no second band test. The rectangle is applied on the
                // RELEASE, so a press that never moves opens an empty one and
                // deselects.
                if (!(mModifiers.testFlag(Qt::ControlModifier) && insertKeyAt(aCamera, point)))
                    beginMarquee(point);
            }
            // The press is reported as consumed (the panel wants its repaint),
            // but ownsCursor claims only a press in the plot, a key or a handle -
            // so the Driver passes every other press through and the viewport
            // tools act on it.
            return true;
        }

        if (aCursor.emitsLeftDraggedEvent()) {
            if (!mDragging)
                return false; // a press that took nothing leaves the drag to nobody

            if (mMarqueeArmed) {
                // The marquee is a live rectangle: every move re-draws it.
                updateMarquee(point);
                return true;
            }

            if (mKeyMoveArmed) {
                // A key move answers with whether it moved the key: the caller
                // only has a repaint to make when the document changed, and a
                // refused step (an occupied frame) changed nothing.
                return updateKeyMove(point);
            }

            updateDrag(point);
            return true;
        }

        if (aCursor.emitsLeftReleasedEvent()) {
            if (mMarqueeArmed) {
                // The last pointer position decides the rectangle, then it is
                // applied to the selection.
                updateMarquee(point);
                endMarquee();
                return true;
            }
            if (mDragging) {
                endDrag();
                return true;
            }
            return false;
        }

        // A plain move with no button down, or an event this editor takes no
        // action on.
        return false;
    }

    //-------------------------------------------------------------------------------------------------
    bool GraphEditor::updateWheel(
        const core::CameraInfo& aCamera, const QPoint& aPos, int aWheelDelta, Qt::KeyboardModifiers aModifiers
    ) {
        // The panel owns the wheel inside its rect and nowhere else. The rect is
        // now the whole viewport (the panel is fullscreen), so the wheel over the
        // animation always zooms the graph while it is up; hiding the graph is
        // how the canvas gets the wheel back. (Ownership stayed rect-based here
        // on purpose: the wheel is not a pointer the tools need to track.)
        const QRect panel = currentPanelRect(aCamera);
        if (!panel.contains(aPos))
            return false;

        mScale.setRect(panel);
        syncFrameAxis();
        syncFrameWindow();
        // Deliberately NO syncValueAxis() here: the wheel changes no curve, so
        // there is no basis change to refit for - and running the sync would be
        // harmless only because of that. The wheel is the user's own movement of
        // the window, and this path is where it is made.

        // BOTH axes by default, so a curve keeps its shape while zooming and the
        // point under the cursor stays under it (each axis anchors on the
        // cursor's own coordinate). A modifier narrows the zoom to one axis:
        // Shift is the frame axis (horizontal), Ctrl the value axis (vertical).
        //
        // DIRECTION: matching the rest of the application. A wheel notch that Qt
        // reports as positive (the wheel rotated away from the user) makes
        // ctrl::CanvasMover and ctrl::TimeLineEditor zoom OUT, and both do it per
        // 1.1 of scale - so the panel does the same on both axes, and one wheel
        // feels the same over the panel and over the viewport. (The value axis
        // used to zoom IN on that notch, i.e. opposite both the frame axis beside
        // it and the canvas below it; the two axes have to agree for a uniform
        // zoom to mean anything.)
        const bool frameOnly = aModifiers.testFlag(Qt::ShiftModifier);
        const bool valueOnly = aModifiers.testFlag(Qt::ControlModifier);

        if (!frameOnly) {
            // The value axis about the cursor's row, so the number under the
            // cursor stays there. The negated delta is what turns "Qt's notch
            // direction" into "a notch up means further away", the canvas's own
            // sense.
            mScale.updateValueZoom(static_cast<float>(notchZoomFactor(-aWheelDelta)), aPos.y());
        }
        if (!valueOnly) {
            // The frame axis delegates its ladder and its anchoring rule to the
            // timeline's scaler, about the cursor's column; it already reads the
            // raw delta in the application's own sense. WHILE A TIMELINE OWNS THE
            // FRAME AXIS the notch is handed to the TIMELINE instead - the frames
            // on screen are its window, so a zoom that moved only this panel would
            // leave the two views showing different frames. The panel's X then
            // moves only when the timeline pushes its new window back (see the
            // shared-frame-window note in the header).
            if (mFrameWindowShared) {
                if (mZoomFrames)
                    mZoomFrames(aWheelDelta, mScale.frameAt(aPos.x()));
            } else {
                mScale.updateFrameZoom(aWheelDelta, aPos.x());
            }
        }
        // Taken even when the axes were already clamped at their floor or
        // ceiling: the pointer is over the panel, so the canvas must not also
        // zoom.
        return true;
    }

    //-------------------------------------------------------------------------------------------------
    void GraphEditor::setModifiers(Qt::KeyboardModifiers aModifiers) { mModifiers = aModifiers; }

    //-------------------------------------------------------------------------------------------------
    void GraphEditor::setSelectionSource(time::Selection* aSelection) {
        // The pointer a caller attaches is somebody else's live set (the
        // timeline's). Attaching the set that is already attached is a no-op -
        // and re-attaching the private instance is how a caller detaches.
        mSelection = aSelection ? aSelection : &mOwnSelection;
    }

    //-------------------------------------------------------------------------------------------------
    int GraphEditor::selectionIndexOf(
        const core::ObjectNode& aNode, const graph::Channel& aChannel, core::Frame aFrame
    ) const {
        // The triple the timeline names a key by, compared the way selectKey
        // writes it: the node by pointer identity (never dereferenced here) plus
        // the type and the frame. Read through the shared selection, so this
        // answers for the timeline's set as well as for the private fallback.
        const QVector<time::Selection::Item>& items = mSelection->items();
        for (int i = 0; i < items.size(); ++i) {
            const core::TimeLineEvent::Target& target = items[i].target;
            if (target.node == &aNode && target.pos.type() == aChannel.type()
                && target.pos.index() == aFrame.get()) {
                return i;
            }
        }
        return -1;
    }

    void GraphEditor::modifySelection(
        core::ObjectNode& aNode, const graph::Channel& aChannel, core::Frame aFrame, bool aAdd
    ) {
        // ONE key, named the timeline's way: the event is the selection's own
        // currency, so add/subtract below are the set's own operations (and they
        // rebuild the keys' focus, which is what the timeline highlights). The
        // "already there" / "not there" cases are the set's rules, not this
        // method's: Selection::add ignores an entry it has, subtract ignores one
        // it has not.
        core::TimeLineEvent one;
        one.pushTarget(aNode, aChannel.type(), aFrame.get());
        if (aAdd) {
            mSelection->add(one);
        } else {
            mSelection->subtract(one);
        }
    }

    //-------------------------------------------------------------------------------------------------
    void GraphEditor::beginMarquee(const QPoint& aPos) {
        // A marquee is a pointer gesture, so it takes the cursor and clears the
        // other kinds - exactly like beginDrag and beginKeyMove.
        mDragging = true;
        mMarqueeArmed = true;
        mKeyMoveArmed = false;
        mMarqueeOrigin = aPos;
        mMarqueeCurrent = aPos;
        // The Shift state is recorded AT THE PRESS: whether the marquee adds to
        // the selection or replaces it is decided by the gesture the user began,
        // not by what a modifier happens to be doing at the release.
        mAddToSelection = mModifiers.testFlag(Qt::ShiftModifier);
        mDragKey = nullptr;
        mDragItem = -1;
        mDragSegment = -1;
        mDragHandle = -1;
    }

    void GraphEditor::updateMarquee(const QPoint& aPos) { mMarqueeCurrent = aPos; }

    QRect GraphEditor::marqueeRect() const {
        // Empty while no marquee drag is in flight, and empty for a press that
        // never moved: QRect(topLeft, bottomRight) would give a 1x1 box for equal
        // corners, which is a rectangle no user drew and - since a key's square
        // is centred on its own pixel - one that could select the very key whose
        // centre it covers. "Nothing was dragged" has to be "nothing is selected".
        if (!mMarqueeArmed || mMarqueeOrigin == mMarqueeCurrent) {
            return QRect();
        }
        // Normalized (a drag up-left is as valid as down-right) and clipped to
        // the plot, whose rectangle is the only place the panel draws data - so
        // the box a user sees is the box that selects.
        return QRect(mMarqueeOrigin, mMarqueeCurrent).normalized().intersected(mScale.plotRect());
    }

    void GraphEditor::endMarquee() {
        // The rectangle is read BEFORE the flags are cleared, since marqueeRect()
        // is what says whether a drag happened at all.
        const QRect box = marqueeRect();
        mMarqueeArmed = false;
        mDragging = false;
        mMarqueeOrigin = QPoint();
        mMarqueeCurrent = QPoint();

        if (!mAddToSelection) {
            clearSelection();
        }
        if (box.isEmpty()) {
            return; // a click that never dragged deselects and selects nothing
        }

        // Every key whose DRAWN square's centre falls inside the box, through the
        // same graph::keyRect the hit-test and the drawing layer read, so the key
        // a user sees inside the rectangle is the key that is selected. Collected
        // into ONE event and added in one call: Selection::add is a union, so a
        // key already in the selection (a Shift-marquee overlapping a previous
        // one, or the same key met twice) is not added twice - the set owns that
        // rule, so this does not repeat it.
        const graph::Scaler& scale = mScale;
        core::TimeLineEvent hits;
        for (const Entry& entry : buildEntries()) {
            for (const core::TimeKey* key : entry.curve.keys()) {
                const core::Frame frame(key->frame());
                const QRect rect = graph::keyRect(scale, frame, entry.curve.valueOf(*key));
                if (box.contains(rect.center())) {
                    hits.pushTarget(*entry.node, entry.channel.type(), frame.get());
                }
            }
        }
        mSelection->add(hits);
    }

    //-------------------------------------------------------------------------------------------------
    void GraphEditor::beginPan(const QPoint& aPos) {
        mDragging = true;
        mPanning = true;
        mKeyMoveArmed = false;
        mMarqueeArmed = false;
        mDragKey = nullptr;
        mDragItem = -1;
        mDragSegment = -1;
        mDragHandle = -1;
        // A pan is incremental from wherever the pointer is, so there is no
        // press-time origin to keep - only the last position.
        mPanPointer = aPos;
    }

    void GraphEditor::updatePan(const QPoint& aPos) {
        const QPoint delta = aPos - mPanPointer;
        mPanPointer = aPos;
        if (delta.isNull()) {
            return;
        }
        // Both axes, as a plain pixel offset: the frame axis moves horizontally
        // and the value axis vertically, and each keeps the OTHER axis' state
        // (panFrame touches the frame origin only, panValue the value window
        // only), so a diagonal drag moves the content diagonally without
        // changing either zoom.
        //
        // The HORIZONTAL half is the timeline's while it owns the frame axis: the
        // pointer's pixel delta is converted to FRAMES on this panel's ladder and
        // handed over, which is the timeline's own unit - its pixels-per-frame is
        // its own, and the shared quantity is the window (see the header). The
        // sign is the pan's: dragging right moves the content right, which shows
        // EARLIER frames, so the frame delta is the negative of the pixel delta.
        if (mFrameWindowShared) {
            if (mScrollFrames && delta.x() != 0)
                mScrollFrames(-static_cast<double>(delta.x()) / framesPerPixel());
        } else {
            mScale.panFrame(delta.x());
        }
        mScale.panValue(delta.y());
    }

    void GraphEditor::endPan() {
        mPanning = false;
        mDragging = false;
    }

    //-------------------------------------------------------------------------------------------------
    QVector<graph::Curve> GraphEditor::curves() const {
        const QVector<Entry> entries = buildEntries();
        QVector<graph::Curve> curves;
        curves.reserve(entries.size());
        for (const Entry& entry : entries) {
            curves.append(entry.curve);
        }
        return curves;
    }

    QVector<GraphEditor::Entry> GraphEditor::buildEntries() const {
        QVector<Entry> items;

        for (core::ObjectNode* node : mTargets) {
            if (!node)
                continue;
            core::TimeLine* timeLine = node->timeLine();
            if (!timeLine) // a folder, or a node kind that holds no keys at all
                continue;

            for (core::TimeKeyType type : graph::Channel::graphableTypes()) {
                // A type whose map is empty has no curve: every sample would come
                // from the default key, i.e. a value no key ever wrote, drawn as
                // a dead flat line through the panel.
                if (timeLine->map(type).isEmpty())
                    continue;

                for (const graph::Channel& channel : graph::Channel::channelsOf(type)) {
                    if (!isChannelVisible(*node, channel))
                        continue;

                    Entry entry{};
                    entry.node = node;
                    entry.line = node; // the layer that owns the key map
                    entry.channel = channel;
                    entry.curve = graph::Curve(*timeLine, channel);
                    items.append(entry);
                }
            }
        }

        return items;
    }

    //-------------------------------------------------------------------------------------------------
    GraphEditor::Hit
    GraphEditor::hitTest(const QVector<Entry>& aItems, const graph::Scaler& aScale, const QPoint& aPos) const {
        Hit hit;
        const QPointF pos(aPos);

        // Handles first, nearest wins. A handle sits within its key's
        // neighbourhood and its drag edits the easing instead of the key, so it
        // is the more specific target of the two.
        double bestHandle = graph::kHandleHitRadius;
        for (int i = 0; i < aItems.size(); ++i) {
            const graph::Curve& curve = aItems[i].curve;
            const int segments = curve.segmentCount();
            for (int s = 0; s < segments; ++s) {
                const graph::Curve::Segment segment = curve.segment(s);
                if (!curve.isCustom(segment))
                    continue; // only a Custom easing exposes tangent handles at all

                for (int h = 0; h < 2; ++h) {
                    const double distance = pixelDistance(graph::handlePoint(aScale, segment, h), pos);
                    if (distance <= bestHandle) {
                        bestHandle = distance;
                        hit.hitCurve = true;
                        hit.hitHandle = true;
                        hit.item = i;
                        hit.segment = s;
                        hit.handle = h;
                    }
                }
            }
        }
        if (hit.hitHandle)
            return hit;

        // Then the keys, nearest wins. The rect comes from graph::keyRect, so a
        // click lands on the key as it is drawn. The comparison is `<=`: a press
        // EXACTLY on the hit radius still hits, which is the boundary the test
        // pins (changing it to `<` would make the outermost ring dead).
        double bestKey = graph::kKeyHitRadius;
        for (int i = 0; i < aItems.size(); ++i) {
            const graph::Curve& curve = aItems[i].curve;
            for (const core::TimeKey* key : curve.keys()) {
                const core::Frame frame(key->frame());
                const QRect rect = graph::keyRect(aScale, frame, curve.valueOf(*key));
                const double distance = pixelDistance(QRectF(rect).center(), pos);
                if (distance <= bestKey) {
                    bestKey = distance;
                    hit.hitCurve = true;
                    hit.hitHandle = false;
                    hit.item = i;
                    hit.segment = -1;
                    hit.handle = -1;
                    hit.keyFrame = frame;
                }
            }
        }

        return hit;
    }

    //-------------------------------------------------------------------------------------------------
    int GraphEditor::curveBandItem(
        const QVector<Entry>& aItems, const graph::Scaler& aScale, const QPoint& aPos
    ) const {
        const QRect plot = aScale.plotRect();
        if (plot.isEmpty() || !plot.contains(aPos))
            return -1; // the label column, the margins and the panel's edges are not data

        // The frame the pointer points at, at the same integral rounding the
        // insert will use: the point must be "at" the curve for the frame the key
        // would land on, so the value tested and the value written are one number.
        const core::Frame frame = aScale.frameAt(aPos.x());
        const double y = static_cast<double>(aPos.y());

        int best = -1;
        double bestDistance = static_cast<double>(kCurveBandRadius);
        for (int i = 0; i < aItems.size(); ++i) {
            const graph::Curve& curve = aItems[i].curve;
            const QVector<const core::TimeKey*> keys = curve.keys();
            if (keys.isEmpty())
                continue;

            // A curve is stroked between its first and last key and no further
            // (buildPolyline is handed that span), so the band stops there
            // too: owning a stretch of curve that is not drawn would be owning an
            // invisible thing, and the panel would take presses on nothing.
            const core::Frame firstFrame(keys.first()->frame());
            const core::Frame lastFrame(keys.last()->frame());
            if (frame < firstFrame || lastFrame < frame)
                continue;

            const double distance = std::fabs(static_cast<double>(aScale.pixelY(curve.sample(frame))) - y);
            if (distance <= bestDistance) {
                bestDistance = distance;
                best = i;
            }
        }
        return best;
    }

    //-------------------------------------------------------------------------------------------------
    bool GraphEditor::insertKeyAt(const core::CameraInfo& aCamera, const QPoint& aPos) {
        if (!mProject)
            return false;

        // The geometry the hit-test and the paint share: the live window and
        // ladder on a LOCAL copy of the scale pinned to this camera's panel rect.
        // A local copy because this is a public entry point - it cannot assume a
        // cursor pass has already written mScale, and it must not write it itself.
        graph::Scaler scale = mScale;
        scale.setRect(currentPanelRect(aCamera));

        const QVector<Entry> entries = buildEntries();

        // A key or a handle at the point is the more specific target: the press
        // that means to grab or to select one must not insert instead, and it
        // must not be REPLACED either (TimeLine::createPusher removes the key it
        // lands on).
        if (hitTest(entries, scale, aPos).hitCurve)
            return false;

        const int item = curveBandItem(entries, scale, aPos);
        if (item < 0)
            return false; // not on any shown curve: nothing to insert into

        const Entry& entry = entries[item];
        core::TimeLine* line = entry.line ? entry.line->timeLine() : nullptr;
        if (!line)
            return false; // a folder, or a node kind that holds no keys

        const core::Frame frame = scale.frameAt(aPos.x());

        // An occupied frame is REFUSED, not replaced: the key already there is the
        // user's, and an insert is not an overwrite (see the header). In practice
        // the key radius catches this press first - a key sits on its own curve,
        // inside the band - so this is the backstop that keeps the rule true even
        // when the pointer is on ANOTHER channel's band.
        if (line->hasTimeKey(entry.channel.type(), frame.get()))
            return false;

        if (!pushSeededKey(*line, *entry.line, entry.channel, frame))
            return false;

        // The inserted key is the panel's current key now: the press landed on its
        // curve, so it also focuses that channel, exactly as a press on a key
        // does. Neither the focus nor the selection moves a WINDOW (see DELIBERATE
        // FIT in the header), so an insert press cannot move the band under its own
        // release either.
        selectKey(*entry.node, entry.channel, frame);
        setFocusedChannel(*entry.node, entry.channel);
        return true;
    }

    //-------------------------------------------------------------------------------------------------
    bool GraphEditor::pushSeededKey(
        core::TimeLine& aLine, core::ObjectNode& aTarget, const graph::Channel& aChannel, core::Frame aFrame
    ) {
        const core::TimeKeyType type = aChannel.type();
        const int frame = aFrame.get();

        // The key playback is on at this frame, and where the payload and the
        // easing come from: the last key at or before the frame, else the first
        // key of the map (playback holds the first key before the first key), else
        // the timeline's default key. A shown curve always has at least one key -
        // buildEntries skips a type whose map is empty - so the last fallback is
        // the guard for a caller that reached here some other way.
        core::TimeKey* played = nullptr;
        {
            const core::TimeLine::MapType& map = aLine.map(type);
            auto itr = map.upperBound(frame);
            if (itr != map.begin()) {
                --itr;
                played = itr.value();
            } else if (!map.isEmpty()) {
                played = map.first();
            }
        }
        if (!played)
            played = aLine.defaultKey(type);
        if (!played)
            return false;

        // Cloned, not constructed: everything the graph does not draw - a Move
        // key's centroid, an HSV key's absolute flag, an Image key's resource and
        // blend mode - is the document's own value for this frame instead of an
        // invented default. That is also why the key playback is ON is the right
        // template: its easing is the param playback applies across the very
        // segment this insert splits.
        std::unique_ptr<core::TimeKey> seed(played->createClone());
        if (!seed)
            return false;

        // Each case overwrites the components the graph DRAWS with the value the
        // curve samples at that frame - all of them, so the insert is invisible on
        // every channel of this type, not just the one it was made from - and
        // pushes the key through the timeline's own TimeLineUtil helper, which owns
        // the notifier, the macro and the new-key command (one undo step, the same
        // Type_PushKey event the timeline's add-key buttons emit).
        switch (type) {
        case core::TimeKeyType_Move: {
            auto* key = static_cast<core::MoveKey*>(seed.get());
            key->setPos(QVector2D(
                sampledChannelValue(aLine, type, 0, aFrame), sampledChannelValue(aLine, type, 1, aFrame)
            ));
            TimeLineUtil::pushNewMoveKey(*mProject, aTarget, frame, static_cast<core::MoveKey*>(seed.release()));
            break;
        }
        case core::TimeKeyType_Rotate: {
            auto* key = static_cast<core::RotateKey*>(seed.get());
            key->setRotate(sampledChannelValue(aLine, type, 0, aFrame));
            TimeLineUtil::pushNewRotateKey(*mProject, aTarget, frame, static_cast<core::RotateKey*>(seed.release()));
            break;
        }
        case core::TimeKeyType_Scale: {
            auto* key = static_cast<core::ScaleKey*>(seed.get());
            key->setScale(QVector2D(
                sampledChannelValue(aLine, type, 0, aFrame), sampledChannelValue(aLine, type, 1, aFrame)
            ));
            TimeLineUtil::pushNewScaleKey(*mProject, aTarget, frame, static_cast<core::ScaleKey*>(seed.release()));
            break;
        }
        case core::TimeKeyType_Depth: {
            auto* key = static_cast<core::DepthKey*>(seed.get());
            key->setDepth(sampledChannelValue(aLine, type, 0, aFrame));
            TimeLineUtil::pushNewDepthKey(*mProject, aTarget, frame, static_cast<core::DepthKey*>(seed.release()));
            break;
        }
        case core::TimeKeyType_Opa: {
            auto* key = static_cast<core::OpaKey*>(seed.get());
            key->setOpacity(sampledChannelValue(aLine, type, 0, aFrame));
            TimeLineUtil::pushNewOpaKey(*mProject, aTarget, frame, static_cast<core::OpaKey*>(seed.release()));
            break;
        }
        case core::TimeKeyType_Blur: {
            auto* key = static_cast<core::BlurKey*>(seed.get());
            key->setBlurX(sampledChannelValue(aLine, type, 0, aFrame));
            key->setBlurY(sampledChannelValue(aLine, type, 1, aFrame));
            key->setAngleDeg(sampledChannelValue(aLine, type, 2, aFrame));
            TimeLineUtil::pushNewBlurKey(*mProject, aTarget, frame, static_cast<core::BlurKey*>(seed.release()));
            break;
        }
        case core::TimeKeyType_HSV: {
            auto* key = static_cast<core::HSVKey*>(seed.get());
            // The stored channel is an int and the graph READS that int, so the
            // sample is truncated exactly as the document stores it: the seeded
            // value is the value the channel reads back. The fourth slot
            // ("absolute") is not a graphable component and keeps the template's.
            QList<int> hsv = key->hsv();
            hsv[0] = static_cast<int>(sampledChannelValue(aLine, type, 0, aFrame));
            hsv[1] = static_cast<int>(sampledChannelValue(aLine, type, 1, aFrame));
            hsv[2] = static_cast<int>(sampledChannelValue(aLine, type, 2, aFrame));
            key->setHSV(hsv);
            TimeLineUtil::pushNewHSVKey(*mProject, aTarget, frame, static_cast<core::HSVKey*>(seed.release()));
            break;
        }
        case core::TimeKeyType_Image: {
            auto* key = static_cast<core::ImageKey*>(seed.get());
            key->setImageOffset(QVector2D(
                sampledChannelValue(aLine, type, 0, aFrame), sampledChannelValue(aLine, type, 1, aFrame)
            ));
            TimeLineUtil::pushNewImageKey(*mProject, aTarget, frame, static_cast<core::ImageKey*>(seed.release()));
            break;
        }
        default:
            // No component of this type is graphable, so no Channel could have
            // named it: the seed is dropped by its scoped pointer and nothing is
            // pushed.
            return false;
        }
        return true;
    }

    //-------------------------------------------------------------------------------------------------
    void GraphEditor::beginDrag(const Hit& aHit, const QVector<Entry>& aItems) {
        if (!mProject)
            return;
        if (aHit.item < 0 || aHit.item >= aItems.size() || aHit.segment < 0 || aHit.handle < 0)
            return;

        const Entry& entry = aItems[aHit.item];
        if (aHit.segment >= entry.curve.segmentCount())
            return;

        const graph::Curve::Segment segment = entry.curve.segment(aHit.segment);

        // Playback reads the segment's easing from its `from` key, for the whole
        // span, so both of the segment's handles edit that one key's param.
        mDragging = true;
        mKeyMoveArmed = false; // this is a handle drag: the kinds never coexist
        mMarqueeArmed = false;
        mPanning = false;
        mDragItem = aHit.item;
        mDragSegment = aHit.segment;
        mDragHandle = aHit.handle;
        mDragKey = segment.from;
        // No command is pushed yet: the first move is what commits, so a press
        // that never moves leaves the history untouched.
    }

    //-------------------------------------------------------------------------------------------------
    void GraphEditor::beginKeyMove(const Hit& aHit, const QVector<Entry>& aItems, const QPoint& aPressPos) {
        if (!mProject)
            return;
        if (aHit.item < 0 || aHit.item >= aItems.size())
            return;

        const Entry& entry = aItems[aHit.item];
        core::TimeLine* line = entry.line ? entry.line->timeLine() : nullptr;
        const core::TimeKey* key = line ? line->timeKey(entry.channel.type(), aHit.keyFrame.get()) : nullptr;
        if (!key)
            return; // the key the hit-test named is gone already: nothing to hold

        // The key the press hit, in the timeline's own currency - the layer that
        // owns the map, the node the event will name, the key type and the frame
        // the key is at. Held as those four things rather than as aHit.item, so
        // the move follows the key it grabbed and not the position of an entry
        // list that a re-selection could rebuild differently. The COMPONENT is
        // held with them: it is which field of the key's Data the value half of
        // the gesture writes.
        mDragging = true;   // the panel keeps the cursor while the key is held
        mKeyMoveArmed = true;
        mMarqueeArmed = false;
        mPanning = false;
        mDragItem = aHit.item;
        mDragKey = nullptr; // no handle: this drag moves the key, not its easing
        mDragSegment = -1;
        mDragHandle = -1;
        mMoveLine = entry.line;
        mMoveNode = entry.node;
        mMoveType = entry.channel.type();
        mMoveComponent = entry.channel.component();
        mMoveFrame = aHit.keyFrame;
        // Where the pointer was, in PIXELS: both axes follow the pointer's own
        // delta from here, so a press up to the key radius off the key's centre
        // does not make the key jump by that offset on the first move. The value
        // is read through the same live scale, so the two conversions always
        // agree about the row the pointer is on.
        mMovePointerFrame = mScale.frameAt(aPressPos.x());
        mMovePressPos = aPressPos;
        // The key's own value at the press: the drag's value delta is added to
        // THIS, so the gesture is one absolute function of the pointer.
        mMoveBaseValue = entry.curve.valueOf(*key);
        // No command yet: the first ACCEPTED step is what commits, so a press
        // that never moves leaves the history untouched.
        mMoveCommand = nullptr;
        mValueCommand = nullptr;
    }

    //-------------------------------------------------------------------------------------------------
    bool GraphEditor::updateKeyMove(const QPoint& aPos) {
        if (!mProject || !mDragging || !mKeyMoveArmed)
            return false;

        core::TimeLine* line = mMoveLine ? mMoveLine->timeLine() : nullptr;
        if (!line || !mMoveNode) {
            endDrag();
            return false;
        }

        // The anchor check, the handle drag's own honesty: if the armed key is no
        // longer in the document (an undo removed it, or another view did), this
        // move would write a key that is not there. MoveFrameOfKey's own
        // validation would refuse every step, leaving the drag silently dead under
        // the cursor - so close the edit and say why.
        core::TimeKey* key = line->timeKey(mMoveType, mMoveFrame.get());
        if (!key) {
            mLogger.pushLog(
                QObject::tr("The animation graph stopped moving: the dragged key changed."), UILogType_Info
            );
            endDrag();
            return false;
        }

        // The SHIFT axis snap: the pointer's own pixel displacement from the
        // press decides which axis is dominant, and the other one is pinned. The
        // displacement is a gesture-wide quantity (not a per-step one), so the
        // chosen axis does not flicker while the pointer wiggles: a drag that is
        // overwhelmingly horizontal does not smear a stray pixel of vertical
        // movement into the value.
        const int dx = aPos.x() - mMovePressPos.x();
        const int dy = aPos.y() - mMovePressPos.y();
        const bool snap = mModifiers.testFlag(Qt::ShiftModifier);
        const bool frameAllowed = !snap || (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy);
        const bool valueAllowed = !snap || (dy < 0 ? -dy : dy) > (dx < 0 ? -dx : dx);

        // The FRAME delta from the last ACCEPTED frame: a refused step leaves
        // mMovePointerFrame where it was, so the next accepted step is measured
        // from the last accepted place.
        const core::Frame pointerFrame = mScale.frameAt(aPos.x());
        const int step = frameAllowed ? pointerFrame.get() - mMovePointerFrame.get() : 0;

        // The VALUE: the pointer's delta from the row it went down on, added to
        // the key's own value at the press. Both rows go through the SAME live
        // scale, so this is an absolute function of the pointer and cannot
        // accumulate the key's own clamp.
        const float valueDelta = valueAllowed ? mScale.valueAt(aPos.y()) - mScale.valueAt(mMovePressPos.y()) : 0.0f;
        const float nextValue = mMoveBaseValue + valueDelta;
        const bool valueMoves = (nextValue != graph::Channel(mMoveType, mMoveComponent).value(*key));

        if (!valueMoves && step == 0)
            return false; // the pointer has not moved the key: nothing to rewrite, nothing to open

        // The frame range is playback's business, not the keying system's: keys
        // may live anywhere, so the move is asked with the widest range there is
        // (the timeline's own move asks the same way).
        const util::Range frameRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());

        if (mMoveCommand && mProject->commandStack().isModifiable(mMoveCommand)) {
            // The commands this gesture already pushed: REWRITE them, which is
            // what makes the whole drag one undo step. modifyMove refuses a
            // destination another key of the type owns, and a refusal leaves both
            // commands exactly as they were (no move, no mMove, no notification).
            int clampedStep = step;
            core::TimeLineEvent modEvent;
            bool frameMoved = false;
            if (step != 0) {
                if (!mMoveCommand->modifyMove(modEvent, step, frameRange, &clampedStep) || clampedStep == 0)
                    return false; // the frame is occupied: the WHOLE step is refused
                frameMoved = true;
            }

            if (valueMoves) {
                mValueCommand->modifyValue(nextValue);
            }

            // ONE notification per step, as the timeline emits one per gesture
            // step: the frame event when the frame moved (it names where the key
            // came from and where it went, the structural half), and the value
            // event otherwise - the same two events the timeline uses for its own
            // frame moves and value edits. Nothing in the application tells the two
            // apart (the object tree and the timeline editor only act on the
            // push/remove/copy types), so the second call a both-axes step would
            // otherwise make is work with no reader.
            if (frameMoved) {
                mProject->onTimeLineModified(modEvent, false);
            } else {
                core::TimeLineEvent valueEvent;
                valueEvent.setType(core::TimeLineEvent::Type_ChangeKeyValue);
                valueEvent.pushTarget(*mMoveNode, mMoveType, mMoveFrame.get());
                mProject->onTimeLineModified(valueEvent, false);
            }

            mMoveFrame = core::Frame(mMoveFrame.get() + clampedStep);
            mMovePointerFrame = core::Frame(mMovePointerFrame.get() + clampedStep);
            return true;
        }

        // The gesture's first accepted step, or its first after an undo dropped
        // the stack's modifiable link.
        //
        // Both commands are built and VALIDATED before the macro is opened: a
        // not-yet-pushed MoveFrameOfKey only records where it would move to (its
        // own mCurrent == 0 path), so a REFUSED step can be dropped right here -
        // no commands on the stack, no notification, no empty history entry. Both
        // are always pushed, exactly once per gesture, so a later step that moves
        // the axis this one did not can still rewrite the command that owns it -
        // the one that was inert on this step simply stays inert.
        int clampedStep = step;
        core::TimeLineEvent source;
        source.setType(step != 0 ? core::TimeLineEvent::Type_MoveKey : core::TimeLineEvent::Type_ChangeKeyValue);
        source.pushTarget(*mMoveNode, mMoveType, mMoveFrame.get());

        std::unique_ptr<TimeLineUtil::MoveFrameOfKey> frameCommand(new TimeLineUtil::MoveFrameOfKey(source));
        if (step != 0) {
            core::TimeLineEvent modEvent;
            if (!frameCommand->modifyMove(modEvent, step, frameRange, &clampedStep) || clampedStep == 0)
                return false;
        }
        std::unique_ptr<TimeLineUtil::AssignKeyValue> valueCommand(
            new TimeLineUtil::AssignKeyValue(*key, mMoveType, mMoveComponent, nextValue)
        );

        TimeLineUtil::MoveFrameOfKey* pushedFrame = frameCommand.release();
        TimeLineUtil::AssignKeyValue* pushedValue = valueCommand.release();
        {
            cmnd::ScopedMacro macro(mProject->commandStack(), CmndName::tr("move a key"));

            // The notifier carries the step's ORIGINAL target, exactly like the
            // timeline's beginMoveKeys: what reaches the application names where
            // the key came from, and the macro's close is what fires it for this
            // first commit (the later rewrites notify by hand, above).
            auto* notifier = new TimeLineUtil::Notifier(*mProject);
            notifier->event() = source;
            macro.grabListener(notifier);

            // The value command is pushed FIRST, so the FRAME command is the
            // stack's modifiable one: isModifiable(mMoveCommand) is the single
            // gate for rewriting either half of the gesture.
            mProject->commandStack().push(pushedValue);
            mProject->commandStack().push(pushedFrame);
        }

        mMoveCommand = pushedFrame;
        mValueCommand = pushedValue;
        mMoveFrame = core::Frame(mMoveFrame.get() + clampedStep);
        mMovePointerFrame = core::Frame(mMovePointerFrame.get() + clampedStep);
        return true;
    }

    void GraphEditor::updateDrag(const QPoint& aPos) {
        if (!mProject || !mDragging || !mDragKey)
            return;

        // The entries are rebuilt from the document on every use, so the drag
        // follows the keys the document holds now instead of a press-time
        // snapshot.
        const QVector<Entry> entries = buildEntries();
        if (mDragItem < 0 || mDragItem >= entries.size() || mDragSegment < 0)
            return;

        const Entry& entry = entries[mDragItem];
        if (mDragSegment >= entry.curve.segmentCount())
            return;

        const graph::Curve::Segment segment = entry.curve.segment(mDragSegment);

        // The anchor check: if the keys moved under the drag (another view, an
        // undo), the segment at this index is no longer the anchor's and the
        // write would land on a different key's easing.
        if (segment.from != mDragKey) {
            // The key this drag grabbed is no longer the segment's start - an
            // undo mid-drag removed or moved it - so this move would write
            // ANOTHER key's easing. Close the edit and say why: silently
            // ignoring the moves would leave the handle visibly stuck under the
            // cursor with nothing to explain it.
            mLogger.pushLog(
                QObject::tr("The animation graph stopped editing: the dragged key changed."), UILogType_Info
            );
            endDrag();
            return;
        }

        core::TimeLine* line = entry.line ? entry.line->timeLine() : nullptr;
        core::TimeKey* key = line ? line->timeKey(entry.channel.type(), mDragKey->frame()) : nullptr;
        util::Easing::Param* easing = key ? graph::Curve::easingOf(*key, entry.channel.type()) : nullptr;
        if (!easing)
            return; // the type carries no easing: nothing this graph could be editing

        const QPointF normalized = handleNormalized(
            mScale, segment, aPos, graph::Curve::handle(segment, mDragHandle).y()
        );

        // Built from the param the document holds NOW, never from a press-time
        // copy, so the fields this drag does not touch - the easing type above
        // all - cannot be reverted by a stale snapshot.
        util::Easing::Param next = *easing;
        graph::Curve::setHandle(next, mDragHandle, normalized);

        if (mGesture.continues()) {
            // Rewrite the command this drag already pushed, so the whole drag
            // stays ONE undo step.
            mGesture.command()->modifyValue(next);

            // The rewrite goes straight through the stack, so the notification
            // the first commit pushed has to be sent again here - the same event
            // the timeline emits for a key whose value changed.
            core::TimeLineEvent event;
            event.setType(core::TimeLineEvent::Type_ChangeKeyValue);
            event.pushTarget(*entry.node, entry.channel.type(), mDragKey->frame());
            mProject->onTimeLineModified(event, false);
        } else {
            // The drag's first commit (or its first after an undo/redo dropped
            // the stack's modifiable link): the notifier and the assign are
            // pushed inside ONE macro, so one notification and one command are
            // one commit.
            mGesture.begin(CmndName::tr("change a key easing"), [&](cmnd::ScopedMacro& aMacro) {
                auto* notifier = new TimeLineUtil::Notifier(*mProject);
                notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
                notifier->event().pushTarget(*entry.node, entry.channel.type(), mDragKey->frame());
                aMacro.grabListener(notifier);

                return new cmnd::ModifiableAssign<util::Easing::Param>(easing, next);
            });
        }
    }

    void GraphEditor::endDrag() {
        // Every command is already on the stack with its last value, so closing
        // the edit is all a release has to do: Gesture::end() makes the next drag
        // open a fresh command instead of coalescing into this one, and a gesture
        // that never committed pushes nothing at all.
        mGesture.end();
        mDragging = false;
        mKeyMoveArmed = false;
        mMarqueeArmed = false;
        mPanning = false;
        mDragKey = nullptr;
        mDragItem = -1;
        mDragSegment = -1;
        mDragHandle = -1;

        // The marquee's box goes with it: a gesture that ends without applying it
        // (a hide, or a press that closed it) must not leave a rectangle behind
        // for the drawing layer to find.
        mMarqueeOrigin = QPoint();
        mMarqueeCurrent = QPoint();
        mAddToSelection = false;

        // The key move's commands stay on the stack (they ARE the gesture's one
        // undo step, whatever they were last asked to do); this drops the handles
        // to them, so no later press can rewrite commands that belong to a
        // finished gesture. The armed key goes with them: nothing may move until
        // a new press arms one.
        mMoveCommand = nullptr;
        mValueCommand = nullptr;
        mMoveLine = nullptr;
        mMoveNode = nullptr;
        mMoveType = core::TimeKeyType_TERM;
        mMoveComponent = 0;
        mMoveFrame = core::Frame();
        mMovePointerFrame = core::Frame();
        mMovePressPos = QPoint();
        mMoveBaseValue = 0.0f;
    }

    //-------------------------------------------------------------------------------------------------
    void GraphEditor::setChannelVisible(core::ObjectNode& aTarget, const graph::Channel& aChannel, bool aVisible) {
        // Only the HIDDEN channels are stored: a channel is visible unless it was
        // hidden, so an untouched document needs no entries at all. Identity is
        // (node, channel) - the node pointer the caller owns, and the channel's
        // type/component - so a hidden channel survives its target leaving and
        // rejoining the selection.
        const QPair<core::ObjectNode*, graph::Channel> key(&aTarget, aChannel);
        const auto itr = std::find(mHiddenChannels.begin(), mHiddenChannels.end(), key);

        if (aVisible) {
            if (itr != mHiddenChannels.end()) {
                mHiddenChannels.erase(itr);
            }
        } else if (itr == mHiddenChannels.end()) {
            mHiddenChannels.append(key);
        }
    }

    bool GraphEditor::isChannelVisible(const core::ObjectNode& aTarget, const graph::Channel& aChannel) const {
        for (const QPair<core::ObjectNode*, graph::Channel>& hidden : mHiddenChannels) {
            // Pointer identity only. A deleted target can leave an entry nothing
            // can reach, which is never dereferenced; setProject clears the set.
            if (hidden.first == &aTarget && hidden.second == aChannel) {
                return false;
            }
        }
        return true;
    }

    //-------------------------------------------------------------------------------------------------
    bool GraphEditor::retrieveSelection(core::TimeLineEvent& aOut) const {
        // The selection is handed out in the timeline's currency rather than
        // translated into a second type, because it IS the timeline's set: the
        // targets go into the caller's event through the set's own assign(). The
        // type stays as it was set (Type_TERM when the selection came from here):
        // picking a key is not a document change, and inventing one of the change
        // types here would make a consumer act on a value that did not move.
        if (mSelection->empty()) {
            return false;
        }
        mSelection->assign(aOut);
        return true;
    }

    void GraphEditor::clearSelection() { mSelection->clear(); }

    void GraphEditor::selectKey(core::ObjectNode& aNode, const graph::Channel& aChannel, core::Frame aFrame) {
        // One key, one target: the graph selects a key, not a range, and it is
        // addressed by the same (line, type, frame) triple the timeline's own keys
        // are. set() is the whole-selection replace, so this drops whatever was
        // there - in the timeline's set too, which is the point of sharing it.
        core::TimeLineEvent one;
        one.pushTarget(aNode, aChannel.type(), aFrame.get());
        mSelection->set(one);
    }

    //-------------------------------------------------------------------------------------------------
    bool GraphEditor::removeSelectedKey() {
        if (!mProject)
            return false;

        // WHAT CAN BE REMOVED, collected BEFORE the macro is opened. cmnd::Stack
        // appends a macro even when it holds no command and counts it as an edit,
        // so an action that opened the macro and then found nothing to remove
        // would leave an empty undo step behind.
        //
        // The key must be there to be removed. TimeLine::createRemover with
        // nothing at the frame builds an EMPTY command, and a selection can
        // outlive the keys it names (an undo, or another view's edit), so a stale
        // target is skipped here rather than pushed against.
        //
        // The selection is read as ONE snapshot in the timeline's own currency:
        // the shared set may be the timeline's, and its items are not this
        // method's to walk while the document is being edited (a removal below
        // notifies, and the notification can refresh the set under us).
        core::TimeLineEvent selection;
        mSelection->assign(selection);

        QVector<int> removals;
        for (int i = 0; i < selection.targets().size(); ++i) {
            const core::TimeLineEvent::Target& target = selection.targets().at(i);
            if (!target.node || target.pos.isNull())
                continue;
            const core::TimeLine* line = target.pos.line();
            if (!line)
                continue;
            if (!line->hasTimeKey(target.pos.type(), target.pos.index()))
                continue;
            removals.append(i);
        }

        if (removals.isEmpty()) {
            // Nothing the selection names is in the document: drop it rather than
            // keep naming keys that are not there.
            clearSelection();
            return false;
        }

        {
            // The timeline's own delete route (TimeLineEditor::deleteCheckedKeys):
            // ONE notifier carrying the Type_RemoveKey event and ONE macro, so a
            // whole multi-key selection is ONE undo step.
            cmnd::ScopedMacro macro(mProject->commandStack(), CmndName::tr("remove a key"));

            auto* notifier = new TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_RemoveKey);
            macro.grabListener(notifier);

            for (int index : removals) {
                // The mutable target: the remover factory needs the owning
                // TimeLine, and TimeKeyPos hands that out non-const only from a
                // non-const pos.
                core::TimeLineEvent::Target& target = selection.targets()[index];
                core::TimeLine* line = target.pos.line();
                notifier->event().pushTarget(*target.node, target.pos.type(), target.pos.index());
                mProject->commandStack().push(
                    line->createRemover(target.pos.type(), target.pos.index(), true)
                );
            }
        }

        // The keys are gone, so the selection goes with them: keeping it would
        // name keys no longer in the document (and the next remove would skip
        // them - the guard above - rather than remove neighbours by accident).
        clearSelection();
        return true;
    }

    //-------------------------------------------------------------------------------------------------
    bool GraphEditor::setSelectedKeysEasing(util::Easing::Type aType) {
        if (!mProject)
            return false;

        // WHAT CAN BE WRITTEN, collected BEFORE the macro is opened. The header
        // says why this has to happen first: cmnd::Stack appends a macro even
        // when it holds no command, and counts it as an edit, so an action that
        // opened the macro and then found nothing to write would leave an empty
        // undo step behind - the defect removeSelectedKey's own guard avoids.
        struct Edit {
            util::Easing::Param* easing;
            core::ObjectNode* node;
            core::TimeKeyType type;
            int frame;
        };
        QVector<Edit> edits;

        // The selection as ONE snapshot in the timeline's own currency, for the
        // same reason removeSelectedKey takes one: it may be the timeline's set,
        // and the edits below notify.
        core::TimeLineEvent selection;
        mSelection->assign(selection);

        for (core::TimeLineEvent::Target& target : selection.targets()) {
            // The mutable target: the action writes the keys through the map the
            // TimeLinePos names, and it is TimeKeyPos::line()'s non-const overload
            // that hands out a writable map.
            core::TimeLine* line = target.pos.line();
            if (!line || !target.node)
                continue; // a hand-made target with no owner: nothing to address

            // The key must BE there. A selection can outlive the key it names (an
            // undo, or another view's edit), and a stale one must not push a
            // command against a frame that holds nothing.
            core::TimeKey* key = line->timeKey(target.pos.type(), target.pos.index());
            if (!key)
                continue;

            // The type has to carry an easing at all: bone, pose, mesh and FFD
            // keys are structural and easingOf answers null for them, which is
            // also why the panel has no channel for them.
            util::Easing::Param* easing = graph::Curve::easingOf(*key, target.pos.type());
            if (!easing)
                continue;

            // ...and playback has to CONSULT that easing. The image offset is the
            // one graphable value that is step-held (blendImageKey holds the last
            // key; graph::Channel::isInterpolated is the rule), so its param
            // changes no drawn sample and no played value: refusing it is the
            // timeline's own behaviour with Image, not a special case invented
            // here.
            if (!graph::Channel(target.pos.type(), 0).isInterpolated())
                continue;

            // Already at the requested type: writing it would push a command whose
            // exec and undo are the same value - a history entry for a change that
            // did not happen. So a menu entry that matches the key's current easing
            // leaves the document and the history exactly as they are.
            if (easing->type == aType)
                continue;

            edits.append(Edit{easing, target.node, target.pos.type(), key->frame()});
        }

        if (edits.isEmpty())
            return false;

        {
            // ONE macro and ONE notifier for the whole selection - the timeline's
            // own route (TimeLineUtil::assignKeyEasing: a cmnd::Assign of the key's
            // util::Easing::Param, a TimeLineUtil::Notifier, and a
            // core::TimeLineEvent stating Type_ChangeKeyValue), batched so the
            // selection is ONE undo step. The per-key helper cannot be reused for
            // that: it opens a macro of its own, and cmnd::Stack::beginMacro
            // asserts on a nested one.
            cmnd::ScopedMacro macro(mProject->commandStack(), CmndName::tr("change key easing"));

            auto* notifier = new TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
            macro.grabListener(notifier);

            util::Easing::Param next;
            for (const Edit& edit : edits) {
                // The key's OWN param with its type replaced: the range, the
                // weight and the Custom bezier's two controls belong to the key,
                // and the action only chooses the type.
                next = *edit.easing;
                next.type = aType;

                // The frame the key is at NOW rather than the one the selection
                // names: they are the same for a live key (the lookup above went
                // through the same frame), and the key's own is the one the
                // document answers with.
                notifier->event().pushTarget(*edit.node, edit.type, edit.frame);
                mProject->commandStack().push(new cmnd::Assign<util::Easing::Param>(edit.easing, next));
            }
        }
        return true;
    }

    //-------------------------------------------------------------------------------------------------
    void GraphEditor::setFocusedChannel(core::ObjectNode& aNode, const graph::Channel& aChannel) {
        // Re-focusing the channel that is already focused is a NO-OP: not for the
        // window's sake any more (nothing here moves a window), but because a
        // redundant assignment is work with no observable effect.
        if (mFocusNode == &aNode && mFocusChannel == aChannel)
            return;

        mFocusNode = &aNode;
        mFocusChannel = aChannel;

        // NOTHING ELSE. The focus is emphasis and a report of what the panel is
        // working on; it does not touch the value window, the shown-curve basis or
        // anything else the user owns. See DELIBERATE FIT in the header - a click
        // that zoomed the view was the defect this replaced.
    }

    void GraphEditor::clearFocus() {
        if (!mFocusNode)
            return; // already clear: nothing to drop

        mFocusNode = nullptr;
        mFocusChannel = graph::Channel();
        // No window moves here either: dropping the emphasis changes what is
        // painted brightly, not what the axes show.
    }

    //-------------------------------------------------------------------------------------------------
    core::Frame GraphEditor::currentFrame() const {
        // The playhead's frame, for views that draw it. A missing project has no
        // frame; -1 is outside the real range (frames start at 1), so it reads as
        // "nothing" without inventing a second type.
        return mProject ? mProject->currentTimeInfo().frame : core::Frame(-1);
    }

} // namespace ctrl
