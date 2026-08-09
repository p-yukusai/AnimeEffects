#include "gui/PlayBackWidget.h"
#include <QFile>
#include <QTextStream>
#include <functional>

namespace {
int kButtonSize = 24;
int kIconSize = 20;
const int kGap = 4;
const int kButtonCount = 7;
} // namespace

namespace gui {

PlayBackWidget::PlayBackWidget(GUIResources& aResources, QWidget* aParent, core::Project& mProject):
    QWidget(aParent), mGUIResources(aResources), mButtons() {
    if (mGUIResources.getTheme().contains("high_dpi")) {
        kButtonSize = 28;
        kIconSize = 24;
    }
    this->setGeometry(0, 0, kButtonSize, (kButtonSize + kGap) * kButtonCount);
    aProject = &mProject;

    mButtons.push_back(createButton("rewind", false, 0, tr("Return to initial frame")));
    mButtons.push_back(createButton("step-back", false, 1, tr("One frame back")));
    mButtons.push_back(createButton("play", true, 2, tr("Play")));
    mButtons.push_back(createButton("step", false, 3, tr("One frame forward")));
    mButtons.push_back(createButton("fast", false, 4, tr("Advance to final frame")));
    mButtons.push_back(createButton("loop", true, 5, tr("Loop")));
    mButtons.push_back(createButton("audio", false, 6, tr("Audio track")));
    audioWidget->setupUi(audioUI, &mediaPlayer, aConf);
    mGUIResources.onThemeChanged.connect(this, &PlayBackWidget::onThemeUpdated);
}

void PlayBackWidget::setPushDelegate(const PushDelegate& aDelegate) {
    PlayBackWidget* owner = this;
    mPushDelegate = aDelegate;

    connect(mButtons.at(2), &QPushButton::pressed, [=]() {
        const bool isChecked = owner->mButtons.at(2)->isChecked();
        const char* name = isChecked ? "play" : "pause";
        owner->mButtons.at(2)->setIcon(owner->mGUIResources.icon(name));
        owner->mButtons.at(2)->setToolTip(isChecked ? tr("Play") : tr("Pause"));
        owner->mPushDelegate(isChecked ? PushType_Pause : PushType_Play);
    });
    connect(mButtons.at(5), &QPushButton::pressed, [=]() {
        const bool isChecked = owner->mButtons.at(5)->isChecked();
        owner->mPushDelegate(isChecked ? PushType_NoLoop : PushType_Loop);
    });

    connect(mButtons.at(0), &QPushButton::pressed, [=]() { owner->mPushDelegate(PushType_Rewind); });
    connect(mButtons.at(1), &QPushButton::pressed, [=]() { owner->mPushDelegate(PushType_StepBack); });
    connect(mButtons.at(3), &QPushButton::pressed, [=]() { owner->mPushDelegate(PushType_Step); });
    connect(mButtons.at(4), &QPushButton::pressed, [=]() { owner->mPushDelegate(PushType_Fast); });
    connect(mButtons.at(6), &QPushButton::pressed, [=](){
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

int PlayBackWidget::constantWidth() { return kButtonSize + 10; }

void PlayBackWidget::pushPauseButton() {
    QPushButton* button = mButtons.at(2);
    if (button->isChecked()) {
        button->setIcon(mGUIResources.icon("play"));
        button->setChecked(false);
        mPushDelegate(PushType_Pause);
    }
}

QPushButton*
PlayBackWidget::createButton(const QString& aName, bool aIsCheckable, int aColumn, const QString& aToolTip) {
    auto* button = new QPushButton(this);
    XC_PTR_ASSERT(button);
    button->setObjectName(aName);
    button->setIcon(mGUIResources.icon(aName));
    button->setIconSize(QSize(kIconSize, kIconSize));
    button->setCheckable(aIsCheckable);
    button->setToolTip(aToolTip);
    button->setFocusPolicy(Qt::NoFocus);
    button->setGeometry(0, 2 + (kButtonSize + kGap) * aColumn, kButtonSize, kButtonSize);
    return button;
}

void PlayBackWidget::onThemeUpdated(theme::Theme& aTheme) {
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
