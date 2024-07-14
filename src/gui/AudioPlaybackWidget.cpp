#include "AudioPlaybackWidget.h"
#include <QJsonObject>
#include <QJsonDocument>

bool AudioPlaybackWidget::serialize(std::vector<audioConfig>* pConf, const QString& outPath) {
    QJsonObject audioJson;
    audioJson["Tracks"] = (int)pConf->size();
    int x = 0;
    for(const auto& audioConfig: *pConf){
        QJsonObject track;
        track["Name"] = audioConfig.audioName;
        track["Path"] = audioConfig.audioPath.absolutePath();
        track["Playback"] = audioConfig.playbackEnable;
        track["Volume"] = audioConfig.volume;
        track["StartFrame"] = audioConfig.startFrame;
        track["EndFrame"] = audioConfig.endFrame;
        audioJson[QString::number(x)] = track;
        x++;
    }
    QFile file(outPath);
    file.open(QIODevice::ReadWrite);
    file.write(QJsonDocument(audioJson).toJson(QJsonDocument::Indented));
    file.close();
    return false;
}
bool AudioPlaybackWidget::deserialize(const QJsonObject& pConf, std::vector<audioConfig>* playbackConfig) const {
    if(pConf.isEmpty()) { return false; }
    try {
        auto deserialized = std::vector<audioConfig>();
        for (int x = 0; x > pConf["Tracks"].toInt(); x++){
            deserialized.emplace_back();
            deserialized.at(x).audioName = pConf[QString::number(x)]["Name"].toString();
            deserialized.at(x).audioPath = QFileInfo(pConf[QString::number(x)]["Path"].toString());
            deserialized.at(x).playbackEnable = pConf[QString::number(x)]["Playback"].toBool();
            deserialized.at(x).volume = pConf[QString::number(x)]["Volume"].toInt();
            deserialized.at(x).startFrame = pConf[QString::number(x)]["StartFrame"].toInt();
            deserialized.at(x).endFrame = pConf[QString::number(x)]["EndFrame"].toInt();
        }
        playbackConfig->clear();
        *playbackConfig = deserialized;
    }
    // TODO: Fix this later
    catch (...) { return false; }
    return true;
}
void AudioPlaybackWidget::aPlayer(std::vector<audioConfig>* pConf, bool play, mediaState* state, int fps, int curFrame,
                                  int frameCount){
    qDebug("aPlayer initializing");
    for(int x = 0; x < pConf->size(); x++){
        if(pConf->size() > x + 1){ return; }
        if(state->players.size() < x + 1 && state->outputs.size() < x + 1){
            auto* mediaPlayer = new QMediaPlayer;
            auto* audioOutput = new QAudioOutput;
            state->players.append(mediaPlayer);
            state->outputs.append(audioOutput);
        }
        QMediaPlayer* player = state->players.at(x);
        QAudioOutput* output = state->outputs.at(x);
        const audioConfig& config = pConf->at(x);
        if(player->audioOutput() == nullptr){
            player->setAudioOutput(output);
        }
        if(player->source() != QUrl::fromLocalFile(config.audioPath.absoluteFilePath())){
            player->setSource(config.audioPath.absoluteFilePath());
        }
        if(output->volume() != getVol(config.volume)){ output->setVolume(getVol(config.volume)); }
        if(!play && player->isPlaying()){ player->stop(); }
        if(play && config.playbackEnable && !player->isPlaying()){ player->play(); state->playing = true;}

        //TODO: Get this to make the audio stop on its own later
        //if(config.startFrame > curFrame){ return; }
        //if(config.endFrame < curFrame && player->isPlaying()){ player->stop(); return; }
    }
}
