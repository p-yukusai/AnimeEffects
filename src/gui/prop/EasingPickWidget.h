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
    void setGlyphColor(const QColor& aColor);
    void setStrokeWidth(double aWidth); // default 1.5

protected:
    void paintEvent(QPaintEvent*) override;

private:
    util::Easing::Param mParam;
    QColor mColor;
    double mStrokeWidth = 1.5;
};

// ---------------------------------------------------------------------------
// paintEasingCurve — paint an easing curve into aRect, reproducing the mock's
// SVG (curveD in mock.js): the path is computed in a 100x100 viewBox with
// pad=2 and value axis [kVMIN,kVMAX], then non-uniformly scaled into aRect
// (preserveAspectRatio="none"). Shared by CurveGlyph and the EasingChip so the
// curve looks identical wherever it is drawn.
// ---------------------------------------------------------------------------
void paintEasingCurve(QPainter& aPainter, const QRectF& aRect,
                      const util::Easing::Param& aParam,
                      const QColor& aColor, qreal aStrokeWidth = 1.5);

// ---------------------------------------------------------------------------
// EasingPickWidget — the locked "shape-first" easing picker (variant A).
// A self-contained 300px popover: a direction segmented control, a motion
// pipeline bar, the curve-tile catalog, and a curve<->linear blend slider.
// Custom mode swaps the tile catalog for a bezier edit panel. Mirrors the
// reference DOM so the layout can be verified by geometry (each sub-widget
// carries a stable objectName).
//
// Live-apply model: every edit is committed to the model (no Apply button).
// Two signals separate the concerns:
//   changed(p)  — the draft changed (fires on every user edit, including each
//                 tick of a continuous drag). The owner uses this to update
//                 any live/derived view (the chip that opens the menu). It
//                 does NOT commit to the model.
//   applied(p)  — an edit is committed to the model (one undo step). Fires
//                 immediately on a discrete edit (tile / seg / custom / bezier
//                 spin / paste) and once when a continuous gesture ends
//                 (blend slider release, bezier handle drop). Keeping commits
//                 at gesture granularity avoids flooding the undo stack with a
//                 per-tick command per drag.
//   dismissed() — the user asked to dismiss the popup (the close ✕).
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
    static constexpr int kCustomHeight = 442;  // Custom

    explicit EasingPickWidget(const util::Easing::Param& aInitial, QWidget* aParent = nullptr);
    ~EasingPickWidget() override;

    util::Easing::Param value() const { return mDraft; }
    void setValue(const util::Easing::Param& aParam);

    // Segment / tile interactions (public so a harness can drive them).
    void setType(util::Easing::Type aType);
    void setRange(util::Easing::Range aRange);
    void setWeight(float aWeight);
    void setCustom();

    // Session undo/redo (per-step, driven by Ctrl+Z / Ctrl+Shift+Z inside the
    // popup). The whole session still commits to the model as ONE undo step on
    // close; these let the user walk back individual edits (e.g. a bezier
    // handle drag) before then.
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

signals:
    // Live draft change (fires on every user edit, including each tick of a
    // continuous drag, and on session undo/redo). The owner uses it to update
    // derived/display views (the chip) live; it does not commit to the model.
    void changed(util::Easing::Param aParam);
    // Commit to the model as ONE undo step. Fires once when the popup closes
    // (dismiss ✕ or an outside click), only if the draft changed since open.
    void applied(util::Easing::Param aParam);
    void dismissed();

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
    void applyAppearance();

    // Session undo/redo helpers. Each user gesture (a discrete click, or one
    // continuous drag) records ONE history entry so the popup can walk back
    // individual edits; the whole session still commits to the model once, on
    // close.
    void pushState(const util::Easing::Param& aState); // push a draft to step back to
    void beginGesture();                               // snapshot draft for a drag
    void endGesture();                                 // coalesce a drag into one step

    QToolButton* makeTileAt(int aIndex, util::Easing::Type aType);

    // Re-deliver a popup-level mouse move/release to the blend slider while it
    // is mid-drag, so the drag keeps tracking after the cursor leaves the popup
    // (see mouseMoveEvent/mouseReleaseEvent).
    void forwardBlendCursorEvent(QMouseEvent* aEvent);

    util::Easing::Param mDraft;     // live draft (edited in the popup)
    util::Easing::Param mApplied;   // baseline: last value committed / loaded
    QVector<util::Easing::Param> mUndo; // session states to step back to
    QVector<util::Easing::Param> mRedo; // session states to re-do forward to
    util::Easing::Param mGestureStart;  // draft snapshot at drag start
    bool mGestureActive = false;        // a continuous drag is in progress
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
    float mBezierProgress = 0.0f;
    int mRefitAttempts = 0; // bounded retry for the popup height re-fit
};

} // namespace prop
} // namespace gui

#endif // GUI_PROP_EASINGPICKWIDGET_H
