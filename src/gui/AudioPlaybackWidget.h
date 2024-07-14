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

class AudioPlaybackWidget {
public:
    mediaState media;
    QGridLayout *gridLayout{};
    QTabWidget *tabWidget{};
    QWidget *musPlayer{};
    QHBoxLayout *horizontalLayout{};
    QScrollArea *musScroll{};
    QWidget *musTab{};
    QGridLayout *gridLayout_2{};
    QToolButton *addNewTrack{};
    QToolButton *selectMusButton{};
    QCheckBox *playAudio{};
    QSpinBox *endSpinBox{};
    QSpinBox *startSpinBox{};
    QLabel *startLabel{};
    QLabel *endLabel{};
    QLabel *musDurationLabel;
    QLabel *volumeLabel{};
    QSlider *volumeSlider{};
    QFrame *line{};
    QWidget *configTab{};
    QGridLayout *gridLayout_3{};
    QPushButton *saveConfigButton{};
    QPushButton *loadConfigButton{};

    void setupUi(QWidget *audioWidget, mediaState mediaPlayer){
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
        selectMusButton = new QToolButton(musTab);
        selectMusButton->setObjectName(QString::fromUtf8("selectMusButton"));
        selectMusButton->setAutoRepeat(false);

        gridLayout_2->addWidget(selectMusButton, 1, 3, 1, 1);

        playAudio = new QCheckBox(musTab);
        playAudio->setObjectName(QString::fromUtf8("playAudio"));
        playAudio->setChecked(true);

        gridLayout_2->addWidget(playAudio, 1, 0, 1, 1);

        endSpinBox = new QSpinBox(musTab);
        endSpinBox->setObjectName(QString::fromUtf8("endSpinBox"));

        gridLayout_2->addWidget(endSpinBox, 1, 2, 1, 1);

        startSpinBox = new QSpinBox(musTab);
        startSpinBox->setObjectName(QString::fromUtf8("startSpinBox"));

        gridLayout_2->addWidget(startSpinBox, 1, 1, 1, 1);

        addNewTrack = new QToolButton(musTab);
        addNewTrack->setObjectName(QString::fromUtf8("startLabel"));
        QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(addNewTrack->sizePolicy().hasHeightForWidth());
        addNewTrack->setSizePolicy(sizePolicy);

        gridLayout_2->addWidget(addNewTrack, 0, 0, 1, 1);

        startLabel = new QLabel(musTab);
        startLabel->setObjectName(QString::fromUtf8("startLabel"));
        sizePolicy.setHeightForWidth(startLabel->sizePolicy().hasHeightForWidth());
        startLabel->setSizePolicy(sizePolicy);

        gridLayout_2->addWidget(startLabel, 0, 1, 1, 1);

        endLabel = new QLabel(musTab);
        endLabel->setObjectName(QString::fromUtf8("endLabel"));
        sizePolicy.setHeightForWidth(endLabel->sizePolicy().hasHeightForWidth());
        endLabel->setSizePolicy(sizePolicy);

        gridLayout_2->addWidget(endLabel, 0, 2, 1, 1);

        volumeLabel = new QLabel(musTab);
        volumeLabel->setObjectName(QString::fromUtf8("volumeLabel"));
        volumeLabel->setSizePolicy(sizePolicy);
        gridLayout_2->addWidget(volumeLabel, 2, 0, 1, 1);

        volumeSlider = new QSlider(musTab);
        volumeSlider->setObjectName(QString::fromUtf8("volumeSlider"));
        volumeSlider->setMaximum(100);
        volumeSlider->setMinimum(0);
        volumeSlider->setOrientation(Qt::Horizontal);
        volumeSlider->setSizePolicy(sizePolicy);
        gridLayout_2->addWidget(volumeSlider, 2, 1, 1, 3);


        line = new QFrame(musTab);
        line->setObjectName(QString::fromUtf8("line"));
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);

        gridLayout_2->addWidget(line, 3, 0, 1, 4);

        musDurationLabel = new QLabel(musTab);
        musDurationLabel->setObjectName(QString::fromUtf8("musDurationLabel"));

        gridLayout_2->addWidget(musDurationLabel, 0, 3, 1, 1);

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

        tabWidget->setCurrentIndex(0);

        QMetaObject::connectSlotsByName(audioWidget);
    }
    void translateUi(QWidget *audioWidget) const{
        audioWidget->setWindowTitle(QCoreApplication::translate("audioWidget", "Audio Player", nullptr));
        selectMusButton->setText(QCoreApplication::translate("audioWidget", "Select audio file...", nullptr));
        playAudio->setText(QCoreApplication::translate("audioWidget", "Enable playback", nullptr));
        addNewTrack->setText(QCoreApplication::translate("audioWidget", "Add new audio track", nullptr));
        startLabel->setText(QCoreApplication::translate("audioWidget", "<html><head/><body><p align=\"center\">Playback start frame</p></body></html>", nullptr));
        endLabel->setText(QCoreApplication::translate("audioWidget", "<html><head/><body><p align=\"center\">Playback end frame</p></body></html>", nullptr));
        musDurationLabel->setText(QCoreApplication::translate("audioWidget", "<html><head/><body><p align=\"center\">Duration (in frames): 0</p></body></html>", nullptr));
        volumeLabel->setText(QCoreApplication::translate("audioWidget", "Media volume (0%)", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(musPlayer), QCoreApplication::translate("audioWidget", "Audio player", nullptr));
        saveConfigButton->setText(QCoreApplication::translate("audioWidget", "Save current audio configuration", nullptr));
        loadConfigButton->setText(QCoreApplication::translate("audioWidget", "Load audio configuration from file", nullptr));
        tabWidget->setTabText(tabWidget->indexOf(configTab), QCoreApplication::translate("audioWidget", "Save/Load audio config", nullptr));
    }
    static void aPlayer(std::vector<audioConfig>* pConf, bool play, mediaState* state, int fps, int curFrame, int frameCount);
    static bool serialize(std::vector<audioConfig>* pConf, const QString& outPath);
    bool deserialize(const QJsonObject& pConf, std::vector<audioConfig>* playbackConfig) const;
    static float getVol(int volume){ return static_cast<float>(volume / 100.0); }
};

#endif // ANIMEEFFECTS_AUDIOPLAYBACKWIDGET_H
