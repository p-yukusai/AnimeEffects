#ifndef GUI_PROP_EASINGPICKWIDGET_H
#define GUI_PROP_EASINGPICKWIDGET_H

#include <QFrame>
#include <QObject>
#include <QWidget>
#include <QToolButton>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QVector>
#include <QColor>
#include <QTimer>
#include <QPainter>
#include <QRect>
#include "util/Easing.h"

namespace gui { class AppStyle; } // fwd (slider proxy style)

class QGridLayout;
class QHBoxLayout;
class QVBoxLayout;
class FlowLayout;
class QDoubleSpinBox;
class QComboBox;
class BezierCurveEditor; // global ns (see bezierCurveEditor.h)

namespace gui {
namespace prop {

// ---------------------------------------------------------------------------
// CurveGlyph — paints an easing curve exactly the way the reference mock's
// SVG does. The reference draws the curve into a virtual 100x100 viewBox
// (curveD in mock.js) with preserveAspectRatio="none" and non-scaling
// stroke; this widget reproduces that by computing the path in the same
// 100x100 space and scaling the coordinates to the widget size, so the
// shape matches the mock pixel-for-pixel.
// ---------------------------------------------------------------------------
class BlendControl; // plain self-driving blend slider (define in the .cpp)
// ---------------------------------------------------------------------------
class CurveGlyph : public QWidget {
    Q_OBJECT
public:
    explicit CurveGlyph(QWidget* aParent = nullptr);
    void setParam(const util::Easing::Param& aParam);
    // The neighbouring keys a derived (Auto/AutoClamped) side is shaped by, plus
    // the TO key's incoming mode (AutoContext::inMode). Only a Custom param with
    // a derived side reads it; an empty context is the neutral (straight) curve.
    // The picker's Auto PRESET tile is the one caller that hands in a FIXED pair
    // rather than the document's keys, so the catalog image does not change with
    // the selection (see autoPreviewContext below).
    void setAutoContext(const util::Easing::AutoContext& aContext);
    void setGlyphColor(const QColor& aColor);
    void setStrokeWidth(double aWidth); // default 1.5
    // Also draw the DERIVED control points (two dots joined to the segment's
    // endpoints by short stems) beside the curve, so the glyph reads as "a
    // bezier with automatic handles" rather than as another function plot. Off
    // by default: the catalog tile and the header button opt in, while a preview
    // (the blend thumbnails, the timeline chip) stays a bare curve. A param with
    // no derived side ignores it.
    void setAutoHandlesVisible(bool aVisible);

