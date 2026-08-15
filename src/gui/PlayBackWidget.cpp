#include "gui/PlayBackWidget.h"
#include <QFile>
#include <QPainter>
#include <QTimer>
#include <QTextStream>
#include <functional>
#include "gui/theme/Colors.h"

namespace {
const int kButtonSize = 24;
const int kIconSize = 18;
const int kGap = 4;
const int kButtonCount = 8;
const int kLeftMargin = 4;

// A playback button whose glyph brightens to the active (max-contrast) color
// on hover — the icon participates in the hover feedback, not just the
// background. setRestIcon() records the resting icon, so the play/pause glyph
// swap keeps working; the hover icon is set explicitly via setHoverIcon and
// re-set on theme changes. (QAbstractButton::setIcon is not virtual, so the
// rest icon is tracked through the explicit setter only.)
class IconButton : public QPushButton {
public:
    using QPushButton::QPushButton;
    void setRestIcon(const QIcon& aIcon) { mRest = aIcon; QPushButton::setIcon(aIcon); }
    void setHoverIcon(const QIcon& aIcon) { mHover = aIcon; }
protected:
    void enterEvent(QEnterEvent*) override {
        if (isEnabled() && !mHover.isNull()) QPushButton::setIcon(mHover);
    }
    void leaveEvent(QEvent*) override { QPushButton::setIcon(mRest); }
private:
    QIcon mRest;
    QIcon mHover;
};
} // namespace

namespace gui {

PlayBackWidget::PlayBackWidget(GUIResources& aResources, QWidget* aParent, core::Project& mProject):
    QWidget(aParent), mGUIResources(aResources), mButtons(), mDoesLoop(true) {
    // Fixed width (the layout must never stretch the toolbar), full height
    // (the column fills the dock; the buttons stay top-anchored).
    this->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    aProject = &mProject;

    mButtons.push_back(createButton("rewind", false, 0, tr("Return to initial frame")));
    mButtons.push_back(createButton("step-back", false, 1, tr("One frame back")));
    mButtons.push_back(createButton("play", true, 2, tr("Play")));
    mButtons.push_back(createButton("step", false, 3, tr("One frame forward")));
    mButtons.push_back(createButton("fast", false, 4, tr("Advance to final frame")));
    mButtons.push_back(createButton("loop", true, 5, tr("Loop animation")));
    mButtons.back()->setChecked(mDoesLoop);
    mButtons.push_back(createButton("faders-horizontal", false, 6, tr("Animation settings")));
    mButtons.push_back(createButton("audio", false, 7, tr("Audio track")));
    audioWidget->setupUi(audioUI, &mediaPlayer, aConf);
    mGUIResources.onThemeChanged.connect(this, &PlayBackWidget::onThemeUpdated);
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
    // mirrors it via setPlayBackLoop and the loop shortcut via checkLoop()
    // (the Project menu's "Loop animation" is a separate persisted attribute).
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
    // Symmetric side padding: left and right margins are the same.
    return QSize(kButtonSize + kLeftMargin * 2, (kButtonSize + kGap) * kButtonCount);
}

QSize PlayBackWidget::minimumSizeHint() const {
    return sizeHint();
}

void PlayBackWidget::paintEvent(QPaintEvent* aEvent) {
    // The playback bar is a fixed toolbar beside the timeline; the 1px
    // hairline (the design system's section-separator token) marks its left
    // edge. Painted here, like the ThinSplitter/DockSash hairlines, because
    // QSS box rules do not render on plain widget surfaces.
    QPainter painter(this);
    painter.fillRect(0, 0, 1, height(), theme::Colors::current().hairline);
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
    button->setToolTip(aToolTip);
    button->setFocusPolicy(Qt::NoFocus);
    // The QSS width/height rule pins the box; geometry alone is not enough
    // because theme restyles resize absolute children to the style's
    // sizeFromContents (icon + button margins -> 29px tall).
    button->setGeometry(kLeftMargin, 2 + (kButtonSize + kGap) * aColumn, kButtonSize, kButtonSize);
    return button;
}

void PlayBackWidget::onThemeUpdated(theme::Theme& aTheme) {
    this->setStyleSheet(aTheme.loadStylesheet("playbackwidget.ssa"));

    // Theme restyles (the cascade through MainWindow) resize these
    // absolute-positioned children to the style's sizeFromContents (icon +
    // button margins), which sticks at 29px tall even after the QSS pins
    // the sizeHint to 24x24. Re-assert the fixed grid once the restyle
    // cascade has settled.
    QTimer::singleShot(0, [this]() {
        for (int i = 0; i < (int)mButtons.size(); ++i) {
            mButtons[i]->setGeometry(kLeftMargin, 2 + (kButtonSize + kGap) * i, kButtonSize, kButtonSize);
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
