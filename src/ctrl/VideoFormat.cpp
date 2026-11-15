#include "ctrl/VideoFormat.h"
#include "util/TextUtil.h"

#include <QFile>
#include <QDomDocument>

namespace ctrl {

VideoCodec::VideoCodec():
    name(), label(), icodec(), command(), pixfmts(), lossless(), transparent(), colorspace(), gpuenc() {}

VideoFormat::VideoFormat(): name(), label(), icodec(), command(), codecs() {}

VideoFormat VideoFormat::find(const QString& aName) {
    QFile file(QStringLiteral(":/encode/VideoEncode.txt"));
    if (!file.open(QIODevice::ReadOnly))
        return VideoFormat();

    QDomDocument doc;
    if (!doc.setContent(&file))
        return VideoFormat();

    const QDomElement root = doc.firstChildElement("video_encode");
    for (QDomElement fmt = root.firstChildElement("format"); !fmt.isNull();
         fmt = fmt.nextSiblingElement("format")) {
        if (fmt.attribute("name") != aName)
            continue;

        VideoFormat out;
        out.name = fmt.attribute("name");
        out.label = fmt.attribute("label");
        out.icodec = fmt.attribute("icodec");
        out.command = fmt.attribute("command");
        if (out.label.isEmpty())
            out.label = out.name;
        if (out.icodec.isEmpty())
            out.icodec = QStringLiteral("png");

        for (QDomElement cd = fmt.firstChildElement("codec"); !cd.isNull();
             cd = cd.nextSiblingElement("codec")) {
            VideoCodec codec;
            codec.name = cd.attribute("name");
            if (codec.name.isEmpty())
                continue;
            codec.label = cd.attribute("label");
            codec.icodec = cd.attribute("icodec");
            codec.command = cd.attribute("command");
            if (codec.label.isEmpty())
                codec.label = codec.name;
            if (codec.icodec.isEmpty())
                codec.icodec = out.icodec;
            if (codec.command.isEmpty())
                codec.command = out.command;
            const QStringList hints = util::TextUtil::splitAndTrim(cd.attribute("hint"), ',');
            for (const QString& hint : hints) {
                if (hint == QLatin1String("lossless"))       codec.lossless = true;
                else if (hint == QLatin1String("transparent")) codec.transparent = true;
                else if (hint == QLatin1String("colorspace"))  codec.colorspace = true;
                else if (hint == QLatin1String("gpuenc"))      codec.gpuenc = true;
            }
            codec.pixfmts = util::TextUtil::splitAndTrim(cd.attribute("pixfmt"), ',');
            out.codecs.push_back(codec);
        }
        return out; // first match by name
    }
    return VideoFormat();
}

} // namespace ctrl
