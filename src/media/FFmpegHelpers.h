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
//
// NOTE: this only proves the encoder was COMPILED INTO this libavcodec. It does
// not prove it can run: av1_nvenc is listed on machines with no capable NVIDIA
// device and fails at avcodec_open2 ("No capable devices found"). Use
// encoderUsable() wherever the answer decides something.
bool encoderAvailable(const char* aEncoderName);

// True iff the named encoder can actually be opened AND encode a frame here.
// This is the real capability probe: a one-frame test encode, cached per name,
// because hardware encoders routinely exist in the binary but have no device.
bool encoderUsable(const char* aEncoderName);

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
//
// NOTE: this queries the encoder's CLAIMED list, which is not the same as what
// it will accept. libx265 lists yuva420p and then refuses avcodec_open2 with
// ENOSYS. Where the answer decides behaviour, use
// encoderSupportsPixFormatUsable().
bool encoderSupportsPixFormat(const QString& aEncoderName, const QString& aPixFmt);

// True iff the encoder can be opened AND encode a frame using this pixel
// format (cached one-frame probe). This is the one that is actually true.
bool encoderSupportsPixFormatUsable(const QString& aEncoderName, const QString& aPixFmt);


// True iff the container's muxer (e.g. "mp4", "webm") can carry the named
// encoder's codec. Used to reject a container/codec mismatch up front rather
// than failing confusingly at mux time.
bool muxerSupportsCodec(const QString& aContainer, const QString& aEncoderName);

// Pick a default video encoder the container's muxer can carry (and the linked
// build provides), in priority order. Empty string if none fit -> caller rejects.
QString defaultCodecForContainer(const QString& aContainer);

// A container this build can actually WRITE, and the encoder it defaults to.
//
// This exists because "which formats can I export?" was answered by a
// hand-maintained text file (assets/encode/VideoEncode.txt) that listed AVI,
// Huffyuv, Ogg and Theora as though every build had them, and carried ffmpeg
// COMMAND LINES from the era when the export shelled out to the CLI. The list is
// now DERIVED: a container appears only if its muxer exists, its default encoder
// is compiled in, and — the part no list can know — that encoder actually opens
// and encodes a frame HERE. Hardware encoders are the reason: av1_nvenc is
// compiled into most builds and fails at avcodec_open2 on a machine with no
// capable device.
struct ContainerOption {
    QString name;         // "mp4" — the name callers pass to the exporter
    QString label;        // "MP4" — what the user recognizes
    QString defaultCodec; // already probed usable here
    bool alphaCapable = false;
};

// Ordered most-commonly-wanted first. Probed once per process (each entry costs a
// one-frame encode) and cached, so this is safe to call when building a dialog.
QList<ContainerOption> writableContainers();

// The encoders this container could carry that actually run here, best first,
// each with the friendly name it is known by. An encoder absent from the
// known-name table is skipped rather than shown by its libav identifier: a string
// like "h264_qsv" in a codec list helps nobody, and the defaults cover every real
// case.
struct CodecOption {
    QString encoder; // "libx265"
    QString label;   // "H.265"
    bool lossless = false;
    bool alphaCapable = false;
};
QList<CodecOption> usableCodecsForContainer(const QString& aContainer);

// Whether we can mux a standard audio stream into this container. The list lived
// in two places and they disagreed: ExportParams.h offered AVI and the exporter
// silently dropped audio for it, so "export with audio" was a checkbox that did
// nothing for one of the containers it was shown for.
bool containerCarriesAudio(const QString& aContainer);

// True iff the named encoder runs here AND the container's muxer accepts it.
// The single question that decides whether a container/codec pair is offerable.
bool containerAcceptsUsableCodec(const QString& aContainer, const QString& aEncoderName);

} // namespace ffmpeg
} // namespace media

#endif
