#ifndef ANIMEEFFECTS_AUDIOPLAYBACKWIDGET_H
#define ANIMEEFFECTS_AUDIOPLAYBACKWIDGET_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QWidget>
#include <QtWidgets/QSlider>
#include <QDir>
#include <QFileInfo>
#include <QtMultimedia/QAudio>
#include <QtMultimedia/QMediaPlayer>
#include <QtMultimedia/QAudioOutput>
#include <utility>

struct audioConfig{
    QString audioName = "Placeholder";
    QFileInfo audioPath = QFileInfo();
    bool playbackEnable = true;
    int volume = 50;
    int startFrame = 0;
    int endFrame = 0;
};
struct mediaState{
    bool playing = false;
    QVector<QMediaPlayer*> players;
    QVector<QAudioOutput*> outputs;
};

struct UIState{
    QToolButton *addNewTrack;
    QToolButton *selectMusButton;
    QCheckBox *playAudio;
    QSpinBox *endSpinBox;
    QSpinBox *startSpinBox;
    QLabel *startLabel;
    QLabel *endLabel;
    QLabel *musDurationLabel;
    QLabel *volumeLabel;
    QSlider *volumeSlider;
    QFrame *line;
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

    void connectUIState(QVector<UIState> *state, std::vector<audioConfig>* config);
    void connectUI(QWidget *audioWidget, mediaState *state, std::vector<audioConfig>* config);

