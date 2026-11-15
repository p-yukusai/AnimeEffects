#ifndef CTRL_GRAPHEDITOR_H
#define CTRL_GRAPHEDITOR_H

#include <QRect>
#include <QVector>
#include <QtCore/qnamespace.h>
#include <functional>
#include "cmnd/BasicCommands.h"
#include "cmnd/Gesture.h"
#include "core/AbstractCursor.h"
#include "core/ObjectNode.h"
#include "core/Project.h"
#include "core/CameraInfo.h"
#include "core/TimeLineEvent.h"
#include "ctrl/UILogger.h"
#include "ctrl/graph/graph_Curve.h"
#include "ctrl/graph/graph_Scaler.h"
// The selection the panel writes is the TIMELINE's own currency (see the
// SELECTION note below): time::Selection is what carries the focus the
// timeline's renderer highlights, so the panel holds a reference to the very
// instance the timeline owns rather than a second set of its own.
#include "ctrl/time/time_Selection.h"
#include "util/Range.h"

namespace ctrl {

// The two document commands a key-move gesture drives (defined in
// ctrl/TimeLineUtil.h): the frame half and the value half. Declared here so this
// header does not drag the timeline utilities - and every key type they include
// - into every consumer, the same reason the Entry list below is built from core
// headers alone.
namespace TimeLineUtil {
    class MoveFrameOfKey;
    class AssignKeyValue;
}

// The animation graph editor: the value curves of the selected layers'
// channels, drawn as a fullscreen overlay over the animation.
//
// The panel covers the WHOLE viewport and has no background of its own - it is
// an overlay over the artwork, not a band beside it. That is a change with two
// consequences the rest of this header spells out:
//   * it must not own the whole viewport by rectangle (a fullscreen rect would
//     swallow every event the viewport tools need): ownership is narrowed to the
//     panel's PLOT - the data area the curves are drawn in - for a press, for a
//     drag it began, and for the middle button, and is otherwise HIT-ONLY, see
//     ownsCursor;
//   * the cursor is the only thing that marks a channel, so the panel keeps a
//     STICKY FOCUS - one shown channel the panel EMPHASISES (the drawing layer
//     paints every other curve dimmed) - which a press on a curve sets and a
//     press on empty space deliberately leaves alone. The focus does NOT move
//     the value window: fitting is deliberate (fitAll or the axes' own fit),
//     never a side effect of selecting.
//
// The layering is the timeline's, deliberately:
//   * geometry, scales, sampling and hit-testing live here and in ctrl::graph -
//     widget-free, no GL, no camera;
//   * painting is NOT here: the panel is drawn by gui::drawPanel (Dear ImGui)
//     from gui::GraphPanelModel, in the ImGui pass at the end of
//     MainDisplayWidget::paintEvent. The geometry this class picks through is
//     the model's geometry too - both read ctrl/graph/graph_Geometry.h - so the
//     drawn item and the picked item are one;
//   * the widget only feeds the cursor, so this class owns the mouse semantics
//     for the panel the same way ctrl::Driver's editors own them elsewhere.
//
// Every edit is ONE undo step, and every edit announces itself on the timeline's
// own wire: a handle drag is a cmnd::Gesture over a single
// cmnd::ModifiableAssign<util::Easing::Param>, and the three key edits - insert,
// move and remove (see insertKeyAt, the MOVE note next to it and
// removeSelectedKey) - go through a ctrl::TimeLineUtil::Notifier inside a
// cmnd::ScopedMacro, carrying the same core::TimeLineEvent the timeline emits.
// So the two views share one selection currency and one change currency, and
// undo/redo is one command per gesture for every edit this panel can make.
class GraphEditor {
public:
    GraphEditor(core::Project& aProject, UILogger& aLogger);
    ~GraphEditor();

    void setProject(core::Project* aProject);

    // The layers whose curves are drawn: the object tree selection.
    void setTargets(const QVector<core::ObjectNode*>& aTargets);
    const QVector<core::ObjectNode*>& targets() const { return mTargets; }

    // Shown/hidden. Hidden keeps every state (selection, scale, hidden channels)
    // so the panel comes back as it was.
    void setVisible(bool aVisible);
    bool isVisible() const { return mVisible; }

    // The panel rect for a viewport of this size, in viewport pixels: the WHOLE
    // viewport. The panel is a fullscreen overlay drawn over the animation with
    // no background of its own, so this is not a band and there is no inset or
    // height to clamp. Still a pure function of the size: no camera, no layout
    // pass.
    static QRect panelRect(const QSize& aViewportSize);

    // Cursor handling, the IEditor::updateCursor shape: returns true when the
    // graph needs a repaint.
    //
    // ownsCursor is the arbitration. The panel covers the whole viewport, so
    // "inside the panel" cannot decide it - that would mean "inside the
    // viewport" and the graph would swallow every event the tools need. Instead:
    //   * an in-flight gesture (a handle drag, a key move, a marquee, a
    //     middle-button pan) owns the cursor wherever it goes, so the release
    //     that ends it reaches this class;
    //   * a key or a tangent handle owns the point it is under, wherever it is;
    //   * a LEFT PRESS inside the panel's PLOT rect is the panel's, because that
    //     is where its own plot gestures begin: on a key it selects (and arms the
    //     move), on a curve's band WITH CTRL it inserts (see insertKeyAt), and
    //     anywhere else - including a plain press on a band - it opens the
    //     MARQUEE (see the SELECTION note);
    //   * a MIDDLE-button event inside the viewport is the panel's too - the
    //     middle drag pans the graph while the panel is up, never the canvas
    //     camera (see updateCursor).
    // A plain hover, a press outside the plot and every event on a point with
    // nothing to act on pass through: the tools keep their feedback over the
    // whole viewport, their presses in the label column and the margins, and the
    // camera gestures the panel does not own.
    //
    // WHAT THE PLOT COSTS THE TOOLS. The plot is most of the canvas, so claiming
    // every press in it does take those presses from the viewport tools while the
    // panel is up - there is no modifier left to let a rubber band start
    // anywhere in the data area otherwise (a band that had to be started with Ctrl
    // was a band nobody draws by accident, but also one nobody finds). Showing the
    // panel is the user saying they are working on curves; hiding it (or putting
    // the pointer outside the plot) is how the tools get the canvas back. The
    // events this does NOT take are the ones above: hovers, the chrome outside the
    // plot, and a right-press, which is nobody's here.
    //
    // The Driver asks ownsCursor first and routes the event to exactly one owner
    // - the viewport has no accept()/reject() seam, and adding one would change
    // every mode's contract.
    bool ownsCursor(const core::CameraInfo& aCamera, const core::AbstractCursor& aCursor) const;
    bool updateCursor(const core::CameraInfo& aCamera, const core::AbstractCursor& aCursor);

