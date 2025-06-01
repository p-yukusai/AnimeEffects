#include "gui/PlayBackWidget.h"
#include "gui/PlayBackWidget.h"
#include <QFile>
#include <QTextStream>
#include <functional>
#include <QVBoxLayout>
#include <QFontMetrics>

// namespace {
// int kButtonSize = 28; // No longer needed
// const int kButtonCount = 7; // No longer needed
// } // namespace

namespace gui {

PlayBackWidget::PlayBackWidget(GUIResources& aResources, QWidget* aParent, core::Project& mProject):
    QWidget(aParent), mGUIResources(aResources), mButtons() {
    // if (mGUIResources.getTheme().contains("high_dpi")) {
    //     kButtonSize = 32; // No longer needed
    // }
    // this->setGeometry(0, 0, kButtonSize, kButtonSize * kButtonCount); // Will be handled by layout
    aProject = &mProject;

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(2, 2, 2, 2); // Optional: add some small margins
    mainLayout->setSpacing(2);                 // Optional: add some spacing between buttons

    // Order matters for layout addition
    mButtons.push_back(createButton("rewind", false, tr("Return to initial frame")));
    mButtons.push_back(createButton("stepback", false, tr("One frame back")));
    mButtons.push_back(createButton("play", true, tr("Play")));
    mButtons.push_back(createButton("step", false, tr("One frame forward")));
    mButtons.push_back(createButton("fast", false, tr("Advance to final frame")));
    mButtons.push_back(createButton("loop", true, tr("Loop")));
    mButtons.push_back(createButton("audio", false, tr("Audio track")));

    for (QPushButton* button : mButtons) {
        mainLayout->addWidget(button);
    }

    this->setLayout(mainLayout);
    // Adjust size to content after layout is set and populated
    // this->adjustSize(); // Often better to let parent layout handle this, or set a size policy

    audioWidget->setupUi(audioUI, &mediaPlayer, aConf);
    mGUIResources.onThemeChanged.connect(this, &PlayBackWidget::onThemeUpdated);
}

void PlayBackWidget::setPushDelegate(const PushDelegate& aDelegate) {
    PlayBackWidget* owner = this;
    mPushDelegate = aDelegate;

    gui::PlayBackWidget::connect(mButtons.at(2), &QPushButton::pressed, [=]() {
        const bool isChecked = owner->mButtons.at(2)->isChecked();
        const char* name = isChecked ? "play" : "pause";
        owner->mButtons.at(2)->setIcon(owner->mGUIResources.icon(name));
        owner->mButtons.at(2)->setToolTip(isChecked ? tr("Play") : tr("Pause"));
        owner->mPushDelegate(isChecked ? PushType_Pause : PushType_Play);
    });
    gui::PlayBackWidget::connect(mButtons.at(5), &QPushButton::pressed, [=]() {
        const bool isChecked = owner->mButtons.at(5)->isChecked();
        owner->mPushDelegate(isChecked ? PushType_NoLoop : PushType_Loop);
    });

    gui::PlayBackWidget::connect(mButtons.at(0), &QPushButton::pressed, [=]() { owner->mPushDelegate(PushType_Rewind); });
    gui::PlayBackWidget::connect(mButtons.at(1), &QPushButton::pressed, [=]() { owner->mPushDelegate(PushType_StepBack); });
    gui::PlayBackWidget::connect(mButtons.at(3), &QPushButton::pressed, [=]() { owner->mPushDelegate(PushType_Step); });
    gui::PlayBackWidget::connect(mButtons.at(4), &QPushButton::pressed, [=]() { owner->mPushDelegate(PushType_Fast); });
    gui::PlayBackWidget::connect(mButtons.at(6), &QPushButton::pressed, [=](){
        if(audioUI == nullptr || audioWidget == nullptr){
            audioWidget =  new AudioPlaybackWidget;
            audioUI = new QWidget(this, Qt::Window);
            audioWidget->setupUi(audioUI, &mediaPlayer, aConf);
        }

        if(aProject && aProject->mediaRefresh){
            owner->aConf = aProject->pConf;
            owner->mediaPlayer = *aProject->mediaPlayer;
            aProject->mediaRefresh = false;
        }
        if(!audioUI->isHidden()){
            return;
        }
        for(auto player: mediaPlayer.players){ player->stop(); }

        if(aProject && aProject->uiRefresh){
            audioWidget->rectifyUI(aProject->pConf, aProject->mediaPlayer, true);
            aProject->uiRefresh = false;
        }

        audioUI->show();
    });
}

bool PlayBackWidget::isLoopChecked() {
    PlayBackWidget* owner = this;
    bool isChecked = owner->mButtons.at(5)->isChecked();
    return isChecked;
}

void PlayBackWidget::checkLoop(bool checkStatus) {
    PlayBackWidget* owner = this;
    owner->mButtons.at(5)->setChecked(checkStatus);
}

void PlayBackWidget::PlayPause() {
    PlayBackWidget* owner = this;
    bool isChecked = owner->mButtons.at(2)->isChecked();
    // There are no functions currently available to check for playback, this'll do for now
    if (!isChecked) {
        auto name = "pause";
        owner->mButtons.at(2)->setIcon(owner->mGUIResources.icon(name));
        owner->mButtons.at(2)->setToolTip(tr("Pause"));
        owner->mPushDelegate(PushType_Play);
        owner->mButtons.at(2)->setChecked(true);
    } else {
        auto name = "play";
        owner->mButtons.at(2)->setIcon(owner->mGUIResources.icon(name));
        owner->mButtons.at(2)->setToolTip(tr("Play"));
        owner->mPushDelegate(PushType_Pause);
        owner->mButtons.at(2)->setChecked(false);
    }
}

int PlayBackWidget::constantWidth() {
    // This method might need re-evaluation.
    // It used to return kButtonSize + 10.
    // Now it should probably return widthHint() or minimumWidth().
    // For now, let's return a value based on a typical button's minimum width.
    if (!mButtons.empty()) {
        QFontMetrics fm(mButtons.at(0)->font());
        // Estimate based on typical icon + padding, or use minimumSizeHint().width()
        return fm.height() + 16; // fm.height() is a proxy for icon size, + padding
    }
    return 32 + 10; // Fallback if no buttons yet
}

void PlayBackWidget::pushPauseButton() {
    QPushButton* button = mButtons.at(2);
    if (button->isChecked()) {
        button->setIcon(mGUIResources.icon("play"));
        button->setChecked(false);
        mPushDelegate(PushType_Pause);
    }
}

QPushButton*
PlayBackWidget::createButton(const QString& aName, bool aIsCheckable, const QString& aToolTip) {
    auto* button = new QPushButton(/*this*/); // Parent will be set by layout
    XC_PTR_ASSERT(button);
    button->setObjectName(aName);
    button->setIcon(mGUIResources.icon(aName));
    // Calculate minimum size based on font
    QFontMetrics fm(button->font());
    int textHeight = fm.height();
    int iconHeight = fm.height(); // Assume icon is roughly text height for placeholder
                                  // A more robust way would be to get actual icon size if possible,
                                  // or have a predefined minimum icon dimension.

    // Determine a base size for the button, e.g., to fit an icon that's ~font height
    // Add some padding around it.
    int minButtonSize = qMax(textHeight, iconHeight) + 8; // 4px padding on each side of the icon/text vertically
                                                          // For square buttons, this will be width and height.

    button->setMinimumSize(minButtonSize, minButtonSize);
    button->setIconSize(QSize(iconHeight, iconHeight)); // Make icon size adaptive to font too

    button->setCheckable(aIsCheckable);
    button->setToolTip(aToolTip);
    button->setFocusPolicy(Qt::NoFocus);
    // button->setGeometry(0, 2 + kButtonSize * aColumn, kButtonSize, kButtonSize); // Removed
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred); // Allow expansion
    return button;
}

