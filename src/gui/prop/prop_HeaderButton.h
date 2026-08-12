#ifndef GUI_PROP_HEADERBUTTON_H
#define GUI_PROP_HEADERBUTTON_H

#include <QAbstractButton>
#include <QIcon>
#include <QPixmap>

class QLabel;
class QHBoxLayout;

namespace gui {

class GUIResources;

namespace prop {

    // Full-row section header. The whole band is the click target (the old
    // checkable-QGroupBox header accepted clicks only on the style-painted
    // title/indicator subrects, leaving the rest of the row dead).
    //
    // Composition: a fixed caret column (the glyph centered) followed by
    // the title as a plain QLabel. The column width is constant across the
    // header levels, so the caret centers land on the same axis and every
    // title starts at the same x -- independent of the QPushButton's
    // style-dependent icon/text spacing. The caret rests dim and brightens
    // on hover (enter/leave); the row tint/typography come from the QSS.
    class HeaderButton: public QAbstractButton {
        Q_OBJECT
    public:
        // aIconSize: 14 for the top-level headers, 12 for the subsections
        // (the carets track the font-size hierarchy: 9pt vs 8pt)
        HeaderButton(
            const QString& aText, GUIResources* aGUIResources,
            const QString& aObjectName, QWidget* aParent = nullptr, int aIconSize = 14
        );

    protected:
        QSize sizeHint() const override;
        // the caret and title labels render the row; the button itself
        // paints nothing (its background comes from the QSS)
        void paintEvent(QPaintEvent* aEvent) override;
        void enterEvent(QEnterEvent* aEvent) override;
        void leaveEvent(QEvent* aEvent) override;

    private:
        void updateCaret();

        static const int kCaretColumn = 20;

        QLabel* mCaret;
        QLabel* mTitle;
        QHBoxLayout* mLayout;
        bool mHovered;
        QPixmap mRightIcon;  // bright, expanded-direction carets
        QPixmap mDownIcon;
        QPixmap mRightDim;   // dim (resting) variants
        QPixmap mDownDim;
    };

} // namespace prop
} // namespace gui

#endif // GUI_PROP_HEADERBUTTON_H