    // Wheel over the panel: zoom. Returns true when the graph took the event, so
    // the viewport does not also zoom the canvas.
    //
    // The plain wheel zooms BOTH axes about the cursor by the same notch factor,
    // so the point under the pointer stays under it - the two axes have to scale
    // together for a curve's shape to stay readable while zooming. A modifier
    // narrows it to one axis, which is what the wheel did before both axes were
    // the default:
    //   * Shift - the FRAME axis alone (horizontal), about the cursor's column;
    //   * Ctrl  - the VALUE axis alone (vertical), about the cursor's row.
    // Both use the canvas mover's and the timeline scaler's own per-notch factor
    // (1.1 per notch) AND their direction: a notch Qt reports as positive zooms
    // OUT, so one wheel motion does the same thing over the panel and over the
    // viewport. (The value axis alone used to zoom IN on that notch, opposite the
    // frame axis beside it; the two have to agree for a uniform zoom to mean
    // anything.) The application's invert-scaling settings are NOT consulted
    // here: the panel reads the raw wheel, as it always has.
    //
    // The FRAME half is the TIMELINE's while a timeline owns the frame axis (see
    // the shared-frame-window note): the notch goes to the timeline through its
    // delegate, about the frame under the cursor, and the panel's own X moves only
    // because the timeline pushed its new window back. The VALUE half is always
    // this panel's - it is the axis nothing else shows.
    bool updateWheel(
        const core::CameraInfo& aCamera, const QPoint& aPos, int aWheelDelta, Qt::KeyboardModifiers aModifiers
    );

    // The modifier state the panel's own gestures read, fed by the widget before
    // it routes a cursor or wheel event (core::AbstractCursor carries no
    // modifiers). It is plain state, not a gesture: a press reads it and a drag
    // re-reads it on every move, so releasing Shift mid-drag un-constrains the
    // axis at once, like the viewport's own modifier-driven motions.
    //
    //   * Shift - a key drag moves on ONE axis: the dominant one of the gesture
    //     so far (see the MOVE note); a marquee ADDS its keys to the selection
    //     instead of replacing it (see the SELECTION note);
    //   * Ctrl  - a press ON A CURVE'S BAND inserts a key there (see insertKeyAt).
    //     It is the ONLY thing Ctrl does with a press now: every other press in
    //     the plot - on a key it selects and arms the move, everywhere else it
    //     opens the marquee - has the same meaning with or without it.
    void setModifiers(Qt::KeyboardModifiers aModifiers);
    Qt::KeyboardModifiers modifiers() const { return mModifiers; }

    // The curves currently drawn: one per visible channel of every target.
    // Rebuilt from the document on demand, so an edit anywhere is reflected.
    QVector<graph::Curve> curves() const;

    // Channel visibility, keyed by (target, channel). A channel is visible
    // unless it has been hidden.
    void setChannelVisible(core::ObjectNode& aTarget, const graph::Channel& aChannel, bool aVisible);
    bool isChannelVisible(const core::ObjectNode& aTarget, const graph::Channel& aChannel) const;

    // --- the selection, in the timeline's own currency ---
    //
    // SELECTION. The panel selects KEYS, and it can select MORE THAN ONE: the
    // selection is the timeline's own time::Selection, one entry per selected
    // key, so a multi-key selection is the same list the timeline itself
    // carries. A press chooses between the three plain behaviours by the
    // modifier the widget fed in:
    //   * a plain press on a key REPLACES the selection with that one key;
    //   * Shift-press on a key ADDS it if it is not already there (an existing
    //     entry is left alone - Shift extends, it does not toggle);
    //   * Ctrl-press on a key TOGGLES it (in when out, out when in).
    // A MARQUEE - a PLAIN press in the plot on a point that is neither a key nor
    // a handle (Ctrl on a curve's band inserts instead, see insertKeyAt) -
    // dragged before it is released, replaces the selection with every key whose
    // drawn square falls inside the dragged rectangle, or, with Shift held at the
    // press, adds those keys to the selection. It is drawn as a live rectangle
    // while the drag is in flight (marqueeRect).
    //
    // ONE SET, ONE HIGHLIGHT. The set an edit here writes is the live selection
    // of the timeline the panel is a view of (see setSelectionSource): writing it
    // is what makes the timeline's renderer highlight those keys (time::Selection
    // rebuilds every member key's focus on every change, and that focus IS the
    // highlight), and a selection made in the timeline is what this panel reads
    // and acts on. With no timeline attached - the unit tests, and the frame
    // before the window wires one in - the panel uses a private instance of the
    // same type, so there is exactly one set either way and the code below never
    // branches on which it is.
    //
    // The selection is what removeSelectedKey and setSelectedKeysEasing act on,
    // and it is what the drawing layer highlights (gui::PanelFocus::selectedKeys).
    // It is NOT touched by anything else: a plain hover and every press outside
    // the plot leave it alone.
    bool retrieveSelection(core::TimeLineEvent& aOut) const;
    void clearSelection();
    // Replaces the whole selection with this one key.
    void selectKey(core::ObjectNode& aNode, const graph::Channel& aChannel, core::Frame aFrame);
    // Adds (aAdd == true) or removes (aAdd == false) this key. Adding a key
    // already in the selection, or removing one that is not, changes nothing.
    void modifySelection(core::ObjectNode& aNode, const graph::Channel& aChannel, core::Frame aFrame, bool aAdd);
    // Attaches the selection this editor writes to aSelection - the timeline's
    // own live set (ctrl::TimeLineEditor::selection()) - or, with nullptr, back
    // to the private instance this editor starts on. The caller owns the
    // lifetime: whoever attaches a selection must detach (or die) before it goes
    // away. Nothing else about the selection changes: the entries already in it
    // are the entries the panel then reads.
    void setSelectionSource(time::Selection* aSelection);
    // The live selection this editor writes. Never null.
    time::Selection& selection() { return *mSelection; }
    const time::Selection& selection() const { return *mSelection; }
    // True while a marquee drag is in flight.
    bool hasMarquee() const { return mMarqueeArmed; }
    // The marquee's rectangle in viewport pixels, normalized (a positive width
    // and height) and clipped to the plot, or an EMPTY rect while no marquee drag
    // is in flight. A press and release with no movement gives an empty rect:
    // nothing falls inside it, so the selection becomes empty, which is the
    // "click empty space to deselect" gesture.
    QRect marqueeRect() const;
    // True while a middle-button pan is in flight.
    bool isPanning() const { return mPanning; }

