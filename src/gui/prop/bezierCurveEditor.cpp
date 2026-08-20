#include "gui/prop/bezierCurveEditor.h"
#include <QDoubleSpinBox>
#include <QPainterPath>
#include <QPainter>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <utility>
#include "gui/theme/Colors.h"

// Value ↔ pixel maps: the fixed viewport [kMinValue, kMaxValue] ×
// [kMinX, kMaxX] onto the widget rect. Affine, so clamping the value and
// clamping the pixel are the same operation; the value-space clamp is the
// spec, kept explicit so the reachable range is the constant viewport.
qreal BezierCurveEditor::valueToX(const qreal aValue, const qreal aWidth) {
    return POINT_RADIUS + (aValue - kMinX) / (kMaxX - kMinX) * (aWidth - 2 * POINT_RADIUS);
}
qreal BezierCurveEditor::xToValue(const qreal aPixelX, const qreal aWidth) {
    return kMinX + (aPixelX - POINT_RADIUS) / (aWidth - 2 * POINT_RADIUS) * (kMaxX - kMinX);
}
qreal BezierCurveEditor::valueToY(const qreal aValue, const qreal aHeight) {
    return (kMaxValue - aValue) / (kMaxValue - kMinValue) * aHeight;
}
qreal BezierCurveEditor::yToValue(const qreal aPixelY, const qreal aHeight) {
    return kMaxValue - aPixelY / aHeight * (kMaxValue - kMinValue);
}
qreal BezierCurveEditor::clampX(const qreal aValue) {
    return std::clamp(aValue, kMinX, kMaxX);
}
qreal BezierCurveEditor::clampValue(const qreal aValue) {
    return std::clamp(aValue, kMinValue, kMaxValue);
}

BezierCurveEditor::BezierCurveEditor(QWidget *parent, util::Easing::CubicBezier* cubicBezier, QVector<QDoubleSpinBox*> spins, float* pro):
    QWidget(parent), m_dragging(false) {
    const theme::Colors c = theme::Colors::current();
    m_curvePen.setColor(c.text);
    m_curvePen.setWidth(2);
    bezier = cubicBezier;
    progress = pro;

    spinBoxes = std::move(spins);
    // tangent-handle hover feedback needs move events without a button down
    setMouseTracking(true);
}

BezierCurveEditor::~BezierCurveEditor() = default;

void BezierCurveEditor::mousePressEvent(QMouseEvent *event)
{
    for(int i = 0; i < NUM_POINTS; i++) {
        if (i != StartPoint && i != EndPoint) {
            if(distance(m_points[i], event->pos()) <= HIT_RADIUS) {
                m_selectedPoint = i;
                m_hoveredPoint = i;
                m_dragging = true;
                setCursor(Qt::PointingHandCursor);
                break;
            }
        }
    }
}

