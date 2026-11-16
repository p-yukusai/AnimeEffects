#include "gui/PlayBackWidget.h"
#include "gui/IconButton.h"
#include <QFile>
#include <QTimer>
#include <QTextStream>
#include <functional>

namespace {
// The transport's button grid, in the header's own numbers: 28px buttons (the
// design's transport size; the column this replaced used 24px, which read small
// in a bar) with 20px icons and a 1px gap between them. The grid is
// absolute-geometry, and the QSS pins the same 28px box (playbackwidget.ssa),
// because a theme restyle resizes absolute children to the style's
// sizeFromContents unless the box is pinned in both places.
const int kButtonSize = 28;
const int kIconSize = 20;
const int kGap = 1;
const int kButtonCount = 8;
} // namespace

namespace gui {

PlayBackWidget::PlayBackWidget(GUIResources& aResources, QWidget* aParent, core::Project& mProject):
    QWidget(aParent), mGUIResources(aResources), mButtons(), mDoesLoop(true) {
    // Fixed in both axes: the header lays the group out at its own sizeHint and
    // never stretches it, so the 28px grid below is the geometry the user sees.
    this->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    aProject = &mProject;

    mButtons.push_back(createButton("skip-back", false, 0, tr("Return to initial frame")));
    mButtons.push_back(createButton("step-back", false, 1, tr("One frame back")));
    mButtons.push_back(createButton("play", true, 2, tr("Play")));
    mButtons.push_back(createButton("step-forward", false, 3, tr("One frame forward")));
    mButtons.push_back(createButton("skip-forward", false, 4, tr("Advance to final frame")));
    mButtons.push_back(createButton("arrows-clockwise", true, 5, tr("Loop playback")));
    mButtons.back()->setChecked(mDoesLoop);
    mButtons.push_back(createButton("faders-horizontal", false, 6, tr("Playback settings")));
    mButtons.push_back(createButton("speaker-high", false, 7, tr("Audio track")));
    // Frame nudge buttons repeat on hold: fire once on press, then every
    // 50ms after a 400ms hold (QAbstractButton re-emits pressed()).
    for (const int column : {1, 3}) {
        QPushButton* b = mButtons.at(column);
        b->setAutoRepeat(true);
        b->setAutoRepeatDelay(400);
        b->setAutoRepeatInterval(50);
    }
    audioWidget->setupUi(audioUI, &mediaPlayer, aConf);
    mGUIResources.onThemeChanged.connect(this, &PlayBackWidget::onThemeUpdated);
    // The audio window's speaker glyph is a runtime-tinted icon; refresh it
    // (and let the ToolSlider repaint) on theme changes.
    mGUIResources.onThemeChanged.connect(audioWidget, &AudioPlaybackWidget::onThemeUpdated);
}

void PlayBackWidget::setPushDelegate(const PushDelegate& aDelegate) {
    PlayBackWidget* owner = this;
    mPushDelegate = aDelegate;

    connect(mButtons.at(2), &QPushButton::pressed, [=]() {
        const bool isChecked = owner->mButtons.at(2)->isChecked();
        const char* name = isChecked ? "play" : "pause";
        owner->setButtonIcon(2, name);
        owner->mButtons.at(2)->setToolTip(isChecked ? tr("Play") : tr("Pause"));
        owner->mPushDelegate(isChecked ? PushType_Pause : PushType_Play);
    });
    // clicked (release), not pressed: the handler opens a modal exec() loop,
    // which would swallow the mouse release and leave the button stuck in the
    // pressed visual state.
    connect(mButtons.at(6), &QPushButton::clicked, [=]() { owner->mPushDelegate(PushType_Options); });

    // Loop toggle: checked state IS the live playback loop; the timeline
    // mirrors it via setPlayBackLoop and the loop shortcut via checkLoop().
    // It is called "Loop playback" so it cannot be read as the Project menu's
    // "Loop animation", which is a persisted attribute of the document (the
    // interpolation from the last key back to the first).
    connect(mButtons.at(5), &QPushButton::clicked, [=]() {
        owner->mDoesLoop = owner->mButtons.at(5)->isChecked();
        owner->mPushDelegate(PushType_Loop);
    });

    connect(mButtons.at(0), &QPushButton::pressed, [=]() { owner->mPushDelegate(PushType_Rewind); });
    connect(mButtons.at(1), &QPushButton::pressed, [=]() { owner->mPushDelegate(PushType_StepBack); });
    connect(mButtons.at(3), &QPushButton::pressed, [=]() { owner->mPushDelegate(PushType_Step); });
    connect(mButtons.at(4), &QPushButton::pressed, [=]() { owner->mPushDelegate(PushType_Fast); });
    connect(mButtons.at(7), &QPushButton::pressed, [=](){
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
    return mDoesLoop;
}

void PlayBackWidget::checkLoop(bool checkStatus) {
    mDoesLoop = checkStatus;
    if (mButtons.size() > 5) {
        mButtons.at(5)->setChecked(checkStatus);
    }
}

void PlayBackWidget::PlayPause() {
    PlayBackWidget* owner = this;
    bool isChecked = owner->mButtons.at(2)->isChecked();
    // There are no functions currently available to check for playback, this'll do for now
    if (!isChecked) {
        auto name = "pause";
        owner->setButtonIcon(2, name);
        owner->mButtons.at(2)->setToolTip(tr("Pause"));
        owner->mPushDelegate(PushType_Play);
        owner->mButtons.at(2)->setChecked(true);
    } else {
        auto name = "play";
        owner->setButtonIcon(2, name);
        owner->mButtons.at(2)->setToolTip(tr("Play"));
        owner->mPushDelegate(PushType_Pause);
        owner->mButtons.at(2)->setChecked(false);
    }
}

QSize PlayBackWidget::sizeHint() const {
    // The transport is one horizontal row: kButtonCount buttons with a gap between
    // them (its outer inset is the header's own padding, not the group's).
    return QSize(kButtonSize * kButtonCount + kGap * (kButtonCount - 1), kButtonSize);
}

QSize PlayBackWidget::minimumSizeHint() const {
    return sizeHint();
}

void PlayBackWidget::pushPauseButton() {
    QPushButton* button = mButtons.at(2);
    if (button->isChecked()) {
        setButtonIcon(2, QStringLiteral("play"));
        button->setChecked(false);
        mPushDelegate(PushType_Pause);
    }
}

void PlayBackWidget::setButtonIcon(int aIndex, const QString& aName) {
    auto* b = static_cast<IconButton*>(mButtons.at(aIndex));
    b->setRestIcon(mGUIResources.icon(aName));
    b->setHoverIcon(mGUIResources.iconActive(aName));
}

QPushButton*
PlayBackWidget::createButton(const QString& aName, bool aIsCheckable, int aColumn, const QString& aToolTip) {
    auto* button = new IconButton(this);
    XC_PTR_ASSERT(button);
    button->setObjectName(aName);
    button->setRestIcon(mGUIResources.icon(aName));
    button->setHoverIcon(mGUIResources.iconActive(aName));
    button->setIconSize(QSize(kIconSize, kIconSize));
    button->setCheckable(aIsCheckable);
    if (aIsCheckable) {
        // active-state button: press reads as hover, not the sunken press
        button->setProperty("activeButton", true);
    }
    button->setToolTip(aToolTip);
    button->setFocusPolicy(Qt::NoFocus);
    // The QSS width/height rule pins the box; geometry alone is not enough
    // because theme restyles resize absolute children to the style's
    // sizeFromContents (icon + button margins -> 29px tall). The column index is
    // the button's place in the row.
    button->setGeometry((kButtonSize + kGap) * aColumn, 0, kButtonSize, kButtonSize);
    return button;
}

void PlayBackWidget::onThemeUpdated(theme::Theme& aTheme) {
    this->setStyleSheet(aTheme.loadStylesheet("playbackwidget.ssa"));

    // Theme restyles (the cascade through MainWindow) resize these
    // absolute-positioned children to the style's sizeFromContents (icon +
    // button margins), which sticks at 29px tall even after the QSS pins
    // the sizeHint to 28x28. Re-assert the fixed grid once the restyle
    // cascade has settled.
    QTimer::singleShot(0, [this]() {
        for (int i = 0; i < (int)mButtons.size(); ++i) {
            mButtons[i]->setGeometry((kButtonSize + kGap) * i, 0, kButtonSize, kButtonSize);
        }
    });

    if (!mButtons.empty()) {
        for (int i = 0; i < (int)mButtons.size(); ++i) {
            // The play button shows pause while playing; its objectName stays
            // "play", so pick the glyph by the check state here.
            const QString name = (i == 2 && mButtons.at(2)->isChecked())
                                     ? QStringLiteral("pause") : mButtons.at(i)->objectName();
            setButtonIcon(i, name);
        }
    }
}
bool PlayBackWidget::isPlaying() {  return this->mButtons.at(2)->isChecked(); }

} // namespace gui
