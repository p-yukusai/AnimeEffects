#ifndef ANIMEEFFECTS_SRC_CTRL_EXPORTTARGET_H
#define ANIMEEFFECTS_SRC_CTRL_EXPORTTARGET_H

#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QColor>

#include <algorithm>
#include <functional>

#include "media/VideoEncoder.h"

// Destination-driven export targets (DESIGN PROTOTYPE).
//
// The export UI is built from a data table (assets/export/ExportTargets.txt),
// not from a wall of codec combos. Each row is one destination: it resolves to
// a container, a codec (dictated, or picked by a ladder), and the hard limits
// and capabilities that destination imposes.
//
// The split that matters: the USER states a fact they possess ("it's for
// Telegram"); the SYSTEM applies the consequences (WebM/VP9+alpha, no audio,
// <=256KB, <=3s). Capabilities are therefore declared per row as a MODALITY,
// and only a `Choice` ever becomes a control the user can get wrong.
namespace ctrl {

enum class Capability {
    Required,    // forced on — stated in the UI, never a toggle
    Forbidden,   // impossible or rejected — stated, or hidden
    DefaultOn,   // pre-set checkbox, overridable
    DefaultOff,  // pre-set checkbox, overridable
    Choice       // a genuine user decision
};

// What happens to our file after we hand it over. This is the axis that decides
// whether to optimize for efficiency or for decode compatibility.
enum class DestClass {
    Fixed,     // destination dictates the codec (most rows)
    Reencode,  // platform re-encodes our upload (YouTube, X, TikTok)
    Direct,    // platform serves our file as-is (website, messaging)
    Archive    // keep a master
};

// WHAT the destination wants produced. This is not the container: an image
// destination has no container at all, and the difference between one still and a
// directory of stills is the difference between two platforms' rules.
//
//   video    one muxed file (everything that goes through libav* or GifEncoder)
//   still    ONE image file at the path the user named (posting an illustration)
//   sequence a directory of numbered stills (pixiv ugoira, compositing elsewhere)
//
// Data, like every other per-destination fact: pixiv demands a frame sequence,
// DeviantArt wants a single image, and neither is a property of the artwork.
enum class ArtifactKind { Video, Still, Sequence };

enum class SizeMode {
    Free,          // use the project size
    Exact,         // exactly w x h (needs crop-or-pad — open design item)
    Max,           // fit inside w x h, preserve aspect
    MaxExactSide,  // long side == side, short side <= side
    MaxMinSide     // fit inside w x h, then ensure long side >= side
};

// The user's quality intent, in their words. Never a bitrate number: bitrate is
// codec-specific and meaningless to this audience.
enum class QualityBucket { Smaller, Balanced, Best, Lossless };

enum class FitMode { None, Pad, Crop };

struct SizeRule {
    SizeMode mode = SizeMode::Free;
    int w = 0;
    int h = 0;
    int side = 0; // MaxExactSide: the exact long side. MaxMinSide: the minimum long side.

