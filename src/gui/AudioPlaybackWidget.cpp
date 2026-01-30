#include "AudioPlaybackWidget.h"
#include <QJsonObject>
#include <QJsonDocument>
#include <QStringRef>
#include <QMessageBox>

void checkConnection(const bool connection){
    if(!connection){
        QMessageBox::warning(nullptr, "Qt connection error", "Unable to connect UI component");
    }
}

bool AudioPlaybackWidget::serialize(std::vector<audioConfig>* pConf, const QString& outPath) {
    QJsonObject audioJson;
    std::vector<audioConfig> validStreams = getValidAudioStreams(*pConf);
    audioJson["Tracks"] = static_cast<int>(validStreams.size());
    int x = 0;
    for(const auto& audioConfig: validStreams){
        QJsonObject track;
        track["Name"] = audioConfig.audioName;
        track["Path"] = audioConfig.audioPath.absoluteFilePath();
        track["Playback"] = audioConfig.playbackEnable;
        track["Volume"] = audioConfig.volume;
        track["StartFrame"] = audioConfig.startFrame;
        track["EndFrame"] = audioConfig.endFrame;
        audioJson[QString::number(x)] = track;
        x++;
    }
    QFile file(outPath);
    if (!file.open(QIODevice::ReadWrite)) {
        return false;
    }
    file.write(QJsonDocument(audioJson).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool AudioPlaybackWidget::deserialize(const QJsonObject& pConf, std::vector<audioConfig>* playbackConfig) {
    if(pConf.isEmpty()) { return false; }
    try {
        auto deserialized = std::vector<audioConfig>();
        for (int x = 0; x < pConf["Tracks"].toInt(); x++){
            deserialized.emplace_back();
            deserialized.at(x).audioName = pConf[QString::number(x)].toObject()["Name"].toString();
            deserialized.at(x).audioPath = QFileInfo(pConf[QString::number(x)].toObject()["Path"].toString());
            deserialized.at(x).playbackEnable = pConf[QString::number(x)].toObject()["Playback"].toBool();
            deserialized.at(x).volume = pConf[QString::number(x)].toObject()["Volume"].toInt();
            deserialized.at(x).startFrame = pConf[QString::number(x)].toObject()["StartFrame"].toInt();
            deserialized.at(x).endFrame = pConf[QString::number(x)].toObject()["EndFrame"].toInt();
        }
        playbackConfig->clear();
        for (const auto& config: deserialized){
            playbackConfig->emplace_back(config);
        }
    }
    // TODO: Fix this later
    catch (...) { return false; }
    return true;
}

void AudioPlaybackWidget::aPlayer(std::vector<audioConfig>* pConf, bool play, mediaState* state, int fps, int curFrame, int frameCount){
    //qDebug("Media player requested");
    int x = 0;
    for(auto conf: *pConf){
        if(state->players.size() < x + 1 && state->outputs.size() < x + 1){
            auto* mediaPlayer = new QMediaPlayer;
            auto* audioOutput = new QAudioOutput;
            state->players.append(mediaPlayer);
            state->outputs.append(audioOutput);
        }
        QMediaPlayer* player = state->players.at(x);
        QAudioOutput* output = state->outputs.at(x);
        const audioConfig& config = pConf->at(x);



        if(player->audioOutput() == nullptr){ player->setAudioOutput(output); }
        if(player->source() != QUrl::fromLocalFile(config.audioPath.absoluteFilePath())){
            player->setSource(config.audioPath.absoluteFilePath());
        }
        if(output->volume() != getVol(config.volume)){ output->setVolume(getVol(config.volume)); }
        correctTrackPos(player, curFrame, frameCount, fps, const_cast<audioConfig&>(config));
        //qDebug("---");
        //qDebug() << "x = " << x << "; track = " << player->source() << "; current playback state = " << player->playbackState() << "; play = " << play;
        //qDebug("---");
        if(!play){ player->stop(); state->playing = false;}
        if(play && config.startFrame < curFrame && config.endFrame > curFrame && config.playbackEnable){
            player->play();
            state->playing = true;
        }
        x++;
    }
}

void AudioPlaybackWidget::connect(QWidget *audioWidget, mediaState *state, std::vector<audioConfig>* config){
    checkConnection(QToolButton::connect(saveConfigButton, &QToolButton::clicked, [=](){
        QFileDialog diag(audioWidget);
        diag.setAcceptMode(QFileDialog::AcceptSave);
        diag.setDirectory(QDir::currentPath());
        diag.setNameFilter(QCoreApplication::translate("SaveMus", "Anie audio configuration file (*.aemus)", nullptr));
        if(diag.exec()) {
            QString fileName = diag.selectedFiles().first();
            static QRegularExpression fileRegex("\\.aemus$");
            if (!fileRegex.match(fileName).hasMatch()) { fileName.append(".aemus"); }
            serialize(config, fileName);
        }
    }));
    checkConnection(QToolButton::connect(loadConfigButton, &QToolButton::clicked, [=]()mutable {
        QFileDialog diag(audioWidget);
        diag.setAcceptMode(QFileDialog::AcceptOpen);
        diag.setDirectory(QDir::currentPath());
        diag.setNameFilter(QCoreApplication::translate("LoadMus", "Anie audio configuration file (*.aemus)", nullptr));
        if(diag.exec()) {
            auto file = QFile(diag.selectedFiles().first());
            if(!file.open(QIODevice::ReadOnly)) {
                QMessageBox::warning(audioWidget, "File error", file.errorString());
                return;
            }
            QJsonDocument json = QJsonDocument::fromJson(file.readAll());
            if (json.isEmpty() || json.isNull()) {
                QMessageBox::warning(
                    audioWidget,
                    "Invalid file \"" + QFileInfo(file).baseName() + '"',
                    "The requested file's json contents could not be parsed."
                );
                return;
            }
            if (!(config->size() == 1 && config->front().audioName == "Placeholder")) {
                QMessageBox overwrite;
                overwrite.setWindowTitle("Overwrite existing audio configuration");
                overwrite.setIcon(QMessageBox::Warning);
                overwrite.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
                overwrite.setDefaultButton(QMessageBox::Ok);
                overwrite.setText(QCoreApplication::translate("LoadMusOverwrite",
                    "Loading this file will overwrite your current audio configuration, continue?",
                    nullptr));
                if (overwrite.exec() == QMessageBox::Cancel) {
                    return;
                }
            }
            const auto configBackup = *config;
            for (const auto player: state->players) { player->deleteLater();}
            for (const auto output: state->outputs) { output->deleteLater();}
            state->players.clear();
            state->outputs.clear();
            config->clear();
            if (!deserialize(json.object(), config)) {
                *config = configBackup;
                int x = 0;
                for (const auto& conf: configBackup) {
                    addTrack(state, conf.audioPath.absoluteFilePath());
                    modifyTrack(state, config, x);
                    x++;
                }
                QMessageBox::warning(audioWidget, "Invalid file",
                    "Failed to deserialize audio configuration as the requested file is not a valid aemus file, previous audio config restored."
                    );
            }
            else {
                int x = 0;
                for (const auto& conf: *config) {
                    addTrack(state, conf.audioPath.absoluteFilePath());
                    modifyTrack(state, config, x);
                    x++;
                }
                QMessageBox::information(audioWidget, "Success", "Successfully deserialized the requested audio file.");
            }
            rectifyUI(config, state);
        }
    }));

}

void AudioPlaybackWidget::rectifyUI(std::vector<audioConfig>* config, mediaState* mediaPlayer, const bool bulk) {
    // We reconstruct the entire state so we don't mismatch UI and media indexes
    if(config->empty()){ config->emplace_back(); }
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

    int x = 0;
    for(auto conf: *config){
        addUIState(config, x, mediaPlayer, bulk);
        x++;
    }
    gridLayout_2->update();
}

void AudioPlaybackWidget::addUIState(std::vector<audioConfig>* config, int index, mediaState* mediaPlayer, const bool bulk) {
    // This is a great time to tell you that we accept pull requests that fix this kind of nonsense
    const auto curUIState = UIState();
    // Grid layout pos
    const int vecIdx = static_cast<int>(vecUIState.size());
    const int offset = vecIdx * 4;
    const int idxRow0 = offset;
    const int idxRow1 = offset + 1;
    const int idxRow2 = offset + 2;
    const int idxRow3 = offset + 3;
    // Playback
    curUIState.playAudio->setObjectName(QString::fromUtf8("playAudio"));
    gridLayout_2->addWidget(curUIState.playAudio, idxRow1, 0, 1, 1);
    // Frame end
    curUIState.endSpinBox->setObjectName(QString::fromUtf8("endSpinBox"));
    curUIState.endSpinBox->setMaximum(INT32_MAX);
    curUIState.endSpinBox->setMinimum(0);
    gridLayout_2->addWidget(curUIState.endSpinBox, idxRow1, 2, 1, 1);
    // Frame start
    curUIState.startSpinBox->setObjectName(QString::fromUtf8("startSpinBox"));
    curUIState.startSpinBox->setMaximum(INT32_MAX);
    curUIState.startSpinBox->setMinimum(0);
    gridLayout_2->addWidget(curUIState.startSpinBox, idxRow1, 1 , 1, 1);
    // New track
    curUIState.addNewTrack->setObjectName(QString::fromUtf8("startLabel"));
    QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    sizePolicy.setHorizontalStretch(0);
    sizePolicy.setVerticalStretch(0);
    sizePolicy.setHeightForWidth(curUIState.addNewTrack->sizePolicy().hasHeightForWidth());
    curUIState.addNewTrack->setSizePolicy(sizePolicy);
    gridLayout_2->addWidget(curUIState.addNewTrack, idxRow0, 0, 1, 1);
    // Frame start text
    curUIState.startLabel->setObjectName(QString::fromUtf8("startLabel"));
    sizePolicy.setHeightForWidth(curUIState.startLabel->sizePolicy().hasHeightForWidth());
    curUIState.startLabel->setSizePolicy(sizePolicy);
    gridLayout_2->addWidget(curUIState.startLabel, idxRow0, 1, 1, 1);
    // Frame end text
    curUIState.endLabel->setObjectName(QString::fromUtf8("endLabel"));
    sizePolicy.setHeightForWidth(curUIState.endLabel->sizePolicy().hasHeightForWidth());
    curUIState.endLabel->setSizePolicy(sizePolicy);
    gridLayout_2->addWidget(curUIState.endLabel, idxRow0, 2, 1, 1);
    // Volume text
    curUIState.volumeLabel->setObjectName(QString::fromUtf8("volumeLabel"));
    curUIState.volumeLabel->setSizePolicy(sizePolicy);
    gridLayout_2->addWidget(curUIState.volumeLabel, idxRow2, 0, 1, 1);
    // Volume
    curUIState.volumeSlider->setObjectName(QString::fromUtf8("volumeSlider"));
    curUIState.volumeSlider->setMaximum(100);
    curUIState.volumeSlider->setMinimum(0);
    curUIState.volumeSlider->setOrientation(Qt::Horizontal);
    curUIState.volumeSlider->setSizePolicy(sizePolicy);
    gridLayout_2->addWidget(curUIState.volumeSlider, idxRow2, 1, 1, 3);
    // Separator
    curUIState.line->setObjectName(QString::fromUtf8("line"));
    curUIState.line->setFrameShape(QFrame::HLine);
    curUIState.line->setFrameShadow(QFrame::Sunken);
    gridLayout_2->addWidget(curUIState.line, idxRow3, 0, 1, 4);
    // Frame text
    curUIState.musDurationLabel->setObjectName(QString::fromUtf8("musDurationLabel"));
    gridLayout_2->addWidget(curUIState.musDurationLabel, idxRow0, 3, 1, 1);
    // Audio selector
    curUIState.selectMusButton->setObjectName(QString::fromUtf8("selectMusButton"));
    curUIState.selectMusButton->setAutoRepeat(false);
    gridLayout_2->addWidget(curUIState.selectMusButton, idxRow1, 3, 1, 1);
    // Translate
    curUIState.playAudio->setText(QCoreApplication::translate("audioWidget", "Enable playback", nullptr));
    curUIState.addNewTrack->setText(QCoreApplication::translate("audioWidget", "Add new audio track", nullptr));
    curUIState.startLabel->setText(QCoreApplication::translate("audioWidget", "<html><head/><body><p align=\"center\">Playback start frame</p></body></html>", nullptr));
    curUIState.endLabel->setText(QCoreApplication::translate("audioWidget", "<html><head/><body><p align=\"center\">Playback end frame</p></body></html>", nullptr));
    curUIState.musDurationLabel->setText(QCoreApplication::translate("audioWidget", "<html><head/><body><p align=\"center\">Duration (in frames): </p></body></html>", nullptr) +
                                       "<html><head/><body><p align=\"center\">" + QString::number(config->at(index).endFrame - config->at(index).startFrame) + "</p></body></html>");
    curUIState.volumeLabel->setText(QCoreApplication::translate("audioWidget", "Media volume", nullptr) + " (" + QString::number(config->at(index).volume) + "%)");
    // Init
    curUIState.playAudio->setChecked(config->at(index).playbackEnable);
    curUIState.startSpinBox->setValue(config->at(index).startFrame);
    curUIState.endSpinBox->setValue(config->at(index).endFrame);
    curUIState.volumeSlider->setValue(config->at(index).volume);

    if(config->at(index).audioName != "Placeholder"){
        QString ref = QStringRef(&config->at(index).audioName, 0, 17).toString();
        if (config->at(index).audioName.size() > ref.size()) {
            ref.append("...");
        }
        curUIState.selectMusButton->setText(ref);
        curUIState.selectMusButton->setToolTip(config->at(index).audioName);
    }
    else{
        curUIState.selectMusButton->setText(QCoreApplication::translate("audioWidget", "Select audio file...", nullptr));
    }

    if(bulk){
        if(index == 0) {
            for (const auto player: mediaPlayer->players) { player->deleteLater(); }
            for (const auto output: mediaPlayer->outputs) { output->deleteLater(); }
            mediaPlayer->players.clear();
            mediaPlayer->outputs.clear();
            int x = 0;
            for (const auto& conf: *config) {
                addTrack(mediaPlayer, conf.audioPath.absoluteFilePath());
                modifyTrack(mediaPlayer, config, x);
                x++;
            }
        }
        if (index > 0 && index + 1 != config->size() || ((index == 0 || index == 1 && index + 1 != config->size()) && config->size() == 2)) {
            if (index < config->size() - 1){
                curUIState.addNewTrack->setText(QCoreApplication::translate("audioWidget", "Remove next audio track", nullptr));
            }
            else {
                // It shouldn't trigger but just in case...
                curUIState.addNewTrack->setText(QCoreApplication::translate("audioWidget", "Remove current audio track", nullptr));
            }
            curUIState.addTrack = false;
        }
        else{ curUIState.addTrack = true; }
    }
    vecUIState.append(curUIState);
    // Connect
    checkConnection(QToolButton::connect(curUIState.selectMusButton, &QToolButton::clicked, [=] {
        const auto file = QFileInfo(QFileDialog::getOpenFileName(
            this->musPlayer,
            QCoreApplication::translate("SelectMus", "Open audio file", nullptr),
            QDir::currentPath(),
            QCoreApplication::translate("SelectMus", "Audio Files (*.mp3 *.mp4 *.wav *.ogg *.flac)", nullptr)
        ));
        if(!file.exists()){ return; }
        config->at(index).audioPath = file;
        config->at(index).audioName = file.fileName();
        modifyTrack(mediaPlayer, config, index);
        {
            int maxSize = 17;
            if (config->at(index).audioName.size() < 17) { maxSize = config->at(index).audioName.size(); }
            QString ref = QStringRef(&config->at(index).audioName, 0, maxSize).toString();
            if (config->at(index).audioName.size() > ref.size()) { ref.append("..."); }
            curUIState.selectMusButton->setText(ref);
            curUIState.selectMusButton->setToolTip(file.fileName());
        }
    }));
    checkConnection(QToolButton::connect(curUIState.addNewTrack, &QToolButton::clicked, [=] {
        if (vecUIState.size() != config->size()) { rectifyUI(config, mediaPlayer, true); return; }

        if (curUIState.addTrack) {
            qDebug() << "addTrack at " << index + 1 << " | player size is " << mediaPlayer->players.size();
            config->emplace_back();
            addTrack(mediaPlayer, QUrl());
            this->addUIState(config, index + 1, mediaPlayer);
            curUIState.addNewTrack->setText(QCoreApplication::translate("audioWidget", "Remove next audio track", nullptr));
            curUIState.addTrack = false;
        }
        else {
            if (config->size() == 1) {
                vecUIState.first().addTrack = true;
                rectifyUI(config, mediaPlayer);
                return;
            }
            bool deleteNext = config->size() > 2;
            int delIndex = index;
            if (deleteNext && index + 1 <= static_cast<int>(config->size()) - 1) {
                delIndex = index + 1;
            }
            qDebug() << "removeTrack at " << delIndex << " | player size is " << mediaPlayer->players.size();
            config->erase(config->begin() + delIndex);
            rectifyUI(config, mediaPlayer, true);
            vecUIState.last().addTrack = true;
        }
    }));
    checkConnection(QSpinBox::connect(curUIState.startSpinBox, &QSpinBox::valueChanged, [=](const int val){
        config->at(index).startFrame = val;
        vecUIState.at(index).musDurationLabel->setText(QCoreApplication::translate("audioWidget","<html><head/><body><p align=\"center\">Duration (in frames): </p></body></html>", nullptr) +
                                       "<html><head/><body><p align=\"center\">" + QString::number(config->at(index).endFrame - val) + "</p></body></html>");
    }));
    checkConnection(QSpinBox::connect(curUIState.endSpinBox, &QSpinBox::valueChanged, [=](const int val){
        config->at(index).endFrame = val;
        curUIState.musDurationLabel->setText(QCoreApplication::translate("audioWidget", "<html><head/><body><p align=\"center\">Duration (in frames): </p></body></html>", nullptr) +
                                       "<html><head/><body><p align=\"center\">" + QString::number(val - config->at(index).startFrame) + "</p></body></html>");
    }));
    checkConnection(QCheckBox::connect(curUIState.playAudio, &QCheckBox::checkStateChanged, [=](const Qt::CheckState val){
        config->at(index).playbackEnable = (val == Qt::Checked);
    }));
    checkConnection(QSlider::connect(curUIState.volumeSlider, &QSlider::valueChanged, [=](const int val){
        config->at(index).volume = val;
        modifyTrack(mediaPlayer, config, index);
        curUIState.volumeLabel->setText(QCoreApplication::translate("audioWidget", "Media volume", nullptr) + " (" + QString::number(val) + "%)");
    }));
}

void AudioPlaybackWidget::addTrack(mediaState* state, const QUrl& source) {
    auto* mediaPlayer = new QMediaPlayer;
    auto* audioOutput = new QAudioOutput;
    mediaPlayer->setSource(source);
    mediaPlayer->setAudioOutput(audioOutput);
    state->players.append(mediaPlayer);
    state->outputs.append(audioOutput);
}

void AudioPlaybackWidget::removeTrack(mediaState* state, const int index) {
    const int size = static_cast<int>(state->players.size());
    state->players.at(index)->stop();
    state->players.at(index)->deleteLater();
    state->outputs.at(index)->deleteLater();
    if (size > state->players.size()) {
        state->players.removeAt(index);
        state->outputs.removeAt(index);
    }
}

void AudioPlaybackWidget::modifyTrack(mediaState* state, std::vector<audioConfig>* config, int index) {
    const auto& modifiedConfig = config->at(index);
    if(state->players.isEmpty() || (state->players.size() <= index && index != 0)){
        addTrack(state, QUrl());
    }
    auto* currentPlayer = state->players.at(index);
    auto* currentOutput = state->outputs.at(index);
    if(currentPlayer->playbackState() == QMediaPlayer::PlayingState) { currentPlayer->stop(); }
    if(currentPlayer->source().isEmpty() || currentPlayer->source().fileName() != modifiedConfig.audioPath.fileName()){
        currentPlayer->setSource(modifiedConfig.audioPath.absoluteFilePath());
    }
    if(currentOutput->volume() != getVol(modifiedConfig.volume)){
        currentOutput->setVolume(getVol(modifiedConfig.volume));
    }
}

void AudioPlaybackWidget::correctTrackPos(QMediaPlayer* player, int curFrame,  int frameCount, int fps, audioConfig& config) {
    // Calculate times in milliseconds
    constexpr float toMillis = 1000.0;
    const int frame = frameCount - (frameCount - config.startFrame - curFrame);
    auto positionMs = static_cast<qint64>(static_cast<float>(frame) / static_cast<float>(fps) * toMillis);
    if (player->hasAudio() && positionMs >= 0) { player->setPosition(positionMs); }
}