    // What the glyph is drawn from, read back for the layout gate: the catalog is
    // asserted on the SHAPE its own state derives (a tile that quietly changed its
    // context would still "differ from Linear" while drawing the wrong curve).
    const util::Easing::Param& param() const { return mParam; }
    const util::Easing::AutoContext& autoContext() const { return mAuto; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    util::Easing::Param mParam;
    util::Easing::AutoContext mAuto;
    QColor mColor;
    double mStrokeWidth = 1.5;
    bool mAutoHandles = false;
};

// ---------------------------------------------------------------------------
// paintEasingCurve — paint an easing curve into aRect, reproducing the mock's
// SVG (curveD in mock.js): the path is computed in a 100x100 viewBox with
// pad=2 and value axis [kVMIN,kVMAX], then non-uniformly scaled into aRect
// (preserveAspectRatio="none"). Shared by CurveGlyph and the EasingChip so the
// curve looks identical wherever it is drawn.
//
// aAutoHandles asks for the Auto icon rather than a plot: for a Custom param
// with a derived side it also draws the two control points the mode derives, as
// small dots joined to the segment's endpoints by short stems. The dots/stems are sized from aRect
// (not from fixed pixels), so the same call reads at the 28px catalog tile and at
// a 16-18px header button. It is a trailing defaulted flag so every existing
// caller - the EasingChip, the blend thumbnails - keeps painting byte-for-byte
// the same curve, and only a catalog/icon caller has to say what it wants.
// ---------------------------------------------------------------------------
void paintEasingCurve(QPainter& aPainter, const QRectF& aRect,
                      const util::Easing::Param& aParam,
                      const QColor& aColor, qreal aStrokeWidth = 1.5,
                      const util::Easing::AutoContext* aAuto = nullptr,
                      bool aAutoHandles = false);

// THE canonical neighbour pair a catalog/glyph image of the Auto mode draws with.
// A tile and the header's quick-easing button are pictures of the easing, not
// previews of the selected keys (the picker hands them no document), and an
// automatic pair with the real empty context collapses to the identities - the
// straight line. This pair is one segment wide (the glyph's d = 1), symmetric,
// and chosen so the central differences at BOTH ends of the drawn segment are
// zero: the key before holds the segment's END value and the key after its START
// value, so the real derivation produces the canonical flat-ended
// ease-in-out - outgoing control point (1/3, 0), incoming (2/3, 1). Both sides
// are kAutoPresetHandleMode (the arriving mode lives in the TO key), because the
// preset is the CLAMPED automatic pair. There is exactly ONE definition, so the
// picker's Auto tile and the header's Auto button cannot paint two different
// curves.
util::Easing::AutoContext autoPreviewContext();

// ---------------------------------------------------------------------------
// EasingPickWidget — the locked "shape-first" easing picker (variant A).
// A self-contained 300px popover: a direction segmented control, a motion
// pipeline bar, the curve-tile catalog, and a curve<->linear blend slider.
// Custom mode swaps the tile catalog for a bezier edit panel. Mirrors the
// reference DOM so the layout can be verified by geometry (each sub-widget
// carries a stable objectName).
//
// Live-apply model: every edit is committed to the model (no Apply button).
// The signals separate the concerns:
//   changed(p)     — the draft changed (fires on every user edit, including each
//                    step of a continuous drag). The owner uses this to update
//                    any live/derived view (the chip that opens the menu). It
//                    does NOT commit to the model.
//   liveEditStarted()/liveEdited(p)/liveEditEnded()
//                  — ONE CONTINUOUS SESSION whose steps are applied to the model
//                    as they happen: the bezier handle drag (every pointer move)
//                    and a spinbox's typing/clicks (each keystroke is a step of
//                    the same gesture). Begun on the first step, one liveEdited
//                    per step, ended when the POPUP closes. The owner keeps ONE
//                    rewriteable command open across the session, so the document,
//                    the graph and every view update per step while the whole
//                    session is a single undo entry (the blend slider's
//                    close-time commit is the same one-entry-per-open rule).
//   applied(p)     — a DISCRETE edit is committed to the model (one undo step):
//                    tile / seg / custom / mode combo / paste, and the close-time
//                    commit of a gesture the picker does not stream (the blend
//                    slider, whose ticks stay a draft until close).
//   dismissed()    — the user asked to dismiss the popup (the close ✕).
//
// The widget owns its sub-widgets; the geometry harness names each part with
// the mock's selector (e.g. "seg", "seg-r-0", "tile-0", "trow", "pipeline",
// "blend-wrap").
// ---------------------------------------------------------------------------
class EasingPickWidget : public QFrame {
    Q_OBJECT
public:
    // Known content-heights (logical px) for the two layout modes. The popup
    // is a fixed width (300) and its height is deterministic per mode, so we
    // pin it rather than measuring a racy just-shown geometry(). The tallest
    // (Custom) is what EasingItem reserves so a mode switch never relocates the
    // popup. Logical px scale with DPI automatically (Qt Widgets has no rem).
    // The footer (Cancel/Apply) was removed, so each is 48px shorter than the
    // pre-footer value (the root layout had header 40 + body + footer 48, all
    // between 1px root margins; dropping the footer removes exactly its 48px).
    static constexpr int kNormalHeight = 202;  // In/Out/In-Out (and Linear/Cubic/etc.)
    // Custom: the bezier stage, the x1/y1/x2/y2 field row, the per-side mode
    // field row and the actions. The mode row is a labelled field (12px label +
    // 2px + 24px value = 38) exactly like the spin row, so it is 14px taller
    // than the old label-beside-combo row the constant was first measured with.
    static constexpr int kCustomHeight = 488;

    explicit EasingPickWidget(const util::Easing::Param& aInitial, QWidget* aParent = nullptr);
    ~EasingPickWidget() override;

    util::Easing::Param value() const { return mDraft; }
    void setValue(const util::Easing::Param& aParam);