void BezierCurveEditor::mouseMoveEvent(QMouseEvent *event)
{
    // hover follows the cursor; while dragging it stays on the grabbed
    // handle (which may sit past the clamp while the cursor drifts)
    const int hovered = m_dragging ? m_selectedPoint : handleAt(event->pos());
    if (hovered != m_hoveredPoint) {
        m_hoveredPoint = hovered;
        setCursor(hovered >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
    if(m_dragging) {
        m_points[m_selectedPoint] = event->pos();
        // Editing restarts the preview from the new curve; hover alone must
        // not freeze it (mouse tracking fires move events without buttons).
        *progress = 0;
    }
    update();
}

void BezierCurveEditor::leaveEvent(QEvent *)
{
    if (m_hoveredPoint >= 0) {
        m_hoveredPoint = -1;
        setCursor(Qt::ArrowCursor);
        update();
    }
}

int BezierCurveEditor::handleAt(const QPoint& aPos) const {
    for (int i = 0; i < NUM_POINTS; i++) {
        if (i == StartPoint || i == EndPoint) continue;
        if (distance(m_points[i], aPos) <= HIT_RADIUS) return i;
    }
    return -1;
}

void BezierCurveEditor::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragging = false;
    // A drag may end with the cursor far outside the grabbed handle (the
    // handle clamps to the canvas while the cursor keeps going); the hover
    // state and cursor were pinned to the grabbed handle during the drag,
    // so recompute them from the release position — a release without a
    // following move must not leave a stale hover fill or pointing hand.
    const int hovered = handleAt(event->pos());
    if (hovered != m_hoveredPoint) {
        m_hoveredPoint = hovered;
        setCursor(hovered >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
    update();
}

void BezierCurveEditor::resizeEvent(QResizeEvent *)
{
    m_points[StartPoint] = QPointF(valueToX(kMinX, width()), valueToY(0.0, height()));
    m_points[ControlPoint1] = QPointF(valueToX(clampX(bezier->x1), width()), valueToY(clampValue(bezier->y1), height()));
    m_points[ControlPoint2] = QPointF(valueToX(clampX(bezier->x2), width()), valueToY(clampValue(bezier->y2), height()));
    m_points[EndPoint] = QPointF(valueToX(kMaxX, width()), valueToY(1.0, height()));
}

void BezierCurveEditor::paintEvent(QPaintEvent *)
{
    // Anchors are fixed at value (0,0) and (1,1) — the curve's start and
    // end; only the tangent handles move.
    m_points[StartPoint] = QPointF(valueToX(kMinX, width()), valueToY(0.0, height()));
    m_points[EndPoint]   = QPointF(valueToX(kMaxX, width()), valueToY(1.0, height()));
    // Clamp the handles in value space to the fixed viewport: y may swing
    // ±kUnderOverRange past the 0..1 band, x stays in the [0, 1] domain.
    m_points[ControlPoint1] = QPointF(
        valueToX(clampX(xToValue(m_points[ControlPoint1].x(), width())), width()),
        valueToY(clampValue(yToValue(m_points[ControlPoint1].y(), height())), height()));
    m_points[ControlPoint2] = QPointF(
        valueToX(clampX(xToValue(m_points[ControlPoint2].x(), width())), width()),
        valueToY(clampValue(yToValue(m_points[ControlPoint2].y(), height())), height()));

    // The editor is authoritative: write the clamped values back so bezier
    // (and the spin boxes below) always hold viewport values. Y is inverted
    // in pixels (y grows downward), hence the inverse map.
    bezier->x1 = static_cast<float>(xToValue(m_points[ControlPoint1].x(), width()));
    bezier->y1 = static_cast<float>(yToValue(m_points[ControlPoint1].y(), height()));
    bezier->x2 = static_cast<float>(xToValue(m_points[ControlPoint2].x(), width()));
    bezier->y2 = static_cast<float>(yToValue(m_points[ControlPoint2].y(), height()));

    if (!spinBoxes.empty() && spinBoxes.at(0)) {
        for (const auto box : spinBoxes) {
            box->blockSignals(true);
        }
        spinBoxes.at(0)->setValue(bezier->x1);
        spinBoxes.at(1)->setValue(bezier->y1);
        spinBoxes.at(2)->setValue(bezier->x2);
        spinBoxes.at(3)->setValue(bezier->y2);
        for (const auto box : spinBoxes) {
            box->blockSignals(false);
        }
    }
    const theme::Colors& c = theme::Colors::current();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Canvas: recessed rounded container (8px = the standard container
    // rounding, same as the popup panels).
    painter.setPen(Qt::NoPen);
    painter.setBrush(c.recessed);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8.0, 8.0);

    // Value-axis hairlines at Y=0 and Y=1 — the anchors sit on these rows.
    // The viewport maps [−0.5, 1.5] onto the full height, so they fall at
    // fixed fractions (25%/75%) and the reachable overshoot is the constant
    // ±kUnderOverRange, not an artifact of the widget size.
    // recessedHairline — between hairline (fades on the sunken canvas) and
    // recessedGuideLine (too strong for an informational axis). Rounded to a
    // pixel row then +0.5: an integer y with AA on straddles two rows at
    // half coverage and reads as a 2px line.
    painter.setPen(QPen(c.recessedHairline, 1.0, Qt::SolidLine));
    const qreal axisY0 = std::round(valueToY(0.0, height())) + 0.5;
    const qreal axisY1 = std::round(valueToY(1.0, height())) + 0.5;
    painter.drawLine(QLineF(0, axisY0, width(), axisY0));
    painter.drawLine(QLineF(0, axisY1, width(), axisY1));

    // Tangent guides: dashed line from each curve anchor to its handle.
    // recessedGuideLine is equidistant from the recessed column in both
    // themes, so the guides read at the same strength on the sunken canvas
    // (the plain hairline fades against the pale light canvas). AA stays on
    // for the diagonal; the +0.5 shift moves the line onto pixel centers so
    // the 1px pen doesn't straddle two rows at half coverage.
    painter.setPen(QPen(c.recessedGuideLine, 1.0, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    const QPointF half(0.5, 0.5);
    painter.drawLine(m_points[StartPoint] + half, m_points[ControlPoint1] + half);
    painter.drawLine(m_points[EndPoint] + half, m_points[ControlPoint2] + half);

    // Curve.
    m_curvePen.setColor(c.text);
    painter.setPen(m_curvePen);
    QPainterPath path;
    path.moveTo(m_points[StartPoint]);
    path.cubicTo(m_points[ControlPoint1], m_points[ControlPoint2], m_points[EndPoint]);
    painter.drawPath(path);

    // Endpoint anchors: filled dots.
    painter.setPen(Qt::NoPen);
    painter.setBrush(c.text);
    painter.drawEllipse(m_points[StartPoint], 3.0, 3.0);
    painter.drawEllipse(m_points[EndPoint], 3.0, 3.0);

    // Tangent handles: the mid accent tone at rest (visible on both
    // canvases — the plain accent fill is ~1.3:1 against the light canvas),
    // accentBright on hover. accentBright sits on the hover-bright side of
    // each theme (darker on light, brighter on dark), so hover feedback
    // reads in the conventional direction; the rest tone is violet-500 in
    // both themes. The visual radius (POINT_RADIUS) is smaller than the
    // grab radius (HIT_RADIUS).
    for (int i = 0; i < NUM_POINTS; i++) {
        if (i == StartPoint || i == EndPoint) continue;
        painter.setBrush(i == m_hoveredPoint ? c.accentBright : c.accentSwatch);
        painter.drawEllipse(m_points[i], POINT_RADIUS, POINT_RADIUS);
    }
}
