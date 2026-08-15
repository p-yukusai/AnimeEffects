#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QPixmap>
#include <QToolButton>

#include "gui/prop/prop_KeyKnocker.h"
#include "gui/GUIResources.h"

namespace gui {
namespace prop {

    // the resting plus is the theme-tinted glyph at reduced opacity (the
    // same dimming the header carets use); it brightens on hover
    static QIcon dimmedIcon(const QIcon& aIcon) {
        const QPixmap src = aIcon.pixmap(14, 14);
        QPixmap pm(src.size());
        pm.fill(Qt::transparent);
        {
            QPainter painter(&pm);
            painter.setOpacity(0.8);
            painter.drawPixmap(0, 0, src);
        }
        return QIcon(pm);
    }

    KeyKnocker::KeyKnocker(const QString& aLabel, GUIResources* aGUIResources):
        QWidget(), mLabel(nullptr), mPlus(nullptr), mLayout(nullptr), mKnocker() {
        this->setObjectName("keyKnocker");
        this->setFocusPolicy(Qt::NoFocus);
        this->setCursor(Qt::PointingHandCursor);
        this->setAttribute(Qt::WA_Hover); // QSS :hover on a plain QWidget

        mPlusIcon = aGUIResources->icon("plus");
        mPlusDim = dimmedIcon(mPlusIcon);

        mLabel = new QLabel(aLabel, this);
        mLabel->setObjectName("keyKnockerLabel");
        // not transparent-for-mouse: clicks propagate to the row
        mLabel->setCursor(Qt::PointingHandCursor);

        mPlus = new QToolButton(this);
        mPlus->setObjectName("keyKnockerPlus");
        mPlus->setFocusPolicy(Qt::NoFocus);
        mPlus->setCursor(Qt::PointingHandCursor);
        mPlus->setIconSize(QSize(14, 14));
        mPlus->setIcon(mPlusDim);
        // hovering the plus itself must brighten it too (the row's
        // enter/leave only fire over the label/stretch area)
        mPlus->installEventFilter(this);
        mPlus->connect(mPlus, &QToolButton::clicked, this, [this]() {
            if (mKnocker) {
                mKnocker();
            }
        });

        // the text sits at the section-title column (the knocker is the
        // header's stand-in); the plus is a right-aligned add affordance
        mLayout = new QHBoxLayout(this);
        mLayout->setSpacing(4);
        // the text sits at the headers' title column (12 + 20 caret column)
        mLayout->setContentsMargins(32, 0, 16, 0);
        mLayout->addWidget(mLabel);
        mLayout->addStretch();
        mLayout->addWidget(mPlus);
    }

    void KeyKnocker::set(const std::function<void()>& aKnocker) { mKnocker = aKnocker; }

    bool KeyKnocker::eventFilter(QObject* aWatched, QEvent* aEvent) {
        if (aWatched == mPlus) {
            if (aEvent->type() == QEvent::Enter) {
                mPlus->setIcon(mPlusIcon);
            } else if (aEvent->type() == QEvent::Leave) {
                mPlus->setIcon(mPlusDim);
            }
        }
        return QWidget::eventFilter(aWatched, aEvent);
    }

    void KeyKnocker::enterEvent(QEnterEvent* aEvent) {
        // the plus brightens like the header carets; the QSS brightens the
        // label via the hover property (re-polish)
        mPlus->setIcon(mPlusIcon);
        mLabel->setProperty("hover", true);
        mLabel->style()->unpolish(mLabel);
        mLabel->style()->polish(mLabel);
        QWidget::enterEvent(aEvent);
    }

    void KeyKnocker::leaveEvent(QEvent* aEvent) {
        mPlus->setIcon(mPlusDim);
        mLabel->setProperty("hover", false);
        mLabel->style()->unpolish(mLabel);
        mLabel->style()->polish(mLabel);
        QWidget::leaveEvent(aEvent);
    }

    void KeyKnocker::mouseReleaseEvent(QMouseEvent* aEvent) {
        if (aEvent->button() == Qt::LeftButton && rect().contains(aEvent->pos())) {
            if (mKnocker) {
                mKnocker();
            }
        }
        QWidget::mouseReleaseEvent(aEvent);
    }

} // namespace prop
} // namespace gui