    // THE EXTERNAL RE-READ: the document's value for the thing this popup SHOWS, pushed
    // by its owner whenever the document or the selection changes while the popup is
    // open - the graph's handle drag, a mode change, another view's edit. The popup is
    // a VIEW of one key's easing, so it must follow the key, not only lead it.
    //
    // NOTHING IS EMITTED and nothing is committed: this is a re-read, not an edit.
    // FALSE - and the draft left exactly as it was - in three cases:
    //   * the popup is not on screen (there is nothing to keep in step), or
    //   * A GESTURE IS IN PROGRESS IN THE POPUP (a bezier handle drag, a spin being
    //     typed, the blend slider being dragged): the value the user is dragging WINS
    //     until the gesture ends, and the owner pushes again then, and
    //   * the incoming state already is the draft's (idempotent, so a notification
    //     storm costs one compare).
    bool setShownValue(const util::Easing::Param& aParam, const util::Easing::AutoContext& aAutoContext);
    // True while a gesture owns the draft (see setShownValue). The owner uses it to skip
    // the re-read entirely; setShownValue checks it anyway, so a caller that forgets
    // cannot clobber a drag.
    bool isEditing() const { return mGestureActive; }

    // The neighbouring keys an Auto draft is shaped by. The widget cannot know
    // them (they live in the timeline), so its owner hands them in whenever the
    // edited key or its neighbours change; an empty context is correct for every
    // non-Auto draft. They drive the blend thumbnails and the motion preview, so
    // the popup previews the curve the document would play rather than a shape
    // that belongs to no key. The Auto TILE is deliberately NOT one of them: a
    // tile is a catalog image of the easing, so it always draws the canonical
    // automatic-handle pair (see kTileOrder's neighbour in the .cpp).
    void setAutoContext(const util::Easing::AutoContext& aContext);

    // Segment / tile interactions (public so a harness can drive them).
    void setType(util::Easing::Type aType);
    void setRange(util::Easing::Range aRange);
    void setWeight(float aWeight);
    void setCustom();
    // The Auto preset: Type_Custom with BOTH handle sides on
    // Easing::kAutoPresetHandleMode (AutoClamped). Automatic handles are per-side
    // modes now, so this writes modes (and the Custom curve that exposes them),
    // never a retired easing type. This is what the catalog's Auto tile clicks,
    // and it is the CLAMPED behaviour the retired Auto type had: unclamped
    // HandleMode_Auto overshoots by construction and stays a deliberate
    // per-side choice in the mode combos.
    //
    // This is the DRAFT only: it names the pair the preset stores. The ACTION
    // applies to the ONE SEGMENT AFTER EACH SELECTED KEY, so the picking widget's
    // commit routes these modes to the selection-level Auto action, which writes
    // the key's type and outgoing side plus the next key's incoming side and
    // leaves the arriving segment alone; a last (or the only) key instead gets
    // its OWN pair written - type Custom and both sides - because it has no
    // segment after it yet (see
    // ctrl::TimeLineEditor::assignSelectionAutoHandles).
    void setAutoHandlesPreset();
    // One side's handle mode (Blender's five). aIncoming selects the TO key's
    // arriving side (the param's handleIn); the other writes handleOut. The
    // Custom panel exposes both as small combos, and they are public so a
    // harness can drive them.
    void setHandleMode(bool aIncoming, util::Easing::HandleMode aMode);

    // Session undo/redo (per-step, driven by Ctrl+Z / Ctrl+Shift+Z inside the
    // popup). Discrete edits have already been committed (see applied); a drag's
    // final value still commits on close. These walk the popup's own draft
    // history so the user can step back individual edits before then.
    void undo();
    void redo();


    // Hide every label/button/spinbox glyph by painting its text fully
    // transparent (the box geometry and all graphics stay identical), so a
    // harness can screenshot the picker's pure layout. Off by default.
    void setHideText(bool aHide);
    bool hideText() const { return mHideText; }

    int maxContentHeight();
    int liveCustomHeight() const;
    bool verifyMaxHeightMatchesLive(QString* aMsg = nullptr) const;

    // Record the global top-left this popup should stay at. The picker is a
    // Qt::Popup (top-level, no layout), so on a mode switch it re-fits its
    // height in-place — and some platforms/compositors nudge a growing popup
    // to keep it on-screen. Re-asserting the anchor after every refit makes
    // the position deterministic and immune to that nudge.
    void setAnchorPoint(const QPoint& aPoint);
    QPoint anchorPoint() const { return mAnchorPoint; }