    void setupUi(QWidget *audioWidget, mediaState *mediaPlayer, std::vector<audioConfig>* config){
        if (audioWidget->objectName().isEmpty())
            audioWidget->setObjectName(QString::fromUtf8("audioWidget"));
        audioWidget->resize(648, 291);

        gridLayout = new QGridLayout(audioWidget);
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));

        tabWidget = new QTabWidget(audioWidget);
        tabWidget->setObjectName(QString::fromUtf8("tabWidget"));

        musPlayer = new QWidget();
        musPlayer->setObjectName(QString::fromUtf8("musPlayer"));
        musPlayer->setAcceptDrops(false);

        horizontalLayout = new QHBoxLayout(musPlayer);
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));

        musScroll = new QScrollArea(musPlayer);
        musScroll->setObjectName(QString::fromUtf8("musScroll"));
        musScroll->setAcceptDrops(true);
        musScroll->setWidgetResizable(true);

        musTab = new QWidget();
        musTab->setObjectName(QString::fromUtf8("musTab"));
        musTab->setGeometry(QRect(0, 0, 606, 220));
        gridLayout_2 = new QGridLayout(musTab);
        gridLayout_2->setObjectName(QString::fromUtf8("gridLayout_2"));

        musScroll->setWidget(musTab);
        horizontalLayout->addWidget(musScroll);

        tabWidget->addTab(musPlayer, QString());
        configTab = new QWidget();
        configTab->setObjectName(QString::fromUtf8("configTab"));
        gridLayout_3 = new QGridLayout(configTab);
        gridLayout_3->setObjectName(QString::fromUtf8("gridLayout_3"));
        saveConfigButton = new QPushButton(configTab);
        saveConfigButton->setObjectName(QString::fromUtf8("saveConfigButton"));
        gridLayout_3->addWidget(saveConfigButton, 0, 0, 1, 1);

        loadConfigButton = new QPushButton(configTab);
        loadConfigButton->setObjectName(QString::fromUtf8("loadConfigButton"));
        gridLayout_3->addWidget(loadConfigButton, 1, 0, 1, 1);

        tabWidget->addTab(configTab, QString());
        gridLayout->addWidget(tabWidget, 0, 0, 1, 1);


        translateUi(audioWidget);
        connectUI(audioWidget, mediaPlayer, config);
        QMetaObject::connectSlotsByName(audioWidget);
        config->emplace_back();
        addUIState(config, 0);
        connectUIState(&vecUIState, config);
        tabWidget->setCurrentIndex(0);
    }
    void translateUi(QWidget *audioWidget) const{
        audioWidget->setWindowTitle(QCoreApplication::translate("audioWidget", "Audio Player", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(musPlayer), QCoreApplication::translate("audioWidget", "Audio player", nullptr));
        saveConfigButton->setText(QCoreApplication::translate("audioWidget", "Save current audio configuration", nullptr));
        loadConfigButton->setText(QCoreApplication::translate("audioWidget", "Load audio configuration from file", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(configTab), QCoreApplication::translate("audioWidget", "Save/Load audio config", nullptr));
    }

    void addUIState(std::vector<audioConfig>* config, int index, bool bulk = false){
        // This is a great time to tell you that we accept pull requests that fix this kind of nonsense
        auto newState =
            UIState{new QToolButton(musPlayer), new QToolButton(musPlayer), new QCheckBox(musPlayer),
                    new QSpinBox(musPlayer), new QSpinBox(musPlayer), new QLabel(musPlayer), new QLabel(musPlayer),
                    new QLabel(musPlayer), new QLabel(musPlayer), new QSlider(musPlayer), new QFrame(musPlayer)
            };

        int idx = (int)vecUIState.size();
        int offset = idx * 4;
        int idxRow0 = offset;
        int idxRow1 = offset + 1;
        int idxRow2 = offset + 2;
        int idxRow3 = offset + 3;

        newState.playAudio->setObjectName(QString::fromUtf8("playAudio"));
        gridLayout_2->addWidget(newState.playAudio, idxRow1, 0, 1, 1);

        newState.endSpinBox->setObjectName(QString::fromUtf8("endSpinBox"));
        gridLayout_2->addWidget(newState.endSpinBox, idxRow1, 2, 1, 1);

        newState.startSpinBox->setObjectName(QString::fromUtf8("startSpinBox"));
        gridLayout_2->addWidget(newState.startSpinBox, idxRow1, 1 , 1, 1);

        newState.addNewTrack->setObjectName(QString::fromUtf8("startLabel"));
        QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(newState.addNewTrack->sizePolicy().hasHeightForWidth());
        newState.addNewTrack->setSizePolicy(sizePolicy);
        gridLayout_2->addWidget(newState.addNewTrack, idxRow0, 0, 1, 1);

        newState.startLabel->setObjectName(QString::fromUtf8("startLabel"));
        sizePolicy.setHeightForWidth(newState.startLabel->sizePolicy().hasHeightForWidth());
        newState.startLabel->setSizePolicy(sizePolicy);
        gridLayout_2->addWidget(newState.startLabel, idxRow0, 1, 1, 1);

        newState.endLabel->setObjectName(QString::fromUtf8("endLabel"));
        sizePolicy.setHeightForWidth(newState.endLabel->sizePolicy().hasHeightForWidth());
        newState.endLabel->setSizePolicy(sizePolicy);
        gridLayout_2->addWidget(newState.endLabel, idxRow0, 2, 1, 1);

        newState.volumeLabel->setObjectName(QString::fromUtf8("volumeLabel"));
        newState.volumeLabel->setSizePolicy(sizePolicy);
        gridLayout_2->addWidget(newState.volumeLabel, idxRow2, 0, 1, 1);

        newState.volumeSlider->setObjectName(QString::fromUtf8("volumeSlider"));
        newState.volumeSlider->setMaximum(100);
        newState.volumeSlider->setMinimum(0);
        newState.volumeSlider->setOrientation(Qt::Horizontal);
        newState.volumeSlider->setSizePolicy(sizePolicy);
        gridLayout_2->addWidget(newState.volumeSlider, idxRow2, 1, 1, 3);

        newState.line->setObjectName(QString::fromUtf8("line"));
        newState.line->setFrameShape(QFrame::HLine);
        newState.line->setFrameShadow(QFrame::Sunken);
        gridLayout_2->addWidget(newState.line, idxRow3, 0, 1, 4);

        newState.musDurationLabel->setObjectName(QString::fromUtf8("musDurationLabel"));
        gridLayout_2->addWidget(newState.musDurationLabel, idxRow0, 3, 1, 1);

        newState.selectMusButton->setObjectName(QString::fromUtf8("selectMusButton"));
        newState.selectMusButton->setAutoRepeat(false);
        gridLayout_2->addWidget(newState.selectMusButton, idxRow1, 3, 1, 1);

        newState.playAudio->setText(QCoreApplication::translate("audioWidget", "Enable playback", nullptr));
        newState.addNewTrack->setText(QCoreApplication::translate("audioWidget", "Add new audio track", nullptr));
        newState.startLabel->setText(QCoreApplication::translate("audioWidget", "<html><head/><body><p align=\"center\">Playback start frame</p></body></html>", nullptr));
        newState.endLabel->setText(QCoreApplication::translate("audioWidget", "<html><head/><body><p align=\"center\">Playback end frame</p></body></html>", nullptr));
        newState.musDurationLabel->setText(QCoreApplication::translate("audioWidget", "<html><head/><body><p align=\"center\">Duration (in frames): </p></body></html>", nullptr) +
                                           "<html><head/><body><p align=\"center\">" + QString::number(config->at(index).endFrame - config->at(index).startFrame) + "</p></body></html>");
        newState.volumeLabel->setText(QCoreApplication::translate("audioWidget", "Media volume", nullptr) + " (%" + QString::number(config->at(index).volume) + ")");


        newState.playAudio->setChecked(config->at(index).playbackEnable);
        newState.startSpinBox->setValue(config->at(index).startFrame);
        newState.endSpinBox->setValue(config->at(index).endFrame);
        newState.volumeSlider->setValue(config->at(index).volume);
        newState.selectMusButton->setText(config->at(index).audioName == "Placeholder"? QCoreApplication::translate("audioWidget", "Select audio file...", nullptr) : config->at(index).audioName);

        if(bulk){
            if (index > 0 && index + 1 > idx) {
                newState.addNewTrack->setText(QCoreApplication::translate("audioWidget", "Remove audio track", nullptr));
                newState.addTrack = false;
            } else if (idx > 1) {
                newState.addNewTrack->setDisabled(true);
            }
            else{
                newState.addTrack = true;
            }
        }

        vecUIState.append(newState);
    }

    void removeUIState(std::vector<audioConfig>* config){
        // We reconstruct the entire state so we don't mismatch the UI indices
        {
            QLayoutItem* item;
            while ((item = gridLayout_2->takeAt(0)) != nullptr) {
                delete item->widget();
                delete item;
            }
            delete gridLayout_2;
        }
        gridLayout_2 = new QGridLayout(musTab);
        gridLayout_2->setObjectName(QString::fromUtf8("gridLayout_2"));
        QCoreApplication::processEvents();
        vecUIState.clear();
        rectifyUI(config);
    }

    void rectifyUI(std::vector<audioConfig>* config){
        int x = 0;
        for(auto conf: *config){
            addUIState(config, x, true);
            x++;
        }
        gridLayout_2->update();
    }

    static void addTrack(mediaState *state, QUrl source){
        auto* mediaPlayer = new QMediaPlayer;
        auto* audioOutput = new QAudioOutput;
        mediaPlayer->setSource(source);
        mediaPlayer->setAudioOutput(audioOutput);
        state->players.append(mediaPlayer);
        state->outputs.append(audioOutput);
    }
    static void modifyTrack(mediaState *state, std::vector<audioConfig>* config, int index, audioConfig modifiedConfig){
        config->at(index) = std::move(modifiedConfig);
        auto* currentPlayer = state->players.at(index);
        auto* currentOutput = state->outputs.at(index);
        if(currentPlayer->isPlaying()) { currentPlayer->stop(); }
        currentPlayer->setSource(modifiedConfig.audioPath.absoluteFilePath());
        currentOutput->setVolume((float)modifiedConfig.volume);
    }
    static void removeTrack(mediaState *state, std::vector<audioConfig>* config, int index){
        Q_UNIMPLEMENTED();
    }
    static void aPlayer(std::vector<audioConfig>* pConf, bool play, mediaState* state, int fps, int curFrame, int frameCount);
    static bool serialize(std::vector<audioConfig>* pConf, const QString& outPath);
    static bool deserialize(const QJsonObject& pConf, std::vector<audioConfig>* playbackConfig) ;
    static float getVol(int volume){ return static_cast<float>(volume / 100.0); }
};

#endif // ANIMEEFFECTS_AUDIOPLAYBACKWIDGET_H