    // How to reconcile a differing aspect ratio for an exact size. Platforms
    // disagree about margins, so this is declared per destination: Signal asks
    // for a 16px margin, WhatsApp for an 8px stroke, LINE forbids margins
    // entirely. Pad is the safe default (never loses content); Crop fills the
    // frame at the risk of cutting off the subject.
    FitMode fit = FitMode::Pad;
};

// Audio parameters for a destination. Everything is optional: an empty codec
// means "derive from the container" (WebM cannot carry AAC), and zeros mean
// "use the defaults". These are per-destination because they genuinely differ:
// a source upload wants generous bitrate, a website cares about bandwidth, and
// an archive wants lossless.
struct AudioSpec {
    QString codec;     // empty = derive from the container
    int bitrate = 0;   // bits/sec; 0 = default
    int channels = 0;  // 0 = default
    int sampleRate = 0;
};

struct Caps {
    qint64 maxBytes = 0;      // hard upload rejection above this
    double maxDurationSec = 0;
    // Floors as well as ceilings: some destinations refuse a file that is too
    // SHORT (Snapchat Spotlight wants 5 seconds or more). Nothing here can be
    // satisfied by exporting less, so a floor is reported rather than applied.
    double minDurationSec = 0;
    int maxFps = 0;
    int minFrames = 0;
    int maxFrames = 0;
};

struct ExportTarget {
    QString id;
    QString label;
    // Icon file stem in assets/icons (see assets/icons/README.md). Presentation,
    // but it belongs beside the label: the alternative is a switch over every
    // destination inside the UI code.
    QString icon;
    // The section this row is listed under in the destination picker: the PLATFORM
    // a person is posting to, keyed into the <groups> block of the data file.
    // Deliberately not derived from the label (which is translated, and whose
    // leading word is not always a platform) and deliberately not `family`: that
    // axis is what kind of handover this is, and it puts 29 of the 55 rows in one
    // block, mixing what a file IS with what it is FOR.
    QString group;
    // Where this row sits inside its section, when that order is a DESIGN decision
    // rather than an alphabet (the four `general` rows: Video, Animated image,
    // Image sequence, Archive file). 0 = unranked, and 0 is what an absent or
    // unparseable attribute means - the row is then ordered by its label alone.
    //
    // Explicit because the label is TRANSLATED: a label sort is a different order
    // in every language (it would put "Animated image" above "Video" and re-shuffle
    // the block again under a translation), and because rank is the only way those
    // rows can sit above the archive row - a sibling of theirs - without having to
    // out-vote it by name. Data, like every other layout fact on this struct.
    int order = 0;
    QString family;
    DestClass klass = DestClass::Fixed;

    // I44 (review: "Discord codec fixed"): the codecs this destination ACCEPTS,
    // most preferred first, and which one is the default. A LIST, not a mode -
    // the old `codecSelect fixed|ladder` said how we would pick one; this says
    // what the platform actually takes, so the codec row can offer AV1/H.265/
    // H.264 where they are accepted instead of a fixed encoder. Keys are FORMAT
    // names ("h264", "hevc", "av1", "vp9", "webp", "apng", "gif", "ffv1",
    // "prores"): the facts name formats ("H.264 or HEVC in a closed GOP"), and
    // which encoder produces one stays internal (the ladder's own rule).
    QStringList codecs;
    // The declared default among `codecs`. Empty = the row states no preference,
    // so the class policy decides (the ladder, constrained to `codecs`) - the
    // same "empty = derive" convention AudioSpec uses for its codec. Either way
    // the answer must be a member of `codecs` that this build can produce.
    QString defaultCodec;

    // For an image artifact (Still/Sequence) the CONTAINER names the image format
    // instead of a muxed container: "png", "jpg".
    QString container;
    // The ENCODER implementation this row pins for its codecs (an internal, not a
    // fact): empty = pick by the format's own table. Kept per row because exactly
    // one row needs it - safari.transparent must use hevc_videotoolbox (libx265
    // advertises yuva420p and then refuses it at open) - and because the pinned
    // encoders of the fixed rows are the measured, working choice.
    QString codec;
    QString pixFmt;
    ArtifactKind artifact = ArtifactKind::Video;

    // True when the export's frame rate does not reach the destination at all: pixiv
    // ugoira is a folder of numbered stills whose timing is authored on the upload
    // page, so the frame rate is not a question the form should ask. The frame COUNT
    // still applies (caps.maxFrames).
    bool fpsIgnored = false;

    Capability alpha = Capability::Forbidden;
    Capability audio = Capability::Choice;
    Capability loop = Capability::Choice;

    SizeRule size;
    Caps caps;

