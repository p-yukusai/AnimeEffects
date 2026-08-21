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

    void paintEvent(QPaintEvent *);
    void resizeEvent(QResizeEvent *);
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *);
    void mouseReleaseEvent(QMouseEvent *);
    void leaveEvent(QEvent *);

    signals:

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
    // widget's interior [POINT_RADIUS, height − POINT_RADIUS], so the
    // reachable under/over range is the constant kUnderOverRange (±0.5) in
    // both themes and at every widget size — the canvas is a fixed value
    // window, like the timeline's model viewport. The padding (equal to the
    // handle's visual radius) keeps a handle at the viewport boundary fully
    // visible; the value-space clamp, not the padding, defines the range.
    // The X axis maps the CSS-required [0, 1] domain onto the same inset.
    // Anchors sit at value (0,0) and (1,1); handles clamp in value space,
    // so their reach always equals the viewport.
    static constexpr qreal kUnderOverRange = 0.5; // overshoot past the 0..1 band
    static constexpr qreal kMinValue = -kUnderOverRange;
    static constexpr qreal kMaxValue = 1 + kUnderOverRange;
    static constexpr qreal kMinX = 0.0; // x stays a function domain:
    static constexpr qreal kMaxX = 1.0; // cubic-bezier requires x ∈ [0, 1]

    // Value ↔ pixel maps (affine). Y is inverted: y grows downward.
    static qreal valueToX(qreal aValue, qreal aWidth);   // [kMinX, kMaxX] → [POINT_RADIUS, w − POINT_RADIUS]
    static qreal xToValue(qreal aPixelX, qreal aWidth);  // inverse of valueToX
    static qreal valueToY(qreal aValue, qreal aHeight);  // [kMinValue, kMaxValue] → [h, 0]
    static qreal yToValue(qreal aPixelY, qreal aHeight); // inverse of valueToY
    static qreal clampX(qreal aValue);    // clamp into the x domain [0, 1]
    static qreal clampValue(qreal aValue); // clamp into the y viewport [−0.5, 1.5]

    QPointF         m_points[4];

    QPen        m_curvePen;

    bool        m_dragging;
    int         m_selectedPoint{};
    int         m_hoveredPoint = -1;
};

#endif // BEZIERCURVEEDITOR_H
