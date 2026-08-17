#include "gui/tool/ToolSlider.h"
#include "theme/Colors.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>

namespace gui {
namespace tool {

    ToolSlider::ToolSlider(QWidget* aParent): QSlider(Qt::Horizontal, aParent) {
        // No focus ring on these option-row sliders.
        setFocusPolicy(Qt::NoFocus);
    }

    int ToolSlider::valueFromPixel(int aX) const {
        const QRect r = contentsRect();
        const qreal span = qMax<qreal>(1.0, r.width() - kHeadDiameter);
        const qreal f = qBound(0.0, (aX - (r.left() + kHeadDiameter / 2.0)) / span, 1.0);
        return minimum() + qRound(f * qreal(maximum() - minimum()));
    }

    qreal ToolSlider::headCenter() const {
        const QRect r = contentsRect();
        const qreal span = qMax<qreal>(1.0, r.width() - kHeadDiameter);
        const qreal f = maximum() > minimum() ? qreal(value() - minimum()) / qreal(maximum() - minimum()) : 0.0;
        return r.left() + kHeadDiameter / 2.0 + f * span;
    }

    void ToolSlider::paintEvent(QPaintEvent*) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF rc = QRectF(contentsRect());
        const theme::Colors& c = theme::Colors::current();

        // Track: a 4px pill, vertically centered in the row.
        const qreal grooveY = rc.center().y() - kTrackHeight / 2.0;
        const QRectF groove(rc.left(), grooveY, rc.width(), kTrackHeight);
        const qreal trackRadius = kTrackHeight / 2.0;

        // Head center from the shared value<->pixel mapping, so the drawn
        // position and the click mapping are the same function.
        const qreal headCX = headCenter();
        const qreal headCY = rc.center().y();
        const qreal headR = kHeadDiameter / 2.0;

        // Base track: the sunken (empty) portion.
        painter.setPen(Qt::NoPen);
        painter.setBrush(c.recessed);
        painter.drawRoundedRect(groove, trackRadius, trackRadius);

        // Filled portion: the same accent token as the head.
        if (headCX > groove.left()) {
            painter.setBrush(c.accent);
            painter.drawRoundedRect(QRectF(groove.left(), groove.top(),
                                           headCX - groove.left(), groove.height()),
                                    trackRadius, trackRadius);
        }

        // Head: solid accent circle.
        painter.setBrush(c.accent);
        painter.drawEllipse(QPointF(headCX, headCY), headR, headR);
    }

    void ToolSlider::mousePressEvent(QMouseEvent* aEvent) {
        if (aEvent->button() == Qt::LeftButton) {
            setSliderPosition(valueFromPixel(aEvent->position().toPoint().x()));
            setSliderDown(true);
            aEvent->accept();
            update();
            return;
        }
        QSlider::mousePressEvent(aEvent);
    }

    void ToolSlider::mouseMoveEvent(QMouseEvent* aEvent) {
        if (isSliderDown() && (aEvent->buttons() & Qt::LeftButton)) {
            setSliderPosition(valueFromPixel(aEvent->position().toPoint().x()));
            aEvent->accept();
            update();
            return;
        }
        QSlider::mouseMoveEvent(aEvent);
    }

    void ToolSlider::mouseReleaseEvent(QMouseEvent* aEvent) {
        if (aEvent->button() == Qt::LeftButton) {
            setSliderDown(false);
            aEvent->accept();
            update();
            return;
        }
        QSlider::mouseReleaseEvent(aEvent);
    }

    void ToolSlider::changeEvent(QEvent* aEvent) {
        QSlider::changeEvent(aEvent);
        // Repaint when the theme restyles the dock; the colors are read
        // from theme::Colors at paint time.
        if (aEvent->type() == QEvent::PaletteChange || aEvent->type() == QEvent::StyleChange)
            update();
    }

} // namespace tool
} // namespace gui
