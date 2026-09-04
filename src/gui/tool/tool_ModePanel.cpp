#include <QDebug>
#include "XC.h"
#include "gui/tool/tool_ModePanel.h"
#include <QSettings>

namespace {
// The toolbox is the dock's primary control: 24x24 buttons (matching the
// playback column) with explicit 18px glyphs. Fixed size keeps the buttons
// exactly square — the QSS padding/sizeHint math otherwise yields 24x20.
// Sizes are logical pixels; Qt6 scales the whole window on high-DPI screens.
constexpr double kButtonSize = 24.0 * 1.2;
constexpr double kIconSize = 18.0 * 1.2;
} // namespace

namespace gui {
namespace tool {

    ModePanel::ModePanel(QWidget* aParent, GUIResources& aResources, const PushDelegate& aOnPushed):
        QGroupBox(aParent),
        mGUIResources(aResources),
        mGroup(new QButtonGroup(this)),
        mButtons(),
        mLayout(this, 0, 2, 2),
        mOnPushed(aOnPushed) {
        this->setTitle(tr("Toolbox"));
        this->setObjectName("modePanel"); // QSS id for the toolbox row's active-tool accent
        mGroup->setExclusive(true);
        this->setLayout(&mLayout);

        addButton(ctrl::ToolType_Cursor, "hand", tr("Pan tool"));
        addButton(ctrl::ToolType_SRT, "arrows-out-cardinal", tr("Transform"));
        addButton(ctrl::ToolType_Bone, "bone", tr("Bone editor"));
        addButton(ctrl::ToolType_Pose, "pose", tr("Pose editor"));
        addButton(ctrl::ToolType_Mesh, "triangle", tr("Mesh editor"));
        addButton(ctrl::ToolType_FFD, "ffd", tr("Free-form deformation"));

        mGUIResources.onThemeChanged.connect(this, &ModePanel::onThemeUpdated);
    }

    void ModePanel::addButton(ctrl::ToolType aType, const QString& aIconName, const QString& aToolTip) {
        QSettings settings;
        auto uiScale = settings.value("generalsettings/ui/uiScale", 1.0).toDouble();
        QPushButton* button = new QPushButton(this);
        button->setObjectName(aIconName);
        button->setIcon(mGUIResources.icon(aIconName));
        button->setIconSize(QSize(kIconSize * uiScale, kIconSize * uiScale));
        button->setFixedSize(kButtonSize * uiScale, kButtonSize * uiScale);
        button->setCheckable(true);
        // active-state button: press reads as hover, not the sunken press
        button->setProperty("activeButton", true);
        button->setToolTip(aToolTip);

        XC_ASSERT(aType == mButtons.size());

        mGroup->addButton(button);
        mButtons.push_back(button);
        mLayout.addWidget(button);

        this->connect(button, &QPushButton::clicked, [=](bool aChecked) { this->mOnPushed(aType, aChecked); });
    }

    void ModePanel::onThemeUpdated(theme::Theme&) {
        QSettings settings;
        auto uiScale = settings.value("generalsettings/ui/uiScale", 1.0).toDouble();
        if (mButtons.size() > 0) {
            for (auto button : mButtons) {
                button->setIcon(mGUIResources.icon(button->objectName()));
                button->setIconSize(QSize(kIconSize * uiScale, kIconSize * uiScale));
                button->setFixedSize(kButtonSize * uiScale, kButtonSize * uiScale);
            }
        }
    }

    void ModePanel::pushButton(ctrl::ToolType aId) { mButtons.at(aId)->click(); }

    int ModePanel::updateGeometry(const QPoint& aPos, int aWidth) {
        QMargins margins = this->contentsMargins();
        int l = margins.left();
        int r = margins.right();
        int t = margins.top();
        int b = margins.bottom();

        // card height = top padding + content + bottom padding (see ViewPanel)
        auto height = mLayout.heightForWidth(aWidth - l - r);
        this->setGeometry(aPos.x(), aPos.y(), aWidth, height + t + b);

        return aPos.y() + height + t + b;
    }

} // namespace tool
} // namespace gui