    // Two DIFFERENT mechanisms, deliberately not merged:
    //  - encOptions: private encoder options handed to avcodec_open2 (libvpx's
    //    auto-alt-ref=0, which VP9+alpha requires or the alpha flickers).
    //  - codecTag:   four-char container stream tag (hvc1 for HEVC, without
    //    which Safari/iOS refuse the file). It is not an encoder option.
    QMap<QString, QString> encOptions;
    // Container-level requirements, passed to the muxer (movflags=+faststart for
    // Instagram, which rejects an MP4 without its moov atom at the front).
    QMap<QString, QString> muxerOptions;
    QString codecTag;
    // Codec FAMILY the tag above applies to (the `codec=` attribute on the
    // element: "hevc"). Empty = unconditional, which is what a row whose codec
    // list is a single family needs. A qualified tag is written only when the
    // codec the plan actually resolved is that family (ruling 2026-10-03:
    // hvc1 for HEVC in an ISO container, and NOTHING ELSE changes).
    QString codecTagCodec;
    AudioSpec audioSpec; // parameters (the `audio` member is the capability)

    // Floor for fit-to-cap's scale phase, in percent. Below this the image is
    // visibly degraded, so the loop stops and reports rather than shrinking
    // further. Data, not a code constant.
    int minScalePct = 25;

    // Order in which fit-to-cap sacrifices quality, taken from the data.
    // Default rationale: quality first (platforms' own guidance, and it changes
    // neither composition nor timing), then scale (loses detail, keeps timing),
    // then frames (changes the animation itself — most destructive).
    QStringList degradeOrder;

    QString note; // shown to the user as the "why"

    // Provenance. These facts move; the date is how we know when to re-check.
    QString checked;
    QString source;
    bool verified = false;

    // Some destinations can only be produced on one OS (HEVC+alpha needs
    // VideoToolbox). Declared rather than discovered, so a row can be skipped
    // without pretending it is broken.
    QString platform; // empty/"any" = everywhere
};

// One section of the destination list: the platform a person is posting to. The
// ORDER of these in the data file is the order of the sections on screen.
struct ExportGroup {
    QString id;    // matches ExportTarget::group
    QString title; // what the section says ("Telegram")
    // Whether this section's rows are VARIANTS of one platform (Telegram: a sticker,
    // a custom emoji, a video) or merely rows that share the absence of one
    // ("general": a video, an animated image, a sequence, a master copy).
    // Variants are a node you open; the others are destinations and are listed
    // directly, because a platform holding one line, or holding four unrelated
    // answers, is a level of hierarchy that asks nothing.
    bool variants = true;
    // Draw a full-width RULE after this group's rows (the `separatorAfter`
    // attribute on the data table's <group>; set on `general`, the four answers with
    // no platform of their own, whose end is where the platform sections begin).
    // Which groups get one is a fact about the list's STRUCTURE, so it is data here
    // for the same reason `variants` is: adding a block that needs its own boundary
    // must be an edit to the table, never a constant in the view.
    //
    // It marks the END of the whole group, so it follows every row the group
    // contributed whatever order they ended up in (the picker's sort runs before
    // the insertion) - and it is set on `general` alone, after all FOUR of its
    // rows. The archive row is why: a master copy is no more a platform than the
    // video, image and sequence above it, so a rule between it and the platform
    // sections would say something false about what a section means.
    bool separatorAfter = false;
};

// An inclusive span of project frames. Inclusive rather than half-open because
// it mirrors util::Range / the project's own frame ranges (which support
// negative starts — frame -1 is a normal frame in this codebase).
struct FrameRange {
    int first = 0;
    int last = 0;

    int count() const { return last >= first ? (last - first + 1) : 0; }
    bool valid() const { return last >= first; }
};

// WHICH frames to export, as one or more ranges.
//
// This exists because the export dialog has always let the user select several
// non-contiguous ranges and the model could only hold a single count, so the
// exporter collapsed them to [first of the first .. last of the last] — i.e. it
// silently exported frames the user had excluded. A count cannot express "these
// frames but not those", which is why this is a list.
//
// Semantics, deliberately explicit:
//  - Ranges are emitted in the ORDER GIVEN, packed contiguously. Gaps between
//    ranges are DROPPED, not held: selecting 0-10 and 50-60 yields 22 frames of
//    output, not 61 with 39 blank/holding ones. Holding or blanking a gap is
//    never what someone exporting a selection wants.
//  - Duration is therefore total() / fps. Gaps do not extend it.
//  - Range order is honoured rather than sorted, so the output matches the list
//    the user is looking at (WYSIWYG). Overlaps are NOT merged or deduplicated:
//    total() must equal the number of frames actually written, and duplicating a
//    frame is a real (if unusual) way to hold one.
struct FrameSelection {
    QList<FrameRange> ranges;