    // --- the three key edits: add, move, remove ---
    //
    // The panel's document-mutating gestures, next to the handle drag below.
    // Each is ONE undo step, and each goes through the same
    // ctrl::TimeLineUtil::Notifier + cmnd::ScopedMacro route the drag (and the
    // timeline) uses: one core::TimeLineEvent in the timeline's own currency
    // reaches the rest of the application, so the timeline and the object tree
    // see an edit made here exactly as they see one made there.
    //
    // ADD. A CTRL-press on a curve's BAND - a point on no key and no handle,
    // whose frame the curve covers and whose value is within kCurveBandRadius
    // pixels of the value the curve evaluates there - inserts the key the curve
    // already passes through: the value written is the curve's own sample at that
    // frame, so inserting it changes no sampled value AT THAT FRAME. The new
    // key's payload - the components the graph does not draw, and the key's
    // easing - is taken from the key playback is on at that frame (the segment's
    // own `from` key, which owns that segment's easing), so the insert invents
    // neither a value nor a payload.
    //
    // WHY THE MODIFIER. The band lies on the curve, i.e. in the middle of the
    // plot, and the plot is where a MARQUEE begins (see the SELECTION note). A
    // plain press on the band would insert a key whenever a user started a
    // rubber band that happened to cross a curve - an edit nobody asked for, from
    // a gesture that means "select". Ctrl-press is the graph-editor convention
    // for "add a key here" (Blender's f-curve editor does exactly this), and it is
    // unambiguous here because Ctrl on a KEY also means "toggle this key in the
    // selection": the two never overlap, since a band point has no key on it. So
    // one press has one meaning decided by what is under the pointer: a key
    // selects (Ctrl toggles it), a band point with Ctrl inserts, and every other
    // point in the plot - a band point WITHOUT Ctrl included - opens the marquee.
    //
    // The modifier is therefore the INSERT's alone now, and the marquee's cost is
    // the plain press it takes: see ownsCursor for what that costs the viewport
    // tools inside the plot.
    //
    // The inserted key becomes the panel's whole selection and its channel the
    // focused channel, exactly as a pressed key would (so a following
    // removeSelectedKey() removes it). The focus no longer moves the value
    // window (see the class comment), so an insert press moves nothing under its
    // own release.
    //
    // One caveat the key's own setters impose: the value is written through the
    // concrete key's setter, which clamps to that type's own bound - a
    // Move/Scale/Rotate/Depth value is bounded far outside any real animation (±
    // 10000), opacity is 0..1 and an HSV component is an int, so a sampled value
    // beyond those bounds is seeded as the bound. That is the one case where an
    // insert can move a sampled value, and the graph does not invent a bound of
    // its own to avoid it (the bounds belong to the key types).
    //
    // The insert is REFUSED, with nothing pushed and the document untouched,
    // when the hit-test finds a key or a handle at the point (the more specific
    // target wins - this is what makes an occupied frame safe, since
    // TimeLine::createPusher REPLACES a key at its own frame and replacing is a
    // different edit with a different undo story), when that frame already holds
    // a key of the channel's type (the same rule, as the backstop for a pointer
    // on another channel's band), and when the point is outside the plot or off
    // every shown curve.
    bool insertKeyAt(const core::CameraInfo& aCamera, const QPoint& aPos);