    // Open the picker anchored AT aGlobalAnchor - the bottom-left of the control
    // it belongs to (the header's easing chip) or the pointer a key menu was
    // opened at - showing aShown and shaping its live previews (the blend
    // thumbnails and the motion bar) with aAutoContext.
    // aAnchorWidget is the widget the anchor belongs to: it supplies the screen
    // the popup is clamped into and the window it is made a transient child of
    // (both needed for a stable position; see the definition).
    //
    // The placement lives here rather than in each caller because there is ONE
    // picker: it is opened from two anchors, and a second copy of the realize/
    // clamp/anchor dance would be a second popover's worth of drift.
    void popUpAt(const QPoint& aGlobalAnchor, const QWidget* aAnchorWidget,
                 const util::Easing::Param& aShown,
                 const util::Easing::AutoContext& aAutoContext);

signals:
    // Live draft change (fires on every user edit, including each step of a
    // continuous gesture, and on session undo/redo). The owner uses it to update
    // derived/display views (the chip) live; it does not commit to the model.
    void changed(util::Easing::Param aParam);
    // A CONTINUOUS SESSION began whose steps are streamed to the model: a bezier
    // handle drag (at the press) or a spinbox's first change. The owner opens ONE
    // rewriteable command here - see TimeLineEditorWidget::beginEasingEdit - and
    // keeps it open until liveEditEnded, so every step lands in the document at
    // once while the whole session stays one undo entry.
    void liveEditStarted();
    // One step of that session, the draft already holding it. The owner rewrites
    // the open command; nothing new is pushed, so this is NOT a second undo step.
    void liveEdited(util::Easing::Param aParam);
    // The session ended - the popup went away (one entry per open, the same rule
    // the blend slider's close-time commit follows). The owner closes its command.
    // Idempotent.
    void liveEditEnded();
    // Commit to the model as ONE undo step. A discrete edit (tile / seg / custom /
    // mode combo / paste) fires it at once; the blend slider's ticks stay a draft
    // and fire it once on close. A streamed gesture (liveEditStarted/…) never
    // fires it: its command already holds the final value.
    void applied(util::Easing::Param aParam);
    void dismissed();
    // The popup went away - the ✕, an outside click or Escape - after any commit
    // the close carried (applied fires first). The owner of a TEMPORARY selection
    // (the key menu's right-click) ends it here: the selection the popup edits must
    // outlive the open, not the menu that led to it.
    void closed();

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void moveEvent(QMoveEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;
    bool eventFilter(QObject* aWatched, QEvent* aEvent) override;
    // Qt's popup path re-targets a move/release to the popup once the cursor
    // leaves the popup rect (QWidgetWindow::handleMouseEvent), so it would drop
    // a drag on a child. Forward those to the blend slider so drags keep
    // tracking outside (see the definition).
    void mouseMoveEvent(QMouseEvent* aEvent) override;
    void mouseReleaseEvent(QMouseEvent* aEvent) override;

private:
    QWidget* buildHeader();
    QWidget* buildBody();
    QWidget* buildSeg(util::Easing::Range aRange);
    QWidget* buildPipeline();
    QWidget* buildTrow();
    QWidget* buildBlend();
    QWidget* buildBezierPanel();

    void refresh();
    void refreshTiles();
    void refreshSeg();
    void updateBlend();
    void renderBezier();
    void animStep();
    void refit();
    void onBezierSpin(int aIndex, double aValue);
    // One pointer move of a tangent-handle drag: the editor already wrote the new
    // curve into the draft, so free the dragged side if it was derived, report the
    // step live and stream it to the owner's session. The press opens the session;
    // the popup's close ends it (a release only closes the gesture's own history).
    void onBezierDragStep();
    // Open/close the streamed session (signals + the popup's draft history). The
    // model's ONE command is the owner's (liveEditStarted/liveEditEnded); these
    // also fold the popup's own draft history into one entry per gesture, so a
    // typed number is not one entry per digit. endLiveEdit runs when the popup
    // goes away, which is when the owner's command is closed.
    void beginLiveEdit();
    void endLiveEdit();
    // The header/mode combos, re-read from the draft without the heavy refresh()
    // (no height re-fit, no preview restart): a live drag frees a side per step.
    void refreshHandleModes();
    // A control point was edited (a spin, a paste or a canvas drag): FREE the side
    // it belongs to if that side was DERIVED. x1/y1 is this param's handleOut (the
    // FROM key's outgoing side); x2/y2 is its handleIn (the TO key's arriving one).
    // This is the graph drag's own rule - a derived side has no stored value, so
    // writing one must make the side read it - and every bezier edit goes through
    // it, otherwise a drag/spin/paste of an Auto or Vector side would change a
    // stored value nothing reads. A stored side (Free/Aligned) keeps its mode: its
    // stored value IS what it reads, so the edit already lands. TRUE when a mode
    // was freed.
    bool freeEditedSides(const util::Easing::CubicBezier& aBefore);
    void applyAppearance();

    // A discrete edit is committed to the model NOW (emit applied once). A
    // STREAMED gesture (bezier handle drag, typed spin) does not call this: its
    // steps already wrote the model through the owner's open command, so close has
    // nothing left to commit. The blend slider is the one drag that does not
    // stream; its final draft is committed by hideEvent, one undo step per drag.
    void commitDiscrete();

    // Session undo/redo helpers. Each user gesture (a discrete click, or one
    // continuous drag) records ONE history entry so the popup can walk back
    // individual edits.
    void pushState(const util::Easing::Param& aState); // push a draft to step back to
    void beginGesture();                               // snapshot draft for a drag
    void endGesture();                                 // coalesce a drag into one step

    QToolButton* makeTileAt(int aIndex, util::Easing::Type aType, bool aAutoPreset);

    // Re-deliver a popup-level mouse move/release to the blend slider while it
    // is mid-drag, so the drag keeps tracking after the cursor leaves the popup
    // (see mouseMoveEvent/mouseReleaseEvent).
    void forwardBlendCursorEvent(QMouseEvent* aEvent);

    util::Easing::Param mDraft;     // live draft (edited in the popup)
    util::Easing::Param mApplied;   // baseline: last value committed / loaded
    util::Easing::AutoContext mAuto; // neighbours + the TO mode a derived draft is shaped by
    QVector<util::Easing::Param> mUndo; // session states to step back to
    QVector<util::Easing::Param> mRedo; // session states to re-do forward to
    util::Easing::Param mGestureStart;  // draft snapshot at drag start
    bool mGestureActive = false;        // a continuous drag is in progress
    // A streamed gesture is open (liveEditStarted emitted, liveEditEnded not yet).
    // Separate from mGestureActive: the blend slider is a gesture the popup does
    // NOT stream (its ticks stay a draft until close), so only the bezier drag and
    // the spin boxes set this.
    bool mLiveEditActive = false;
    // The bezier panel is shown for a Custom draft - EXCEPT while that draft is
    // exactly the Auto preset, which is a CATALOG state (its tile must stay
    // visible and checked). This flag is an explicit "I clicked Custom", so a user
    // on the preset can still reach the bezier controls without changing the pair.
    bool mForceBezierPanel = false;
    bool mHideText = false;
    QPoint mAnchorPoint; // global top-left the popup is anchored to
    bool mHasAnchor = false;

    QLabel* mTitle = nullptr;
    QWidget* mHead = nullptr; // header chrome (fixed kHeaderH)

    // seg
    QGridLayout* mSegLayout = nullptr;
    QVector<QToolButton*> mSegBtns; // [In, Out, InOut, Custom]

    // pipeline
    QWidget* mPipeline = nullptr;
    QWidget* mPipelineFill = nullptr;
    QTimer* mAnimTimer = nullptr;
    double mAnimT = 0.0;

    // trow
    QWidget* mTrow = nullptr;
    QVector<QToolButton*> mTileBtns; // in display order

    // blend
    BlendControl* mBlendInput = nullptr;
    AppStyle* mSliderStyle = nullptr; // proxy that draws the blend slider
    QLabel* mBlendVal = nullptr;
    CurveGlyph* mBlendLinear = nullptr;
    CurveGlyph* mBlendCurve = nullptr;

    // body / bezier
    QWidget* mBody = nullptr;
    QWidget* mNormalPanel = nullptr;   // seg shared; normal = trow + blend
    QWidget* mBezierPanel = nullptr;
    BezierCurveEditor* mBezierEditor = nullptr;
    QWidget* mBezierGauge = nullptr; // PreviewGauge: animation preview beside the graph
    QVector<QDoubleSpinBox*> mBezierSpins;
    // The two per-side handle-mode combos (Blender's Free/Aligned/Vector/Auto/
    // Auto Clamped). They live in the Custom panel because a mode shapes a
    // bezier and nothing else.
    QComboBox* mHandleIn = nullptr;
    QComboBox* mHandleOut = nullptr;
    float mBezierProgress = 0.0f;
    int mRefitAttempts = 0; // bounded retry for the popup height re-fit
};

} // namespace prop
} // namespace gui

#endif // GUI_PROP_EASINGPICKWIDGET_H
