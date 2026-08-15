#include <QPainter>
#include <QEnterEvent>
#include "gui/TabCloseButton.h"
#include "gui/theme/Colors.h"

namespace gui {

//-------------------------------------------------------------------------------------------------
TabCloseButton::TabCloseButton(QWidget* aParent):
    QAbstractButton(aParent), mDirty(false), mTabHovered(false), mButtonHovered(false), mActive(false) {
    this->setFixedSize(16, 16);
    this->setFocusPolicy(Qt::NoFocus);
    this->setCursor(Qt::ArrowCursor);
}

void TabCloseButton::setDirty(bool aDirty) {
    if (mDirty != aDirty) {
        mDirty = aDirty;
        this->update();
    }
}

void TabCloseButton::setTabHovered(bool aHovered) {
    if (mTabHovered != aHovered) {
        mTabHovered = aHovered;
        this->update();
    }
}

void TabCloseButton::setActive(bool aActive) {
    if (mActive != aActive) {
        mActive = aActive;
        this->update();
    }
}

void TabCloseButton::drawCloseGlyph(QPainter& aPainter, const QPointF& aCenter, qreal aSize, const QColor& aColor) const {
    QPen pen(aColor, qMax<qreal>(1.2, aSize / 9.0), Qt::SolidLine, Qt::RoundCap);
    aPainter.setPen(pen);
    aPainter.setBrush(Qt::NoBrush);
    const qreal margin = aSize * 0.22;
    aPainter.drawLine(aCenter + QPointF(-margin, -margin), aCenter + QPointF(margin, margin));
    aPainter.drawLine(aCenter + QPointF(-margin, margin), aCenter + QPointF(margin, -margin));
}

void TabCloseButton::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF rect = QRectF(this->rect());
    const QPointF center = rect.center();
    const qreal size = qMin(rect.width(), rect.height());

    QColor color = this->palette().color(QPalette::WindowText);
    if (!color.isValid()) {
        color = theme::Colors::current().textMuted;
    }

    const bool showClose = mTabHovered || mButtonHovered;

    if (mButtonHovered) {
        QColor circle = color;
        circle.setAlpha(36);
        painter.setPen(Qt::NoPen);
        painter.setBrush(circle);
        painter.drawEllipse(rect.adjusted(1.0, 1.0, -1.0, -1.0));
        color.setAlpha(255);
        drawCloseGlyph(painter, center, size, color);
    } else if (showClose) {
        color.setAlpha(mActive ? 190 : 150);
        drawCloseGlyph(painter, center, size, color);
    } else if (mDirty) {
        color.setAlpha(mActive ? 190 : 95);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        const qreal radius = qMax<qreal>(2.0, size * 0.24);
        painter.drawEllipse(center, radius, radius);
    }
}

void TabCloseButton::enterEvent(QEnterEvent*) {
    mButtonHovered = true;
    this->update();
}

void TabCloseButton::leaveEvent(QEvent*) {
    mButtonHovered = false;
    this->update();
}

} // namespace gui
