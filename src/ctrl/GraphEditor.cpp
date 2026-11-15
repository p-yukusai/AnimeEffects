#include "ctrl/GraphEditor.h"

#include "ctrl/AutoScroll.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

#include <QApplication>
#include <QObject>
#include <QSettings>
#include <QtGlobal>
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

// The TIMELINE's own Shift+wheel scroll (TimeLineWidget::wheelEvent): 50 content
// pixels per notch, wheel up toward EARLIER frames. The graph's Shift branch moves
// the frame window the same distance in the same direction, so the two panes of
// one dock - the timeline and the plot above it - cannot disagree about the frame
// axis; while a timeline owns that axis the move is delegated through
// scrollFramesBy so both views keep moving one window.
constexpr int kFrameWheelScrollPixels = 50;

// How many points a segment is walked through when the value axis is fitted.
// The fit cannot read the key values alone: a Custom easing's cubic bezier (and
// Back/Elastic) reaches values beyond BOTH of the keys it sits between, and a
// window fitted to the key values would map that overshoot outside the plot,
// where the panel's plot clip removes it. 16 is the order of
// Curve::buildPolyline's drawing rate, so the fit sees what is drawn.
constexpr int kFitSamplesPerSegment = 16;

// THE MARQUEE'S CLICK SLOP, in pixels - the TIMELINE's own rule, adopted whole:
// its box is `QRect(anchor, current).normalized()` and its release treats the box
// as a click only when `box.width() < 2 && box.height() < 2` (ctrl::TimeLineEditor::
// updateCursor's release branch). QRect's width is INCLUSIVE, so that pair is true
// for exactly one rectangle: the one whose corners are the same point. A gesture is
// therefore a marquee the moment the pointer moves at all, and only a press and
// release on ONE pixel is the click. (This replaces the graph's own 4px slop, the
// design's `r.w > 4 || r.h > 4`: two views of one selection cannot disagree about
// whether the user dragged.)
constexpr int kMarqueeSlop = 0;

double notchZoomFactor(int aWheelDelta) {
    return std::pow(kValueZoomPerNotch, static_cast<double>(aWheelDelta) / kWheelNotch);
}

double pixelDistance(const QPointF& aLhs, const QPointF& aRhs) {
    return std::hypot(aLhs.x() - aRhs.x(), aLhs.y() - aRhs.y());
}

// The inverse of graph::handlePointAt for one panel pixel. aFlatY is what the y
// component falls back to when the segment's value span is zero: a constant
// segment has no value axis to normalize across, so the vertical component of
// the drag has nothing to write and the handle keeps the y it had.
//
// aPos is a QPointF, not a QPoint, because the OPPOSITE handle of an aligned
// key is not a pointer pixel: it is the collinear point at that handle's own
// length, and rounding it to a pixel before mapping it back would put it half a
// pixel off the line the user sees (see updateDrag).
QPointF
handleNormalized(const ctrl::graph::Scaler& aScale, const ctrl::graph::Curve::Segment& aSegment, const QPointF& aPos,
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
        (static_cast<double>(aScale.valueAtPixel(aPos.y())) - aSegment.fromValue) / valueSpan;
    return QPointF(nx, ny);
}

// ---- HANDLE ALIGNMENT (a key's two tangents) ----
//
// A key owns TWO tangent handles: its OUTGOING one, stored as (x1,y1) of the
// segment that STARTS at the key, and its INCOMING one, stored as (x2,y2) of
// the segment that ENDS at it. Each handle has its OWN mode - the key's
// `util::Easing::Param::handleOut` and `handleIn` (Blender's five: Free,
// Aligned, Vector, Auto, AutoClamped) - and the gestures below write ONE side
// at a time:
//
//   * dragging any side writes that side; whether the OTHER side follows is
//     decided by ITS stored mode:
//       - DERIVED (Auto/AutoClamped/Vector): the pair was symmetric by
//         construction (one slope for both sides), so the partner is mirrored
//         about the key in angle AND length and becomes Free;
//       - ALIGNED: the partner stays collinear at its own length (the
//         smooth-node behaviour) and keeps its mode;
//       - FREE: the partner is not touched.
//   * Alt on any step sets the dragged side Free for good and freezes the
//     partner; Alt+CLICK on a key sets BOTH sides Aligned along the
//     previous->next anchor chord.
//
// The modes are DOCUMENT state written by these gestures and serialized with
// the key, never inferred from the pixel angle: a key moved on the value axis
// changes that angle without changing what the user meant.

// The pair of handles a segment actually SHOWS. The segment's own modes decide
// per side: a stored side is its stored control, a derived side is the point
// the derivation produces. See Curve::effectiveHandles.
bool effectiveHandles(
    const ctrl::graph::Curve& aCurve, const ctrl::graph::Curve::Segment& aSegment, QPointF aOut[2]
) {
    return aCurve.effectiveHandles(aSegment, aOut);
}

// aParam with ONE handle placed at aPixel, in the segment's own normalization.
// aEffective is the pair the segment is DRAWN with - its y is the fallback
// handleNormalized needs on a constant segment. The param's TYPE and both
// MODES are the caller's business: this only writes the stored control point.
bool alignedHandleParam(
    const ctrl::graph::Scaler& aScale, const ctrl::graph::Curve::Segment& aSegment,
    const util::Easing::Param& aParam, const QPointF aEffective[2], int aHandleIndex,
    const QPointF& aPixel, util::Easing::Param& aOut
) {
    aOut = aParam;
    const QPointF normalized =
        handleNormalized(aScale, aSegment, aPixel, static_cast<double>(aEffective[aHandleIndex].y()));
    ctrl::graph::Curve::setHandle(aOut, aHandleIndex, normalized);
    return true;
}

// The value the graph draws for one component of one key type at aFrame: what
// Curve::sample() returns for that (type, component) channel, i.e. the number
// playback evaluates there through the `from` key's easing.
//
// The insert seeds EVERY component of the new key through this, not just the
// one the user's curve shows: a key holds all of its type's components at once,
// so adding a key from a Move X curve must carry the evaluated Y as well or the
// insert would change the animation it is meant to leave alone.


} // namespace

namespace ctrl {

    //-------------------------------------------------------------------------------------------------
    GraphEditor::Hit::Hit(): hitCurve(false), hitHandle(false), item(-1), segment(-1), handle(-1), keyFrame() {}

    //-------------------------------------------------------------------------------------------------
    KeyMoveCommand::KeyMoveCommand(): mFrames(nullptr), mValues() {}

    KeyMoveCommand::~KeyMoveCommand() {
        delete mFrames;
        for (const Value& value : mValues) {
            delete value.command;
        }
        mValues.clear();
    }

    void KeyMoveCommand::buildFrames(const core::TimeLineEvent& aTargets) {
        delete mFrames;
        mFrames = new TimeLineUtil::MoveFrameOfKey(aTargets);
    }

    bool KeyMoveCommand::modifyMove(
        core::TimeLineEvent& aModEvent, int aStep, const util::Range& aFrame, int* aClampedStep
    ) {
        if (!mFrames) {
            return false;
        }
        return mFrames->modifyMove(aModEvent, aStep, aFrame, aClampedStep);
    }

    void KeyMoveCommand::addValue(core::TimeKey& aKey, core::TimeKeyType aType, int aComponent, float aBaseValue) {
        Value value;
        value.command = new TimeLineUtil::AssignKeyValue(aKey, aType, aComponent, aBaseValue);
        value.base = aBaseValue;
        mValues.append(value);
    }

    void KeyMoveCommand::modifyValues(float aValueDelta) {
        for (const Value& value : mValues) {
            value.command->modifyValue(value.base + aValueDelta);
        }
        // The values are written, so the proportional keys between the bounds
        // follow them: their stored weights were captured before the gesture, so
        // this reads the bounds' CURRENT values and never a previous projection.
        for (int i = 0; i < mReflows.size(); ++i) {
            mReflows[i].modify();
        }
        // ...and every stored tangent keeps its offset from its key, which is what the
        // spans just changed.
        applyTangents();
    }

    void KeyMoveCommand::buildValueReflows(const QVector<ValueReflowTarget>& aTargets) {
        mReflows.clear();

        // Group by (line, type): one ValueReflow owns one map. A gesture can touch
        // several types (a whole-key selection expands to every component) and
        // several layers' timelines, and each group captured against its own map is
        // what keeps their restore bases apart.
        struct Group {
            core::TimeLine* line;
            core::TimeKeyType type;
            QVector<core::TimeKey*> edited;
        };
        QVector<Group> groups;

        // Index loops, not ranged-for: a non-const QVector's ranged-for detaches,
        // and this runs once per gesture over the whole selection.
        for (int i = 0; i < aTargets.size(); ++i) {
            const ValueReflowTarget& target = aTargets.at(i);
            if (!target.line || !target.key)
                continue;

            int group = -1;
            for (int g = 0; g < groups.size(); ++g) {
                if (groups.at(g).line == target.line && groups.at(g).type == target.type) {
                    group = g;
                    break;
                }
            }
            if (group < 0) {
                Group fresh;
                fresh.line = target.line;
                fresh.type = target.type;
                groups.append(fresh);
                group = groups.size() - 1;
            }
            groups[group].edited.append(target.key);
        }

        // capture() dedups the edited list and excludes absolute keys itself; a
        // group whose map holds no proportional key leaves the reflow empty, and
        // every later apply()/restore() of it is a no-op.
        for (int g = 0; g < groups.size(); ++g) {
            const Group& group = groups.at(g);
            mReflows.append(proportional::ValueReflow());
            mReflows.last().capture(*group.line, group.type, group.edited);
        }
    }

    void KeyMoveCommand::buildTangents(const QVector<TangentTarget>& aTargets) {
        mTangents.clear();

        // A stored side is one a user placed (Free or Aligned); a derived one
        // (Vector/Auto/AutoClamped) is computed from the neighbours on every read, so
        // there is nothing stored to keep. Only Custom carries a bezier at all.
        const auto storedSide = [](const util::Easing::Param& aParam, int aSide) {
            if (aParam.type != util::Easing::Type_Custom)
                return false;
            const util::Easing::HandleMode mode = (aSide == 0) ? aParam.handleOut : aParam.handleIn;
            return mode == util::Easing::HandleMode_Free || mode == util::Easing::HandleMode_Aligned;
        };

        for (const TangentTarget& target : aTargets) {
            if (!target.line || !target.key)
                continue;
            const int components = graph::Channel::componentCount(target.type);
            if (target.component < 0 || target.component >= components)
                continue;

            const graph::Channel channel(target.type, target.component);
            const core::TimeLine::MapType& map = target.line->map(target.type);
            const core::TimeLine::MapType::const_iterator self = map.find(target.key->frame());
            if (self == map.end() || self.value() != target.key)
                continue;
            const core::TimeLine::MapType::const_iterator before = (self == map.begin()) ? map.end() : std::prev(self);
            const core::TimeLine::MapType::const_iterator after = std::next(self);

            // THE TWO SEGMENTS the key is an end of. Each one's stored pair lives in
            // its FROM key's param: x1,y1 is that key's OUTGOING handle, x2,y2 the TO
            // key's INCOMING one - so each side's anchor is a different key, and it is
            // the anchor the offset is measured from.
            struct Side {
                core::TimeKey* owner;  // the key whose param stores the pair
                int side;              // 0 = (x1,y1), 1 = (x2,y2)
                const core::TimeKey* anchor;
                const core::TimeKey* from;
                const core::TimeKey* to;
            };
            QVector<Side> sides;
            if (before != map.end()) {
                sides.append(Side { before.value(), 0, before.value(), before.value(), target.key });
                sides.append(Side { before.value(), 1, target.key, before.value(), target.key });
            }
            if (after != map.end()) {
                sides.append(Side { target.key, 0, target.key, target.key, after.value() });
                sides.append(Side { target.key, 1, after.value(), target.key, after.value() });
            }

            for (const Side& side : sides) {
                util::Easing::Param* param = graph::Curve::easingOf(*side.owner, target.type, target.component);
                if (!param || !storedSide(*param, side.side))
                    continue;

                // Deduplicated by (param, side): two moved keys can share a segment,
                // and a pair captured twice would restore twice.
                bool captured = false;
                for (const Tangent& held : mTangents) {
                    if (held.param == param && held.side == side.side) {
                        captured = true;
                        break;
                    }
                }
                if (captured)
                    continue;

                const double fromValue = channel.value(*side.from);
                const double toValue = channel.value(*side.to);
                const double span = toValue - fromValue;
                if (span == 0.0)
                    continue; // a constant segment already draws both handles ON the keys

                const double ny = (side.side == 0) ? param->cubicBezier.y1 : param->cubicBezier.y2;
                const double handleValue = fromValue + ny * span;

                Tangent tangent;
                tangent.param = param;
                tangent.side = side.side;
                tangent.base = *param;
                tangent.anchor = side.anchor;
                tangent.from = side.from;
                tangent.to = side.to;
                tangent.type = target.type;
                tangent.component = target.component;
                tangent.offset = static_cast<float>(handleValue - channel.value(*side.anchor));
                mTangents.append(tangent);
            }
        }
    }

    void KeyMoveCommand::applyTangents() {
        // The handle keeps its VALUE offset from its own key, so the bezier's y is
        // re-derived against the segment's NEW span: y = (anchor + offset - from) /
        // (to - from). Every value here is read from the document, so the step is an
        // absolute function of the values the gesture has written and can be repeated
        // (a rewritten step, a redo) without drifting.
        for (const Tangent& tangent : mTangents) {
            const graph::Channel channel(tangent.type, tangent.component);
            const double fromValue = channel.value(*tangent.from);
            const double toValue = channel.value(*tangent.to);
            const double span = toValue - fromValue;
            if (span == 0.0)
                continue; // a flat segment can express no offset; keep the stored y
            const double anchorValue = channel.value(*tangent.anchor);
            const double ny = (anchorValue + tangent.offset - fromValue) / span;
            if (tangent.side == 0)
                tangent.param->cubicBezier.y1 = static_cast<float>(ny);
            else
                tangent.param->cubicBezier.y2 = static_cast<float>(ny);
        }
    }

    void KeyMoveCommand::exec() {
        // The frame half first: the value halves hold a key by POINTER, and
        // TimeLine::move re-keys the same object, so the two orders agree - the
        // frame move never invalidates a value command's key.
        if (mFrames) {
            mFrames->exec();
        }
        for (const Value& value : mValues) {
            value.command->exec();
        }
        // The values are final for this commit: the proportional keys follow them,
        // and the stored tangents are re-derived against the new spans.
        for (int i = 0; i < mReflows.size(); ++i) {
            mReflows[i].apply();
        }
        applyTangents();
    }

    void KeyMoveCommand::undo() {
        // Reverse of exec, so a key's value is restored before the map entry that
        // names its frame (they are independent, but an undo that reads like the
        // exec backwards is the one that is easy to check). The reflow goes FIRST:
        // it wrote the proportional keys' values, and putting them back before the
        // value halves' own undo is the same backwards order.
        // The tangents were written LAST, so they come back FIRST: each param is
        // restored to the value the gesture found (two tangents of one param share
        // that value, so restoring it twice is the same restore).
        for (int i = mTangents.size() - 1; i >= 0; --i) {
            *mTangents[i].param = mTangents[i].base;
        }
        for (int i = mReflows.size() - 1; i >= 0; --i) {
            mReflows[i].restore();
        }
        for (int i = mValues.size() - 1; i >= 0; --i) {
            mValues[i].command->undo();
        }
        if (mFrames) {
            mFrames->undo();
        }
    }

    void KeyMoveCommand::redo() {
        if (mFrames) {
            mFrames->redo();
        }
        for (const Value& value : mValues) {
            value.command->redo();
        }
        for (int i = 0; i < mReflows.size(); ++i) {
            mReflows[i].apply();
        }
        applyTangents();
    }

    //-------------------------------------------------------------------------------------------------
    HandleDragCommand::HandleDragCommand(
        util::Easing::Param* aMain, const util::Easing::Param& aMainValue, util::Easing::Param* aMirror,
        const util::Easing::Param* aMirrorValue
    ):
        mMain{aMain, util::Easing::Param(), aMainValue},
        mMirror{aMirror, util::Easing::Param(), aMirrorValue ? *aMirrorValue : util::Easing::Param()},
        mHasMirror(aMirror && aMirrorValue),
        mDone(false) {}