    static FrameSelection fromRange(int aFirst, int aLast);
    static FrameSelection fromCount(int aCount); // frames 0 .. aCount-1

    // Total frames written — what duration, the caps and the ETA all use.
    int total() const;

    // Number of usable ranges (a backwards range counts as none). >1 means the
    // selection has gaps, which is the case worth telling the user about.
    int rangeCount() const;

    bool isEmpty() const { return total() == 0; }

    // Project frame numbers to render, in output order. This is the
    // authoritative answer; with several ranges the count alone does not say
    // what to render.
    QList<int> indices() const;

    // The first N frames of the selection, in output order. Monotone by
    // construction (a smaller N is always a prefix), which is what makes it
    // usable both for cap enforcement and as a fit-to-cap search dimension.
    FrameSelection firstN(int aCount) const;

    // For notes: "90 frames" or "90 frames from 3 ranges".
    QString describe() const;
};

struct ExportRequest {
    QString outputPath;
    QSize sourceSize;
    // The output size the caller's user asked for. It applies ONLY to a
    // destination whose size rule is Free — the one case where the destination
    // does not decide — and empty means "use the source size".
    //
    // Deliberately separate from sourceSize: sourceSize is the ART, and the fit
    // rules (max / exact / side) derive the aspect they preserve from it. Feeding
    // a size the user typed for one destination into another destination's fit
    // would silently change the aspect the art is padded or cropped to.
    QSize requestedSize;
    int fps = 24;
    // Which frames. Defaults to a single frame, matching the old `frames = 1`.
    FrameSelection selection = FrameSelection::fromRange(0, 0);
    // The stem for a frame SEQUENCE's numbered files. The app names them after the
    // project (the sequence's target is a directory, so there is no file name to
    // take it from); the fit loop has to write the same names a plain export would,
    // or "fitted" would mean a differently named set.
    QString imageStem;
    // The frame the timeline is showing. A Still destination writes exactly this
    // one image ("post the illustration I am looking at"), which is a different
    // question from "which range do you want", so it is not read out of the
    // selection: the dialog's range has nothing to say about a single image and its
    // start is usually frame 0.
    int playhead = 0;

    // The VIDEO codec the user picked from the codec row (I44): a libav encoder
    // name taken from the destination's offered list (what it accepts ∩ what this
    // build can produce). Empty = the destination's own default (its declared
    // default, or the class policy where it declares none). Deliberately the
    // opposite of the AUDIO codec note below: a destination that merely defaults
    // the codec leaves the choice to the user (contract item 42), and the plan
    // re-validates the choice against the accepted list anyway, so a stale pick
    // cannot smuggle in a codec the destination refuses.
    QString codec;

    // The "Export with transparency" checkbox (the dialog always carries its
    // state; bare requests assume it on). This is the ALPHA STATE the archive
    // codec rule follows (user's call 2026-10-01): the default codec may depend
    // on whether transparency is wanted, because the alpha-capable codecs are a
    // strict subset of the accepted ones (MEASURED 2026-10-01 in this build:
    // ffv1 and libvpx-vp9 carry yuva420p; libsvtav1 accepts the request but
    // stores no alpha at all; libx265 refuses it outright).
    bool allowTransparency = true;

