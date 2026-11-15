#ifndef GUI_ICONBUTTON_H
#define GUI_ICONBUTTON_H

#include <QIcon>
#include <QPushButton>

namespace gui {

// A button whose glyph brightens to the active (max-contrast) color on hover -
// the icon participates in the hover feedback, not just the background.
// setRestIcon() records the resting icon, so the play/pause glyph swap keeps
// working; the hover icon is set explicitly via setHoverIcon and re-set on theme
// changes. (QAbstractButton::setIcon is not virtual, so the rest icon is tracked
// through the explicit setter only.) A null hover icon is tolerated: the resting
// glyph stays, which is what an icon with no -active variant gets.
class IconButton: public QPushButton {
public:
    using QPushButton::QPushButton;
    void setRestIcon(const QIcon& aIcon) {
        mRest = aIcon;
        QPushButton::setIcon(aIcon);
    }
    void setHoverIcon(const QIcon& aIcon) { mHover = aIcon; }

protected:
    void enterEvent(QEnterEvent*) override {
        if (isEnabled() && !mHover.isNull())
            QPushButton::setIcon(mHover);
    }
    void leaveEvent(QEvent*) override { QPushButton::setIcon(mRest); }

private:
    QIcon mRest;
    QIcon mHover;
};

} // namespace gui

#endif // GUI_ICONBUTTON_H
