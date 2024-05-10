#include "gui/PlayBackWidget.h"
#include <QFile>
#include <QTextStream>
#include <functional>
#include "gui/AudioPlaybackWidget.h"

namespace {
int kButtonSize = 28;
const int kButtonCount = 6;
} // namespace

namespace gui {

PlayBackWidget::PlayBackWidget(GUIResources& aResources, QWidget* aParent):
    QWidget(aParent), mGUIResources(aResources), mButtons() {
    if (mGUIResources.getTheme().contains("high_dpi")) {
        kButtonSize = 34;
    }
    this->setGeometry(0, 0, kButtonSize, kButtonSize * kButtonCount);

    mButtons.push_back(createButton("rewind", false, 0, tr("Return to initial frame")));
    mButtons.push_back(createButton("stepback", false, 1, tr("One frame back")));
    mButtons.push_back(createButton("play", true, 2, tr("Play")));
    mButtons.push_back(createButton("step", false, 3, tr("One frame forward")));
    mButtons.push_back(createButton("fast", false, 4, tr("Advance to final frame")));
    mButtons.push_back(createButton("loop", true, 5, tr("Loop")));
    mButtons.push_back(createButton("audio", true, 6, tr("Audio track")));

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
        if(mButtons.at(6)->isChecked()){
            mButtons.at(6)->setChecked(true); // Avoid unchecking accidentally
            return;
        }
        mButtons.at(6)->setChecked(true);
        auto audioWidget = new AudioPlaybackWidget;
        auto* audioUI = new QWidget(this, Qt::Window);
        audioWidget->setupUi(audioUI);
        audioUI->show();
        mButtons.at(6)->setChecked(false);
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
    button->setIconSize(QSize(kButtonSize, kButtonSize));
    button->setCheckable(aIsCheckable);
    button->setToolTip(aToolTip);
    button->setFocusPolicy(Qt::NoFocus);
    button->setGeometry(0, 2 + kButtonSize * aColumn, kButtonSize, kButtonSize);
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

} // namespace gui