    void HandleDragCommand::modifyValues(const util::Easing::Param& aMain, const util::Easing::Param* aMirror) {
        mMain.next = aMain;
        if (mHasMirror && aMirror) {
            mMirror.next = *aMirror;
        }
        if (mDone) {
            redo();
        }
    }

    void HandleDragCommand::exec() {
        // The PRE-drag values are read off the live document when the command
        // first runs, exactly as cmnd::ModifiableAssign does: the drag may have
        // written nothing yet (the first move is the commit), and a side's new
        // HandleMode is part of `next`, not of `prev`.
        mMain.prev = *mMain.target;
        if (mHasMirror) {
            mMirror.prev = *mMirror.target;
        }
        redo();
    }

    void HandleDragCommand::undo() {
        *mMain.target = mMain.prev;
        if (mHasMirror) {
            *mMirror.target = mMirror.prev;
        }
        mDone = false;
    }

    void HandleDragCommand::redo() {
        *mMain.target = mMain.next;
        if (mHasMirror) {
            *mMirror.target = mMirror.next;
        }
        mDone = true;
    }

    //-------------------------------------------------------------------------------------------------
    GraphEditor::GraphEditor(core::Project& aProject, UILogger& aLogger):
        mProject(&aProject),
        mLogger(aLogger),
        mTargets(),
        mHiddenChannels(),
        mOwnSelection(),
        mSelection(&mOwnSelection),
        mScale(),
        mFrameWindowShared(false),
        mFrameWindowLo(0.0),
        mFrameWindowHi(0.0),
        mAppliedLo(0.0),
        mAppliedHi(0.0),
        mAppliedPanelWidth(0), // nothing mapped yet: the first sync always maps
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
        mDragPartnerMirrors(false),
        mWrite(),
        mKeyMoveGesture(aProject.commandStack()),
        mMarqueeOrigin(),
        mMarqueeCurrent(),
        mMarqueeMode(Marquee_Replace),
        mPreMarqueeSelection(),
        mClickCollapseTarget(),
        mClickCollapseComponent(time::Selection::kKeyLevel),
        mClickCollapseValid(false),
        mPanPointer(),
        mScrollBarAxis(ScrollBar_None),
        mScrollBarPointer(),
        mScrollBarContentPerPixel(0.0),
        mScrollBarWheelRemainder(0),
        mMoveLine(nullptr),
        mMoveNode(nullptr),
        mMoveType(core::TimeKeyType_TERM),
        mMoveFrame(),
        mMoveComponent(-1),
        mMovePointerFrame(),
        mMovePressPointerFrame(),
        mMovePressKeyFrame(),
        mMovePressPos(),
        mMoveKeys(),
        mMoveMembers(),
        mAxisFps(0),
        // The frame-number format, from the SAME key the timeline's editor reads
        // (ctrl/TimeLineEditor.h:203, and gui/TimeLineWidget.cpp applies it live):
        // the panel's ruler and the timeline's ruler must write one frame one way.
        mTimeFormat(static_cast<core::TimeFormatType>(
            QSettings().value("generalsettings/ui/timeformat", int(core::TimeFormatType_Frames_From0)).toInt()
        )) {
        // The frame axis' ladder is seeded from the project's fps and range here,
        // so the first geometry the host asks for is already against the right
        // ruler. There is no shown/hidden state to seed: the panel's dock page is
        // the panel whenever its tab is current, and the window itself is fitted
        // when the application hands the editor its targets (see the
        // syncValueAxis note).
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
            // The key move's gesture holds the command stack by reference, so a
            // different document needs a different stack and the reference cannot
            // be reassigned. The gesture is inert at this point (endDrag() above),
            // so it is reconstructed in place - the only way to move it - and an
            // edit can never be pushed into a stack that no longer belongs to the
            // document being edited. The handle drag's live edit holds no such
            // reference: endDrag() closed it, and begin() takes the new stack.
            mKeyMoveGesture.~Gesture();
            ::new (static_cast<void*>(&mKeyMoveGesture))
                cmnd::Gesture<KeyMoveCommand>(aProject->commandStack());
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

    void GraphEditor::panelHidden() {
        // The panel's dock page is no longer current. Nothing here describes a
        // shown/hidden state any more - the page IS the panel while its tab is
        // current, and nothing in this class may gate itself on a flag for it (the
        // viewport's routing no longer asks; gui::GraphPanelWidget owns its page's
        // events). What a hide still has to do is the ONE side effect hiding ever
        // had: drop a gesture in flight. The panel is off screen, so the pixels a
        // further move would be measured against are gone too, and a gesture left
        // open would keep coalescing the next edit into a command whose geometry
        // nobody can see. The selection, the scale and the hidden channels are
        // kept, so the page coming back restores the panel as it was - and the
        // command a dropped gesture pushed stays on the stack (dropping the
        // gesture is not an undo).
        endDrag();
    }

    //-------------------------------------------------------------------------------------------------
    QRect GraphEditor::panelRect(const QSize& aViewportSize) {
        // The panel's own surface: the WHOLE of it, with no band, no inset and no
        // clamped height. It cannot own the pointer by rectangle for exactly that
        // reason - a rect that contains everything the host hands it would make
        // every point the panel's - so ownership is hit-based instead, see
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
    // A curve of this project's channel, carrying the project's rule for the frames OUTSIDE the keys
    // (see graph::Curve::setLoop): a looping project continues the easing from the last key back to
    // the first, and everything that reads a curve - the drawn polyline, the value fit's samples, the
    // insert's seeding - has to read the same rule or the panel would disagree with playback.
    graph::Curve GraphEditor::curveFor(const core::TimeLine& aLine, const graph::Channel& aChannel) const {
        graph::Curve curve(aLine, aChannel);
        curve.setLoop(isLooping(), frameRange());
        return curve;
    }

    // The value playback reports for one component of a key type at a frame: the insert's seeding
    // (an insert must be invisible on every channel, so it carries the value the curve shows there,
    // loop rule included).
    float GraphEditor::sampledChannelValue(
        const core::TimeLine& aLine, core::TimeKeyType aType, int aComponent, core::Frame aFrame
    ) const {
        return curveFor(aLine, graph::Channel(aType, aComponent)).sample(aFrame);
    }

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
    // The shared frame window (see the header). The timeline hands in the frames it is
    // showing; this maps them onto the panel's plot.
    void GraphEditor::setFrameWindow(double aFramesLo, double aFramesHi) {
        mFrameWindowShared = true;
        mFrameWindowLo = aFramesLo;
        mFrameWindowHi = aFramesHi;
        syncFrameWindow();
    }

    void GraphEditor::setFrameGestureDelegates(FrameScrollFunc aScrollFrames, FrameZoomFunc aZoomFrames) {
        mScrollFrames = aScrollFrames;
        mZoomFrames = aZoomFrames;
    }

    void GraphEditor::syncFrameWindowFor(const QSize& aViewportSize) {
        if (!mFrameWindowShared)
            return; // the frame axis is the panel's own: nothing to derive

        mScale.setRect(panelRect(aViewportSize));
        syncFrameWindow();
    }

    void GraphEditor::clearFrameAxis() {
        mFrameWindowShared = false;
        mFrameWindowLo = mFrameWindowHi = 0.0;
        mAppliedLo = mAppliedHi = 0.0;
        mAppliedPanelWidth = 0;
        mScale.clearFrameWindow();
        mScrollFrames = nullptr;
        mZoomFrames = nullptr;
    }

    void GraphEditor::syncFrameWindow() {
        if (!mFrameWindowShared)
            return;

        // The mapping spans the PANEL now (Scaler::applyFrameWindow), so the width
        // that matters is the panel's, not the plot's: a change of the label column's
        // measured reserve must not re-map the shared window.
        const int panelWidth = mScale.rect().width();
        if (panelWidth <= 0)
            return; // no panel laid out yet: keep the window

        if (mFrameWindowLo == mAppliedLo && mFrameWindowHi == mAppliedHi && panelWidth == mAppliedPanelWidth)
            return;

        if (!mScale.applyFrameWindow(mFrameWindowLo, mFrameWindowHi))
            return;

        mAppliedLo = mFrameWindowLo;
        mAppliedHi = mFrameWindowHi;
        mAppliedPanelWidth = panelWidth;
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
        // Both axes at once: what the panel's "Fit graph" context-menu entry runs.
        // (The plot's double-click used to do this and was removed - it was too
        // easy to trigger by accident.) The value side is the union of every
        // shown curve; the frame side covers every shown key.
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
        // The extent is the union of EVERY shown curve's keys and of a walk through
        // its segments (see valueExtentOf) - the one computation the vertical scroll
        // bar's content reads too (shownValueExtent), so the window the fit sets and
        // the window the bar measures its thumb against cannot drift.
        float lo = 0.0f;
        float hi = 0.0f;

        // No shown curve has a key: the panel is empty (or every channel shown is
        // keyless), and blanking the axis here would throw away a window the user
        // zoomed. Leaving it is the only harmless answer.
        if (!valueExtentOf(aItems, lo, hi))
            return;

        mScale.fitValueWindow(lo, hi);
    }

    //-------------------------------------------------------------------------------------------------
    namespace {
    // The ruler band's slot, exactly as gui::GraphPanelModel publishes it: the
    // scaler's own kRulerBandHeight rows directly above the plot, as wide as the PANEL
    // (the shared frame mapping puts frames on the timeline's columns, and the band
    // covers every one of them, including the value column's top corner). A press
    // here is the ruler's seek (the timeline's TimeRuler gesture), so this rect is HIT
    // geometry and must not be derived anywhere else.
    QRect rulerBandRect(const graph::Scaler& aScale) {
        const QRect plot = aScale.plotRect();
        const QRect panel = aScale.rect();
        if (plot.isEmpty())
            return QRect();
        // The scaler's MARGIN rows sit between the hairline and the plot (see
        // Scaler::plotRect: top inset = kMargin + kRulerBandHeight), so the band is
        // the 22 rows ABOVE that margin - the slot gui::GraphPanelModel anchors to the
        // panel's top. A band rect derived from plot.top() alone would cover the margin
        // and miss the band's own first rows.
        return QRect(
            panel.left(), plot.top() - graph::Scaler::kMargin - graph::Scaler::kRulerBandHeight, panel.width(),
            graph::Scaler::kRulerBandHeight
        );
    }
    } // namespace

    bool GraphEditor::ownsCursor(const core::CameraInfo& aCamera, const core::AbstractCursor& aCursor) const {
        // A gesture keeps the cursor even when the pointer is nowhere near an
        // item: the button is still down on something this editor is moving, and
        // the release that ends it (and every move in between) may land anywhere
        // in the viewport.
        //
        // mSeeking IS ONE OF THOSE GESTURES, and it was missing here - which is what
        // broke scrubbing in the running application while this file's own headless
        // test passed: the press claimed the band, updateCursor started the seek, and
        // then every MOVE and the RELEASE were asked about with mDragging still false,
        // so ownsCursor answered "nobody's" and the host never delivered them. A
        // band press therefore seeked exactly once, at the press; a press on the pill
        // itself (whose x is the frame already showing) moved nothing at all, which is
        // the "grabbing the playhead does nothing" report. The seek arms no command,
        // but it is a gesture and it owns the pointer until its release.
        if (mDragging || mSeeking || mWindowing)
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

        // THE RULER BAND: the timeline's seek, on the timeline's geometry. Claimed
        // before the bars (the band is above the plot, the bars beside/below it) and
        // only while a band delegate exists - without one the band belongs to nobody
        // and a press there is not the panel's to swallow. A SHIFT-press is the
        // band's other gesture, the temporary playback window (see
        // setPlayWindowDelegate), so it needs the window delegate rather than the
        // seek's; the two presses are told apart by the modifier.
        if (aCursor.emitsLeftPressedEvent() && rulerBandRect(scale).contains(point)) {
            if (mSetPlayWindow && mModifiers.testFlag(Qt::ShiftModifier))
                return true;
            if (mSeekFrame)
                return true;
        }

        // THE SCROLL BARS, before every other hit: a press on a track or a thumb is
        // the panel's own window gesture (see THE SCROLL BARS in the header), and it
        // must not become a plot gesture - a tangent handle can hang over the bar's
        // rows, since a handle is grabbed wherever its dot is (see hitTest).
        //
        // ONLY A PRESS asks: the bars' geometry costs the value extent's walk (one
        // sample per kFitSamplesPerSegment per segment, see valueExtentOf), and this
        // method runs on EVERY cursor move - a hover over a bar is nobody's, and the
        // question "is this press on a bar" is the only one wanted here.
        if (aCursor.emitsLeftPressedEvent()) {
            const graph::Scaler::ScrollBars bars = scrollBarsFor(scale, entries);
            // A lane with a RUN is the panel's, whether or not the window covers its
            // content: a full-length run is still grabbable, exactly as the
            // timeline's full-width handle is (TimeLineWidget::eventFilter reaches
            // beginScrollbarDrag from handleRect, and its `range / span` ratio stays
            // positive at range == pageStep). Gating on "the window is narrower than
            // the track" left the full lane inert here while the timeline's moved -
            // the reported "the scroll bar does not work when it is full width".
            // hHasRun is the layout's own answer (an empty handle is the only dead
            // lane), so a degenerate track still claims nothing.
            const bool onHorizontal = bars.hHasRun() && bars.hTrack.contains(point);
            const bool onVertical = bars.vHasRun() && bars.vTrack.contains(point);
            if (onHorizontal || onVertical)
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
            // this is endDrag() and not just mWrite.end(): a gesture still
            // continuing would coalesce the next drag into the previous undo
            // step, and a key-move command left open would be rewritten by the
            // next drag - updateKeyMove asks isModifiable, and it would still be
            // the stack's modifiable command, now aiming at the key this press
            // armed instead.
            endDrag();

            const QVector<Entry> entries = buildEntries();

            // THE RULER BAND'S SHIFT GESTURE (see setPlayWindowDelegate): the
            // temporary playback window, the timeline's own second ruler gesture.
            // The press only ANCHORS (the timeline shows nothing until the first
            // drag), and it takes precedence over the seek - exactly as Shift does
            // over the header seek in ctrl::TimeLineEditor::updateCursor.
            if (mSetPlayWindow && rulerBandRect(mScale).contains(point)
                && mModifiers.testFlag(Qt::ShiftModifier)) {
                mWindowing = true;
                mSetPlayWindow(TimeLineEditor::PlayWindowStep::Begin, core::Frame(mScale.frameAt(point.x())));
                return true;
            }

            // THE RULER BAND'S SEEK (see setFrameSeekDelegate): the frame under the
            // press, then one per move - the timeline's own gesture, whose every step
            // is a frame the user sees. Nothing is armed and no command is pushed:
            // the frame is the project's, not the document's.
            if (mSeekFrame && rulerBandRect(mScale).contains(point)) {
                mSeeking = true;
                mSeekFrame(core::Frame(mScale.frameAt(point.x())));
                return true;
            }

            // THE SCROLL BARS: the bars are chrome OUTSIDE the plot, and a press on
            // one is the panel's own window gesture - never the plot's, and never the
            // tools' (ownsCursor claims it before its other hits, and this is the pass
            // that acts on it). The test comes before the hit-test for the same reason
            // it does there: a tangent handle can hang over the bar's rows, and the
            // bar is what the pointer is on.
            if (beginScrollBarPress(scrollBarsFor(mScale, entries), point))
                return true;

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
                // THE KEY UNDER THE PRESS, written in the TIMELINE's own currency
                // and with the timeline's own rules: a press on a marker names the
                // KEY that owns it, and all three gestures below are the table
                // ctrl::TimeLineEditor::updateCursor follows for a press on a key.
                // The panel's finer marker selection is the Ctrl-insert's alone
                // (selectMarker, see insertKeyAt).
                const graph::Channel channel = entries[hit.item].channel;
                const core::TimeLineEvent::Target target =
                    markerTarget(*entries[hit.item].node, channel, hit.keyFrame);
                if (!target.pos.isNull()) {
                    // THE MARKER IS THE UNIT the pointer writes - time::Selection's
                    // own second level, a (key, component) pair. The two components of
                    // one key are INDEPENDENT CHANNELS, and only their FRAME is shared,
                    // so a press that wrote the whole key made a value drag write the
                    // sibling's value with the same delta. MEASURED 2026-10-24, the
                    // user's report: "dragging one moves the sibling linked on both
                    // axes; they should only be on the time axis, not the value axis".
                    // The frame half of a move reads every selected KEY (a marker entry
                    // names its key exactly as a whole-key one does), so nothing is lost
                    // on the time axis - see beginKeyMove's two halves.
                    const int component = channel.component();
                    if (mModifiers.testFlag(Qt::ShiftModifier)
                        || mModifiers.testFlag(Qt::ControlModifier)) {
                        // SHIFT AND CTRL BOTH TOGGLE the marker: in when it is out, out
                        // when it is in - the timeline's toggle, one level finer. A
                        // whole-key entry is what subtractMarker removes for it, the
                        // set's own rule.
                        if (mSelection->containsMarker(target, component))
                            mSelection->subtractMarker(target, component);
                        else
                            mSelection->addMarker(target, component);
                    } else {
                        // A PLAIN PRESS: a marker the selection already covers is
                        // GRABBED - the selection is left exactly as it is, so the
                        // drag armed below carries the whole group - while a marker
                        // that is out is what the press REPLACES the selection
                        // with. Either way the release of a press that never moved
                        // collapses to this one marker: that is the timeline's
                        // mPressedTarget pair, and it is what makes the group
                        // survive a DRAG but not a CLICK.
                        mClickCollapseTarget = target;
                        mClickCollapseComponent = component;
                        mClickCollapseValid = true;
                        if (!mSelection->containsMarker(target, component))
                            mSelection->setMarker(target, component);
                    }
                }
                // The same hit makes this entry the focused channel, which is
                // what the panel emphasises.
                setFocusedChannel(*entries[hit.item].node, channel);

                if (mModifiers.testFlag(Qt::AltModifier)) {
                    // ALT+CLICK ON A KEY IS THE LINK ACTION ("make smooth"): the
                    // key's two handles are made collinear through it along the
                    // CHORD from the PREVIOUS key's anchor to the NEXT key's
                    // anchor, in PIXELS - a bezier editor's smooth tangent - and
                    // each handle keeps its own pixel length. The direction is
                    // READ FROM THE NEIGHBOURS ALONE, so it cannot depend on
                    // which handle happens to be longer, and it is the same
                    // direction every time the click is repeated.
                    //
                    // A first/last key owns one handle, so it has nothing to
                    // pair and the press is inert (it needs BOTH neighbouring
                    // anchors to have a chord). The write is ONE command over
                    // BOTH params in ONE macro, so one undo restores the free
                    // pair AND the modes whole - and a derived side is simply
                    // overwritten by the stored chord position (its mode becomes
                    // Aligned, which makes that stored position the truth).
                    const Entry& entry = entries[hit.item];
                    // The entry's own line: the map its curve draws.
                    core::TimeLine* line = entry.line;
                    const core::TimeKeyType type = entry.channel.type();
                    core::TimeKey* key = line->timeKey(type, hit.keyFrame.get());

                    int outIndex = -1;
                    int inIndex = -1;
                    for (int s = 0; key && s < entry.curve.segmentCount(); ++s) {
                        const graph::Curve::Segment seg = entry.curve.segment(s);
                        if (seg.from->frame() == key->frame())
                            outIndex = s;
                        if (seg.to->frame() == key->frame())
                            inIndex = s;
                    }

                    // The clicked key's own param carries the flag; the previous
                    // key's param holds the clicked key's INCOMING handle.
                    util::Easing::Param* outParam = key ? graph::Curve::easingOf(*key, type, entry.channel.component()) : nullptr;
                    core::TimeKey* prevKey =
                        (inIndex >= 0)
                        ? line->timeKey(type, entry.curve.segment(inIndex).from->frame())
                        : nullptr;
                    util::Easing::Param* inParam =
                        prevKey ? graph::Curve::easingOf(*prevKey, type, entry.channel.component()) : nullptr;

                    QPointF outPair[2];
                    QPointF inPair[2];
                    if (mProject && outParam && inParam && outIndex >= 0 && inIndex >= 0
                        && effectiveHandles(entry.curve, entry.curve.segment(outIndex), outPair)
                        && effectiveHandles(entry.curve, entry.curve.segment(inIndex), inPair)) {
                        const graph::Curve::Segment outSegment = entry.curve.segment(outIndex);
                        const graph::Curve::Segment inSegment = entry.curve.segment(inIndex);
                        const core::TimeKey* nextKey = outSegment.to;
                        const QPointF keyPixel =
                            graph::keyDot(mScale, core::Frame(key->frame()), entry.curve.valueOf(*key));
                        const QPointF prevPixel = graph::keyDot(
                            mScale, core::Frame(prevKey->frame()), entry.curve.valueOf(*prevKey)
                        );
                        const QPointF nextPixel = graph::keyDot(
                            mScale, core::Frame(nextKey->frame()), entry.curve.valueOf(*nextKey)
                        );
                        const QPointF chord(
                            nextPixel.x() - prevPixel.x(), nextPixel.y() - prevPixel.y()
                        );
                        const double chordLength = std::hypot(chord.x(), chord.y());

                        if (chordLength > 0.0) {
                            const QPointF direction(
                                chord.x() / chordLength, chord.y() / chordLength
                            );
                            const QPointF outPixel = graph::handlePointAt(mScale, outSegment, outPair[0]);
                            const QPointF inPixel = graph::handlePointAt(mScale, inSegment, inPair[1]);
                            const double outLength = pixelDistance(keyPixel, outPixel);
                            const double inLength = pixelDistance(keyPixel, inPixel);

                            // The OUTGOING handle continues along the chord (it
                            // leaves toward the next anchor); the INCOMING one
                            // sits on the opposite ray, on the side the previous
                            // anchor came from. Both at their own lengths.
                            const QPointF outWant(
                                keyPixel.x() + direction.x() * outLength,
                                keyPixel.y() + direction.y() * outLength
                            );
                            const QPointF inWant(
                                keyPixel.x() - direction.x() * inLength,
                                keyPixel.y() - direction.y() * inLength
                            );

                            util::Easing::Param outNext = *outParam;
                            util::Easing::Param inNext = *inParam;
                            const bool writtenOut = alignedHandleParam(
                                mScale, outSegment, *outParam, outPair, 0, outWant, outNext
                            );
                            const bool writtenIn = alignedHandleParam(
                                mScale, inSegment, *inParam, inPair, 1, inWant, inNext
                            );
                            // The MODES belong to the clicked key, i.e. the
                            // param holding its outgoing handle - not to the
                            // previous key whose param merely stores the incoming
                            // one. BOTH sides become Aligned (Blender's smooth
                            // node): the two stored control points were just
                            // placed on the chord, at their own lengths.
                            outNext.handleOut = util::Easing::HandleMode_Aligned;
                            outNext.handleIn = util::Easing::HandleMode_Aligned;

                            // Already linked and already in that exact place: the
                            // click changed nothing, so it pushes nothing.
                            if (writtenOut && writtenIn
                                && (outNext != *outParam || inNext != *inParam)) {
                                cmnd::ScopedMacro macro(
                                    mProject->commandStack(), CmndName::tr("align handles")
                                );
                                auto* notifier = new TimeLineUtil::Notifier(*mProject);
                                notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
                                notifier->event().pushTarget(*entry.node, type, key->frame());
                                notifier->event().pushTarget(*entry.node, type, prevKey->frame());
                                macro.grabListener(notifier);
                                mProject->commandStack().push(
                                    new HandleDragCommand(outParam, outNext, inParam, &inNext)
                                );
                            }
                        }
                    }
                } else {
                    // ...and ARMS the move: a drag from this press changes the
                    // key's frame and value. Nothing is committed by the press
                    // itself, so a click that only selects leaves the history
                    // untouched.
                    beginKeyMove(hit, entries, point);
                }
            } else if (inPlot) {
                // ONE plot, two gestures, decided by what is under the pointer:
                // a CTRL-press on a curve's band INSERTS the key that curve
                // already passes through (see insertKeyAt's ADD note), and
                // everything else - a plain press on the band included, and a
                // Ctrl-press off every band - opens the MARQUEE. They cannot
                // collide: a point on a band that carries a key is caught by the
                // key branch above, where Ctrl means "toggle it in the
                // selection". insertKeyAt answers whether it inserted, so this
                // needs no second band test. The marquee clears the selection at
                // the press (plain), adds or subtracts with a modifier, and applies
                // its box on every drag step - the timeline's own gesture; only a
                // press and release on the same pixel is the click (see
                // endMarquee).
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
            // THE WINDOW'S DRAG: the band's Shift-press keeps setting the window
            // wherever the pointer goes (the frame is clamped inside the timeline's
            // own step), and it is checked before the seek because a press starts
            // one gesture or the other, never both.
            if (mWindowing) {
                if (mSetPlayWindow)
                    mSetPlayWindow(TimeLineEditor::PlayWindowStep::Drag, core::Frame(mScale.frameAt(point.x())));
                return true;
            }
            // THE SEEK'S DRAG: the band's press keeps seeking wherever the pointer
            // goes (the frame clamps into the range inside the timeline's own setter),
            // and it is checked before the drags because a seek arms none of them.
            if (mSeeking) {
                if (mSeekFrame)
                    mSeekFrame(core::Frame(mScale.frameAt(point.x())));
                return true;
            }
            if (!mDragging)
                return false; // a press that took nothing leaves the drag to nobody

            if (mScrollBarAxis != ScrollBar_None) {
                // THE SCROLL BAR DRAG: the WINDOW follows the pill, so the content
                // under the pointer moves with it - drag the horizontal pill RIGHT and
                // later frames come into view, drag the vertical pill UP and higher
                // values do. That is the timeline's own bar direction
                // (TimeLineWidget::beginScrollbarDrag: a rightward drag advances the
                // viewport), and it is the OPPOSITE of the middle-button pan's sign -
                // a pan drags the CONTENT, a scroll bar drags the VIEWPORT - which is
                // why the two do not share panFramesByPixels.
                //
                // THE DELTA IS THE BAR'S OWN RATIO, not the axis' page-per-panel
                // fraction (mScrollBarContentPerPixel, anchored at press exactly as
                // TimeLineWidget::beginScrollbarDrag anchors its ratio). A scroll bar
                // whose window is P of a C-unit content moves its handle by dx when the
                // window moves by dx * C/T; converting the pointer through the page
                // fraction P/T instead moved the handle by dx * P/C and left it sliding
                // out from under the pointer. The pointer may leave the lane: ownsCursor
                // keeps the gesture (mDragging), and a pointer past the end keeps
                // scrolling rather than sticking.
                const QPoint delta = point - mScrollBarPointer;
                mScrollBarPointer = point;
                if (!delta.isNull()) {
                    if (mScrollBarAxis == ScrollBar_Horizontal) {
                        const double frameDelta = double(delta.x()) * mScrollBarContentPerPixel;
                        if (mFrameWindowShared) {
                            if (mScrollFrames)
                                mScrollFrames(frameDelta);
                        } else {
                            mScale.panFrame(-frameDelta * mScale.framePpf());
                        }
                    } else {
                        const double valueDelta = -double(delta.y()) * mScrollBarContentPerPixel;
                        const double height = double(mScale.plotRect().height());
                        const double span = double(mScale.valueMax()) - double(mScale.valueMin());
                        if (height > 0.0 && span > 0.0)
                            mScale.panValue(valueDelta * height / span);
                    }
                }
                return true;
            }

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
            // THE WINDOW'S RELEASE: the timeline's ending rule (a Shift-click, or a
            // drag that collapsed to one frame, dismisses; a real drag keeps what it
            // set) is applied by the owner of the window, on the release's own frame.
            // The modifier is deliberately NOT consulted: the timeline's gesture does
            // not ask for Shift on the release either, so letting go of Shift mid-drag
            // still commits the window it made.
            if (mWindowing) {
                if (mSetPlayWindow)
                    mSetPlayWindow(TimeLineEditor::PlayWindowStep::End, core::Frame(mScale.frameAt(point.x())));
                mWindowing = false;
                return true;
            }
            if (mSeeking) {
                // The release's own position is the last frame the user aimed at.
                if (mSeekFrame)
                    mSeekFrame(core::Frame(mScale.frameAt(point.x())));
                mSeeking = false;
                return true;
            }
            if (mMarqueeArmed) {
                // The box was applied on the last drag step; the release only ends
                // the gesture (and turns a no-travel press into the click it is -
                // see endMarquee). The release's own position is NOT the box's
                // last corner, which is the timeline's rule too.
                endMarquee();
                return true;
            }
            if (mDragging) {
                // THE CLICK THAT WAS A KEY: the timeline's own release rule. A
                // press on a key arms a move; when the move never happened (no
                // command was pushed) the release collapses the selection to the
                // key that was pressed - a plain click on an already-selected key
                // is how a group is reduced to one key. A move that DID happen
                // leaves the group selected, and the selection follows the keys to
                // the frames they now hold (Selection::refresh, exactly as the
                // timeline's own release does).
                const bool moved = isMovingKeys();
                if (moved) {
                    mSelection->refresh();
                } else if (mClickCollapseValid && !mClickCollapseTarget.pos.isNull()) {
                    // THE CLICK IS THE MARKER'S: the press wrote the marker under the
                    // pointer (or found it covered and grabbed the group), and a
                    // release that never moved keeps exactly that one - the finer
                    // half of the timeline's mPressedTarget rule.
                    mSelection->setMarker(mClickCollapseTarget, mClickCollapseComponent);
                }
                mClickCollapseValid = false;
                endDrag();
                return true;
            }
            if (mClickCollapseValid) {
                // A key press that armed NO drag at all (Alt+click is the link
                // action, which moves the key's handles and not the key): its
                // release is still the CLICK, so the same collapse applies.
                if (!mClickCollapseTarget.pos.isNull())
                    mSelection->setMarker(mClickCollapseTarget, mClickCollapseComponent);
                mClickCollapseValid = false;
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
        const core::CameraInfo& aCamera, const QPoint& aPos, int aWheelDelta, Qt::KeyboardModifiers aModifiers,
        bool aInvertScaling
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

        // THE BARS FIRST: a notch over a LANE scrolls that bar - Qt's own QScrollBar
        // wheel (QApplication::wheelScrollLines() single steps per notch, accumulated
        // across events in mScrollBarWheelRemainder) - and NEVER zooms. Which lane is
        // under the pointer is asked of the same geometry the press routing and the
        // model's drawing read (scrollBarsFor). A lane with a run counts whether or
        // not the window covers its content: the timeline's full-width bar scrolls
        // on a notch too (its eventFilter's Wheel branch never asks how much travel
        // the slider reports), so a full run must not fall through to the zoom.
        {
            const QVector<Entry> entries = buildEntries();
            const graph::Scaler::ScrollBars bars = scrollBarsFor(mScale, entries);
            const bool onHorizontal = bars.hHasRun() && bars.hTrack.contains(aPos);
            const bool onVertical = bars.vHasRun() && bars.vTrack.contains(aPos);
            if (onHorizontal || onVertical) {
                // The bar's own direction, in notches: wheel UP (a positive Qt delta)
                // scrolls toward the lane's START - earlier frames on the horizontal
                // bar, higher values on the vertical one - which is the timeline's own
                // bar rule (TimeLineWidget's eventFilter: `dir = delta > 0 ? -1 : 1`
                // over its value, whose start is earlier content).
                mScrollBarWheelRemainder += aWheelDelta;
                const int notches = mScrollBarWheelRemainder / 120;
                mScrollBarWheelRemainder -= notches * 120;
                if (notches != 0) {
                    const int lines = qMax(1, QApplication::wheelScrollLines());
                    const int stepPixels = notches * lines * graph::Scaler::kScrollBarStepPixels;
                    if (onHorizontal)
                        scrollFramesByBarPixels(mScale, -stepPixels);
                    else
                        scrollValueByBarPixels(mScale, stepPixels);
                }
                return true; // a lane is not a plot: the notch never zooms
            }
        }

        // THE MAPPING, axis by axis (see updateWheel's declaration for the table):
        //   * plain          - zoom BOTH axes about the cursor;
        //   * Shift          - SCROLL the FRAME axis, the timeline's own 50px per
        //                      notch toward EARLIER frames (wheel up);
        //   * Ctrl           - zoom the VALUE axis alone (vertical);
        //   * Ctrl + Shift   - zoom BOTH axes, exactly as plain does.
        // Shift is the timeline's gesture, not a zoom of this panel's: the dock's
        // two views share the frame axis, and over the timeline's plot Shift+wheel
        // scrolls it (TimeLineWidget::wheelEvent). The same notch over the graph
        // must do the same thing in the same direction, through the same shared
        // window (see mScrollFrames below); a frame-only zoom here would make one
        // dock answer one gesture two ways. Ctrl alone and Ctrl+Shift keep their
        // zooms - the value axis is the panel's own and the frame zoom it drives
        // while a timeline owns the axis is delegated too. NOTHING ELSE SCROLLS:
        // without Shift the window still moves through the scroll bars and the
        // middle drag (see the SCROLL BARS note in the header).
        //
        // DIRECTION: a notch Qt reports as positive zooms OUT at 1.1 of scale, the
        // canvas mover's and the timeline scaler's own sense (see aInvertScaling for
        // the user's own). The SCROLL branch is NOT inverted by that setting: the
        // timeline's Shift scroll does not read it either (TimeLineWidget::
        // wheelEvent's Shift branch takes the raw angle delta), and one dock's two
        // panes must scroll the same way whatever the zoom preference is.
        const int delta = aInvertScaling ? -aWheelDelta : aWheelDelta;
        if (delta == 0)
            // a pixel-only wheel event with no angle delta: no notch to act on
            // (and, as before, no canvas zoom either)
            return true;
        const bool shift = aModifiers.testFlag(Qt::ShiftModifier);
        const bool ctrl = aModifiers.testFlag(Qt::ControlModifier);

        // SHIFT ALONE: the timeline's frame-axis scroll. 50 content pixels per notch,
        // wheel up = earlier frames, exactly as TimeLineWidget::wheelEvent's Shift
        // branch moves its view transform. While a timeline owns the axis the move
        // goes through the delegate in FRAMES (scrollFramesBy takes frames and
        // converts them at its own rate, so the distance survives a zoom); the
        // panel's mapping rate is the same rate, which is what makes the delegated
        // delta exactly the timeline's 50 pixels. With no timeline the panel owns
        // the axis, and panFrame moves its content origin the same way.
        if (shift && !ctrl) {
            // The CONTENT-PIXEL step is positive for wheel up, exactly as the
            // timeline's Shift branch adds +50 to its view transform, and
            // panFramesByPixels is the one place the pixel-to-frame sign lives: it
            // moves the content right (earlier frames) on a positive delta, through
            // the delegate while the timeline owns the axis and by panFrame otherwise.
            panFramesByPixels(mScale, aWheelDelta > 0 ? kFrameWheelScrollPixels : -kFrameWheelScrollPixels);
            return true;
        }

        // Which axes this notch zooms: Shift alone (handled above) narrows it to the
        // frame axis, Ctrl alone to the value axis, and both modifiers (like
        // neither) move both.
        const bool zoomFrame = shift || !ctrl;
        const bool zoomValue = ctrl || !shift;

        // The frame half of a zoom, whichever view owns the axis. While a timeline
        // owns it the notch goes to IT, about the frame under the cursor -
        // sub-pixel (frameAtPixel, never frameAt), so the timeline can put that
        // exact frame back under the cursor. The panel's X then follows the window
        // the timeline pushes back.
        const auto zoomFrames = [&]() {
            if (mFrameWindowShared) {
                if (mZoomFrames)
                    mZoomFrames(delta, mScale.frameAtPixel(aPos.x()));
            } else {
                mScale.updateFrameZoom(delta, aPos.x());
            }
        };

        // The two zooms, each about the cursor's own coordinate (the row for the
        // value axis, the column for the frame axis): the point under the pointer
        // stays under it, which is what makes a two-axis notch hold its shape. The
        // negated delta is what turns Qt's notch direction into the canvas's.
        if (zoomValue)
            mScale.updateValueZoom(static_cast<float>(notchZoomFactor(-delta)), aPos.y());
        if (zoomFrame)
            zoomFrames();
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
    int GraphEditor::markerIndexOf(
        const core::ObjectNode& aNode, const graph::Channel& aChannel, core::Frame aFrame
    ) const {
        // The quadruple a marker is named by, compared the way selectMarker writes
        // it: the node by pointer identity (never dereferenced here), the type, the
        // frame and the component. An entry with NO component (a whole key - what
        // the timeline's own gestures write) covers every component its key owns, so
        // it covers this marker too: that is the set's own rule (time::Selection::
        // containsMarker) restated over the items, which is what makes a key the
        // timeline selected behave as fully selected in the panel.
        const int component = aChannel.component();
        const QVector<time::Selection::Item>& items = mSelection->items();
        for (int i = 0; i < items.size(); ++i) {
            const time::Selection::Item& item = items[i];
            const core::TimeLineEvent::Target& target = item.target;
            if (target.node != &aNode || target.pos.type() != aChannel.type()
                || target.pos.index() != aFrame.get()) {
                continue;
            }
            if (item.component == time::Selection::kKeyLevel || item.component == component) {
                return i;
            }
        }
        return -1;
    }

    bool GraphEditor::isMarkerSelected(
        const core::ObjectNode& aNode, core::TimeKeyType aType, core::Frame aFrame, int aComponent
    ) const {
        return markerIndexOf(aNode, graph::Channel(aType, aComponent), aFrame) >= 0;
    }

    bool GraphEditor::isMarkerSelected(
        const core::ObjectNode& aNode, const graph::Channel& aChannel, core::Frame aFrame
    ) const {
        return isMarkerSelected(aNode, aChannel.type(), aFrame, aChannel.component());
    }

    bool GraphEditor::hasSelectedSiblingMarker(
        const core::ObjectNode& aNode, core::TimeKeyType aType, core::Frame aFrame, int aComponent
    ) const {
        // Another MARKER of the same key: an entry that names a different component
        // of it, or names the whole key (which is all of them). A component the
        // graph cannot draw is still a marker of its key, but the drawing layer only
        // ever asks about the components it draws - so nothing here filters by
        // type, and a type with one component simply has no sibling.
        const QVector<time::Selection::Item>& items = mSelection->items();
        for (const time::Selection::Item& item : items) {
            const core::TimeLineEvent::Target& target = item.target;
            if (target.node != &aNode || target.pos.type() != aType || target.pos.index() != aFrame.get()) {
                continue;
            }
            if (item.component == time::Selection::kKeyLevel || item.component != aComponent) {
                return true;
            }
        }
        return false;
    }

    bool GraphEditor::hasSelectedSiblingMarker(
        const core::ObjectNode& aNode, const graph::Channel& aChannel, core::Frame aFrame
    ) const {
        return hasSelectedSiblingMarker(aNode, aChannel.type(), aFrame, aChannel.component());
    }

    bool GraphEditor::modifyMarkerSelection(
        core::ObjectNode& aNode, const graph::Channel& aChannel, core::Frame aFrame, bool aAdd
    ) {
        // ONE MARKER, named the timeline's way plus the component the press's own
        // channel carries: the target names the KEY half of the identity, the
        // component is the marker half. The set's own rules decide the rest -
        // addMarker ignores a marker it already covers, subtractMarker ignores one
        // nothing covers - so this does not repeat them.
        core::TimeLineEvent::Target target = markerTarget(aNode, aChannel, aFrame);
        if (target.pos.isNull())
            return false;
        return aAdd ? mSelection->addMarker(target, aChannel.component())
                    : mSelection->subtractMarker(target, aChannel.component());
    }

    bool GraphEditor::modifySelection(
        core::ObjectNode& aNode, const graph::Channel& aChannel, core::Frame aFrame, bool aAdd
    ) {
        // ONE KEY - the whole of it, every component it owns - named the timeline's
        // way: the event is the selection's own currency, so add/subtract below are
        // the set's own operations (and they rebuild the keys' focus, which is what
        // the timeline highlights). The "already there" / "not there" cases are the
        // set's rules, not this method's: Selection::add ignores an entry it has,
        // subtract ignores one it has not.
        core::TimeLineEvent one;
        one.pushTarget(aNode, aChannel.type(), aFrame.get());
        return aAdd ? mSelection->add(one) : mSelection->subtract(one);
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
        // THE MODE IS THE PRESS'S, recorded here and never re-read from the
        // modifiers: plain replaces, Shift adds, Ctrl subtracts - ctrl::
        // TimeLineEditor::updateCursor's own assignment, in its own order (Ctrl
        // wins when both are held). The selection those two modifier modes work
        // from is the one the press found:
        //   * REPLACE clears it NOW (the timeline does - the click that never
        //     drags has already deselected by the time the button comes up);
        //   * ADD and SUBTRACT snapshot it, because add re-applies the snapshot
        //     to the box on every step.
        mMarqueeMode = mModifiers.testFlag(Qt::ControlModifier)
            ? Marquee_Subtract
            : (mModifiers.testFlag(Qt::ShiftModifier) ? Marquee_Add
                                                      : Marquee_Replace);
        mPreMarqueeSelection = core::TimeLineEvent();
        if (mMarqueeMode == Marquee_Replace) {
            clearSelection();
        } else {
            mSelection->assign(mPreMarqueeSelection);
        }
        mDragKey = nullptr;
        mDragItem = -1;
        mDragSegment = -1;
        mDragHandle = -1;
        mDragPartnerMirrors = false;
    }

    void GraphEditor::updateMarquee(const QPoint& aPos) {
        mMarqueeCurrent = aPos;
        applyMarquee();
    }

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

    QRect GraphEditor::scrollViewport() const { return mScale.plotRect(); }

    bool GraphEditor::autoScrollMarquee() {
        if (!mMarqueeArmed)
            return false;

        const QRect plot = mScale.plotRect();
        const autoscroll::Step step = autoscroll::edgeStep(mMarqueeCurrent, plot);
        if (step.isNull())
            return false;

        // Snapshot the window, apply only the axes the pointer is near, then ask
        // whether anything actually moved. A scroll bar's own primitives are used
        // (shared frame delegate included), so the directions are the ones every
        // other pan/scroll in this panel already has.
        const double frameBefore = mScale.frameAtPixel(plot.left());
        const float valueMinBefore = mScale.valueMin();
        const float valueMaxBefore = mScale.valueMax();

        if (step.x != 0)
            scrollFramesByBarPixels(mScale, step.x); // positive advances the viewport
        if (step.y != 0)
            mScale.panValue(-step.y); // positive step reveals lower values

        const bool frameMoved = mScale.frameAtPixel(plot.left()) != frameBefore;
        const bool valueMoved = mScale.valueMin() != valueMinBefore || mScale.valueMax() != valueMaxBefore;
        if (!frameMoved && !valueMoved)
            return false;

        // The pointer stays at its screen position while the content moves under it;
        // the box is re-evaluated against the moved curves (the timeline's own rule).
        applyMarquee();
        return true;
    }

    core::TimeLineEvent GraphEditor::marqueeTargets() const {
        // Every key whose DRAWN square's centre falls inside the box, through the
        // same graph::keyRect the hit-test and the drawing layer read, so the key
        // a user sees inside the rectangle is the key that is selected - written
        // as ONE WHOLE-KEY entry per key, which is what the timeline's own box
        // gathers too (time_Marquee.cpp walks its rows' lane maps) and what makes
        // the two views agree key for key.
        const QRect box = marqueeRect();
        core::TimeLineEvent hits;
        if (box.isEmpty()) {
            return hits;
        }
        for (const Entry& entry : buildEntries()) {
            for (const core::TimeKey* key : entry.curve.keys()) {
                const core::Frame frame(key->frame());
                const QRect rect = graph::keyRect(mScale, frame, entry.curve.valueOf(*key));
                if (box.contains(rect.center())) {
                    hits.pushTarget(*entry.node, entry.channel.type(), frame.get());
                }
            }
        }
        return hits;
    }

    void GraphEditor::applyMarquee() {
        // The box is committed on EVERY drag step, so the highlights stay live
        // while the user drags and the rectangle itself stays transient - the
        // timeline's own applyMarquee, with the same three modes:
        //   * REPLACE: the selection IS the box;
        //   * ADD: the snapshot taken at the press plus the box (rebuilt from the
        //     snapshot each step, so a box that shrinks gives a key back);
        //   * SUBTRACT: the box is taken out of the live selection as it grows -
        //     the timeline's subtract is the set's own and is not recomputed from
        //     the snapshot.
        if (!mMarqueeArmed) {
            return;
        }
        const core::TimeLineEvent hits = marqueeTargets();
        if (mMarqueeMode == Marquee_Add) {
            core::TimeLineEvent merged = mPreMarqueeSelection;
            for (const core::TimeLineEvent::Target& target : hits.targets()) {
                merged.pushTarget(*target.node, target.pos);
            }
            mSelection->set(merged);
        } else if (mMarqueeMode == Marquee_Subtract) {
            mSelection->subtract(hits);
        } else {
            mSelection->set(hits);
        }
    }

    void GraphEditor::endMarquee() {
        // The box stops here; it was already applied on the last drag step (the
        // release's own position is NOT part of the gesture - the timeline's
        // release does not move its box either). What the release still decides
        // is the spec's one click rule: a box with no travel at all is the CLICK,
        // and a Ctrl-click on empty plot is DESELECT ALL (the timeline clears its
        // selection there, with a plain click having already cleared at the
        // press and Shift keeping what it found - see kMarqueeSlop).
        const bool degenerate = qAbs(mMarqueeCurrent.x() - mMarqueeOrigin.x()) <= kMarqueeSlop
            && qAbs(mMarqueeCurrent.y() - mMarqueeOrigin.y()) <= kMarqueeSlop;
        if (degenerate && mMarqueeMode == Marquee_Subtract) {
            clearSelection();
        }
        mMarqueeArmed = false;
        mDragging = false;
        mMarqueeOrigin = QPoint();
        mMarqueeCurrent = QPoint();
        mMarqueeMode = Marquee_Replace;
        mPreMarqueeSelection = core::TimeLineEvent();
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
        mDragPartnerMirrors = false;
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
        // Both axes, in the one place the pixel-to-frame sign lives for the frame
        // half (panFramesByPixels) and the panel's own panValue for the value half.
        // The scroll bars' THUMB DRAG does not come through here: it converts
        // through the bar's own ratio (see mScrollBarContentPerPixel), while their
        // PAGE and WHEEL use scrollFramesByBarPixels/scrollValueByBarPixels.
        panFramesByPixels(mScale, delta.x());
        mScale.panValue(delta.y());
    }

    void GraphEditor::endPan() {
        mPanning = false;
        mDragging = false;
    }

    //-------------------------------------------------------------------------------------------------
    void GraphEditor::panFramesByPixels(graph::Scaler& aScale, int aPixelDelta) {
        if (aPixelDelta == 0)
            return;
        // The HORIZONTAL half is the timeline's while it owns the frame axis: the
        // pointer's pixel delta is converted to FRAMES on this panel's ladder and
        // handed over, which is the timeline's own unit - its pixels-per-frame is
        // its own, and the shared quantity is the window (see the header). The sign
        // is the drag's: moving right moves the content right, which shows EARLIER
        // frames, so the frame delta is the negative of the pixel delta. The
        // middle-button pan and the wheel's Shift notch (the timeline's own 50px
        // scroll) are its callers.
        if (mFrameWindowShared) {
            if (mScrollFrames)
                mScrollFrames(-static_cast<double>(aPixelDelta) / aScale.framePpf());
        } else {
            aScale.panFrame(aPixelDelta);
        }
    }

    //-------------------------------------------------------------------------------------------------
    void GraphEditor::scrollFramesByBarPixels(graph::Scaler& aScale, int aPixelDelta) {
        if (aPixelDelta == 0)
            return;
        // The WINDOW moves, not the content (see the declaration): positive pixels
        // advance the viewport, exactly as a QScrollBar's value advances when its
        // slider is dragged right. The timeline's delegate already speaks in frames of
        // viewport travel, so the sign stays positive there; the panel's own axis
        // stores the content origin instead, so it moves the other way.
        if (mFrameWindowShared) {
            if (mScrollFrames)
                mScrollFrames(static_cast<double>(aPixelDelta) / aScale.framePpf());
        } else {
            aScale.panFrame(-aPixelDelta);
        }
    }

    void GraphEditor::scrollValueByBarPixels(graph::Scaler& aScale, int aPixelDelta) {
        if (aPixelDelta == 0)
            return;
        // Positive = HIGHER values = the pill UP its lane (the value axis is inverted
        // along the track), so a drag's `-delta.y()` lands here and a page toward the
        // run's start passes a positive pixel count. panValue's own sign is "positive
        // reveals higher values" (graph_Scaler.cpp), which is exactly this one.
        aScale.panValue(aPixelDelta);
    }

    //-------------------------------------------------------------------------------------------------
    bool GraphEditor::valueExtentOf(const QVector<Entry>& aItems, float& aLo, float& aHi) {
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

        if (any) {
            aLo = lo;
            aHi = hi;
        }
        return any;
    }

    bool GraphEditor::shownValueExtent(float& aLo, float& aHi) const {
        return valueExtentOf(buildEntries(), aLo, aHi);
    }

    //-------------------------------------------------------------------------------------------------
    graph::Scaler::ScrollBars GraphEditor::scrollBarsFor(
        const graph::Scaler& aScale, const QVector<Entry>& aItems
    ) const {
        // The two CONTENTS, and nothing else: the frame range the horizontal bar
        // scrolls and the shown curves' value extent the vertical one does. A
        // degenerate value extent (no shown channel has a key) is passed as an empty
        // range, which the layout reads as "the thumb fills its track" - there is
        // nothing to scroll against.
        float lo = 0.0f;
        float hi = 0.0f;
        if (!valueExtentOf(aItems, lo, hi))
            lo = hi = 0.0f;

        const util::Range frames = frameRange();
        return aScale.scrollBars(frames.min(), frames.max(), lo, hi);
    }

    bool GraphEditor::beginScrollBarPress(const graph::Scaler::ScrollBars& aBars, const QPoint& aPos) {
        if (aBars.isEmpty())
            return false;

        // A bar is GRABBABLE while it has a RUN: a full-length slider (the window
        // covering its content) is a run like any other, and pressing it starts the
        // drag exactly as the timeline's full-width handle does. TimeLineWidget::
        // eventFilter reaches beginScrollbarDrag from handleRect without asking for
        // travel, and its ratio `range / span` = trackSpan / laneLength is positive
        // at range == pageStep. hHasRun is that precondition: only a lane the layout
        // could not give a handle (a degenerate track) is nobody's.
        int axis = ScrollBar_None;
        if (aBars.hHasRun() && aBars.hTrack.contains(aPos))
            axis = ScrollBar_Horizontal;
        else if (aBars.vHasRun() && aBars.vTrack.contains(aPos))
            axis = ScrollBar_Vertical;
        if (axis == ScrollBar_None)
            return false;

        // A press on the RUN (the slider's span of the lane, which is what hThumb/
        // vThumb name) starts the drag. A press on the LANE beside it is ONE PAGE
        // toward the press - a page is the VISIBLE WINDOW, the unit a QScrollBar's
        // PageStep uses (the timeline's own bar sets its page step to the viewport
        // width) - and only then the drag begins, so a page followed by a drag is one
        // continuous gesture with no accumulated offset. The page is applied through
        // the same call the drag below uses, so both routes move one window.
        const bool horizontal = axis == ScrollBar_Horizontal;
        const QRect& thumb = horizontal ? aBars.hThumb : aBars.vThumb;
        const int pos = horizontal ? aPos.x() : aPos.y();
        const int runStart = horizontal ? thumb.left() : thumb.top();
        const int runEnd = horizontal ? thumb.right() : thumb.bottom();
        if (pos < runStart || runEnd < pos) {
            // TOWARD THE PRESS, in the bar's own direction. Before the run means
            // earlier frames on the horizontal bar and HIGHER values on the vertical
            // one (the value axis runs downward: its run's start is the top); after it
            // means later frames / lower values. A page is the VISIBLE WINDOW's own
            // pixel length - the PANEL's width while the shared mapping spans the
            // panel, the plot's width while the graph's own axis fills the plot, and
            // the plot's height for the value window. The timeline's own bar uses its
            // viewport width as the page step, which is the same rule.
            const bool towardStart = pos < runStart;
            const int pagePixels = horizontal
                ? (mFrameWindowShared ? mScale.rect().width() : mScale.plotRect().width())
                : mScale.plotRect().height();
            if (horizontal)
                scrollFramesByBarPixels(mScale, towardStart ? -pagePixels : pagePixels);
            else
                scrollValueByBarPixels(mScale, towardStart ? pagePixels : -pagePixels);
        }

        // The drag is armed AFTER the page, at the press position: every later move
        // is that pointer's delta. The ratio is the TIMELINE's own anchored ratio
        // (beginScrollbarDrag): content units per lane pixel at press, so a pointer
        // delta dx moves the WINDOW by dx * ratio and the handle's offset by exactly
        // dx - a 1:1 drag, not the page-per-track fraction a scroll bar's own value
        // mapping would give.
        mDragging = true;
        mScrollBarAxis = axis;
        mScrollBarPointer = aPos;
        mScrollBarContentPerPixel = horizontal ? aBars.hContentPerPixel : aBars.vContentPerPixel;
        return true;
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
                    // The entry's own line: the very TimeLine handed to
                    // curveFor() below, so the map every read of `line` edits is
                    // the one this curve was built from. A read that means the
                    // node's timeline says entry.node->timeLine() instead.
                    entry.line = timeLine;
                    entry.channel = channel;
                    entry.curve = curveFor(*timeLine, channel);
                    items.append(entry);
                }
            }
        }

        return items;
    }

    //-------------------------------------------------------------------------------------------------
    GraphEditor::Hit
    GraphEditor::hitTest(const QVector<Entry>& aItems, const graph::Scaler& aScale, const QPoint& aPos) const {
        Hit handleHit;
        Hit keyHit;
        const QPointF pos(aPos);

        // ONE DISTANCE RULE FOR BOTH KINDS, AND IT IS graph::pickTarget's. Each pass
        // below only finds the NEAREST candidate of its own class, in pixels; which
        // class the point names - and with it whether a candidate exists at all - is
        // the shared comparison in ctrl/graph/graph_Geometry.h. The hover ink and the
        // hover-repaint scheduling ask the same function about the same model, so a
        // highlight cannot light a handle this press would not take (or the other way
        // round), which is what two independent scans used to do.
        double bestHandle = std::numeric_limits<double>::infinity();
        for (int i = 0; i < aItems.size(); ++i) {
            const graph::Curve& curve = aItems[i].curve;
            const int segments = curve.segmentCount();
            for (int s = 0; s < segments; ++s) {
                const graph::Curve::Segment segment = curve.segment(s);

                // A Custom segment exposes TWO real, grabbable dots whatever
                // their modes: a stored side (Free/Aligned) is where its control
                // point is, a derived side (Vector/Auto/AutoClamped) is where the
                // derivation puts it. Every other easing exposes no handle at all.
                //
                // THE DERIVATION IS CULLED FIRST, AND THE CULL IS EXACT. This
                // loop runs on the CURSOR path (ownsCursor hits on every move, over
                // every visible segment), and a derivation is a map lookup plus a
                // neighbour walk - so paying it for a handle the pointer cannot
                // reach scales the cursor path with the KEY COUNT instead of with
                // the pointer. A DERIVED handle's x comes from the segment alone
                // (the thirds of its frame span: the automatic handle's whole
                // definition), so its pixel can be computed BEFORE the derivation;
                // a stored side has no such fixed x, so it is never culled. An x
                // gap bigger than the radius cannot be made up by the y.
                const util::Easing::Param& segEasing = segment.easing;
                const bool derivedOut = util::Easing::isDerived(segEasing.handleOut);
                const bool derivedIn = util::Easing::isDerived(segment.inMode);
                if (derivedOut || derivedIn) {
                    const double frameSpan = static_cast<double>(segment.to->frame() - segment.from->frame());
                    const double fromFrame = static_cast<double>(segment.from->frame());
                    const double firstX = aScale.pixelXOfFrame(fromFrame + (1.0 / 3.0) * frameSpan);
                    const double secondX = aScale.pixelXOfFrame(fromFrame + (2.0 / 3.0) * frameSpan);
                    const bool firstReachable =
                        !derivedOut || std::fabs(firstX - pos.x()) <= graph::kHandleHitRadius;
                    const bool secondReachable =
                        !derivedIn || std::fabs(secondX - pos.x()) <= graph::kHandleHitRadius;
                    if (!firstReachable && !secondReachable)
                        continue;
                }

                QPointF pair[2];
                if (!curve.effectiveHandles(segment, pair))
                    continue; // an analytic easing (or a null segment): no handles

                for (int h = 0; h < 2; ++h) {
                    const QPointF normalized = pair[h];
                    const double distance = pixelDistance(graph::handlePointAt(aScale, segment, normalized), pos);
                    if (distance <= bestHandle) { // ties keep the last candidate, as before
                        bestHandle = distance;
                        handleHit.hitCurve = true;
                        handleHit.hitHandle = true;
                        handleHit.item = i;
                        handleHit.segment = s;
                        handleHit.handle = h;
                    }
                }
            }
        }

        // Then the keys, by the same nearest-per-class rule. The rect comes from
        // graph::keyRect, so a click lands on the key as it is drawn.
        double bestKey = std::numeric_limits<double>::infinity();
        for (int i = 0; i < aItems.size(); ++i) {
            const graph::Curve& curve = aItems[i].curve;
            for (const core::TimeKey* key : curve.keys()) {
                const core::Frame frame(key->frame());
                const QRect rect = graph::keyRect(aScale, frame, curve.valueOf(*key));
                const double distance = pixelDistance(QRectF(rect).center(), pos);
                if (distance <= bestKey) {
                    bestKey = distance;
                    keyHit.hitCurve = true;
                    keyHit.hitHandle = false;
                    keyHit.item = i;
                    keyHit.segment = -1;
                    keyHit.handle = -1;
                    keyHit.keyFrame = frame;
                }
            }
        }

        // WHICH CLASS THE POINT NAMES is graph::pickTarget's answer - the same call
        // the hover ink and the hover repaint make. The exact tie belongs to the KEY
        // (see the function): a press on a key's centre therefore always selects the
        // key, however close a handle's dot sits.
        switch (graph::pickTarget(bestKey, bestHandle)) {
        case graph::PickKind::Key:
            return keyHit;
        case graph::PickKind::Handle:
            return handleHit;
        case graph::PickKind::None:
            break;
        }
        return Hit();
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
        // The entry's own line (the map the insert reads and writes); the event
        // it pushes names the node, which is why pushSeededKey takes both.
        core::TimeLine* line = entry.line;

        const core::Frame frame = scale.frameAt(aPos.x());

        // An occupied frame is REFUSED, not replaced: the key already there is the
        // user's, and an insert is not an overwrite (see the header). In practice
        // the key radius catches this press first - a key sits on its own curve,
        // inside the band - so this is the backstop that keeps the rule true even
        // when the pointer is on ANOTHER channel's band.
        if (line->hasTimeKey(entry.channel.type(), frame.get()))
            return false;

        if (!pushSeededKey(*line, *entry.node, entry.channel, frame))
            return false;

        // The inserted key is the panel's current key now: the press landed on its
        // curve, so it also focuses that channel, exactly as a press on a key
        // does. The selection is the MARKER the insert wrote - the pressed curve's
        // component - which is this editor's ONE marker-level write (a press on a
        // key writes the whole key, as the timeline's own press does).
        // Neither the focus nor the selection moves a WINDOW (see DELIBERATE FIT in
        // the header), so an insert press cannot move the band under its own
        // release either.
        selectMarker(*entry.node, entry.channel, frame);
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
        case core::TimeKeyType_Wave: {
            auto* key = static_cast<core::WaveKey*>(seed.get());
            // The five drawn channels are seeded from the curves; the shape
            // fields (speed, crest, the dual wave, the deform mode) keep the
            // template's, exactly like HSV's absolute flag.
            key->setStrength(sampledChannelValue(aLine, type, 0, aFrame));
            key->setWavelength(sampledChannelValue(aLine, type, 1, aFrame));
            key->setAngleDeg(sampledChannelValue(aLine, type, 2, aFrame));
            key->setPhase(sampledChannelValue(aLine, type, 3, aFrame));
            key->setMix(sampledChannelValue(aLine, type, 4, aFrame));
            TimeLineUtil::pushNewWaveKey(*mProject, aTarget, frame, static_cast<core::WaveKey*>(seed.release()));
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

        // Playback reads the segment's easing from its `from` key for the whole
        // span, so BOTH control points are stored in that one param - but they
        // belong to different keys: handle 0 is the `from` key's OUTGOING
        // tangent and handle 1 the `to` key's INCOMING one. updateDrag keeps the
        // OTHER SIDE of the key the grabbed dot belongs to (it lives in the
        // neighbouring segment): a DERIVED partner is mirrored about the key and
        // freed (the pair was symmetric by construction), an ALIGNED partner
        // stays collinear at its own length, a FREE partner is untouched, and
        // Alt leaves the partner alone. The anchor recorded here is the
        // SEGMENT's `from` key, not the key the grabbed dot belongs to.
        //
        // WHETHER THIS GESTURE MIRRORS is decided HERE, from the partner's
        // stored mode, and kept for the whole gesture (mDragPartnerMirrors): the
        // first move frees the partner, so a per-step read would stop mirroring
        // it on the second move. Alt at the press arms no mirror at all, and an
        // Alt step later clears the flag (see updateDrag).
        mDragPartnerMirrors = false;
        const bool grabbedSecond = (aHit.handle == 1);
        const int partnerSegment = grabbedSecond ? aHit.segment + 1 : aHit.segment - 1;
        if (!mModifiers.testFlag(Qt::AltModifier)
            && 0 <= partnerSegment && partnerSegment < entry.curve.segmentCount()) {
            // The partner's mode is stored on the key that OWNS this handle: the
            // segment's `from` key for handle 0, its `to` key for handle 1. The
            // owner key lives in the entry's own line, the map its curve draws.
            core::TimeLine* line = entry.line;
            core::TimeKey* ownerKey =
                line->timeKey(entry.channel.type(), (grabbedSecond ? segment.to : segment.from)->frame());
            util::Easing::Param* ownerParam =
                ownerKey
                ? graph::Curve::easingOf(*ownerKey, entry.channel.type(), entry.channel.component())
                : nullptr;
            if (ownerParam) {
                mDragPartnerMirrors = util::Easing::isDerived(
                    grabbedSecond ? ownerParam->handleOut : ownerParam->handleIn
                );
            }
        }

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
    // Is a key move in flight: see the header for why "in flight" is the gesture's
    // own command rather than the armed flag.
    bool GraphEditor::isMovingKeys() const { return mKeyMoveGesture.continues(); }

    //-------------------------------------------------------------------------------------------------
    void GraphEditor::beginKeyMove(const Hit& aHit, const QVector<Entry>& aItems, const QPoint& aPressPos) {
        if (!mProject)
            return;
        if (aHit.item < 0 || aHit.item >= aItems.size())
            return;

        const Entry& entry = aItems[aHit.item];
        // The key lives in the entry's own line; the node is what the events name.
        core::TimeLine* line = entry.line;
        const core::TimeKey* key = line->timeKey(entry.channel.type(), aHit.keyFrame.get());
        if (!key)
            return; // the key the hit-test named is gone already: nothing to hold

        // The key the press hit, in the timeline's own currency - the line that
        // owns the map, the node the event will name, the key type and the frame
        // the key is at. Held as those four things rather than as aHit.item, so
        // the move follows the key it grabbed and not the position of an entry
        // list that a re-selection could rebuild differently. The COMPONENT is not
        // among them: it belongs to the marker the press selected, and the members
        // below carry it (a marker is a (key, component) pair).
        mDragging = true;   // the panel keeps the cursor while the key is held
        mKeyMoveArmed = true;
        mMarqueeArmed = false;
        mPanning = false;
        mDragItem = aHit.item;
        mDragKey = nullptr; // no handle: this drag moves the key, not its easing
        mDragSegment = -1;
        mDragHandle = -1;
        mDragPartnerMirrors = false;
        mMoveLine = line;
        mMoveNode = entry.node;
        mMoveType = entry.channel.type();
        mMoveFrame = aHit.keyFrame;
        // The marker's component: the channel the press landed on, and the ONE channel
        // a value drag writes when nothing names it as a marker (see the members below).
        mMoveComponent = entry.channel.component();
        // Where the pointer was, in PIXELS: both axes follow the pointer's own
        // delta from here, so a press up to the key radius off the key's centre
        // does not make the key jump by that offset on the first move. The value
        // is read through the same live scale, so the two conversions always
        // agree about the row the pointer is on.
        mMovePointerFrame = mScale.frameAt(aPressPos.x());
        // The SNAP's origin, kept beside it: the pointer's frame at the press and
        // the pressed key's frame at the press. updateKeyMove measures the snapped
        // frame half from these two, so the axis the snap leaves returns to where
        // the gesture started instead of keeping the part of the drag that moved it.
        mMovePressPointerFrame = mMovePointerFrame;
        mMovePressKeyFrame = mMoveFrame;
        mMovePressPos = aPressPos;

        // THE SELECTION the move carries, in the two currencies the two halves of
        // the gesture need. The panel's selection names MARKERS - a (key, component)
        // pair - which the press that armed this move has just written (a plain
        // press replaced it with the pressed marker, Shift added one, Ctrl toggled
        // one), so each member keeps the target it will be moved by, the key its
        // value half writes, its own component and the base value that delta is
        // added to: the whole gesture is one absolute function of the pointer.
        //
        //   * the VALUE half takes one member per marker. An entry that names one
        //     component is that marker; an entry that names the WHOLE key (the
        //     timeline's own selection) is every component the key owns, so it
        //     expands to one member per graphable component of its type - the
        //     components the panel can write at all (a type with none, a bone key,
        //     has no marker and no member);
        //   * the FRAME half takes one entry per KEY, deduplicated: two markers of
        //     one key share the one frame, and would move it twice.
        //
        // Both are resolved against the DOCUMENT: a selection can name a key that is
        // no longer in its map (a stale entry), and such a member would make every
        // step refuse. The stored selection pointer is never dereferenced - the key
        // comes from (line, type, frame), which is what the selection's own contract
        // requires.
        mMoveKeys.clear();
        mMoveMembers.clear();
        for (const time::Selection::Item& item : mSelection->items()) {
            // A NON-const copy: pos.line()'s non-const overload is how the map
            // owner comes back writable, and the selection hands out const items.
            core::TimeLineEvent::Target target = item.target;
            core::TimeLine* memberLine = target.pos.line();
            if (!memberLine)
                continue;
            core::TimeKey* memberKey = memberLine->timeKey(target.pos.type(), target.pos.index());
            if (!memberKey)
                continue;

            bool keyHeld = false;
            for (const MoveKey& held : mMoveKeys) {
                if (held.key == memberKey) {
                    keyHeld = true;
                    break;
                }
            }
            if (!keyHeld) {
                MoveKey held;
                held.target = target;
                held.key = memberKey;
                mMoveKeys.append(held);
            }

            // WHICH CHANNEL THIS ENTRY'S VALUE HALF WRITES. A MARKER entry names its
            // own component - the channel the user selected. A WHOLE-KEY entry names
            // the key, i.e. the TIME axis (one frame, shared by every component), and
            // carries no channel of its own: its value half is the component the PRESS
            // GRABBED, so a marquee over several keys still moves the channel under the
            // pointer on every one of them, while no sibling channel is touched.
            //
            // The components of one key are INDEPENDENT CHANNELS. MEASURED 2026-10-24,
            // the user's report: a press used to write the whole key, so dragging Move
            // X's marker moved Move Y's value by the same delta - "they should only be
            // linked on the time axis, not the value axis".
            const int components = graph::Channel::componentCount(target.pos.type());
            const int component = item.component < 0 ? mMoveComponent : item.component;
            if (component < 0 || component >= components)
                continue;
            MoveMember member;
            member.target = target;
            member.key = memberKey;
            member.component = component;
            member.baseValue = graph::Channel(target.pos.type(), component).value(*memberKey);
            mMoveMembers.append(member);
        }

        // The pressed marker is always one of these members, and that is a property of
        // the PRESS rather than of this walk: the press either WROTE the marker it
        // landed on (setMarker) or found it covered and grabbed the group - and a
        // whole-key entry's value half above is that same component. So the grabbed
        // channel can always be moved, including while its key is selected whole.
    }

    //-------------------------------------------------------------------------------------------------
    bool GraphEditor::updateKeyMove(const QPoint& aPos) {
        if (!mProject || !mDragging || !mKeyMoveArmed)
            return false;

        // The line the press armed: held as the TimeLine itself, so this step
        // reads the map the pressed entry's curve was built from. The node is
        // separate because the events name it (and it is what owns the line).
        core::TimeLine* line = mMoveLine;
        if (!line || !mMoveNode) {
            endDrag();
            return false;
        }

        // A Ctrl-press that TOGGLED the pressed key out of the selection leaves
        // the move armed with nothing to carry (the selection is the gesture's
        // member list): a drag then moves nothing, and it must not push a command
        // that does. The FRAME half is the test - it holds every selected key, a
        // key whose type has no graphable component (no value half) included, so a
        // frame-only selection still moves.
        if (mMoveKeys.isEmpty())
            return false;

        // The anchor check, the handle drag's own honesty: if the PRESSED key is
        // no longer in the document (an undo removed it, or another view did), the
        // move has nothing to measure against and the member that names it would
        // refuse every step - so close the edit and say why.
        core::TimeKey* key = line->timeKey(mMoveType, mMoveFrame.get());
        if (!key) {
            mLogger.pushLog(
                QObject::tr("The animation graph stopped moving: the dragged key changed."), UILogType_Info
            );
            endDrag();
            return false;
        }

        // The SHIFT axis snap: the pointer's own pixel displacement from the
        // press decides which axis is dominant, and the other one is pinned TO ITS
        // PRESS VALUE (not merely left wherever an earlier step of the gesture put
        // it - see the step's comment below). The displacement is a gesture-wide
        // quantity (not a per-step one), so the chosen axis does not flicker while
        // the pointer wiggles: a drag that is overwhelmingly horizontal does not
        // smear a stray pixel of vertical movement into the value.
        const int dx = aPos.x() - mMovePressPos.x();
        const int dy = aPos.y() - mMovePressPos.y();
        const bool snap = mModifiers.testFlag(Qt::ShiftModifier);
        const bool frameAllowed = !snap || (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy);
        const bool valueAllowed = !snap || (dy < 0 ? -dy : dy) > (dx < 0 ? -dx : dx);

        // The FRAME delta.
        //
        // WITHOUT the snap it is the step since the last ACCEPTED pointer frame: a
        // refused step leaves mMovePointerFrame where it was, so the next accepted
        // step is measured from the last accepted place. It is ONE delta for the
        // whole selection.
        //
        // UNDER the snap it is an ABSOLUTE offset from the PRESS, not an accumulated
        // one, and that is the only way the two halves can agree. The value half
        // below is already absolute (the pointer's row minus the press's row), so an
        // axis that the pointer stops dominating returns to its press value; a frame
        // half that accumulated its steps instead kept the frame the earlier
        // horizontal part of the drag gave it, and the key then switched to the value
        // axis FROM THERE - measured as the report "drag left, then a bit down, and
        // the X stays at the latest X position instead of the starting one". So while
        // snapping, the active axis carries the pointer's whole displacement from the
        // press and the inactive one is its press value. mMoveFrame tracks where the
        // key is NOW (a clamp or a refusal moves it), so the step is the distance
        // from there to that absolute target - and a refused step needs no special
        // case, because the next one recomputes the same target.
        const core::Frame pointerFrame = mScale.frameAt(aPos.x());
        int step = 0;
        if (snap) {
            const core::Frame target =
                frameAllowed ? core::Frame(mMovePressKeyFrame.get() + pointerFrame.get() - mMovePressPointerFrame.get())
                             : mMovePressKeyFrame;
            step = target.get() - mMoveFrame.get();
        } else {
            step = pointerFrame.get() - mMovePointerFrame.get();
        }

        // The VALUE: the pointer's delta from the row it went down on, added to
        // each member's own value at the press. Both rows go through the SAME live
        // scale, so this is an absolute function of the pointer and cannot
        // accumulate a key's own clamp.
        const float valueDelta = valueAllowed ? mScale.valueAt(aPos.y()) - mScale.valueAt(mMovePressPos.y()) : 0.0f;

        // Does the step move ANY member's value? Every member is a marker, so every
        // member names the component its value half writes - the pressed one and the
        // ones Shift added alike, each in its OWN channel.
        bool valueMoves = false;
        for (const MoveMember& member : mMoveMembers) {
            if (member.baseValue + valueDelta
                != graph::Channel(member.target.pos.type(), member.component).value(*member.key)) {
                valueMoves = true;
                break;
            }
        }

        if (!valueMoves && step == 0)
            return false; // the pointer has not moved the selection: nothing to rewrite, nothing to open

        // The frame range is playback's business, not the keying system's: keys
        // may live anywhere, so the move is asked with the widest range there is
        // (the timeline's own move asks the same way).
        const util::Range frameRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());

        if (mKeyMoveGesture.continues()) {
            // The ONE command this gesture already pushed: REWRITE it, which is
            // what makes the whole drag one undo step. modifyMove refuses the
            // whole step when ANY member's destination is occupied, and a refusal
            // leaves the command exactly as it was (no move, no notification, and
            // the value halves are not even asked - the frame is the half that can
            // refuse).
            int clampedStep = step;
            core::TimeLineEvent modEvent;
            bool frameMoved = false;
            if (step != 0) {
                if (!mKeyMoveGesture.command()->modifyMove(modEvent, step, frameRange, &clampedStep)
                    || clampedStep == 0)
                    return false; // an occupied destination: the WHOLE step is refused
                frameMoved = true;
            }

            if (valueMoves) {
                mKeyMoveGesture.command()->modifyValues(valueDelta);
            }

            // ONE notification per step, as the timeline emits one per gesture
            // step: the frame event when the frame moved (it names where each key
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
        // The command is built and VALIDATED before the gesture is opened: a
        // not-yet-pushed MoveFrameOfKey only records where it would move to (its
        // own mCurrent == 0 path), so a REFUSED step can be dropped right here -
        // no command on the stack, no notification, no empty history entry. The
        // frame half always carries every selected KEY (so a later step that moves
        // the axis this one did not can still rewrite it), and the value halves are
        // added even when this step writes none, for the same reason.
        std::unique_ptr<KeyMoveCommand> command(new KeyMoveCommand());

        // The frame half's targets: one per KEY, at the frame it held at the press,
        // deduplicated when the move was armed (two markers of one key share one
        // frame). The notifier carries this same event, so what reaches the
        // application names where each key came from, and the gesture's macro
        // close is what fires it for this first commit (the later rewrites notify
        // by hand, above).
        core::TimeLineEvent source;
        source.setType(step != 0 ? core::TimeLineEvent::Type_MoveKey : core::TimeLineEvent::Type_ChangeKeyValue);
        for (const MoveKey& held : mMoveKeys) {
            source.pushTarget(*held.target.node, held.target.pos);
        }
        command->buildFrames(source);

        int clampedStep = step;
        if (step != 0) {
            core::TimeLineEvent modEvent;
            if (!command->modifyMove(modEvent, step, frameRange, &clampedStep) || clampedStep == 0)
                return false;
        }

        // The value halves: one per MARKER, each on its OWN component - so the key a
        // press landed on writes that component, a Shift-added marker of another
        // component writes that one, and neither touches the other.
        for (const MoveMember& member : mMoveMembers) {
            command->addValue(*member.key, member.target.pos.type(), member.component, member.baseValue);
        }

        // The proportional half: every proportional key of each (line, type) the
        // gesture edits follows the bounds the value halves are about to move.
        // Captured HERE, before modifyValues() below writes the first value, so
        // the base values and weights are the pre-gesture document - the whole
        // reason the capture is a build-time step and not an exec-time one.
        QVector<KeyMoveCommand::ValueReflowTarget> reflowTargets;
        reflowTargets.reserve(mMoveMembers.size());
        for (const MoveMember& member : mMoveMembers) {
            // A NON-const copy: the reflow writes the line's map, and the
            // selection hands out const items (the same copy beginKeyMove makes).
            core::TimeLineEvent::Target target = member.target;
            reflowTargets.append(KeyMoveCommand::ValueReflowTarget {
                target.pos.line(), target.pos.type(), member.key
            });
        }
        command->buildValueReflows(reflowTargets);

        // THE TANGENT HALF, captured with the reflows and for the same reason: it must
        // read the params and the values the gesture FOUND, so it runs before the first
        // value write. One target per member, because a tangent normalizes against a
        // component of a map.
        QVector<KeyMoveCommand::TangentTarget> tangentTargets;
        tangentTargets.reserve(mMoveMembers.size());
        for (const MoveMember& member : mMoveMembers) {
            // A NON-const copy: pos.line()'s non-const overload is how the map owner
            // comes back writable (the same copy the reflow loop makes).
            core::TimeLineEvent::Target memberTarget = member.target;
            KeyMoveCommand::TangentTarget target;
            target.line = memberTarget.pos.line();
            target.type = member.target.pos.type();
            target.key = member.key;
            target.component = member.component;
            tangentTargets.append(target);
        }
        command->buildTangents(tangentTargets);

        command->modifyValues(valueDelta);

        // ONE command in ONE gesture inside ONE macro: the whole selection's move
        // is a single stack entry, and continues() above rewrites exactly it.
        mKeyMoveGesture.begin(CmndName::tr("move a key"), [&](cmnd::ScopedMacro& aMacro) {
            auto* notifier = new TimeLineUtil::Notifier(*mProject);
            notifier->event() = source;
            aMacro.grabListener(notifier);
            return command.release();
        });

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

        // The key the drag edits lives in the entry's own line, the map its curve
        // draws.
        core::TimeLine* line = entry.line;
        core::TimeKey* key = line->timeKey(entry.channel.type(), mDragKey->frame());
        util::Easing::Param* easing =
            key ? graph::Curve::easingOf(*key, entry.channel.type(), entry.channel.component()) : nullptr;
        if (!easing)
            return; // the type carries no easing: nothing this graph could be editing

        // Built from the param the document holds NOW, never from a press-time
        // copy, so the fields this drag does not touch - the easing type above
        // all - cannot be reverted by a stale snapshot.
        util::Easing::Param next = *easing;

        // The dot the user grabbed is the segment's EFFECTIVE control point for
        // that side: stored for a Free/Aligned side, derived for a Vector/Auto/
        // AutoClamped one. It is the drag's starting point and the fallback y for
        // a constant segment; the stored value written below then becomes the
        // side's truth (its mode is set Free when it was derived - see below), so
        // the curve does not move when the gesture begins.
        QPointF effective[2];
        if (!entry.curve.effectiveHandles(segment, effective))
            return; // an analytic easing exposes no handle: the hit-test cannot name one

        const QPointF normalized =
            handleNormalized(mScale, segment, QPointF(aPos), static_cast<double>(effective[mDragHandle].y()));

        // THE GRABBED HANDLE goes where the pointer is. setHandle clamps x into
        // [0,1] (a cubic is only solvable in its own domain, see its note) and
        // leaves y free, so an overshoot - what Back/Elastic and a deliberate
        // anticipation are made of - survives.
        graph::Curve::setHandle(next, mDragHandle, normalized);

        // ---- THE PAIR OF THE KEY THE HANDLE BELONGS TO ----
        //
        // A key owns ONE param but plays TWO handles: its OUTGOING handle is
        // this segment's (x1,y1) when the key is the segment's `from`, and its
        // INCOMING handle is this segment's (x2,y2) when the key is its `to`.
        // The opposite side of the SAME key is therefore stored in the
        // NEIGHBOURING segment:
        //
        //   handle 0 (outgoing, belongs to `from`): the key's incoming handle is
        //     segment i-1's (x2,y2), stored in THAT segment's `from` param;
        //   handle 1 (incoming, belongs to `to`): the key's outgoing handle is
        //     segment i+1's (x1,y1), stored in THAT segment's `from` param.
        //
        // WHAT THE DRAG DOES TO THE OTHER SIDE follows the PARTNER's stored
        // mode, read from the owner's param when the press armed the drag:
        //
        //   * DERIVED (Auto/AutoClamped/Vector): the pair was symmetric by
        //     construction - one slope for both sides - so the partner MIRRORS
        //     the dragged handle about the key, in angle AND length, and becomes
        //     Free. Freeing it is part of the same step: a derived side would
        //     ignore the stored position, and the pair would stop being
        //     symmetric the moment one side stopped deriving.
        //   * ALIGNED, no Alt: the partner keeps its OWN LENGTH and stays
        //     collinear with the dragged one - the angle is linked, the length
        //     is independent. It is deliberately NOT a reflection: a reflection
        //     forces the two lengths equal, which is not what a smooth node
        //     means. The partner's own mode is untouched.
        //   * FREE, or Alt held: the partner is NOT touched at all. Alt clears
        //     the gesture's mirror flag, so a step that sees it freezes the
        //     partner for the REST of the gesture too (releasing Alt does not
        //     re-link it).
        //
        // THE DRAGGED SIDE ITSELF: a derived side cannot express a stored drag,
        // so it becomes Free (the stored value just written is the new truth). A
        // Free side stays Free and an Aligned side stays Aligned unless Alt is
        // held. ALT therefore sets THIS SIDE Free for good - read from
        // mModifiers on EVERY step (the widget feeds the latest state in), so
        // pressing it mid-drag stops the partner at once and releasing it does
        // not re-link. Alt+CLICK on a key is the way back (see the press branch).
        //
        // The FIRST key has no previous segment and the LAST key has no next
        // one: their single handle has no opposite, so the drag writes the
        // grabbed handle and its own mode and leaves everything else alone.
        //
        // THE COLLINEARITY IS IN PIXELS, NOT IN NORMALIZED SPACE. Each segment
        // normalizes y by its OWN value span, so a normalized construction
        // would not be collinear with the key in the picture whenever the two
        // spans differ - the tangent would visibly bend at the key. What has to
        // be collinear is exactly what the user sees, so the arithmetic runs
        // through the SAME mappings the drawing uses:
        //
        //   keyPixel     = mScale.pixelX(core::Frame(key frame)), mScale.pixelY(key value)
        //                  (the marker's centre; the model's keyDot/keyCenter pair)
        //   draggedPixel = handlePointAt(dragged segment, the handle as PLACED)
        //   otherPixel   = keyPixel - unit(draggedPixel - keyPixel) * otherLength
        //
        // and otherPixel is then mapped back through the NEIGHBOUR segment's
        // inverse mapping (handleNormalized): x as a fraction of that segment's
        // frame span, y of its value span. setHandle clamps that x into [0,1];
        // the y stays free, as on the grabbed side.
        const bool draggedIsSecond = (mDragHandle == 1);
        const float placedX = draggedIsSecond ? next.cubicBezier.x2 : next.cubicBezier.x1;
        const float placedY = draggedIsSecond ? next.cubicBezier.y2 : next.cubicBezier.y1;
        const QPointF placed(static_cast<double>(placedX), static_cast<double>(placedY));

        // TimeKey::frame() is an int: a TimeKey is addressed by its integer frame,
        // not by core::Frame (which the Scaler converts from explicitly).
        const int ownerFrame = draggedIsSecond ? segment.to->frame() : segment.from->frame();
        const float ownerValue = draggedIsSecond ? segment.toValue : segment.fromValue;
        // The marker's own pixel, through the one helper the model publishes key dots
        // with, so the pair is built about the pixel the marker is actually drawn at.
        const QPointF keyPixel = graph::keyDot(mScale, core::Frame(ownerFrame), ownerValue);

        // The KEY this handle belongs to, whose param carries its two modes: the
        // segment's `from` for handle 0 (the very param this drag writes), its
        // `to` for handle 1.
        util::Easing::Param* ownerEasing = nullptr;
        if (draggedIsSecond) {
            core::TimeKey* ownerKey = line->timeKey(entry.channel.type(), segment.to->frame());
            ownerEasing =
                ownerKey ? graph::Curve::easingOf(*ownerKey, entry.channel.type(), entry.channel.component()) : nullptr;
        } else {
            ownerEasing = easing;
        }

        const int neighbourIndex = draggedIsSecond ? mDragSegment + 1 : mDragSegment - 1;
        const int neighbourHandle = draggedIsSecond ? 0 : 1;
        const bool hasPartnerSegment = 0 <= neighbourIndex && neighbourIndex < entry.curve.segmentCount();

        // The mode this side HAD when the step began. Read LIVE from the document
        // (Alt may have freed it on an earlier step, so no press-time snapshot).
        const util::Easing::HandleMode liveDraggedMode =
            draggedIsSecond ? (ownerEasing ? ownerEasing->handleIn : util::Easing::HandleMode_Free)
                            : easing->handleOut;
        const util::Easing::HandleMode livePartnerMode =
            draggedIsSecond ? (ownerEasing ? ownerEasing->handleOut : util::Easing::HandleMode_Free)
                            : easing->handleIn;
        const bool altHeld = mModifiers.testFlag(Qt::AltModifier);
        // Alt freezes the partner for the REST of the gesture: drop the press-time
        // mirror decision too, so a later step without Alt cannot re-link it.
        if (altHeld)
            mDragPartnerMirrors = false;
        // The DERIVED-pair mirror is the gesture's press-time decision: the first
        // step frees the partner, so reading its live mode from then on would
        // stop the mirroring after one move.
        const bool mirrorDerived = !altHeld && mDragPartnerMirrors && hasPartnerSegment;
        // An ALIGNED partner is carried collinearly at its own length - today's
        // behaviour - and only while the dragged side is Aligned (a Free or
        // derived dragged side has no line to keep it on).
        const bool carried = !altHeld && !mirrorDerived
            && liveDraggedMode == util::Easing::HandleMode_Aligned
            && livePartnerMode == util::Easing::HandleMode_Aligned && hasPartnerSegment;

        // The command's mirror half: for handle 1 it is the TO key's param (the
        // one that carries the incoming mode) - the next segment's from param
        // when one exists, the TO key's own param at the end of the channel; for
        // handle 0 it is the PREVIOUS segment's param, which stores the partner
        // control point.
        util::Easing::Param* mirrorEasing = nullptr;
        util::Easing::Param mirrorNext;
        int mirrorFrame = 0;
        bool mirrorWritten = false; // this step actually wrote the mirror half

        if (draggedIsSecond) {
            mirrorEasing = ownerEasing;
            mirrorFrame = segment.to->frame();
        } else if (hasPartnerSegment) {
            const graph::Curve::Segment neighbour = entry.curve.segment(neighbourIndex);
            core::TimeKey* neighbourKey = line->timeKey(entry.channel.type(), neighbour.from->frame());
            mirrorEasing =
                neighbourKey
                ? graph::Curve::easingOf(*neighbourKey, entry.channel.type(), entry.channel.component())
                : nullptr;
            mirrorFrame = neighbour.from->frame();
        }
        if (mirrorEasing) {
            mirrorNext = *mirrorEasing;
        }

        if (carried && mirrorEasing) {
            const graph::Curve::Segment neighbour = entry.curve.segment(neighbourIndex);
            QPointF neighbourPair[2];
            if (effectiveHandles(entry.curve, neighbour, neighbourPair)) {
                const QPointF otherBefore =
                    graph::handlePointAt(mScale, neighbour, neighbourPair[neighbourHandle]);

                const QPointF draggedPixel = graph::handlePointAt(mScale, segment, placed);
                const QPointF draggedVector(
                    draggedPixel.x() - keyPixel.x(), draggedPixel.y() - keyPixel.y()
                );
                const double draggedLength = std::hypot(draggedVector.x(), draggedVector.y());
                const QPointF otherVector(
                    otherBefore.x() - keyPixel.x(), otherBefore.y() - keyPixel.y()
                );
                const double otherLength = std::hypot(otherVector.x(), otherVector.y());

                if (draggedLength > 0.0) {
                    // The opposite keeps its own length and takes the opposite
                    // direction of the PLACED (clamped) dragged handle - the dot
                    // the user sees.
                    const QPointF otherPixel(
                        keyPixel.x() - draggedVector.x() / draggedLength * otherLength,
                        keyPixel.y() - draggedVector.y() / draggedLength * otherLength
                    );
                    mirrorWritten = alignedHandleParam(
                        mScale, neighbour, *mirrorEasing, neighbourPair, neighbourHandle,
                        otherPixel, mirrorNext
                    );
                }
            }
        } else if (mirrorDerived && mirrorEasing) {
            const graph::Curve::Segment neighbour = entry.curve.segment(neighbourIndex);
            QPointF neighbourPair[2];
            if (effectiveHandles(entry.curve, neighbour, neighbourPair)) {
                const QPointF draggedPixel = graph::handlePointAt(mScale, segment, placed);
                const QPointF draggedVector(
                    draggedPixel.x() - keyPixel.x(), draggedPixel.y() - keyPixel.y()
                );
                // The DERIVED pair was symmetric by construction (one slope for
                // both sides), so the partner is the REFLECTION about the key -
                // angle AND length of the placed dragged handle. That is what
                // makes the visible pair symmetric until the user breaks it.
                const QPointF otherPixel(
                    keyPixel.x() - draggedVector.x(), keyPixel.y() - draggedVector.y()
                );
                mirrorWritten = alignedHandleParam(
                    mScale, neighbour, *mirrorEasing, neighbourPair, neighbourHandle,
                    otherPixel, mirrorNext
                );
                // The partner now has a stored position, so it must READ it:
                // Free. The mode lives on the key that owns the handle - the
                // main param (this segment's `from`) for handle 0, the TO key's
                // own param (the mirror half) for handle 1.
                if (draggedIsSecond) {
                    mirrorNext.handleOut = util::Easing::HandleMode_Free;
                } else {
                    next.handleIn = util::Easing::HandleMode_Free;
                }
            }
        }

        // THE DRAGGED SIDE'S MODE. A derived side becomes Free (the drag wrote a
        // stored value, and only Free reads it); Alt frees ANY side for good.
        // An Aligned side stays Aligned when the partner was carried, and so does
        // a Free one. The write lands in the half that owns the mode: the main
        // param for handle 0, the TO key's (mirror) param for handle 1.
        if (altHeld || util::Easing::isDerived(liveDraggedMode)) {
            if (draggedIsSecond) {
                if (mirrorEasing) {
                    mirrorNext.handleIn = util::Easing::HandleMode_Free;
                    mirrorWritten = true;
                }
            } else {
                next.handleOut = util::Easing::HandleMode_Free;
            }
        }

        if (!mWrite.continues()) {
            // The drag's first commit (or its first after an undo/redo dropped
            // the stack's modifiable link): ONE macro pushes the notifier and the
            // command, so the first move is one notification and one command. The
            // targets are established HERE for the whole gesture: the mirror key
            // is named whenever the command owns it (mirrorEasing), so a later
            // step that writes it is announced even if Alt kept this step from
            // writing it.
            core::TimeLineEvent targets;
            targets.setType(core::TimeLineEvent::Type_ChangeKeyValue);
            targets.pushTarget(*entry.node, entry.channel.type(), mDragKey->frame());
            if (mirrorEasing) {
                targets.pushTarget(*entry.node, entry.channel.type(), mirrorFrame);
            }

            mWrite.begin(*mProject, mProject->commandStack(), CmndName::tr("change a key easing"), targets, [&] {
                auto* command =
                    new HandleDragCommand(easing, next, mirrorEasing, mirrorEasing ? &mirrorNext : nullptr);
                mProject->commandStack().push(command);
                return static_cast<cmnd::Base*>(command);
            });
        } else {
            // Rewrite the command this drag already pushed, so the whole drag stays
            // ONE undo step - both halves of it - and announce the keys it changed
            // once. RewriteAnnounce owns both halves; the opposite half is updated
            // only on the steps that actually rewrote it (Alt leaves it where the
            // last such step put it).
            mWrite.step([&] {
                mWrite.commandAs<HandleDragCommand>()->modifyValues(next, mirrorWritten ? &mirrorNext : nullptr);
                return true;
            });
        }
    }

    void GraphEditor::endDrag() {
        // Every command is already on the stack with its last value, so closing
        // the edit is all a release has to do: end() makes the next drag open a
        // fresh command instead of coalescing into this one, and a gesture that
        // never committed pushes nothing at all. BOTH edits close: a press ends
        // whatever was open before it starts its own, and the two kinds never
        // coexist.
        mWrite.end();
        mKeyMoveGesture.end();
        mDragging = false;
        mKeyMoveArmed = false;
        mMarqueeArmed = false;
        mPanning = false;
        // The seek arms no command, but it is still a gesture this editor owns: a
        // press elsewhere, or the panel going away mid-drag, must end it so the next
        // pass cannot keep moving the frame. The band's window gesture is the same
        // kind of state (its release normally ends it; this is the abort path).
        mSeeking = false;
        mWindowing = false;
        mDragKey = nullptr;
        mDragItem = -1;
        mDragSegment = -1;
        mDragHandle = -1;
        mDragPartnerMirrors = false;
        // The scroll bar's drag closes with the others. It holds no command (a scroll
        // bar moves a WINDOW, and the window is not the document), so there is
        // nothing to finalise - dropping the axis is the whole of it, and the window
        // it left is exactly where the next gesture finds it.
        mScrollBarAxis = ScrollBar_None;
        mScrollBarPointer = QPoint();
        mScrollBarContentPerPixel = 0.0;

        // The marquee's box goes with it: a gesture that ends without applying it
        // (a hide, or a press that closed it) must not leave a rectangle behind
        // for the drawing layer to find - nor a mode/snapshot for the next press
        // to inherit.
        mMarqueeOrigin = QPoint();
        mMarqueeCurrent = QPoint();
        mMarqueeMode = Marquee_Replace;
        mPreMarqueeSelection = core::TimeLineEvent();

        // The click a key press armed is part of the same gesture: a press that
        // ended anywhere but its own release (a hide, another press) must not
        // collapse the selection on some later release.
        mClickCollapseTarget = core::TimeLineEvent::Target();
        mClickCollapseComponent = time::Selection::kKeyLevel;
        mClickCollapseValid = false;

        // The key move's command stays on the stack (it IS the gesture's one undo
        // step, whatever it was last asked to do); this drops the gesture's handle
        // to it, so no later press can rewrite a command that belongs to a
        // finished gesture. The armed members go with it: nothing may move until a
        // new press arms one.
        mMoveKeys.clear();
        mMoveMembers.clear();
        mMoveLine = nullptr;
        mMoveNode = nullptr;
        mMoveType = core::TimeKeyType_TERM;
        mMoveFrame = core::Frame();
        mMoveComponent = -1;
        mMovePointerFrame = core::Frame();
        mMovePressPointerFrame = core::Frame();
        mMovePressKeyFrame = core::Frame();
        mMovePressPos = QPoint();
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

    bool GraphEditor::clearSelection() { return mSelection->clear(); }

    int GraphEditor::selectionCount() const {
        // The number of KEYS - two markers of one key are one key, which is what the
        // count the panel publishes means ("3 keys", read by the accessibility
        // summary). An entry that names a key the document no longer holds is not
        // counted: a stale entry is not a key the user sees selected.
        const QVector<time::Selection::Item>& items = mSelection->items();
        int count = 0;
        for (int i = 0; i < items.size(); ++i) {
            core::TimeLineEvent::Target target = items[i].target; // non-const: pos.line() writable
            core::TimeLine* line = target.pos.line();
            if (!line || !line->timeKey(target.pos.type(), target.pos.index()))
                continue;
            bool alreadyCounted = false;
            for (int j = 0; j < i; ++j) {
                const core::TimeLineEvent::Target& earlier = items[j].target;
                if (earlier.node == target.node && earlier.pos.type() == target.pos.type()
                    && earlier.pos.index() == target.pos.index()) {
                    alreadyCounted = true;
                    break;
                }
            }
            if (!alreadyCounted)
                ++count;
        }
        return count;
    }

    bool GraphEditor::selectKey(core::ObjectNode& aNode, const graph::Channel& aChannel, core::Frame aFrame) {
        // One KEY, one target - the whole of it, so every marker it owns is selected
        // (the timeline's unit, and what a caller that names a whole key asks for).
        // It is addressed by the same (line, type, frame) triple the timeline's own
        // keys are, and set() is the whole-selection replace, so this drops whatever
        // was there - in the timeline's set too, which is the point of sharing it.
        core::TimeLineEvent one;
        one.pushTarget(aNode, aChannel.type(), aFrame.get());
        return mSelection->set(one);
    }

    bool GraphEditor::selectMarker(core::ObjectNode& aNode, const graph::Channel& aChannel, core::Frame aFrame) {
        // One MARKER: the key the triple names, at the component aChannel is. The
        // Ctrl-insert is the caller (insertKeyAt), so the key's OTHER components are
        // left out of the selection (the drawing layer gives them the faint ring);
        // the pointer's key gestures write the whole key, as the timeline's do.
        const core::TimeLineEvent::Target target = markerTarget(aNode, aChannel, aFrame);
        if (target.pos.isNull())
            return false;
        return mSelection->setMarker(target, aChannel.component());
    }

    bool GraphEditor::selectAllMarkers() {
        // Every marker of every key of the SHOWN channels: the channels the panel
        // draws (the visible ones of its targets, buildEntries) and each of their
        // curves' keys. Written as ONE WHOLE-KEY entry per key - such an entry is
        // every marker its key owns - so a type whose two components are both shown
        // cannot double it, and the timeline's own reading of the set is the keys it
        // would have had from a marquee over the whole plot. With no shown channel
        // there is nothing to select, and the set is emptied: the gesture selects
        // the shown channels' keys, and there are none.
        core::TimeLineEvent all;
        for (const Entry& entry : buildEntries()) {
            for (const core::TimeKey* key : entry.curve.keys()) {
                all.pushTarget(*entry.node, entry.channel.type(), key->frame());
            }
        }
        return mSelection->set(all);
    }

    core::TimeLineEvent::Target GraphEditor::markerTarget(
        core::ObjectNode& aNode, const graph::Channel& aChannel, core::Frame aFrame
    ) {
        core::TimeLine* line = aNode.timeLine();
        core::TimeLineEvent::Target target;
        if (!line)
            return target; // a folder, or a node kind that holds no keys: nothing to name
        target.node = &aNode;
        target.pos = core::TimeKeyPos(*line, aChannel.type(), aFrame.get());
        target.subIndex = aFrame.get();
        return target;
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
        return assignSelectedKeysEasing([aType](const util::Easing::Param& aCurrent) {
            util::Easing::Param next = aCurrent;
            next.type = aType;
            return next;
        });
    }

    bool GraphEditor::setSelectedKeysAutoHandles() {
        if (!mProject)
            return false;

        // AUTOMATIC HANDLES APPLY TO THE SEGMENT AFTER THE KEY (see
        // ctrl::TimeLineEditor's assignSelectionAutoHandles for the rule and its
        // reasoning). A tangent pair belongs to one segment: that segment's curve
        // is the FROM key's param, its start tangent that key's OUTGOING side and
        // its end tangent the TO key's INCOMING side.
        //
        // appendAutoPresetWrites therefore contributes, per selected key: its own
        // type and OUTGOING side (the curve and start tangent of the segment
        // leaving it) plus the next key's INCOMING side (that segment's end
        // tangent). The previous key is never written, and neither is this key's
        // incoming side - they shape the segment ARRIVING here. A LAST key (or
        // the only key) has no segment leaving it YET, so its own pair is
        // written instead: both sides automatic and the type Custom, so the
        // segments that appear when a neighbour is added derive on both sides.
        // The preset is still the CLAMPED pair (kAutoPresetHandleMode), never
        // the retired Type_Auto.
        QVector<TimeLineEditor::EasingWrite> writes;
        for (const time::Selection::Item& item : mSelection->items()) {
            core::TimeLineEvent::Target target = item.target;
            core::TimeLine* line = target.pos.line();
            if (!line || !target.node)
                continue; // a hand-made target with no owner: nothing to address

            const core::TimeKeyType type = target.pos.type();
            core::TimeKey* key = line->timeKey(type, target.pos.index());
            if (!key)
                continue; // a stale selection names a frame that holds nothing

            // The image offset is step-held: playback never consults its param, so
            // an edit to it changes no drawn sample and no played value (the same
            // rule setSelectedKeysEasing applies).
            if (!graph::Channel(type, 0).isInterpolated())
                continue;

            const int componentCount = graph::Channel::componentCount(type);
            const bool wholeKey = (item.component == time::Selection::kKeyLevel);
            if (!wholeKey && (item.component < 0 || componentCount <= item.component))
                continue;

            for (int component = 0; component < componentCount; ++component) {
                if (!wholeKey && component != item.component)
                    continue;
                TimeLineEditor::appendAutoPresetWrites(
                    *line, *target.node, type, component, core::Frame(key->frame()), writes);
            }
        }
        // ONE macro and ONE undo step for every param the preset touches, including
        // the neighbour sides of a multi-key selection (deduplicated by param).
        return TimeLineEditor::pushEasingWrites(
            *mProject, writes, CmndName::tr("Make the segment after the key automatic"));
    }

    bool GraphEditor::assignSelectedKeysEasing(
        const std::function<util::Easing::Param(const util::Easing::Param&)>& aNext) {
        if (!mProject)
            return false;

        // WHAT CAN BE WRITTEN, collected BEFORE the batch is pushed.
        // TimeLineEditor::pushEasingWrites drops every param that would not change
        // and pushes NOTHING when nothing is left: cmnd::Stack appends a macro even
        // when it holds no command and counts it as an edit, so an action that
        // pushed the batch first would leave an empty undo step behind - the defect
        // removeSelectedKey's own guard avoids.
        //
        // ONE EDIT PER COMPONENT, not per key: a selection may name one marker
        // (Move X) or a whole key, which covers every component it owns. Each
        // component writes ITS OWN param, so choosing an easing for Move X does
        // not re-shape Move Y.
        QVector<TimeLineEditor::EasingWrite> writes;

        for (const time::Selection::Item& item : mSelection->items()) {
            // The mutable target: the action writes the keys through the map the
            // TimeLinePos names, and it is TimeKeyPos::line()'s non-const overload
            // that hands out a writable map.
            core::TimeLineEvent::Target target = item.target;
            core::TimeLine* line = target.pos.line();
            if (!line || !target.node)
                continue; // a hand-made target with no owner: nothing to address

            const core::TimeKeyType type = target.pos.type();

            // The key must BE there. A selection can outlive the key it names (an
            // undo, or another view's edit), and a stale one must not push a
            // command against a frame that holds nothing.
            core::TimeKey* key = line->timeKey(type, target.pos.index());
            if (!key)
                continue;

            // ...and playback has to CONSULT that easing. The image offset is the
            // one graphable value that is step-held (blendImageKey holds the last
            // key; graph::Channel::isInterpolated is the rule), so its param
            // changes no drawn sample and no played value: refusing it is the
            // timeline's own behaviour with Image, not a special case invented
            // here.
            if (!graph::Channel(type, 0).isInterpolated())
                continue;

            // A key-level entry covers every component; a marker entry covers the
            // one component it names.
            const int componentCount = graph::Channel::componentCount(type);
            const bool wholeKey = (item.component == time::Selection::kKeyLevel);
            if (!wholeKey && (item.component < 0 || componentCount <= item.component))
                continue;

            for (int component = 0; component < componentCount; ++component) {
                if (!wholeKey && component != item.component)
                    continue;

                util::Easing::Param* easing = graph::Curve::easingOf(*key, type, component);
                if (!easing)
                    continue;

                TimeLineEditor::EasingWrite write;
                write.param = easing;
                write.next = aNext(*easing); // the action's own value; range, weight
                                             // and bezier ride along unless it rewrites them
                write.node = target.node;
                write.type = type;
                write.frame = core::Frame(key->frame()); // the key's frame NOW
                writes.append(write);
            }
        }

        // ONE macro and ONE notifier for the whole selection - the timeline's own
        // route (TimeLineUtil::assignKeyEasing: a cmnd::Assign of the key's
        // util::Easing::Param, a TimeLineUtil::Notifier, and a
        // core::TimeLineEvent stating Type_ChangeKeyValue), batched so the
        // selection is ONE undo step. The per-key helper cannot be reused for
        // that: it opens a macro of its own, and cmnd::Stack::beginMacro
        // asserts on a nested one.
        return TimeLineEditor::pushEasingWrites(
            *mProject, writes, CmndName::tr("change key easing"));
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
        // The playhead's frame, for views that draw it. -1 is this method's "no
        // project" sentinel - a project's frames may be NEGATIVE, so callers that
        // must tell the two apart ask hasProject(); the graph panel's model does.
        return mProject ? mProject->currentTimeInfo().frame : core::Frame(-1);
    }

    //-------------------------------------------------------------------------------------------------
    int GraphEditor::fps() const {
        // The playback rate the frame-number formats are written at. 60 is the
        // timeline editor's own pre-project default (ctrl::TimeLineEditor's mFps),
        // so an empty editor formats exactly as an empty timeline does.
        return mProject ? mProject->attribute().fps() : 60;
    }

    //-------------------------------------------------------------------------------------------------
    GraphEditor::ContextTarget GraphEditor::contextTargetAt(
        const core::CameraInfo& aCamera, const QPoint& aPos
    ) const {
        // THE PRESS'S OWN RULE, on a panel rect pinned to the caller's camera: a
        // public entry point cannot assume a cursor pass has written mScale (the
        // same reason insertKeyAt pins a local copy), so the geometry the
        // right-click is measured against is the geometry of the panel the caller
        // is looking at.
        graph::Scaler scale = mScale;
        scale.setRect(currentPanelRect(aCamera));

        const QVector<Entry> entries = buildEntries();
        const Hit hit = hitTest(entries, scale, aPos);

        ContextTarget target;
        if (hit.item < 0 || hit.item >= entries.size())
            return target; // not a key, not a handle: the background

        const Entry& entry = entries[hit.item];
        target.node = entry.node;
        target.channel = entry.channel;

        if (hit.hitHandle) {
            if (hit.segment < 0 || hit.segment >= entry.curve.segmentCount())
                return ContextTarget(); // a hit-test answer this document no longer holds
            const graph::Curve::Segment segment = entry.curve.segment(hit.segment);
            if (!segment.from || !segment.to)
                return ContextTarget();

            target.kind = ContextTarget::Kind_Handle;
            target.segment = hit.segment;
            target.handle = hit.handle;
            // THE KEY THAT OWNS THE SIDE: handle 0 leaves the segment's `from` key
            // (its handleOut), handle 1 arrives at its `to` key (its handleIn).
            // The mode is read from the same owner, which is also where the mode
            // combo in the picker reads it.
            target.frame = core::Frame(hit.handle == 1 ? segment.to->frame() : segment.from->frame());
            target.mode = (hit.handle == 1) ? segment.inMode : segment.easing.handleOut;
            return target;
        }

        if (hit.hitCurve) {
            target.kind = ContextTarget::Kind_Key;
            target.frame = hit.keyFrame;
            return target;
        }

        return ContextTarget();
    }

    //-------------------------------------------------------------------------------------------------
    bool GraphEditor::setHandleMode(const ContextTarget& aTarget, const util::Easing::HandleMode aMode) {
        if (!mProject || aTarget.kind != ContextTarget::Kind_Handle || !aTarget.node)
            return false;

        core::TimeLine* line = aTarget.node->timeLine();
        if (!line)
            return false; // a folder, or a node kind that holds no keys

        const graph::Curve curve = curveFor(*line, aTarget.channel);
        if (aTarget.segment < 0 || aTarget.segment >= curve.segmentCount())
            return false; // the segment the menu was opened on is gone

        const graph::Curve::Segment segment = curve.segment(aTarget.segment);
        if (!segment.from || !segment.to)
            return false;

        // A SEGMENT'S CURVE IS ITS FROM KEY'S PARAM, so only a Custom one has
        // tangents at all - and the panel publishes (and the hit-test grabs) a
        // handle only for such a segment. An analytic one answers false rather
        // than writing a mode nothing would read.
        if (segment.easing.type != util::Easing::Type_Custom)
            return false;

        QPointF effective[2];
        if (!curve.effectiveHandles(segment, effective))
            return false;

        // The MUTABLE keys of the segment: Curve hands out const ones (it only
        // reads the document), while this action writes through the map the
        // timeline owns - the same (type, frame) lookup the drag uses.
        core::TimeKey* fromKey = line->timeKey(aTarget.channel.type(), segment.from->frame());
        core::TimeKey* toKey = line->timeKey(aTarget.channel.type(), segment.to->frame());
        if (!fromKey || !toKey)
            return false; // the segment moved under the menu

        util::Easing::Param* fromParam =
            graph::Curve::easingOf(*fromKey, aTarget.channel.type(), aTarget.channel.component());
        if (!fromParam)
            return false; // the type carries no easing: nothing the graph could be editing

        const bool incoming = (aTarget.handle == 1);
        core::TimeKey* ownerKey = incoming ? toKey : fromKey;
        util::Easing::Param* ownerParam = incoming
            ? graph::Curve::easingOf(*toKey, aTarget.channel.type(), aTarget.channel.component())
            : fromParam;
        if (!ownerParam)
            return false;

        const util::Easing::HandleMode current =
            incoming ? ownerParam->handleIn : ownerParam->handleOut;

        // THE WRITES, merged per param: the mode lives on the OWNER's param, while
        // the INCOMING side's stored control point lives in the FROM param (the
        // segment's easing) - so one edit can name the same param twice, and an
        // entry that is already there must be updated rather than appended twice.
        QVector<TimeLineEditor::EasingWrite> writes;
        const auto writeFor = [&writes, &aTarget](
                                  core::TimeKey& aKey, util::Easing::Param* aParam) -> util::Easing::Param* {
            for (TimeLineEditor::EasingWrite& existing : writes) {
                if (existing.param == aParam)
                    return &existing.next;
            }
            TimeLineEditor::EasingWrite entry;
            entry.param = aParam;
            entry.next = *aParam;
            entry.node = aTarget.node;
            entry.type = aTarget.channel.type();
            entry.frame = core::Frame(aKey.frame());
            writes.append(entry);
            return &writes.last().next;
        };

        if (util::Easing::isDerived(current) && !util::Easing::isDerived(aMode)) {
            // THE SIDE STARTS READING ITS STORED POINT, so that point must be the
            // one it is SHOWN at: the derived pair the panel drew. x1/y1 is the
            // FROM key's outgoing side, x2/y2 its incoming one - both stored in
            // the FROM param, whatever key owns the mode.
            util::Easing::Param* stored = writeFor(*fromKey, fromParam);
            if (incoming) {
                stored->cubicBezier.x2 = float(effective[1].x());
                stored->cubicBezier.y2 = float(effective[1].y());
            } else {
                stored->cubicBezier.x1 = float(effective[0].x());
                stored->cubicBezier.y1 = float(effective[0].y());
            }
        }

        // THE MODE ITSELF, on the key that owns the side.
        util::Easing::Param* owner = writeFor(*ownerKey, ownerParam);
        if (incoming)
            owner->handleIn = aMode;
        else
            owner->handleOut = aMode;

        // The name says WHICH SIDE, because that is the question the user asked
        // the menu - "Free" alone could be either tangent of the key.
        const QString side = incoming ? CmndName::tr("incoming") : CmndName::tr("outgoing");
        const QString name = CmndName::tr("Change the %1 handle to %2")
                                 .arg(side, util::Easing::getHandleModeName(aMode));
        return TimeLineEditor::pushEasingWrites(*mProject, writes, name);
    }

} // namespace ctrl