    // The effective viewport background colour (the project's override, else the
    // theme's recessed token), resolved by the GUI and carried down plain. It is
    // the colour the export paints behind the art WHEN THE ARTIFACT IS OPAQUE —
    // i.e. when the plan resolves alpha off (planExport's alphaEnabled, which the
    // fit loop reads). While transparency is on it is not used at all.
    //
    // On the request rather than a parameter of the fit call: the fit path
    // re-plans from this same request and pads its own margins, so the colour has
    // to travel with the fields the fit re-reads (see MainWindow's fitRequest).
    QColor viewportBackground;

    // Whether audio is exported is NOT a preference — it is derived:
    //   included  iff  the project HAS audio  AND  not muted  AND  the
    //   destination permits it.
    // There is deliberately no "wantAudio": the project either has sound or it
    // does not, and a destination either accepts it or it does not. Letting a
    // caller *demand* audio invented a conflict that then needed enforcement
    // machinery to resolve. The only real user intent is the restriction below.
    // How many of the project's audio tracks will actually be mixed: tracks
    // flagged exportEnable=false are timing/reference only (a voice pass, a temp
    // music bed) and are heard while animating but must not reach the file.
    // A COUNT, not a bool — the plan can then say how much it is mixing.
    int exportableAudioTracks = 0;
    bool mute = false; // INTENT: leave all sound out of this export
    int audioChannels = 2;
    int audioSampleRate = 48000;
    // NOTE: the audio CODEC is deliberately NOT here — it is derived from the
    // container, because a caller-supplied one can be impossible to mux
    // (AAC into WebM fails at avformat_write_header). Same rule as everything
    // else: don't let callers ask for things that cannot work.

    // GIF only. media::GifEncoder owns that artifact instead of the video path,
    // and the fit loop has to write the SAME file a single-pass export would —
    // otherwise "fitted" would silently mean a different artifact (adaptive
    // palette instead of the user's, permanent loop instead of one play).
    // A user palette is a user file, so it belongs on the request.
    QString gifPalettePath;
    int gifLoop = 0; // 0 = loop forever, -1 = play once (the muxer's own units)
};

struct ExportPlan {
    ExportTarget target;
    // What to write. Video (the default) uses `config`; Still and Sequence have no
    // encoder config at all, because Qt's own image writers produce them.
    ArtifactKind artifact = ArtifactKind::Video;
    QString imageSuffix; // "png"/"jpg" for Still and Sequence, else empty
    media::VideoEncoder::Config config;
    QSize size;        // the OUTPUT size (what the file must be)
    QSize renderSize;  // what to RENDER at — differs from `size` for exact-size
                       // destinations, which fit the art and pad the remainder
    int fps = 0;
    // Which frames to render, in output order, AFTER caps have been applied.
    // Authoritative: with several ranges the count alone does not say what to
    // render, so callers must read this rather than assume 0..frames-1.
    FrameSelection selection;
    // Duration, caps and the ETA all want just the count. A method rather than a
    // field so it cannot drift out of sync with `selection`.
    int frames() const { return selection.total(); }
    bool alphaEnabled = false;
    bool audioEnabled = false;

    // GIF is not a VideoEncoder target — media::GifEncoder owns it (adaptive or
    // custom palette). Flagged here so a caller doesn't feed a GIF row into the
    // video path.
    bool gifPath = false;

    // Human-readable "why" + any warnings. The UI shows these; they are how a
    // non-expert learns what their destination requires.
    QStringList notes;

    QString error;
    bool ok() const { return error.isEmpty(); }
};

// The knob that trades quality for bytes, per codec. Loaded from the data file
// (the values move with encoder versions and content type), not hardcoded.
struct QualityControl {
    QString codec;
    QString option; // private encoder option name, e.g. "crf"
    int best = 0;   // start here: highest quality
    int worst = 0;  // lowest quality we accept before shrinking instead

