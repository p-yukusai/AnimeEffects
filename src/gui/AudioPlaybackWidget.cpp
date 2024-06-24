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
    // REMEMBER TO TAKE THE PREPROCESSOR OFF LATER
#if false
    state->player->setSource(QUrl::fromLocalFile(pConf->at(0).audioPath.absoluteFilePath()));
    if(play){state->player->play();}
    else{state->player->stop();}
#endif
    Q_UNIMPLEMENTED();
}
