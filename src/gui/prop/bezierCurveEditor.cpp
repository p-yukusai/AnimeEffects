#include "gui/prop/bezierCurveEditor.h"
#include <QDoubleSpinBox>
#include <QPainterPath>
#include <QPainter>
#include <QMouseEvent>
#include <filesystem>
#include <utility>
#include "gui/theme/Colors.h"

float normalize(const float val, const int min, const int max) {
    return (val - static_cast<float>(min)) / static_cast<float>(max - min);
}
float denormalize (const float var, const int min, const int max) {
    return var * static_cast<float>(max - min) + static_cast<float>(min);
}
float invert (const int min, const int max, const float value) {
    return static_cast<float>(max) - value + static_cast<float>(min);
}

BezierCurveEditor::BezierCurveEditor(QWidget *parent, util::Easing::CubicBezier* cubicBezier, QVector<QDoubleSpinBox*> spins, int* pro):
    QWidget(parent), m_dragging(false), m_selectedPoint(0) {
    const theme::Colors c = theme::Colors::current();
    m_curvePen.setColor(c.text);
    m_curvePen.setWidth(2);
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
            if(distance(m_points[i], event->pos()) <= POINT_RADIUS) {
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
    m_points[StartPoint]    = QPointF(20, height() - 20);
    const QPointF points1 = { denormalize(bezier->x1, 20, width() - 20), denormalize(invert(0, 1, bezier->y1), 20, height() - 20 ) };
    const QPointF points2 = { denormalize(bezier->x2, 20, width() - 20),denormalize(invert(0, 1, bezier->y2), 20, height() - 20) };
    m_points[1] = points1;
    m_points[2] = points2;
    m_points[EndPoint]      = QPointF(width() - 20, 20);
}

void BezierCurveEditor::paintEvent(QPaintEvent *)
{
    // Points are not meant to be moved for any reason as that would break the calculations
    m_points[StartPoint]    = QPointF(20, height() - 20);
    m_points[EndPoint]      = QPointF(width() - 20, 20);
    // We clamp to not go outside the widget
    m_points[ControlPoint1].setX(std::clamp(m_points[ControlPoint1].x(), qreal(20), qreal(width() - 20)));
    m_points[ControlPoint1].setY(std::clamp(m_points[ControlPoint1].y(), qreal(20), qreal(height() - 20)));
    m_points[ControlPoint2].setX(std::clamp(m_points[ControlPoint2].x(), qreal(20), qreal(width() - 20)));
    m_points[ControlPoint2].setY(std::clamp(m_points[ControlPoint2].y(), qreal(20), qreal(height() -20)));

    // We normalize these because the points are not lower-left 0 and upper-right 1, also, we need to inverse the Y points
    // because the Y axis is inverted in the GUI
    bezier->x1 = normalize(m_points[ControlPoint1].x(), 20, width() - 20);
    const auto inverted_y1 = invert(0, height(), m_points[ControlPoint1].y());
    bezier->y1 = normalize(inverted_y1, 20, height() - 20);

    bezier->x2 = normalize(m_points[ControlPoint2].x(), 20, width() -20);
    const auto inverted_y2 = invert(0, height(), m_points[ControlPoint2].y());
    bezier->y2 = normalize(inverted_y2, 20, height() -20);

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
}