    // There is deliberately NO "auto" value here any more (ruled 2026-10-08: "Auto
    // needs to be a special value, not just a visual thing"). Auto is a STATE of the
    // dialog's own control (ExportViewModel::qualityOverrideAuto) and of the request
    // (ExportParams::qualityOverride, -1 = let the Quality setting decide), so the
    // span above is the codec's real one. The old `autoValue()` - "one below the
    // span" - existed because a QSpinBox could not hold a value outside its range;
    // the QML field can, which removed the sentinel, its clamping problem, and the
    // comparison every codec change had to redo. See
    // docs/export-dialog-implementation-notes.md, "Auto is a state".
};

struct FitResult {
    bool ok = false;
    // The user stopped it. Distinct from a failure: the caller must not report
    // "even the smallest setting did not fit" for a job the user cancelled.
    bool canceled = false;
    QString path;
    qint64 bytes = 0; // size of the ACCEPTED file (or, on failure, the smallest seen)
    int attempts = 0;
    QSize finalSize;
    QStringList steps; // what the loop actually did, for the UI to show
    QString error;
};

class ExportTargets {
public:
    // Resource path the app loads. Overridable so headless tests can read the
    // source file directly (same pattern as the preset matrix test).
    static QString defaultPath();

    static QList<ExportTarget> load(const QString& aPath = defaultPath(), QString* aError = nullptr);

    // The picker's sections, in the order the file lists them. Empty when the file
    // has no <groups> block, in which case the picker falls back to one flat list.
    static QList<ExportGroup> loadGroups(const QString& aPath = defaultPath());

    static ExportTarget find(const QList<ExportTarget>& aTargets, const QString& aId);

    // Per-codec quality knobs (the <quality_control> block), keyed by codec.
    static QHash<QString, QualityControl> loadQualityControl(
        const QString& aPath = defaultPath());

