#include <QFile>
#include <QTextStream>
#include <QFontMetrics>
#include "gui/prop/prop_Panel.h"

// namespace {
// static const int kCollapsedPanelHeight = 22; // No longer needed
// }

namespace gui {
namespace prop {

    Panel::Panel(const QString& aTitle, QWidget* aParent):
        QGroupBox(aParent), mLayout(new QVBoxLayout()), mGroups(), mChecked(true) {
        mLayout->setSpacing(0);
        mLayout->setContentsMargins(0, 0, 0, 0);

        this->setObjectName("propertyPanel");
        this->setTitle(aTitle);
        this->setCheckable(true);
        this->setChecked(mChecked);
        this->setFocusPolicy(Qt::NoFocus);
        this->setLayout(mLayout);

        this->connect(this, &QGroupBox::clicked, this, &Panel::onClicked);
    }

    void Panel::addGroup(QGroupBox* aGroup) {
        mLayout->addWidget(aGroup);
        aGroup->connect(aGroup, &QGroupBox::clicked, this, &Panel::onChildrenClicked);
        mGroups.push_back(aGroup);
    }

    void Panel::addStretch() { mLayout->addStretch(); }

    void Panel::onClicked(bool aChecked) {
        if (mChecked != aChecked) {
            mChecked = aChecked;
            int targetHeight;
            if (aChecked) {
                targetHeight = QWIDGETSIZE_MAX;
            } else {
                QFontMetrics fm(this->font());
                targetHeight = fm.lineSpacing() + 4; // Add some padding
            }
            this->setFixedHeight(targetHeight);
            if (onCollapsed)
                onCollapsed();
        }
    }

    void Panel::onChildrenClicked(bool) {
        this->updateGeometry();
        // this->update();
    }

} // namespace prop
} // namespace gui
