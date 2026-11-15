#ifndef CTRL_VIDEOFORMAT_H
#define CTRL_VIDEOFORMAT_H

#include <QString>
#include <QList>

namespace ctrl {

class VideoCodec {
public:
    VideoCodec();
    QString name;
    QString label;
    QString icodec;
    QString command;
    QStringList pixfmts;
    bool lossless;
    bool transparent;
    bool colorspace;
    bool gpuenc;
};

class VideoFormat {
public:
    VideoFormat();
    QString name;
    QString label;
    QString icodec;
    QString command;
    QList<VideoCodec> codecs;

    // Load the embedded export preset (:/encode/VideoEncode.txt) and return the
    // format with the given name (e.g. "mp4"). Returns a format with an empty
    // name if not found. Used by the exporter to map a format to its codecs.
    static VideoFormat find(const QString& aName);
};

} // namespace ctrl

#endif // CTRL_VIDEOFORMAT_H
