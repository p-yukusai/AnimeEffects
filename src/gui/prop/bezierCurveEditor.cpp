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
    m_colors[0] = c.text;
    m_colors[1] = c.accentBright;
    m_colors[2] = c.accentBright;
    m_colors[3] = c.text;

    for (int i = 0; i < NUM_POINTS; i++) {
        m_pens[i] = QPen(m_colors[i]);
        m_brushes[i] = QBrush(m_colors[i]);
    }
}

BezierCurveEditor::~BezierCurveEditor() = default;

void BezierCurveEditor::mousePressEvent(QMouseEvent *event)
{
    for(int i = 0; i < NUM_POINTS; i++) {
        if (i != StartPoint && i != EndPoint) {
            if(distance(m_points[i], event->pos()) <= POINT_RADIUS + POINT_TOLERANCE) {
                m_selectedPoint = i;
                m_dragging = true;
                break;
            }
        }
    }
}

void BezierCurveEditor::mouseMoveEvent(QMouseEvent *event)
{
    if(m_dragging) {
        m_points[m_selectedPoint] = event->pos();
    }
    *progress = 0;
    update();
}

void BezierCurveEditor::mouseReleaseEvent(QMouseEvent *)
{
    m_dragging = false;
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
    //
    QPainter painter(this);

    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.setPen(m_curvePen);
    QPainterPath path;
    path.moveTo(m_points[StartPoint]);
    path.cubicTo(m_points[ControlPoint1], m_points[ControlPoint2], m_points[EndPoint]);
    painter.drawPath(path);

    for (int i = 0; i < NUM_POINTS; i++) {
        if (i == StartPoint || i == EndPoint) {
            m_brushes[i].setStyle(Qt::BrushStyle::NoBrush);
            painter.setPen(m_pens[i]);
            painter.setBrush(m_brushes[i]);
            painter.drawEllipse(m_points[i], 3.0, 3.0);
        }
        else{
            painter.setPen(m_pens[i]);
            painter.setBrush(m_brushes[i]);
            painter.drawEllipse(m_points[i], POINT_RADIUS, POINT_RADIUS);
        }
    }

    painter.setPen(m_borderPen);
    painter.drawLine(QLine{0, MAGIC_BORDER_Y, width(), MAGIC_BORDER_Y});
    painter.drawLine(QLine{0, height() - MAGIC_BORDER_Y, width(), height() - MAGIC_BORDER_Y});
    painter.drawRect(0, 0, width(), height());
}