    // Where each quality intent starts within a codec's [best, worst] span
    // (0 = the codec's best value, 1 = the lowest we accept before the fit-to-cap
    // loop shrinks the image instead). Keyed by bucket name, from
    // <bucket_positions>. One table for every codec: the intent describes the
    // trade the user will accept, not a particular encoder.
    static QHash<QString, double> loadBucketPositions(const QString& aPath = defaultPath());
};

// The user's quality intent, applied through the codec's OWN knob ('crf' for the
// x264/x265/VP9/AV1 families, 'quality' for WebP): writes aEncOptions[option] and
// returns true. Returns false when the codec has no knob in the table, in which
// case there is nothing to trade and nothing is written.
//
// Shared by planExport() and by the export dialog's manual mode, so "Balanced"
// cannot mean two different things depending on whether the user picked a
// destination. Before this, only the destination path applied the intent at all:
// the Quality control was inert in manual mode, and manual video exports went out
// at whatever bitrate the old Bitrate field happened to say.
//
// aNotes receives the honest caveats (a table with no <bucket_positions>, or
// "Lossless" asked of a lossy codec).
bool applyQualityIntent(const QString& aCodec, QualityBucket aBucket,
                        QMap<QString, QString>& aEncOptions, QStringList* aNotes = nullptr);

// The knob a codec trades quality with ("crf", "quality"), or empty when it has
// none. For a control that must name what it is about to set.
QString qualityKnobFor(const QString& aCodec);

// The human name of a `platform` value ("apple" -> "macOS"). The table stores a
// token; a menu must not show the token, which reads as a missing feature rather
// than a platform requirement.
QString platformName(const QString& aPlatform);

// Apply a destination's size rule to a source size. YUV planar formats need
// even dimensions, so the result is nudged to even.
QSize applySizeRule(const SizeRule& aRule, const QSize& aSource, QStringList* aNotes = nullptr);

// Clamp fps/frame-count to the destination's caps (the pre-encode, deterministic
// half of constraint enforcement).
void applyCaps(const Caps& aCaps, int* aFps, int* aFrames, QStringList* aNotes = nullptr);

// What a person calls the format an encoder produces, and the container it lands in.
// The library name ("libsvtav1", "hevc_nvenc") is an implementation detail of ours: the
// user reads "AV1" and "MP4". Falls back to the name it was given, so an encoder without
// a friendly name is still stated rather than hidden.
QString codecDisplayName(const QString& aCodec);
QString containerDisplayName(const QString& aContainer);

// The FORMAT key an encoder produces ("libx265" -> "hevc"), or empty for an
// encoder the schema has no key for. The inverse of formatEncoders(); the
// schema and the facts name formats, the build names encoders, and everything
// that crosses between them crosses here.
QString formatKeyForEncoder(const QString& aEncoder);

// Pick a codec for a destination whose codec is NOT dictated. Walks the (class,
// bucket) ladder and returns the first candidate that actually RUNS here, with hardware
// preferred inside a format for the buckets that care about time. `aWhy` receives the
// one-line reason the UI shows: the format, and what this destination does with the
// file. Nothing about which library produced it, and nothing about how long it will
// take: neither is the user's to adjudicate.
QString resolveLadderCodec(DestClass aClass, QualityBucket aBucket, QString* aWhy = nullptr);

// One codec the codec row can offer (I44): a FORMAT the destination accepts that
// this build can actually produce into the destination's container, and the
// encoder that will produce it. The row shows `label` ("H.265"); the export uses
// `encoder` ("libx265"); the schema and the facts carry only `format` ("hevc").
struct CodecChoice {
    QString format;
    QString encoder;
    QString label;
    bool lossless = false;
};

// The intersection the contract names: the destination's accepted list ∩ what
// this build can produce (an encoder that RUNS here - not one that merely
// compiled in - and that the container's muxer takes). Empty for a row that
// dictates one codec or produces an image: those have nothing to choose, and the
// codec row stays hidden for them (contract item 42: "controls the destination
// dictates are stated, not offered").
QList<CodecChoice> offeredCodecs(const ExportTarget& aTarget);

// The default codec for a row: its DECLARED default where it has one, else the
// class policy (the ladder, constrained to the row's accepted list - the policy
// picks among what the destination accepts and never beyond it). Returns an
// empty choice when the row accepts nothing this build can produce.
// `aAlphaOn` is the user's transparency state (ExportRequest::allowTransparency):
// it changes the default only for Archive outside Lossless (rule decided
// 2026-10-01) — transparency ON takes the alpha-capable lossy codec (VP9 here;
// measured: AV1 drops alpha silently in this build), OFF takes AV1 where quality
// per bit wins and H.265 where the ladder puts time/size first.
// `aWhy` receives the class-policy reason when the policy decided; a declared
// default needs no advocacy.
CodecChoice resolveDefaultCodec(const ExportTarget& aTarget, QualityBucket aBucket,
                                bool aAlphaOn, QString* aWhy = nullptr);

// The whole point: destination + request + quality intent -> a ready-to-open
// encoder config, plus constraint application and human-readable reasoning.
ExportPlan planExport(const ExportTarget& aTarget, const ExportRequest& aRequest,
                      QualityBucket aBucket);

// Post-encode half of constraint enforcement: encode, MEASURE, and if the file
// is over the destination's cap, degrade and re-encode until it fits.
//
// File size is the one limit that cannot be known before encoding, so measuring
// is the only honest way to satisfy it. Degradation order is least-destructive
// first: quality -> scale -> frame count. These caps are hard upload
// rejections, so this is the difference between a file the platform accepts and
// a 400.
// `aOnStep` (optional) is called for each encode attempt as it happens. The
// attempt list is on the result too, but reaching a cap can take a dozen encodes
// — minutes of work — and a caller that can only report afterwards leaves the
// user watching a frozen progress bar.
// `aShouldCancel` (optional) is polled before each attempt. Fitting can run a
// dozen encodes, so a Cancel button that does nothing until it finishes is a
// button that lies about what it does.
FitResult exportWithFit(const ExportTarget& aTarget, const ExportRequest& aRequest,
                        QualityBucket aBucket, const QList<QImage>& aFrames,
                        const std::function<void(const QString&)>& aOnStep = nullptr,
                        const std::function<bool()>& aShouldCancel = nullptr);

} // namespace ctrl

#endif // ANIMEEFFECTS_SRC_CTRL_EXPORTTARGET_H