void PlayBackWidget::onThemeUpdated(theme::Theme& aTheme) {
    // Recalculate button minimum sizes if font might have changed with theme
    // (Though QApplication::setFont in GUIResources should handle global font,
    // specific theme stylesheets *could* also try to set font on widgets)
    for (QPushButton* button : mButtons) {
        QFontMetrics fm(button->font());
        int textHeight = fm.height();
        int iconHeight = fm.height();
        int minButtonSize = qMax(textHeight, iconHeight) + 8;
        button->setMinimumSize(minButtonSize, minButtonSize);
        button->setIconSize(QSize(iconHeight, iconHeight));
        // Re-set icon in case theme changed icon assets
        button->setIcon(mGUIResources.icon(button->objectName()));
    }

    QFile stylesheet(aTheme.path() + "/stylesheet/playbackwidget.ssa");
    if (stylesheet.open(QIODevice::ReadOnly | QIODevice::Text)) {
        this->setStyleSheet(QTextStream(&stylesheet).readAll());
    }

    if (!mButtons.empty()) {
        for (auto button : mButtons) {
            button->setIcon(mGUIResources.icon(button->objectName()));
        }
    }
}
bool PlayBackWidget::isPlaying() {  return this->mButtons.at(2)->isChecked(); }

} // namespace gui