    // MOVE. Pressing a KEY (not a handle) arms a move: while the button is down
    // the key follows the pointer in BOTH axes - its frame follows the pointer's
    // frame delta and its value follows the pointer's value delta, each measured
    // from where the button went down, so grabbing a key off its centre does not
    // make it jump by the offset - and the whole gesture is ONE undo step. It is
    // driven by updateCursor, like the handle drag, and the rules are:
    //   * the first ACCEPTED move opens ONE commit: a TimeLineUtil::MoveFrameOfKey
    //     for the FRAME and a cmnd::ModifiableAssign<tKey::Data> for the VALUE,
    //     side by side inside ONE cmnd::ScopedMacro carrying ONE
    //     TimeLineUtil::Notifier. Every later move of the gesture rewrites both
    //     through their own modifyMove/modifyValue, so a drag cannot push one
    //     command per move;
    //   * the value command assigns the key's own Data - a copy of what the key
    //     holds, with the one component the dragged channel names replaced by the
    //     value the pointer points at. It is a whole-Data assign precisely so
    //     every component the graph does not draw (a Move key's centroid, an HSV
    //     key's absolute flag, an Image key's resource and blend mode) is carried
    //     through unchanged, and the key's own setter still clamps the value to
    //     its type's bound;
    //   * SHIFT constrains the gesture to the DOMINANT axis: the pointer's own
    //     pixel displacement decides (|dx| >= |dy| keeps the frame, otherwise the
    //     value), so a drag that is overwhelmingly horizontal does not smear a
    //     stray pixel of vertical movement into the value. Shift is re-read on
    //     every move, so it can be pressed and released mid-drag;
    //   * a destination frame that already holds a key of that type is REFUSED
    //     (MoveFrameOfKey's own conflict rule), and a refusal refuses the whole
    //     step - frame AND value stay where the gesture last had them, nothing
    //     is announced, and the gesture stays open so the pointer can be dragged
    //     on to a free frame. Nothing is pushed before a step is accepted, so a
    //     refused drag leaves no empty history entry behind (the timeline's own
    //     move pushes its command on the first drag and can then be refused,
    //     which does leave one; the graph does not repeat that);
    //   * a step that moves neither frame nor value pushes nothing and opens
    //     nothing, so a press that never moves leaves the history untouched;
    //   * an undo mid-drag drops the commands' modifiable link, so the next move
    //     opens a fresh commit from the frame and value the key is at then;
    //   * the armed key is held in the timeline's currency (the layer that owns
    //     the map, the node the event names, the type and the frame it is at
    //     now), not as an index into a rebuilt entry list, so the move follows
    //     the key the press hit; if that key is gone (an undo or another view
    //     removed it) the gesture stops and says so, like the handle drag's
    //     anchor check.
    //
    // REMOVE. Removes every key the panel's CURRENT selection names through
    // TimeLine::createRemover, inside ONE notifier + macro route, so a
    // multi-key selection is ONE undo step. An undo restores the SAME key
    // objects with their values and easings, because the removal is a map
    // removal and not a rewrite. Keys the selection names that are no longer in
    // the document (a stale selection) are skipped; when nothing survives, the
    // selection is dropped and nothing is pushed, so no empty entry is left
    // behind. The selection is dropped with the keys.
    //
    // The application's Delete key is routed by its key map to the timeline
    // ("Delete selected key(s)"), and this panel is fed only cursor events - so
    // THIS call is the panel's remove action, and wiring a key press to it
    // belongs to whatever owns the key map, not to this class.
    bool removeSelectedKey();

    // EASING. Changes the easing TYPE of every key the panel's SELECTION names,
    // as ONE undo step, and returns whether it pushed anything.
    //
    // The route is the timeline's own, so the two views cannot disagree about
    // undo or notifications: each key is assigned through the same
    // cmnd::Assign<util::Easing::Param> command ctrl::TimeLineUtil's
    // assignKeyEasing pushes, under ONE cmnd::ScopedMacro carrying ONE
    // TimeLineUtil::Notifier whose core::TimeLineEvent states Type_ChangeKeyValue
    // and names every target that was written. That is the timeline's
    // "Change key(s) easing to" action, batched: the timeline opens one macro per
    // key (its own assignKeyEasing does, and cmnd::Stack::beginMacro asserts on a
    // nested one), so it takes one undo step per key; here the whole selection is
    // one macro and therefore one entry in the history.
    //
    // WHICH KEYS IT WRITES. The selection is the timeline's own currency
    // (retrieveSelection; selectKey sets it, and a right-click on a key in the
    // panel sets it the same way the timeline's key menu does). A target whose
    // frame holds no key any more is skipped - a stale selection names a key that
    // is not in the document - and so is a key whose type carries no easing
    // (graph::Curve::easingOf answers null) and a key whose easing type is
    // ALREADY aType, so a menu entry that changes nothing leaves no history entry
    // behind. Nothing else is dropped silently: the ones that are written all
    // appear in the notifier's event.
    //
    // EMPTY SELECTION (or a selection of which nothing survives the rules above):
    // returns false and pushes NOTHING. That is not a micro-optimisation -
    // cmnd::Stack appends a macro even when it holds no command and counts it as
    // an edit, so an action that opened the macro first would leave an empty undo
    // step, exactly the defect removeSelectedKey's own guard avoids.
    //
    // SEVERAL KEYS: every target that survives is written, and the whole batch
    // stays ONE undo step, because it is one macro. The panel makes such a
    // selection itself (a marquee, or Shift-presses, see the SELECTION note), and
    // so does the timeline; because the two share ONE set, this action writes
    // exactly the keys the user sees highlighted in either view.
    //
    // The Image channel is deliberately NOT written: its keys carry an easing
    // param, but playback step-holds image offsets (graph::Channel::isInterpolated
    // is false for it), so the param is never consulted and the graph refuses the
    // edit - which is also what the timeline's own easing menu does with Image.
    // The panel's easing action therefore reports false for a selection on an
    // image channel rather than recording a change nothing can see.
    bool setSelectedKeysEasing(util::Easing::Type aType);

    // The half-width, in pixels, of the band around a curve the insert gesture
    // edits (see insertKeyAt's ADD note): a point whose frame the curve covers
    // and whose value is within this distance of the value the curve evaluates
    // at that frame is a point on the curve. Deliberately smaller than
    // graph::kKeyHitRadius (10) and graph::kHandleHitRadius (12), so a press
    // near an existing key or handle always hits that item first and only a
    // genuinely empty stretch of a curve inserts.
    static constexpr int kCurveBandRadius = 6;

