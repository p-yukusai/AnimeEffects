#ifndef BEZIERCURVEEDITOR_H
#define BEZIERCURVEEDITOR_H

#include "util/Easing.h"
#include <QPen>
#include <QBrush>
#include <QDoubleSpinBox>
#include <QWidget>

class BezierCurveEditor : public QWidget
{
    Q_OBJECT
public:
    explicit BezierCurveEditor(QWidget *parent = nullptr, util::Easing::CubicBezier* cubicBezier = nullptr, QVector<QDoubleSpinBox*> spins = {}, int* pro = nullptr);
    ~BezierCurveEditor();
    util::Easing::CubicBezier* bezier;
    QVector<QDoubleSpinBox*> spinBoxes;
    int* progress;

    void paintEvent(QPaintEvent *);
    void resizeEvent(QResizeEvent *);
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *);
    void mouseReleaseEvent(QMouseEvent *);

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

public:
    const int NUM_POINTS = 4;
    const qreal POINT_TOLERANCE = 5.0;
    const qreal POINT_RADIUS = 6.0;
    const int MAGIC_BORDER = 50;

    QPointF         m_points[4];
    QPen            m_pens[4];
    QBrush          m_brushes[4];
    QColor          m_colors[4];

    QPen        m_curvePen;
    QPen        m_borderPen;

    bool        m_dragging;
    int         m_selectedPoint{};
};

#endif // BEZIERCURVEEDITOR_H