#ifndef GUI_PLAYBACKWIDGET_H
#define GUI_PLAYBACKWIDGET_H

#include <vector>
#include <functional>
#include <QWidget>
#include <QPushButton>
#include "gui/GUIResources.h"
#include "gui/AudioPlaybackWidget.h"

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
        PushType_Loop,
        PushType_NoLoop,
        PushType_Fast
    };
    typedef std::function<void(PushType)> PushDelegate;

    PlayBackWidget(GUIResources& aResources, QWidget* aParent);

    void setPushDelegate(const PushDelegate& aDelegate);
    bool isLoopChecked();
    void checkLoop(bool checkStatus);
    void PlayPause();
    static int constantWidth() ;
    void pushPauseButton();
    QWidget* audioUI = nullptr;
    std::vector<audioConfig>* pConf;
    QMediaPlayer* qmp = new QMediaPlayer;
    QAudioOutput* qao = new QAudioOutput;
    mediaState mediaPlayer {qmp, qao};

private:
    QPushButton* createButton(const QString& aName, bool aIsCheckable, int aColumn, const QString& aToolTip);
    GUIResources& mGUIResources;
    std::vector<QPushButton*> mButtons;
    PushDelegate mPushDelegate;
    void onThemeUpdated(theme::Theme&);
};

} // namespace gui

#endif // GUI_PLAYBACKWIDGET_H
