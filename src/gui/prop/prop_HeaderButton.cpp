#include <QEnterEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>

#include "gui/prop/prop_HeaderButton.h"
#include "gui/GUIResources.h"

namespace gui {
namespace prop {

    // the resting caret is the theme-tinted glyph at reduced opacity; on
    // hover/active the full-opacity glyph replaces it, so the caret reads
    // as quiet by default and lights up with the title highlight
    static QPixmap dimmedPixmap(const QPixmap& aSrc) {
        QPixmap pm(aSrc.size());
        pm.fill(Qt::transparent);
        {
            QPainter painter(&pm);
            painter.setOpacity(0.45);
            painter.drawPixmap(0, 0, aSrc);
        }
        return pm;
    }

    HeaderButton::HeaderButton(
        const QString& aText, GUIResources* aGUIResources,
        const QString& aObjectName, QWidget* aParent, int aIconSize
    ): QAbstractButton(aParent), mCaret(nullptr), mTitle(nullptr), mLayout(nullptr),
       mHovered(false), mRightIcon(), mDownIcon(), mRightDim(), mDownDim() {
        this->setObjectName(aObjectName);
        this->setCheckable(true);
        this->setFocusPolicy(Qt::NoFocus);
        this->setAttribute(Qt::WA_Hover); // QSS :hover on the row

        // per-theme tinted caret pair (tools/icon_tint colors the SVGs at
        // build time); the checked state reads expanded (caret down) vs
        // collapsed (caret right)
        mRightIcon = aGUIResources->icon("caret-right-regular").pixmap(aIconSize, aIconSize);
        mDownIcon = aGUIResources->icon("caret-down-regular").pixmap(aIconSize, aIconSize);
        mRightDim = dimmedPixmap(mRightIcon);
        mDownDim = dimmedPixmap(mDownIcon);

        // fixed caret column: the glyph centered, so every header level's
        // caret sits on the same axis and the title starts at one x
        mCaret = new QLabel(this);
        mCaret->setFixedWidth(kCaretColumn);
        mCaret->setAlignment(Qt::AlignCenter);
        mCaret->setAttribute(Qt::WA_TransparentForMouseEvents);

        mTitle = new QLabel(aText, this);
        mTitle->setObjectName("headerTitle");
        // not transparent-for-mouse: the label keeps its own hover state;
        // its clicks propagate to the button and toggle it

        mLayout = new QHBoxLayout(this);
        mLayout->setSpacing(0);
        mLayout->setContentsMargins(12, 0, 0, 0);
        mLayout->addWidget(mCaret);
        mLayout->addWidget(mTitle);
        mLayout->addStretch();

        // connect before setChecked so the initial toggle applies the
        // resting (dim) caret
        this->connect(this, &QAbstractButton::toggled, this, [this](bool) {
            this->updateCaret();
        });
        this->setChecked(true);
    }

    QSize HeaderButton::sizeHint() const { return mLayout ? mLayout->totalSizeHint() : QSize(); }

    void HeaderButton::paintEvent(QPaintEvent* aEvent) {
        // the QSS background (if any) is drawn by the base widget paint;
        // the caret and title labels render the row content
        QWidget::paintEvent(aEvent);
    }

    void HeaderButton::enterEvent(QEnterEvent* aEvent) {
        mHovered = true;
        updateCaret();
        // the QSS brightens the title via the hover property (re-polish so
        // the color rule re-matches)
        mTitle->setProperty("hover", true);
        mTitle->style()->unpolish(mTitle);
        mTitle->style()->polish(mTitle);
        QAbstractButton::enterEvent(aEvent);
    }

    void HeaderButton::leaveEvent(QEvent* aEvent) {
        mHovered = false;
        updateCaret();
        mTitle->setProperty("hover", false);
        mTitle->style()->unpolish(mTitle);
        mTitle->style()->polish(mTitle);
        QAbstractButton::leaveEvent(aEvent);
    }

    void HeaderButton::updateCaret() {
        const QPixmap& src = mHovered ? (isChecked() ? mDownIcon : mRightIcon)
                                      : (isChecked() ? mDownDim : mRightDim);
        mCaret->setPixmap(src);
    }

} // namespace prop
} // namespace gui
