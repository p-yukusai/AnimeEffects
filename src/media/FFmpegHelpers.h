#ifndef ANIMEEFFECTS_SRC_MEDIA_FFMPEGHELPERS_H
#define ANIMEEFFECTS_SRC_MEDIA_FFMPEGHELPERS_H

#include <QString>
#include <QStringList>

// Small, GL-free capability probe layer over the linked libav* libraries.
//
// This is intentionally the FIRST surface of the media rearchitecture: it lets
// the exporter (and tests) ask "which FFmpeg build am I linked against, and
// which encoders does it actually provide?" without touching the (removed)
// `ffmpeg` CLI subprocess. Everything here is pure C/libav* over Qt Core — no
// QApplication, no GL — so it is unit-testable headless.
namespace media {
namespace ffmpeg {

// The FFmpeg/library license string (e.g. "LGPL version 2.1 or later").
QString license();

// avcodec_configuration() — the configure flags the linked libavcodec was
// built with; useful for deciding if GPL encoders like libx264 are present.
QString configuration();

// Non-empty iff an encoder with the given name is registered (avcodec
// can find it). e.g. encoderAvailable("libx264").
bool encoderAvailable(const char* aEncoderName);

// Names of all registered VIDEO encoders (type == AVMEDIA_TYPE_VIDEO,
// encode != nullptr), sorted. Used for diagnostics and to gate presets
// against what the linked build actually provides.
QStringList availableVideoEncoders();

// Resolve a preset encoder name to a libav encoder that actually exists in the
// linked build, applying known aliases first (e.g. "theora" -> "libtheora").
// Returns the encoder name if it is available, or an empty string if not.
// This is how presets stay authorable in friendly names while the exporter
// uses libav* encoder names.
QString resolveEncoderName(const QString& aName);

// True iff the named encoder (resolved via resolveEncoderName) reports the
// given pixel format as supported. Used to validate a preset's pixfmt list
// against the linked build (the compatibility gate).
bool encoderSupportsPixFormat(const QString& aEncoderName, const QString& aPixFmt);

// True iff the container's muxer (e.g. "mp4", "webm") can carry the named
// encoder's codec. Used to reject a container/codec mismatch up front rather
// than failing confusingly at mux time.
bool muxerSupportsCodec(const QString& aContainer, const QString& aEncoderName);

// Pick a default video encoder the container's muxer can carry (and the linked
// build provides), in priority order. Empty string if none fit -> caller rejects.
QString defaultCodecForContainer(const QString& aContainer);

} // namespace ffmpeg
} // namespace media

#endif
