#ifndef BEZIERCURVEEDITOR_H
#define BEZIERCURVEEDITOR_H

#include "util/Easing.h"
#include <QPen>
#include <QDoubleSpinBox>
#include <QWidget>

class BezierCurveEditor : public QWidget
{
    Q_OBJECT
public:
    explicit BezierCurveEditor(QWidget *parent = nullptr, util::Easing::CubicBezier* cubicBezier = nullptr, QVector<QDoubleSpinBox*> spins = {}, float* pro = nullptr);
    ~BezierCurveEditor();
    util::Easing::CubicBezier* bezier;
    QVector<QDoubleSpinBox*> spinBoxes;
    float* progress;

    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void hideEvent(QHideEvent*) override;

    signals:
    // Emitted when a tangent-handle drag starts (a handle is grabbed). The
    // owner (the easing picker popup) uses it to snapshot the pre-drag curve so
    // the whole drag folds into one undo-history entry, and to open the one
    // rewriteable command its steps are streamed into.
    void dragStarted();
    // One STEP of a tangent-handle drag: the grabbed control point moved and
    // `bezier` already holds the new curve (commitPoints ran). Emitted on every
    // pointer move of the drag, so the owner can write/report each step at once
    // instead of waiting for the release; the owner still folds the whole drag
    // into ONE undo entry (dragStarted/editingFinished bracket it).
    void curveDragged();
    // Emitted when a tangent-handle drag ends. The owner uses it to close the
    // drag's own history entry (rather than one per pointer move); the streamed
    // command stays open until the owner's session ends.
    void editingFinished();

    public slots:

    public:
    enum PointIndices {
        StartPoint = 0,
        ControlPoint1 = 1,
        ControlPoint2 = 2,
        EndPoint = 3
    };

    //Functions
private:
    static qreal distance(const QPointF a, const QPointF b) {
        const QPointF diff = a - b;
        return sqrt(diff.x()*diff.x() + diff.y()*diff.y());
    }

    // Which tangent handle (ControlPoint1/2) is under the cursor, or -1.
    int handleAt(const QPoint& aPos) const;

public:
    const int NUM_POINTS = 4;
    static constexpr qreal POINT_RADIUS = 6.0; // visual radius of the tangent handles
    static constexpr qreal HIT_RADIUS = 12.0;  // grab radius: generous like timeline keys

    // Fixed value viewport. The Y axis maps [kMinValue, kMaxValue] onto the
    // full widget height and the X axis maps the CSS-required [0, 1] domain
    // onto the full width — the curve and axis guides reach the canvas edges,
    // exactly like the reference mock's SVG (viewBox 0 0 100 100,
    // preserveAspectRatio none, no padding). The reachable under/over range is
    // the constant kUnderOverRange in both themes and at every widget size; the
    // value-space clamp, not any pixel inset, defines the range. Anchors sit at
    // value (0,0) and (1,1); handles clamp in value space, so their reach
    // always equals the viewport. POINT_RADIUS is only the handle's drawn
    // radius, not a canvas inset.
    // The mock's bezier stage maps the value axis via (1.5 - v)/2 — i.e. the
    // handle range is ±0.5 ([-0.5, 1.5]), SAME axis as the animation gauge.
    static constexpr qreal kUnderOverRange = 0.5; // overshoot past the 0..1 band
    static constexpr qreal kMinValue = -kUnderOverRange;
    static constexpr qreal kMaxValue = 1 + kUnderOverRange;
    static constexpr qreal kMinX = 0.0; // x stays a function domain:
    static constexpr qreal kMaxX = 1.0; // cubic-bezier requires x ∈ [0, 1]

    // Value ↔ pixel maps (affine). Y is inverted: y grows downward.
    static qreal valueToX(qreal aValue, qreal aWidth);   // [kMinX, kMaxX] → [0, w]
    static qreal xToValue(qreal aPixelX, qreal aWidth);  // inverse of valueToX
    static qreal valueToY(qreal aValue, qreal aHeight);  // [kMinValue, kMaxValue] → [h, 0]
    static qreal yToValue(qreal aPixelY, qreal aHeight); // inverse of valueToY
    static qreal clampX(qreal aValue);    // clamp into the x domain [0, 1]
    static qreal clampValue(qreal aValue); // clamp into the y viewport [−0.5, 1.5]

    // Recompute the tangent-handle points from the current bezier. Called when
    // the bezier changes programmatically (spin edit / paste) so the paint
    // event's write-back doesn't revert it to a stale control point. NOT used
    // during a drag (the drag owns m_points).
    void syncFromBezier();
    // Clamp the two control points in value space and write them into `bezier`.
    // The paint event calls it before drawing; a DRAG calls it on every move, so
    // the model follows the pointer rather than the next repaint (an owner that
    // reads the draft at release - the picker's side-freeing rule - must see the
    // dragged value even when no paint happened in between).
    void commitPoints();

    QPointF         m_points[4];

    QPen        m_curvePen;

    bool        m_dragging;
    int         m_selectedPoint{};
    int         m_hoveredPoint = -1;
};

#endif // BEZIERCURVEEDITOR_H
