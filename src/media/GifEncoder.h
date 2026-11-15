#ifndef ANIMEEFFECTS_SRC_MEDIA_GIFENCODER_H
#define ANIMEEFFECTS_SRC_MEDIA_GIFENCODER_H

#include <QImage>
#include <QList>
#include <QString>

// In-process GIF encoder over the linked libav* (avcodec 'gif' encoder + the
// gif muxer). Replaces the legacy 2-pass ffmpeg CLI palettegen/paletteuse
// subprocess. It builds an adaptive 256-color palette from the frames, maps each
// frame to pal8, and encodes + muxes a .gif in one pass.
//
// GL-free and QCoreApplication-free, so it is unit-testable headless.
namespace media {

class GifEncoder {
public:
    struct Config {
        QString outputPath;
        int fps = 15;           // frame delay is 100/fps centiseconds
        bool transparent = false;
        int loop = 0;           // 0 = infinite; -1 = no loop
        // How many palette entries may be used, ignoring any supplied palette's
        // extras. A GIF has no CRF, so this is the only QUALITY knob it has: the
        // fit-to-cap loop reduces it to quantise harder before it starts throwing
        // away resolution or frames.
        int maxColours = 256;
        QString palettePath;    // optional: a PNG whose pixels ARE the 256-color
                                // palette (the ffmpeg paletteuse convention).
                                // Empty = build an adaptive palette from frames.
    };

    GifEncoder() = default;
    ~GifEncoder() = default;

    bool encode(const QList<QImage>& aFrames, const Config& aConfig, QString* aError = nullptr);
    QString error() const { return mError; }

private:
    QString mError;
};

} // namespace media

#endif
