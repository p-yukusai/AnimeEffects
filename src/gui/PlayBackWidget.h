#ifndef GUI_PLAYBACKWIDGET_H
#define GUI_PLAYBACKWIDGET_H

#include <vector>
#include <functional>
#include <QWidget>
#include <QPushButton>
#include "gui/GUIResources.h"
#include "gui/AudioPlaybackWidget.h"
#include "core/Project.h"

namespace gui {

class PlayBackWidget: public QWidget {
    Q_OBJECT
public:
    enum PushType {
        PushType_Play,
        PushType_Pause,
        PushType_Step,
        PushType_StepBack,
        PushType_Rewind,
        PushType_Options,
        PushType_Fast,
        PushType_Loop
    };
    typedef std::function<void(PushType)> PushDelegate;

    PlayBackWidget(GUIResources& aResources, QWidget* aParent, core::Project& aProject);

    void setPushDelegate(const PushDelegate& aDelegate);

    // The options button (index 6); used to anchor the animation-settings popup.
    QPushButton* optionsButton() const { return mButtons.at(6); }
    bool isLoopChecked();
    void checkLoop(bool checkStatus);
    void PlayPause();
    bool isPlaying();
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void pushPauseButton();
    // The event is part of the QWidget::paintEvent signature, which an override
    // cannot change, and this widget paints a fixed 1px hairline regardless of
    // the damaged region. Left unnamed to say so, rather than carrying a name
    // the body never uses (there is no way to remove the parameter itself).
    void paintEvent(QPaintEvent*) override;
    core::Project* aProject;
    QWidget* audioUI = new QWidget(this, Qt::Window);
    std::vector<audioConfig>* aConf = new std::vector<audioConfig>;
    mediaState mediaPlayer;
    AudioPlaybackWidget* audioWidget = new AudioPlaybackWidget;


private:
    QPushButton* createButton(const QString& aName, bool aIsCheckable, int aColumn, const QString& aToolTip);
    // Set a playback button's rest icon and its hover counterpart in one go
    // (the play/pause glyph swap must keep the hover in sync).
    void setButtonIcon(int aIndex, const QString& aName);
    GUIResources& mGUIResources;
    std::vector<QPushButton*> mButtons;
    PushDelegate mPushDelegate;
    bool mDoesLoop;
    void onThemeUpdated(theme::Theme&);
};

} // namespace gui

#endif // GUI_PLAYBACKWIDGET_H