    // The shared frame window's EDGE MARGIN, in pixels: while a timeline owns the
    // frame axis, its first frame is mapped to plotRect().left() + this and its last
    // to plotRect().right() - this (see setFrameWindow), so a key AT either end of
    // the window is drawn and picked WHOLE. Without it the window's first frame lands
    // on the plot's left edge and its marker is half outside the plot's clip - half
    // drawn, and unclickable at its own centre (measured 2026-10-14 on the captured
    // project's single key at frame 0: only the right half of the dot was on screen,
    // under the playhead's own column). The timeline answers the same problem with
    // ctrl::TimeLineEditor::kTimeLineMargin (24px), the gutter its own frame 0 sits
    // in; the panel's window is a different mapping of the same frames and needs only
    // enough to keep a marker whole.
    //
    // WHY 8. The rect the hit-test and the drawing share is +/-graph::kKeyRadius (5)
    // around the key's pixel, and the marker drawn inside it has radius
    // graph::kKeyDotRadius (3.75). The origin is written through the scaler's INTEGER
    // pixel origin (panFrame is the only writer available besides an anchored zoom),
    // so an end can land up to two pixels closer to its edge than the arithmetic
    // asks: one from that integer step and one from the rounding that places it. The
    // floor is therefore kKeyRadius + 2 = 7 - the hit square still whole at either
    // end - and 8 is that floor plus one pixel so neither end can be exactly flush
    // with its own radius.
    static constexpr int kFrameWindowMargin = 8;

    // --- sticky focus ---
    // The panel marks ONE shown channel as focused: the channel a press last hit
    // on a curve. The focus is the panel's EMPHASIS - the drawing layer paints
    // the focused curve and its keys at full colour and every other curve dimmed
    // - and it is reported to the accessibility layer as what the panel is
    // working on. It is NOT a fit trigger: see "deliberate fit" below.
    //
    // The policy is STICKY by design:
    //   * a press that HITS a curve (a key or a tangent handle) makes that
    //     entry's (node, channel) the focus;
    //   * a press that hits nothing leaves the focus - and the selection -
    //     untouched, because empty space is not a command to drop what the user
    //     was working on;
    //   * the focus survives every cursor pass and every paint: it is not a
    //     hover, it is a choice, so moving the pointer off the curve does not
    //     change it;
    //   * setTargets and setProject drop a focus that is no longer among the
    //     shown entries (hiding the focused channel drops it on the next sync,
    //     which a cursor pass runs), since a channel the panel cannot draw
    //     cannot be what the panel emphasises.
    //
    // DELIBERATE FIT. The value window changes only when the SHOWN SET changes
    // (a new layer selection, a channel that appears or disappears) or when a
    // caller asks for a fit (fitValueAxis, fitAll, the double-click). It never
    // changes because the focus moved, because a key was selected or dragged, or
    // because the wheel or a pan was used - a user who has framed a curve keeps
    // that frame while working on it. The fit the focus used to imply was
    // removed for exactly that reason: clicking a curve zoomed the view, which
    // is not what a click means.
    bool hasFocus() const { return mFocusNode != nullptr; }
    // The focused layer, or nullptr when nothing is focused.
    const core::ObjectNode* focusedNode() const { return mFocusNode; }
    // The focused channel; meaningful only while hasFocus() is true.
    graph::Channel focusedChannel() const { return mFocusChannel; }
    // Makes (aNode, aChannel) the focused channel. The pair is expected to be
    // one of the shown entries. Re-focusing the channel that is already focused
    // is a no-op. It moves NO window: the value axis is the user's, and the
    // focus is emphasis only (see DELIBERATE FIT above).
    void setFocusedChannel(core::ObjectNode& aNode, const graph::Channel& aChannel);
    // Drops the focus. Like setFocusedChannel it moves no window.
    void clearFocus();

    // The document's current frame, for views that draw the playhead. -1 when no
    // project is set (core frame numbers start at 1, so -1 cannot be a real
    // frame).
    core::Frame currentFrame() const;

    // The scale, so the view can zoom and pan (wheel) and so a caller can query
    // the window it is showing.
    graph::Scaler& scale() { return mScale; }
    const graph::Scaler& scale() const { return mScale; }

    // Fits the value axis to the values the SHOWN curves occupy now: the min/max
    // of every shown curve's key values AND of a walk through its segments, since
    // a Custom easing (Back/Elastic) reaches values beyond the two keys' values.
    // It is the UNION of every shown curve; the focus does not narrow it (see
    // DELIBERATE FIT).
    // A curve with no keys contributes nothing, and when NO shown curve has keys
    // the window is left untouched, so an empty panel cannot blank a window the
    // user zoomed.
    //
    // This is the fit itself; the non-const paths run it through syncValueAxis(),
    // which skips it while the shown curves are the ones the last fit was
    // computed from - that is what keeps a wheel zoom. A caller wanting the axis
    // back on the data unconditionally calls this.
    void fitValueAxis();

    // Fits the FRAME axis to the shown curves' key frames, with a small pad at
    // both ends. The graph's frame axis is its own ctrl::time::Scaler instance
    // (see graph::Scaler), so this moves the graph's frame window only; the
    // timeline's zoom is untouched. Uses the panel rect the cursor path last
    // wrote into scale(), like the value fit.
    //
    // While a timeline owns the frame axis (see setFrameWindow) this does
    // NOTHING: the panel's X window is the timeline's, and a fit that moved it
    // would be the panel deciding, on its own, which frames are on screen - the
    // one thing the shared window exists to prevent. The value half of a fit
    // still happens, which is what fitAll below does there.
    void fitFrameAxis();
    // Both axes at once: the panel's double-click fit (and the accessibility
    // layer's own "fit" action). Fits the value axis to the union of the shown
    // curves and the frame axis to every shown key's frame, so the whole of the
    // shown data is on screen. This is the one explicit fit that is wired to a
    // pointer gesture, and it is the gesture a user reaches for when the view has
    // drifted away from the data. While a timeline owns the frame axis, only the
    // VALUE half happens - the frames on screen are the timeline's to choose (see
    // fitFrameAxis).
    void fitAll();

