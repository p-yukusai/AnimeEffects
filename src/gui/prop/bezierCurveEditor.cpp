#include "gui/prop/bezierCurveEditor.h"
#include <QDoubleSpinBox>
#include <QPainterPath>
#include <QPainter>
#include <QMouseEvent>
#include <filesystem>
#include <utility>
#include "gui/theme/Colors.h"

static float normalize(const float val, const int min, const int max) {
    return (val - static_cast<float>(min)) / static_cast<float>(max - min);
}
static float denormalize (const float var, const int min, const int max) {
    return var * static_cast<float>(max - min) + static_cast<float>(min);
}
static float invert (const int min, const int max, const float value) {
    return static_cast<float>(max) - value + static_cast<float>(min);
}

BezierCurveEditor::BezierCurveEditor(QWidget *parent, util::Easing::CubicBezier* cubicBezier, QVector<QDoubleSpinBox*> spins, float* pro):
    QWidget(parent), m_dragging(false) {
    const theme::Colors c = theme::Colors::current();
    m_curvePen.setColor(c.text);
    m_curvePen.setWidth(2);

    QColor borderColor = c.text;
    borderColor.setAlpha(128);
    m_borderPen.setColor(borderColor);
    m_borderPen.setWidth(1);
    m_borderPen.setStyle(Qt::PenStyle::DashLine);
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
    }
    *progress = 0;
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
    m_points[StartPoint]    = QPointF(MAGIC_BORDER_X, height() - MAGIC_BORDER_Y);
    const QPointF points1 = { denormalize(bezier->x1, MAGIC_BORDER_X, width() - MAGIC_BORDER_X), denormalize(invert(0, 1, bezier->y1), MAGIC_BORDER_Y, height() - MAGIC_BORDER_Y ) };
    const QPointF points2 = { denormalize(bezier->x2, MAGIC_BORDER_X, width() - MAGIC_BORDER_X),denormalize(invert(0, 1, bezier->y2), MAGIC_BORDER_Y, height() - MAGIC_BORDER_Y) };
    m_points[1] = points1;
    m_points[2] = points2;
    m_points[EndPoint] = QPointF(width() - MAGIC_BORDER_X, MAGIC_BORDER_Y);
}

void BezierCurveEditor::paintEvent(QPaintEvent *)
{
    // Points are not meant to be moved for any reason as that would break the calculations
    m_points[StartPoint]    = QPointF(MAGIC_BORDER_X, height() - MAGIC_BORDER_Y);
    m_points[EndPoint]      = QPointF(width() - MAGIC_BORDER_X, MAGIC_BORDER_Y);
    // We clamp to not go outside the widget
    m_points[ControlPoint1].setX(std::clamp(m_points[ControlPoint1].x(), static_cast<qreal>(MAGIC_BORDER_X), static_cast<qreal>(width() - MAGIC_BORDER_X)));
    m_points[ControlPoint1].setY(std::clamp(m_points[ControlPoint1].y(), POINT_RADIUS, static_cast<qreal>(height() - static_cast<int>(POINT_RADIUS))));

    m_points[ControlPoint2].setX(std::clamp(m_points[ControlPoint2].x(), static_cast<qreal>(MAGIC_BORDER_X), static_cast<qreal>(width() - MAGIC_BORDER_X)));
    m_points[ControlPoint2].setY(std::clamp(m_points[ControlPoint2].y(), POINT_RADIUS, static_cast<qreal>(height() - static_cast<int>(POINT_RADIUS))));

    // We normalize these because the points are not lower-left 0 and upper-right 1, also, we need to inverse the Y points
    // because the Y axis is inverted in the GUI
    bezier->x1 = normalize(m_points[ControlPoint1].x(), MAGIC_BORDER_X, width() - MAGIC_BORDER_X);
    const auto inverted_y1 = invert(0, height(), m_points[ControlPoint1].y());
    bezier->y1 = normalize(inverted_y1, MAGIC_BORDER_Y, height() - MAGIC_BORDER_Y);

    bezier->x2 = normalize(m_points[ControlPoint2].x(), MAGIC_BORDER_X, width() -MAGIC_BORDER_X);
    const auto inverted_y2 = invert(0, height(), m_points[ControlPoint2].y());
    bezier->y2 = normalize(inverted_y2, MAGIC_BORDER_Y, height() -MAGIC_BORDER_Y);

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

    // Tangent guides: dashed hairline from each curve anchor to its handle.
    painter.setPen(QPen(c.hairline, 1.0, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(m_points[StartPoint], m_points[ControlPoint1]);
    painter.drawLine(m_points[EndPoint], m_points[ControlPoint2]);

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

    painter.setPen(m_borderPen);
    painter.setBrush(Qt::NoBrush); // drawRect below must outline only — the
                                   // handles loop leaves accentSwatch set
    painter.drawLine(QLine{0, MAGIC_BORDER_Y, width(), MAGIC_BORDER_Y});
    painter.drawLine(QLine{0, height() - MAGIC_BORDER_Y, width(), height() - MAGIC_BORDER_Y});
    painter.drawRect(0, 0, width(), height());
}
