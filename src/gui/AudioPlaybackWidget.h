#ifndef ANIMEEFFECTS_AUDIOPLAYBACKWIDGET_H
#define ANIMEEFFECTS_AUDIOPLAYBACKWIDGET_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QWidget>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QtMultimedia/QMediaPlayer>
#include <QtMultimedia/QAudioOutput>

#include "gui/tool/ToolSlider.h"

namespace core { class Project; }
namespace theme { class Theme; }

struct audioConfig{
    QString audioName = "Placeholder";
    QFileInfo audioPath = QFileInfo();
    bool playbackEnable = true;
    int volume = 100;
    int startFrame = 0;
    int endFrame = 500;
};
struct mediaState{
    bool playing = false;
    QVector<QMediaPlayer*> players;
    QVector<QAudioOutput*> outputs;
};

struct UIState{
    QToolButton *addNewTrack{ new QToolButton };
    QToolButton *selectMusButton{ new QToolButton };
    QCheckBox *playAudio{ new QCheckBox };
    QSpinBox *endSpinBox{ new QSpinBox };
    QSpinBox *startSpinBox{ new QSpinBox };
    QLabel *startLabel{ new QLabel };
    QLabel *endLabel{ new QLabel };
    QLabel *musDurationLabel{ new QLabel };
    QLabel *volumeLabel{ new QLabel };
    QLabel *volumeIcon{ new QLabel };
    // Custom-painted like the tool-panel sliders: QSS cannot draw a clean
    // circular head on a thin track (see ToolSlider), so the volume row
    // shares the same widget instead of a QSS-styled QSlider.
    QSlider *volumeSlider{ new gui::tool::ToolSlider };
    QFrame *line{ new QFrame };
    mutable bool addTrack = true;
};

class AudioPlaybackWidget {
public:
    QGridLayout *gridLayout{};
    QTabWidget *tabWidget{};
    QWidget *musPlayer{};
    QHBoxLayout *horizontalLayout{};
    QScrollArea *musScroll{};
    QWidget *musTab{};
    QGridLayout *gridLayout_2{};
    QVector<UIState> vecUIState{};
    QWidget *configTab{};
    QGridLayout *gridLayout_3{};
    QPushButton *saveConfigButton{};
    QPushButton *loadConfigButton{};
    QWidget *mainWidget{};

    void connect(QWidget *audioWidget, mediaState *state, std::vector<audioConfig>* config);
    void addUIState(std::vector<audioConfig>* config, int index, mediaState *mediaPlayer, bool bulk = false);
    void rectifyUI(std::vector<audioConfig>* config, mediaState* mediaPlayer, bool bulk = true);
    // Re-fetch the runtime-tinted speaker glyph after a theme change.
    void onThemeUpdated(theme::Theme&);
    static void addTrack(mediaState *state, const QUrl& source);
    static void modifyTrack(mediaState *state, std::vector<audioConfig>* config, int index);
    static void removeTrack(mediaState *state, int index);
    static void aPlayer(std::vector<audioConfig>* pConf, bool play, mediaState* state, int fps, int curFrame);
    static bool serialize(std::vector<audioConfig>* pConf, const QString& outPath);
    static bool deserialize(const QJsonObject& pConf, std::vector<audioConfig>* playbackConfig) ;
    static float getVol(int volume){ return static_cast<float>(volume / 100.0); }
    static std::vector<audioConfig> getValidAudioStreams(const std::vector<audioConfig>& pConf){
        std::vector<audioConfig> conf;
        for(auto config: pConf){
            if(config.startFrame > config.endFrame){ std::swap(config.startFrame, config.endFrame); }
            if( config.playbackEnable && config.startFrame - config.endFrame <= 0 &&
                config.audioPath.exists() && config.audioPath.isReadable()){
                conf.emplace_back(config);
            }
        }
        return conf;
    }
    static void correctTrackPos(QMediaPlayer* player, int curFrame, int fps, audioConfig& config);
    // Keep every track's position aligned with the playhead; call on each frame
    // change while playing. prevFrame is the previously played frame — a jump
    // of more than one frame repositions the track, normal stepping leaves it
    // to its own clock.
    static void syncTracks(core::Project* aProject, int curFrame, int prevFrame, int fps);
    void setupUi(QWidget *audioWidget, mediaState *mediaPlayer, std::vector<audioConfig>* config){
        if (audioWidget->objectName().isEmpty()) {audioWidget->setObjectName(QString::fromUtf8("audioWidget")); }
        mainWidget = audioWidget;
        audioWidget->resize(648, 291);
        // Grid
        gridLayout = new QGridLayout(audioWidget);
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        // Tabs
        tabWidget = new QTabWidget(audioWidget);
        tabWidget->setObjectName(QString::fromUtf8("tabWidget"));
        // Main widget
        musPlayer = new QWidget();
        musPlayer->setObjectName(QString::fromUtf8("musPlayer"));
        musPlayer->setAcceptDrops(false);
        // Layout
        horizontalLayout = new QHBoxLayout(musPlayer);
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        // Scroll area
        musScroll = new QScrollArea(musPlayer);
        musScroll->setObjectName(QString::fromUtf8("musScroll"));
        musScroll->setAcceptDrops(true);
        musScroll->setWidgetResizable(true);
        // Main window
        musTab = new QWidget();
        musTab->setObjectName(QString::fromUtf8("musTab"));
        musTab->setGeometry(QRect(0, 0, 606, 220));
        // Grid layout
        gridLayout_2 = new QGridLayout(musTab);
        gridLayout_2->setObjectName(QString::fromUtf8("gridLayout_2"));
        // Serialization/Deserialization widget
        configTab = new QWidget();
        configTab->setObjectName(QString::fromUtf8("configTab"));
        // Grid layout
        gridLayout_3 = new QGridLayout(configTab);
        gridLayout_3->setObjectName(QString::fromUtf8("gridLayout_3"));
        // Save button
        saveConfigButton = new QPushButton(configTab);
        saveConfigButton->setObjectName(QString::fromUtf8("saveConfigButton"));
        // Load button
        loadConfigButton = new QPushButton(configTab);
        loadConfigButton->setObjectName(QString::fromUtf8("loadConfigButton"));
        // Setup
        musScroll->setWidget(musTab);
        horizontalLayout->addWidget(musScroll);
        tabWidget->addTab(musPlayer, QString());
        gridLayout_3->addWidget(saveConfigButton, 0, 0, 1, 1);
        gridLayout_3->addWidget(loadConfigButton, 1, 0, 1, 1);
        tabWidget->addTab(configTab, QString());
        gridLayout->addWidget(tabWidget, 0, 0, 1, 1);
        // Translate
        audioWidget->setWindowTitle(QCoreApplication::translate("audioWidget", "Audio Player", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(musPlayer), QCoreApplication::translate("audioWidget", "Audio player", nullptr));
        saveConfigButton->setText(QCoreApplication::translate("audioWidget", "Save current audio configuration", nullptr));
        loadConfigButton->setText(QCoreApplication::translate("audioWidget", "Load audio configuration from file", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(configTab), QCoreApplication::translate("audioWidget", "Save/Load audio config", nullptr));
        // Initialize
        QMetaObject::connectSlotsByName(audioWidget);
        config->emplace_back();
        rectifyUI(config, mediaPlayer);
        connect(audioWidget, mediaPlayer, config);
        tabWidget->setCurrentIndex(0);
    }
};

#endif // ANIMEEFFECTS_AUDIOPLAYBACKWIDGET_H