    // --- the shared FRAME window ---
    //
    // X IS THE TIMELINE'S while the panel is a second view of one. "Second view
    // of the same data" means the two views show the same FRAMES: the panel's
    // horizontal window is handed in here as VALUES (setFrameWindow, which the
    // gui repeats whenever the timeline scrolls, zooms or changes its range) and
    // mapped onto the panel's own plot, and the two horizontal gestures - the
    // wheel's frame half and the middle-drag pan's horizontal half - are handed
    // BACK to the timeline through the delegates below, because a gesture that
    // moved only the panel would leave the two views showing different frames.
    // The panel's own X therefore never moves on its own: it is written by
    // setFrameWindow and by nothing else (see fitFrameAxis, updateWheel,
    // updatePan).
    //
    // WHAT IS SHARED IS A WINDOW, NOT A SCALE. The panel maps the pushed range
    // with its own zoom ladder (ctrl::time::Scaler's rung: the largest one that
    // shows the range in its plot) and INSETS it by kFrameWindowMargin at both
    // ends, so the window's first and last frames are drawn and picked whole - a
    // marker centred on the plot's own edge would be half clipped away and
    // unclickable at its centre. The timeline's pixels-per-frame stays the
    // timeline's: the panel's plot is a different width, and forcing one pixel
    // scale on both would either waste the wider one or overflow the narrower.
    // A range too wide for the ladder's floor (1 px/frame) is shown as much of it
    // as fits, which is the same limit graph::Scaler::fitFrameAxis has.
    //
    // The VERTICAL axis is untouched by all of this: value fit, zoom and pan stay
    // this panel's, exactly as before.
    void setFrameWindow(const util::Range& aFrames);
    // Re-derives the shared window's mapping for a panel covering aViewportSize
    // (viewport pixels, the same size the drawing layer builds its model with), and
    // does nothing while no timeline owns the frame axis.
    //
    // WHY THIS EXISTS, and it is load-bearing: setFrameWindow is pushed by the gui
    // the moment the timeline's view or the driver changes - which can be BEFORE the
    // viewport has been laid out, so the panel's plot rect is still degenerate and
    // the mapping has no width to choose a rung from (measured in the running
    // application: the first pushes arrive with a plot of width -79). The editor's
    // own rect is otherwise written only on the cursor path, which the panel reaches
    // only when it OWNS an event - so a panel nobody has touched yet would keep the
    // frame axis it was born with and never become a view of the timeline at all.
    // The gui therefore calls this from the path that is about to DRAW the panel,
    // where the geometry is finally known; a resize re-derives it the same way (the
    // rung depends on the plot's width).
    //
    // Cheap and idempotent: it writes the rect, then returns at once when the range,
    // the width and the rung are the ones already applied (see mAppliedFrameWindow).
    void syncFrameWindowFor(const QSize& aViewportSize);
    // The two horizontal gestures, in the timeline's units: a pan by aFrameDelta
    // FRAMES (positive reveals LATER frames, i.e. the content moves left) and a
    // wheel notch about the frame the panel's cursor is over (aWheelDelta is Qt's
    // raw delta, aFrame the cursor's frame on the panel's own axis). Both are
    // optional and independent of setFrameWindow: with the window set but no
    // delegates, the panel still never moves its own X - the gestures simply have
    // nowhere to go.
    using FrameScrollFunc = std::function<void(double aFrameDelta)>;
    using FrameZoomFunc = std::function<void(int aWheelDelta, core::Frame aAnchorFrame)>;
    void setFrameGestureDelegates(FrameScrollFunc aScrollFrames, FrameZoomFunc aZoomFrames);
    // Detaches: the panel's frame axis is its own again (fit, zoom and pan work
    // as they did before a timeline was ever attached).
    void clearFrameAxis();
    // True while a timeline owns the frame axis.
    bool hasTimelineFrameAxis() const { return mFrameWindowShared; }

private:
    // The curve under a viewport point: which curve, which segment and - when
    // the hit is a tangent handle - which handle.
    struct Hit {
        Hit();
        bool hitCurve;
        bool hitHandle;
        int item;      // index into curves()
        int segment;   // segment index inside the curve
        int handle;    // 0 = leaving `from`, 1 = arriving at `to`
        core::Frame keyFrame; // the key hit when it is a key, not a handle
    };
    // What the graph draws: one curve per visible channel of one target, with
    // the target and channel the hit-test and the edit path need.
    struct Entry {
        core::ObjectNode* node;
        core::ObjectNode* line; // the layer that owns the key map (== node for now)
        graph::Channel channel;
        graph::Curve curve;
    };
    QVector<Entry> buildEntries() const;
    QRect currentPanelRect(const core::CameraInfo& aCamera) const;
    // Hands the project's fps and frame-range end to mScale's frame axis. The
    // two setters rebuild the tick ladder, so this is called from the paths that
    // already have a camera (they run on every cursor event).
    void syncFrameAxis();
    // Re-applies the frame window the timeline pushed, so a panel rect that moved
    // (a resize, the first cursor pass after a setTargets) still shows exactly
    // that range. A no-op while no timeline owns the frame axis. Idempotent by
    // construction - it derives the rung and the origin from the pushed range and
    // the current plot, and nothing else writes either while the axis is shared -
    // so calling it on every cursor and wheel event costs two comparisons and
    // cannot drift.
    void syncFrameWindow();
    // The panel's own frames-per-pixel: its frame ladder's rung. The unit a
    // delegated pan is measured in.
    double framesPerPixel() const { return static_cast<double>(mScale.frameScale().pixelsPerFrame()); }

