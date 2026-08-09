#ifndef GUI_TABCLOSEBUTTON_H
#define GUI_TABCLOSEBUTTON_H

#include <QAbstractButton>

namespace gui {

class TabCloseButton: public QAbstractButton {
public:
    TabCloseButton(QWidget* aParent);

    void setDirty(bool aDirty);
    void setTabHovered(bool aHovered);
    void setActive(bool aActive);

protected:
    virtual void paintEvent(QPaintEvent* aEvent) override;
    virtual void enterEvent(QEnterEvent* aEvent) override;
    virtual void leaveEvent(QEvent* aEvent) override;

private:
    void drawCloseGlyph(QPainter& aPainter, const QPointF& aCenter, qreal aSize, const QColor& aColor) const;

    bool mDirty;
    bool mTabHovered;
    bool mButtonHovered;
    bool mActive;
};

} // namespace gui

#endif // GUI_TABCLOSEBUTTON_H
