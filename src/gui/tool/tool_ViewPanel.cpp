#include "gui/tool/tool_ViewPanel.h"

namespace {
// Same button family as the toolbox: 24x24 with explicit 18px glyphs.
// Sizes are logical pixels; Qt6 scales the whole window on high-DPI screens.
const int kButtonSize = 24;
const int kIconSize = 18;
} // namespace

namespace gui {
namespace tool {

    ViewPanel::ViewPanel(QWidget* aParent, GUIResources& aResources, const QString& aTitle):
        QGroupBox(aParent), mGUIResources(aResources), mButtons(), mLayout(this, 0, 2, 2) {
        this->setTitle(aTitle);
        this->setLayout(&mLayout);

        mGUIResources.onThemeChanged.connect(this, &ViewPanel::onThemeUpdated);
    }

    void ViewPanel::addButton(
        const QString& aIconName, bool aCheckable, const QString& aToolTip, const PushDelegate& aDelegate
    ) {
        QPushButton* button = new QPushButton();
        button->setObjectName(aIconName);
        button->setIcon(mGUIResources.icon(aIconName));
        button->setIconSize(QSize(kIconSize, kIconSize));
        button->setFixedSize(kButtonSize, kButtonSize);
        button->setCheckable(aCheckable);
        button->setToolTip(aToolTip);
        button->setFocusPolicy(Qt::NoFocus);

        mButtons.push_back(button);
        mLayout.addWidget(button);

        this->connect(button, &QPushButton::clicked, aDelegate);
    }

    int ViewPanel::updateGeometry(const QPoint& aPos, int aWidth) {
        QMargins margins = this->contentsMargins();
        int l = margins.left();
        int r = margins.right();
        int t = margins.top();
        int b = margins.bottom();

        // card height = top padding + content + bottom padding; the layout's
        // heightForWidth only measures the content, so without t the buttons
        // overflow the card bottom and get clipped
        auto height = mLayout.heightForWidth(aWidth - l - r);
        this->setGeometry(aPos.x(), aPos.y(), aWidth, height + t + b);

        return aPos.y() + height + t + b;
    }

    void ViewPanel::onThemeUpdated(theme::Theme&) {
        if (mButtons.size() > 0) {
            for (auto button : mButtons) {
                button->setIcon(mGUIResources.icon(button->objectName()));
            }
        }
    }

} // namespace tool
} // namespace gui