    // Fits the value axis only when the shown curves CHANGED since the last fit,
    // by comparing the (node, channel) basis below; it also drops a focus that
    // left the shown set. This is the fit the non-const paths call: setTargets,
    // setVisible(true), setProject and updateCursor.
    // The paths that must NOT run it: a paint no longer exists in this class
    // (the panel is drawn from the model), and updateWheel is not one either:
    // zooming changes no curve, so the user's window has to survive it.
    void syncValueAxis();
    // True when (mFocusNode, mFocusChannel) is one of aItems, i.e. when the
    // focused channel is still something the panel draws.
    bool focusIsShown(const QVector<Entry>& aItems) const;
    // The (node, channel) list buildEntries() is drawing right now, in its own
    // order: the identity of a shown curve SET, which is the granularity the fit
    // is keyed to. Two selections that show the same (layer, channel) pairs are
    // the same basis even if a key moved between them - only a channel APPEARING
    // (which needs its first key, since buildEntries skips an empty map) or
    // leaving is a new one. A key added to an already-shown channel therefore
    // does not refit: the fit follows what is DRAWN, and a window the user set
    // outlives every edit that is not a change of the drawn set.
    static QVector<QPair<core::ObjectNode*, graph::Channel> > basisOf(const QVector<Entry>& aItems);
    // The fit itself, over entries the caller already built: the min/max of the
    // key values and of kFitSamplesPerSegment samples per segment (overshoot
    // included), over EVERY shown curve (the focus does not narrow it). Leaves
    // the window alone when nothing it covers has a key.
    void applyValueFit(const QVector<Entry>& aItems);

    // The index of the selection target naming (aNode, aFrame) of aChannel's
    // type, or -1. Pointer identity plus the type and the frame, which is the
    // triple the timeline names a key by - the same triple selectKey writes.
    int selectionIndexOf(const core::ObjectNode& aNode, const graph::Channel& aChannel, core::Frame aFrame) const;

    // --- marquee ---
    // Begins the marquee a PLAIN press in the plot opens on a point that is no key,
    // no handle and no curve's band: only the origin is recorded, so a press that
    // never moves selects nothing (an empty rectangle contains no key) - which is
    // the "clear the selection" gesture for that press.
    void beginMarquee(const QPoint& aPos);
    void updateMarquee(const QPoint& aPos);
    // Applies the marquee to the selection: every key whose drawn square's centre
    // falls inside the rectangle, replacing the selection (or added to it, when
    // mAddToSelection - the Shift state the press recorded). Then clears the
    // marquee state.
    void endMarquee();

    // --- middle-button pan ---
    // The middle drag moves the graph's window, not the canvas camera: the panel
    // is up, so the middle button belongs to it. The pan is a plain pixel offset
    // on both axes (Scaler::panFrame / panValue), so a pan cannot zoom and a zoom
    // cannot pan. aPos is the pointer the pan follows.
    void beginPan(const QPoint& aPos);
    void updatePan(const QPoint& aPos);
    void endPan();

    Hit hitTest(const QVector<Entry>& aItems, const graph::Scaler& aScale, const QPoint& aPos) const;

    // The entry whose curve band holds aPos, or -1 when none does. The band is
    // kCurveBandRadius wide, measured on the value axis between the point and
    // the value the curve evaluates at the point's own (integral) frame, and it
    // only exists where the curve is DRAWN: inside the plot, at a frame between
    // the curve's first and last key (the stroke goes no further). Nearest
    // wins, so two curves crossing cannot make the hit ambiguous.
    int curveBandItem(const QVector<Entry>& aItems, const graph::Scaler& aScale, const QPoint& aPos) const;

    void beginDrag(const Hit& aHit, const QVector<Entry>& aItems);
    // Arms the key move a press on a KEY opens: no command yet - the first
    // ACCEPTED step is what commits, so a press that never moves leaves the
    // history untouched. aPressPos is where the button went down; the frame and
    // the value there are the origins of the two pointer deltas the move follows.
    void beginKeyMove(const Hit& aHit, const QVector<Entry>& aItems, const QPoint& aPressPos);
    void updateDrag(const QPoint& aPos);
    // Steps the armed key move to aPos; true when the key moved (i.e. when the
    // caller has something to repaint). Both axes move - the frame by the
    // pointer's frame delta, the value by its value delta - unless the shift
    // modifier constrains the gesture to the dominant axis. A destination frame
    // another key of that type owns refuses the WHOLE step: nothing moves, the
    // history is untouched and the gesture stays open. Stops the gesture (with a
    // log line) when the armed key is no longer in the document.
    bool updateKeyMove(const QPoint& aPos);
    // Closes whatever pointer gesture is in flight (the easing gesture, the key
    // move, the marquee, the pan) and clears every flag it set. The commands
    // already pushed stay on the stack - they ARE the gesture's undo step - so
    // this only drops the handles to them.
    void endDrag();

    // Pushes the key the insert writes: a clone of the key playback is on at
    // aFrame - the payload and the easing the graph does not draw, i.e. the
    // easing of the very segment the insert splits - with every component the
    // graph draws overwritten by the value the curve of that component samples
    // at aFrame. Pushed by the timeline's own TimeLineUtil::pushNew*Key helper,
    // so the notifier, the macro, the new-key command and the key's frame are
    // that path's, exactly as if the timeline had added the key. False when
    // there is no key to clone from (a shown curve always has one, so this is
    // the guard for a caller that hands in an empty map).
    bool pushSeededKey(
        core::TimeLine& aLine, core::ObjectNode& aTarget, const graph::Channel& aChannel, core::Frame aFrame
    );

    core::Project* mProject;
    UILogger& mLogger;
    QVector<core::ObjectNode*> mTargets;
    QVector<QPair<core::ObjectNode*, graph::Channel> > mHiddenChannels;
    // The fallback selection, used while no timeline is attached, and the pointer
    // every selection method goes through (see setSelectionSource): it starts on
    // the fallback and is pointed at the attached timeline's own live set when
    // one is wired in. The pointer is never null, so no method has a second path
    // for "there is no selection", and there is only ever ONE selection - the
    // panel's own until the timeline's replaces it, never both.
    time::Selection mOwnSelection;
    time::Selection* mSelection;
    graph::Scaler mScale;
    bool mVisible;

