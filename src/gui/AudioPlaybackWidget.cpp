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
bool AudioPlaybackWidget::deserialize(const QJsonObject& pConf) const {
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
void AudioPlaybackWidget::aPlayer(std::vector<audioConfig>* pConf, bool play, mediaState* state, int fps,
                                      int curFrame, int frameCount){
    // REMEMBER TO SET THE INDEX PER EACH AUDIO TRACK
    const auto& source = pConf->at(state->index);
    auto* player = state->players.at(state->index);
    auto* output = state->outputs.at(state->index);
    if(player == nullptr){
        std::unique_ptr<QMediaPlayer> mediaPlayer;
        state->players.append(mediaPlayer.get());
        player = mediaPlayer.get();
    }
    if(output == nullptr){
        std::unique_ptr<QAudioOutput> audioOutput;
        state->outputs.append(audioOutput.get());
        output = audioOutput.get();
    }
    if(player->audioOutput() == nullptr){ player->setAudioOutput(output); }
    if(player->source().isEmpty()){ player->setSource(source.audioPath.absoluteFilePath()); }
    player->audioOutput()->setVolume(float(source.volume));
    // TODO: Create function to set audio time based on the frame
    if(source.playbackEnable && play){ player->play(); }
    if(player->isPlaying() && !play){ player->stop(); }
}