    // The shared frame window (see setFrameWindow): the flag says a timeline owns
    // the panel's frame axis, the range is what it pushed (kept so the mapping can
    // be re-derived when the panel rect moves), and the two delegates are the
    // gestures that go back to the timeline. Cleared together by clearFrameAxis.
    //
    // mAppliedFrameWindow/mAppliedPlotWidth/mAppliedFramePpf are what the mapping
    // was last computed FROM, so syncFrameWindow() runs only when one of them
    // changed. That is not an optimisation: the mapping writes the scaler's origin
    // through its integer pan, which can land half a pixel off the exact value, and
    // re-deriving the origin from such a value is not a fixed point (it can
    // oscillate by a pixel). Computing it once per distinct (range, plot, rung) and
    // remembering that keeps every later call a no-op.
    bool mFrameWindowShared;
    util::Range mFrameWindow;
    util::Range mAppliedFrameWindow;
    int mAppliedPlotWidth;
    int mAppliedFramePpf;
    FrameScrollFunc mScrollFrames;
    FrameZoomFunc mZoomFrames;

    // The modifier state the widget fed in on the last event (see setModifiers).
    // Read by the press routing (Ctrl = insert) and by every step of a key move
    // (Shift = axis snap, Shift = add-to-selection on a marquee press).
    Qt::KeyboardModifiers mModifiers;

    // The sticky focus: the one shown channel the panel EMPHASISES. A nullptr
    // mFocusNode is "no focus" (mFocusChannel is then unused). Held by pointer
    // identity like mHiddenChannels - never dereferenced to decide anything but
    // identity - and dropped by syncValueAxis when the channel leaves the shown
    // set, with setProject clearing it through the same path as mTargets. It
    // moves NO window: see DELIBERATE FIT in the public section.
    core::ObjectNode* mFocusNode;
    graph::Channel mFocusChannel;

    // The (node, channel) basis the last value fit was computed from. Empty is a
    // real value ("nothing shown"), so a fresh editor fits on the first sync that
    // has data; a wheel zoom never touches it, which is what lets the user's
    // window survive every later cursor pass and paint. Compared by pointer
    // identity and never dereferenced, like mHiddenChannels: a deleted target can
    // leave an entry nothing reaches, and setProject clears the basis with the
    // rest of the old document's state.
    QVector<QPair<core::ObjectNode*, graph::Channel> > mValueBasis;

    // Drag state, and the flags that say WHICH drag. mDragging is true while ANY
    // of the panel's pointer gestures is in flight (a handle drag, a key move, a
    // marquee, a middle-button pan - a press that begins one sets it, and
    // ownsCursor keeps the cursor while it is true); mKeyMoveArmed, mMarqueeArmed
    // and mPanning then distinguish them. They are mutually exclusive by
    // construction: each press entry point clears the others, and endDrag clears
    // them all.
    //
    // Handle drag: the key whose easing is being edited, the segment that owns it
    // and which of its two handles.
    bool mDragging;
    bool mKeyMoveArmed;
    bool mMarqueeArmed;
    bool mPanning;
    const core::TimeKey* mDragKey;
    int mDragItem;
    int mDragSegment;
    int mDragHandle;
    cmnd::Gesture<cmnd::ModifiableAssign<util::Easing::Param> > mGesture;

    // The marquee: where the press went down and where the pointer is now, both
    // in viewport pixels. The rectangle is the two corners - the box the pointer
    // dragged, normalized when it is read (marqueeRect). mAddToSelection is the
    // shift state the press recorded, so a Shift-press that becomes a marquee
    // adds to the selection rather than replacing it.
    QPoint mMarqueeOrigin;
    QPoint mMarqueeCurrent;
    bool mAddToSelection;

    // The middle-button pan: the pointer position the pan last moved to, so each
    // step is a pixel delta (there is no press-time origin to drift from).
    QPoint mPanPointer;

    // The key-move gesture's armed key, in the timeline's own currency: the
    // layer that owns the key map, the node the TimeLineEvent names, the key
    // type, and the frame the key is at NOW (advanced by every accepted move).
    // Held instead of an index into buildEntries() so the move follows the key
    // the press hit rather than the position of a rebuilt list.
    core::ObjectNode* mMoveLine;
    core::ObjectNode* mMoveNode;
    core::TimeKeyType mMoveType;
    // The graphable COMPONENT of the key the drag moves (Photoshop's "X"), which
    // is the half of a graph::Channel that says which field of the key's Data the
    // value half of the gesture writes.
    int mMoveComponent;
    core::Frame mMoveFrame;
    // The pointer's frame as of the last accepted step, and the pointer pixels
    // the press went down at: the key follows the pointer's frame DELTA from the
    // last ACCEPTED frame, so a refused step does not accumulate, and the
    // dominant-axis rule under Shift is decided on the pixel displacement from
    // the press - which is the movement the user actually made.
    core::Frame mMovePointerFrame;
    QPoint mMovePressPos;
    // The key's own value as it was at the press: the value the drag's delta is
    // added to, so the whole gesture is one absolute function of the pointer and
    // cannot accumulate the key's own clamping. The delta itself is taken from
    // the pointer's own rows (valueAt(aPos.y()) - valueAt(mMovePressPos.y())),
    // never from a stored value, so it stays correct whatever the window does.
    float mMoveBaseValue;

    // The commands the gesture opened, or null before its first accepted step.
    // The stack owns them once pushed, so these are only handles, used to ask
    // whether a further step may rewrite them (isModifiable) - the same question
    // the easing drag's Gesture asks. A destination frame another key owns is
    // refused BEFORE either command is opened, so a refused drag never leaves an
    // empty history entry behind. The value command is held type-erased behind
    // its own cmnd::Stable, so the frame and the value halves of one gesture stay
    // one undo step without this class having to know a key's Data type.
    TimeLineUtil::MoveFrameOfKey* mMoveCommand;
    TimeLineUtil::AssignKeyValue* mValueCommand;

    // The FPS last handed to mScale.frameScale(). It is what the axis was BUILT
    // with, not document state the editor draws from: setFps() rebuilds the tick
    // ladder, so the seed is skipped while the project's fps is unchanged (the
    // frame end is compared through the scaler's own getter).
    int mAxisFps;
};

} // namespace ctrl

#endif // CTRL_GRAPHEDITOR_H
