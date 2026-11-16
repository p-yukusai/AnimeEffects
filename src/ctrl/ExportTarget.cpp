#include "ctrl/ExportTarget.h"

#include "media/FFmpegHelpers.h"
#include "media/GifEncoder.h"

#include <QCoreApplication>
#include <QDomDocument>
#include <QFileInfo>
#include <QFile>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <climits>
#include <functional>

namespace ctrl {
namespace {

// ---------- parsing helpers -------------------------------------------------

Capability parseCapability(const QString& aText) {
    if (aText == QLatin1String("required")) return Capability::Required;
    if (aText == QLatin1String("forbidden")) return Capability::Forbidden;
    if (aText == QLatin1String("default-on")) return Capability::DefaultOn;
    if (aText == QLatin1String("default-off")) return Capability::DefaultOff;
    return Capability::Choice;
}

DestClass parseClass(const QString& aText) {
    if (aText == QLatin1String("reencode")) return DestClass::Reencode;
    if (aText == QLatin1String("direct")) return DestClass::Direct;
    if (aText == QLatin1String("archive")) return DestClass::Archive;
    return DestClass::Fixed;
}

FitMode parseFitMode(const QString& aText) {
    if (aText == QLatin1String("crop")) return FitMode::Crop;
    if (aText == QLatin1String("none")) return FitMode::None;
    return FitMode::Pad;
}

const char* bucketName(QualityBucket aBucket) {
    switch (aBucket) {
    case QualityBucket::Smaller: return "smaller";
    case QualityBucket::Balanced: return "balanced";
    case QualityBucket::Best: return "best";
    case QualityBucket::Lossless: return "lossless";
    }
    return "balanced";
}


SizeMode parseSizeMode(const QString& aText) {
    if (aText == QLatin1String("exact")) return SizeMode::Exact;
    if (aText == QLatin1String("max")) return SizeMode::Max;
    if (aText == QLatin1String("maxExactSide")) return SizeMode::MaxExactSide;
    if (aText == QLatin1String("maxMinSide")) return SizeMode::MaxMinSide;
    return SizeMode::Free;
}

int attrInt(const QDomElement& aElem, const QString& aName, int aDefault = 0) {
    bool ok = false;
    const int v = aElem.attribute(aName).toInt(&ok);
    return ok ? v : aDefault;
}

// A byte cap is a qint64 (Caps::maxBytes): a file limit is routinely gigabytes, and
// toInt() FAILS the moment a value passes INT_MAX and returns the default instead.
// Telegram's 2 GB (2147483648) is exactly one past it, so it parsed as "no cap" and
// the destination silently lost its limits row. Same shape as attrInt, same
// default-on-failure, only the width differs.
qint64 attrLongLong(const QDomElement& aElem, const QString& aName, qint64 aDefault = 0) {
    bool ok = false;
    const qint64 v = aElem.attribute(aName).toLongLong(&ok);
    return ok ? v : aDefault;
}

double attrDouble(const QDomElement& aElem, const QString& aName, double aDefault = 0.0) {
    bool ok = false;
    const double v = aElem.attribute(aName).toDouble(&ok);
    return ok ? v : aDefault;
}

// ---------- size helpers ---------------------------------------------------

// YUV planar formats need even dimensions. Always floor to even so we never
// overshoot a maximum.
int evenFloor(double aValue) {
    int v = (int)std::floor(aValue);
    if (v < 2) v = 2;
    if (v & 1) --v;
    return v;
}

// The audio codec a container can actually carry. Derived, not requested:
// WebM cannot hold AAC and fails at avformat_write_header with EINVAL if you
// try — so the container decides.
QString audioCodecForContainer(const QString& aContainer) {
    const QString c = aContainer.toLower();
    if (c == QLatin1String("webm")) return QStringLiteral("libopus");
    if (c == QLatin1String("mp4") || c == QLatin1String("mov") || c == QLatin1String("m4v"))
        return QStringLiteral("aac");
    if (c == QLatin1String("mkv") || c == QLatin1String("matroska"))
        return QStringLiteral("aac");
    return QString(); // this container carries no standard audio stream for us
}


// ---------- the (class, bucket) codec ladder --------------------------------
//
// Ordered best -> fallback. A candidate is used only if its encoder actually runs
// here, which is a question for the linked build and this machine together:
// `ffmpeg -encoders` is not a reliable answer, because it reports what the binary was
// compiled with, so e.g. av1_nvenc is listed on an RTX 3080 and fails at runtime. The
// probe is media::ffmpeg::encoderUsable, not encoderAvailable: a hardware encoder is
// routinely present in the binary while having no capable device on this machine.
//
// A codec FAMILY: one compression format, whose encoders differ only in how they
// produce it (software with the full rate-control toolbox, hardware with the speed).
// The ORDER of these lists is the ladder's policy, and it is a fact rather than a
// taste: per bit, AV1 beats H.265 beats H.264 at the same quality target, and within a
// family the software encoder exposes the better rate control while the hardware one is
// faster. Nothing else about the ladder is hand-ordered.
QStringList familyEncoders(const QString& aFamily) {
    if (aFamily == QLatin1String("av1")) return {"libsvtav1", "libaom-av1", "av1_nvenc", "av1_qsv", "av1_vaapi", "av1_amf"};
    if (aFamily == QLatin1String("hevc")) return {"libx265", "hevc_nvenc", "hevc_qsv", "hevc_vaapi", "hevc_amf"};
    if (aFamily == QLatin1String("h264")) return {"libx264", "h264_nvenc", "h264_qsv", "h264_vaapi", "h264_amf"};
    if (aFamily == QLatin1String("lossless")) return {"ffv1"};
    if (aFamily == QLatin1String("vp9")) return {"libvpx-vp9", "vp9_vaapi"};
    return {};
}

// The <quality_control> entry that governs a codec FAMILY
// (assets/export/ExportTargets.txt:1221-1231). The table names SOFTWARE
// encoders: the span and the option are properties of the format's rate
// control, and the plan resolves the ENCODER, which on a machine with a usable
// hardware encoder is h264_nvenc / hevc_vaapi / av1_qsv ... — no entry of its
// own, so a lookup keyed on the resolved encoder missed the table and dropped
// the user's quality intent on every hardware-resolved row, silently. ONE
// mapping, consulted by every caller that turns a resolved encoder into a
// quality knob (qualityControlFor(), hence applyQualityIntent() and
// qualityKnobFor()), so the dialog and the plan cannot disagree about which
// knob a codec has, or over what span.
//
// MEASURED against the linked libavcodec (`ffmpeg -h encoder=h264_nvenc` on the
// same libavcodec.so.62 the build links): the hardware encoders of these
// families name their quality option differently — `cq` on NVENC, `qp` on
// VAAPI, and QSV and AMF expose none — so the family's option name is what the
// plan writes, and media::VideoEncoder::open's own "unknown encoder option"
// check is what answers a refusal (Exporter::execute then steps the export down
// to the family's software encoder, which does take the name). Renaming per
// backend is a data decision, not this function's.
QString qualityTableNameForFamily(const QString& aFamily) {
    if (aFamily == QLatin1String("av1")) return QStringLiteral("libsvtav1");
    if (aFamily == QLatin1String("hevc")) return QStringLiteral("libx265");
    if (aFamily == QLatin1String("h264")) return QStringLiteral("libx264");
    if (aFamily == QLatin1String("vp9")) return QStringLiteral("libvpx-vp9");
    return QString();
}

// ---------- I44: format keys (what the facts and the schema name) ----------
//
// The schema's `codecs` list names FORMATS ("Discord accepts AV1/H.265/H.264"),
// never encoder libraries (the ladder's own rule: the choice of implementation
// stays internal). These three functions are the translation between the two
// worlds: key -> candidate encoders, key -> what the row shows, encoder -> key.
QStringList formatEncoders(const QString& aFormat) {
    // The four families the ladder knows first: same names, same order policy.
    if (aFormat == QLatin1String("av1") || aFormat == QLatin1String("hevc")
        || aFormat == QLatin1String("h264") || aFormat == QLatin1String("ffv1")) {
        return familyEncoders(aFormat == QLatin1String("ffv1") ? QStringLiteral("lossless")
                                                              : aFormat);
    }
    if (aFormat == QLatin1String("vp9")) return {"libvpx-vp9", "vp9_vaapi"};
    if (aFormat == QLatin1String("vp8")) return {"libvpx"};
    if (aFormat == QLatin1String("webp")) return {"libwebp", "libwebp_anim"};
    if (aFormat == QLatin1String("apng")) return {"apng"};
    if (aFormat == QLatin1String("gif")) return {"gif"};
    if (aFormat == QLatin1String("prores")) return {"prores_ks"};
    return {};
}

QString familyLabel(const QString& aFamily) {
    if (aFamily == QLatin1String("av1")) return QStringLiteral("AV1");
    if (aFamily == QLatin1String("hevc")) return QStringLiteral("H.265");
    if (aFamily == QLatin1String("h264")) return QStringLiteral("H.264");
    if (aFamily == QLatin1String("lossless")) return QStringLiteral("FFV1");
    return aFamily;
}

QString formatLabel(const QString& aFormat) {
    if (aFormat == QLatin1String("ffv1")) return QStringLiteral("FFV1");
    if (aFormat == QLatin1String("vp9")) return QStringLiteral("VP9");
    if (aFormat == QLatin1String("vp8")) return QStringLiteral("VP8");
    if (aFormat == QLatin1String("webp")) return QStringLiteral("WebP");
    if (aFormat == QLatin1String("apng")) return QStringLiteral("APNG");
    if (aFormat == QLatin1String("prores")) return QStringLiteral("ProRes");
    if (aFormat == QLatin1String("gif")) return QStringLiteral("GIF");
    return familyLabel(aFormat); // av1 / hevc / h264, or the key itself
}

bool isHardwareEncoder(const QString& aName) {
    return aName.contains(QLatin1String("nvenc")) || aName.contains(QLatin1String("qsv"))
           || aName.contains(QLatin1String("vaapi")) || aName.contains(QLatin1String("amf"))
           || aName.contains(QLatin1String("videotoolbox"));
}

// Which kind of encoder a bucket wants first, when a format has both.
//
// A FACT ABOUT THE FORMAT, not a measurement: a hardware encoder of the same format is
// built for speed and a software one for rate control, so the time-sensitive buckets
// (Smaller, Balanced) take hardware first and the fidelity buckets (Best, Lossless) keep
// the declared (software) order.

struct Ladder {
    QStringList families; // codec families, most efficient first
    // true for the buckets that care about time: hardware first inside a family.
    bool fastFirst = true;
};

Ladder ladderFor(DestClass aClass, QualityBucket aBucket) {
    Ladder out;
    if (aClass == DestClass::Direct) {
        // The platform serves our file as-is, so DECODE COMPATIBILITY is the
        // requirement and quality per bit is not: H.264 is the one format every
        // browser, phone and TV plays. AV1 and H.265 remain reachable by naming a codec
        // in Advanced; they are not chosen for the user at any bucket.
        out.families = {QStringLiteral("h264")};
        out.fastFirst = true; // hardware H.264 when it runs here
        return out;
    }
    const bool lossless = aBucket == QualityBucket::Lossless;
    out.families = lossless
                       ? QStringList{QStringLiteral("lossless"), QStringLiteral("hevc"),
                                     QStringLiteral("av1"), QStringLiteral("h264")}
                       : QStringList{QStringLiteral("av1"), QStringLiteral("hevc"),
                                     QStringLiteral("h264")};
    // One rule for every bucket, and the only thing a bucket changes: whether
    // quality per bit or time wins when both encoders of a family run here.
    // The buckets that pay for speed are Smaller and Balanced, and no others:
    // "Best" has room to be slow, and Lossless is a master copy where the software
    // encoder's rate control is the whole point of the bucket. `!= Best` alone let
    // Lossless take the hardware encoder first, contradicting the comment this
    // struct carries.
    out.fastFirst = (aBucket == QualityBucket::Smaller
                     || aBucket == QualityBucket::Balanced);
    return out;
}

// The format as a person reads it. NOT the library that produced it: "libsvtav1",
// "hevc_nvenc" and "libvpx-vp9" are ours to choose and nobody's to adjudicate, so the
// facts name the format (AV1, H.265, VP9) and the choice of implementation stays
// internal.
// WHY this encoder, as one whole sentence per destination class, with the encoder as
// %1. These were three English literals glued onto a label with a leading ": ", which
// is untranslatable in practice: word order, the colon and the sentence break all move
// between languages, and a translator had no string to work with at all.
QString classReason(DestClass aClass, const QString& aEncoder) {
    switch (aClass) {
    case DestClass::Reencode:
        return QCoreApplication::translate(
                   "ExportTargets",
                   "%1. This site re-encodes uploads, so source quality matters more than "
                   "compatibility")
            .arg(aEncoder);
    case DestClass::Direct:
        return QCoreApplication::translate(
                   "ExportTargets",
                   "%1. This plays your file directly, so decode compatibility matters most")
            .arg(aEncoder);
    case DestClass::Archive:
        return QCoreApplication::translate(
                   "ExportTargets", "%1. A master copy, so fidelity matters most")
            .arg(aEncoder);
    default:
        return aEncoder;
    }
}

} // namespace

// The exported half of the format-key translation (the encoder tables above are
// TU-local): declared in the header, so it lives OUTSIDE the anonymous
// namespace — a second definition inside it would make every call ambiguous.
QString formatKeyForEncoder(const QString& aEncoder) {
    if (aEncoder.isEmpty()) return QString();
    if (aEncoder.contains(QLatin1String("av1")) || aEncoder == QLatin1String("libaom-av1"))
        return QStringLiteral("av1");
    if (aEncoder.contains(QLatin1String("265")) || aEncoder.contains(QLatin1String("hevc")))
        return QStringLiteral("hevc");
    if (aEncoder.contains(QLatin1String("264"))) return QStringLiteral("h264");
    if (aEncoder == QLatin1String("ffv1")) return QStringLiteral("ffv1");
    if (aEncoder == QLatin1String("libvpx-vp9") || aEncoder == QLatin1String("vp9_vaapi"))
        return QStringLiteral("vp9");
    if (aEncoder == QLatin1String("libvpx")) return QStringLiteral("vp8");
    if (aEncoder.contains(QLatin1String("webp"))) return QStringLiteral("webp");
    if (aEncoder == QLatin1String("apng")) return QStringLiteral("apng");
    if (aEncoder == QLatin1String("gif")) return QStringLiteral("gif");
    if (aEncoder.contains(QLatin1String("prores"))) return QStringLiteral("prores");
    return QString();
}

namespace {

// The <quality_control> entry that governs a RESOLVED encoder: the encoder's own
// entry where the table names it, else its FAMILY's (see
// qualityTableNameForFamily). The table names software encoders, and the plan
// resolves the ENCODER, which on a machine with a usable hardware encoder is
// h264_nvenc / hevc_vaapi / av1_qsv ... — so a lookup keyed on the resolved
// encoder alone missed the table and dropped the user's quality intent on every
// hardware-resolved row, for exactly the inert-control reason this knob exists
// to fix. ONE function for both callers that turn a resolved encoder into a
// quality knob (applyQualityIntent and qualityKnobFor), so the dialog's quality
// control and the plan cannot disagree about which knob a codec has.
QualityControl qualityControlFor(const QString& aCodec) {
    const QString tableName = qualityTableNameForFamily(formatKeyForEncoder(aCodec));
    return ExportTargets::loadQualityControl().value(tableName.isEmpty() ? aCodec : tableName);
}

} // namespace

bool applyQualityIntent(const QString& aCodec, QualityBucket aBucket,
                        QMap<QString, QString>& aEncOptions, QStringList* aNotes) {
    const QualityControl control = qualityControlFor(aCodec);
    if (control.option.isEmpty()) return false; // no knob: nothing to trade
    const QHash<QString, double> positions = ExportTargets::loadBucketPositions();
    const QString bucket = QString::fromUtf8(bucketName(aBucket));
    if (!positions.contains(bucket)) {
        // A table without the policy cannot apply the intent, and saying nothing
        // would leave a control that looks like a decision and is not one.
        if (aNotes) {
            *aNotes << QCoreApplication::translate(
                           "ExportTargets",
                           "Quality intent not applied: the destination table declares no "
                           "<bucket_positions>, so '%1' was left at its own default.")
                           .arg(aCodec);
        }
        return false;
    }
    const double position = positions.value(bucket);
    const int value = (int)std::lround(control.best + (control.worst - control.best) * position);
    aEncOptions.insert(control.option, QString::number(value));
    if (aBucket == QualityBucket::Lossless && aNotes) {
        // The knob exists precisely because the codec is lossy (the table's own
        // rule: no knob means nothing to trade). Asking for lossless cannot be
        // honoured, so say so instead of quietly delivering the best lossy setting.
        *aNotes << QCoreApplication::translate(
                       "ExportTargets",
                       "'%1' is a lossy codec, so Lossless here means its highest quality, "
                       "not a lossless file.")
                       .arg(aCodec);
    }
    return true;
}


QString qualityKnobFor(const QString& aCodec) {
    // The family's entry when the codec has none of its own: the dialog must name
    // the same knob the plan applies through the encoder (ONE mapping,
    // qualityControlFor).
    return qualityControlFor(aCodec).option;
}

// ---------------------------------------------------------------------------

// A string from the data table, translated. The table is data, so its user-visible
// strings are extracted from src/ctrl/ExportTargetStrings.h (a data file is invisible
// to lupdate) and translated through the same context here.
static QString tableText(const QString& aText) {
    if (aText.isEmpty()) return aText;
    return QCoreApplication::translate("ExportTargets", aText.toUtf8().constData());
}

// A note may be wrapped over several lines in the data file for readability. The
// extractor sees it wrapped, so the key looked up at runtime has to be the same one:
// collapse every whitespace run to a single space, and both sides agree. The facts box
// wraps the text itself, so nothing about the display changes.
static QString oneLine(const QString& aText) {
    return aText.simplified();
}

QString ExportTargets::defaultPath() {
    return QStringLiteral(":/export/ExportTargets.txt");
}

QList<ExportGroup> ExportTargets::loadGroups(const QString& aPath) {
    QList<ExportGroup> out;

    QFile file(aPath);
    if (!file.open(QIODevice::ReadOnly)) return out;
    QDomDocument doc;
    if (!doc.setContent(&file)) return out;
    const QDomElement root = doc.firstChildElement(QStringLiteral("export_targets"));
    if (root.isNull()) return out;

    const QDomElement groups = root.firstChildElement(QStringLiteral("groups"));
    for (QDomElement g = groups.firstChildElement(QStringLiteral("group")); !g.isNull();
         g = g.nextSiblingElement(QStringLiteral("group"))) {
        ExportGroup group;
        group.id = g.attribute(QStringLiteral("id"));
        if (group.id.isEmpty()) continue;
        // A section whose title is missing still gets drawn: the KEY is worse than a
        // name but far better than a heading with no text, which reads as a bug.
        group.title = tableText(g.attribute(QStringLiteral("title")));
        if (group.title.isEmpty()) group.title = group.id;
        // Absent means "these rows are variants of one platform", which is what most
        // sections are; a section has to say so to be listed flat.
        group.variants = g.attribute(QStringLiteral("variants")) != QLatin1String("false");
        // Absent means "no rule after this block", which is every block but one. Read
        // the same way `variants` is: only the exact attribute the table documents
        // asks for the rule, so a typo leaves the list as it was rather than drawing
        // a boundary the table never asked for.
        group.separatorAfter =
            g.attribute(QStringLiteral("separatorAfter")) == QLatin1String("true");
        out.push_back(group);
    }
    return out;
}

QList<ExportTarget> ExportTargets::load(const QString& aPath, QString* aError) {
    QList<ExportTarget> out;

    QFile file(aPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (aError)
            *aError = QCoreApplication::translate("ExportTargets", "cannot open %1")
                          .arg(aPath);
        return out;
    }

    QDomDocument doc;
    const auto parsed = doc.setContent(&file);
    if (!parsed) {
        if (aError) {
            *aError = QStringLiteral("%1:%2:%3 %4")
                          .arg(aPath)
                          .arg((qlonglong)parsed.errorLine)
                          .arg((qlonglong)parsed.errorColumn)
                          .arg(parsed.errorMessage);
        }
        return out;
    }

    const QDomElement root = doc.firstChildElement(QStringLiteral("export_targets"));
    if (root.isNull()) {
        if (aError)
            *aError = QCoreApplication::translate("ExportTargets",
                                                          "%1: missing <export_targets> root")
                          .arg(aPath);
        return out;
    }

    for (QDomElement d = root.firstChildElement(QStringLiteral("destination")); !d.isNull();
         d = d.nextSiblingElement(QStringLiteral("destination"))) {
        ExportTarget t;
        t.id = d.attribute(QStringLiteral("id"));
        if (t.id.isEmpty()) continue;
        t.label = tableText(d.attribute(QStringLiteral("label")));

        t.icon = d.attribute(QStringLiteral("icon"));
        t.group = d.attribute(QStringLiteral("group"));
        // The row's rank inside its section, where the section's order is a design
        // decision rather than an alphabet (see ExportTarget::order). Absent = 0 =
        // unranked, and attrInt's default-on-failure applies: a typo'd rank makes
        // the row unranked rather than throwing it out of the table.
        t.order = attrInt(d, QStringLiteral("order"));
        t.family = d.attribute(QStringLiteral("family"));
        t.klass = parseClass(d.attribute(QStringLiteral("class")));
        // I44: accepted codecs as a LIST plus the declared default. The old
        // codecSelect fixed|ladder mode is gone: a row says WHAT the platform
        // takes, and the default either names one of them or is empty (the class
        // policy decides, constrained to the list).
        {
            const QString list = d.attribute(QStringLiteral("codecs"));
            for (const QString& key :
                 list.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts)) {
                t.codecs.append(key.trimmed());
            }
            t.defaultCodec = d.attribute(QStringLiteral("default"));
        }
        t.container = d.attribute(QStringLiteral("container"));
        {
            const QString kind = d.attribute(QStringLiteral("artifact"));
            if (kind == QLatin1String("still")) t.artifact = ArtifactKind::Still;
            else if (kind == QLatin1String("sequence")) t.artifact = ArtifactKind::Sequence;
        }
        t.codec = d.attribute(QStringLiteral("codec"));
        t.pixFmt = d.attribute(QStringLiteral("pixfmt"));
        t.fpsIgnored = d.attribute(QStringLiteral("fps")) == QLatin1String("ignored");
        t.alpha = parseCapability(d.attribute(QStringLiteral("alpha")));
        t.audio = parseCapability(d.attribute(QStringLiteral("audio")));
        t.loop = parseCapability(d.attribute(QStringLiteral("loop")));
        t.note = tableText(oneLine(d.attribute(QStringLiteral("note"))));
        t.checked = d.attribute(QStringLiteral("checked"));
        t.source = d.attribute(QStringLiteral("source"));
        t.verified = d.attribute(QStringLiteral("verified")) == QLatin1String("true");
        t.platform = d.attribute(QStringLiteral("platform"));
        if (t.label.isEmpty()) t.label = t.id;

        const QDomElement sizeElem = d.firstChildElement(QStringLiteral("size"));
        if (!sizeElem.isNull()) {
            t.size.mode = parseSizeMode(sizeElem.attribute(QStringLiteral("mode")));
            t.size.w = attrInt(sizeElem, QStringLiteral("w"));
            t.size.h = attrInt(sizeElem, QStringLiteral("h"));
            t.size.side = attrInt(sizeElem, QStringLiteral("side"));
            t.size.fit = parseFitMode(sizeElem.attribute(QStringLiteral("fit")));
        }
        const QString degrade = d.attribute(QStringLiteral("degrade"));
        t.degradeOrder = degrade.isEmpty()
                             ? QStringList{QStringLiteral("quality"), QStringLiteral("scale"),
                                           QStringLiteral("frames")}
                             : degrade.split(QLatin1Char(','), Qt::SkipEmptyParts);

        const QDomElement capsElem = d.firstChildElement(QStringLiteral("caps"));
        if (!capsElem.isNull()) {
            t.caps.maxBytes = attrLongLong(capsElem, QStringLiteral("maxBytes"));
            t.caps.maxDurationSec = attrDouble(capsElem, QStringLiteral("maxDurationSec"));
            t.caps.minDurationSec = attrDouble(capsElem, QStringLiteral("minDurationSec"));
            t.caps.maxFps = attrInt(capsElem, QStringLiteral("maxFps"));
            t.caps.minFrames = attrInt(capsElem, QStringLiteral("minFrames"));
            t.caps.maxFrames = attrInt(capsElem, QStringLiteral("maxFrames"));
        }

        const QDomElement encOptsElem = d.firstChildElement(QStringLiteral("encOptions"));
        for (QDomElement o = encOptsElem.firstChildElement(QStringLiteral("opt")); !o.isNull();
             o = o.nextSiblingElement(QStringLiteral("opt"))) {
            const QString key = o.attribute(QStringLiteral("name"));
            if (!key.isEmpty()) t.encOptions.insert(key, o.attribute(QStringLiteral("value")));
        }

        // Container requirements, same shape as encOptions but consumed by the muxer.
        const QDomElement muxOptsElem = d.firstChildElement(QStringLiteral("muxOptions"));
        for (QDomElement o = muxOptsElem.firstChildElement(QStringLiteral("opt")); !o.isNull();
             o = o.nextSiblingElement(QStringLiteral("opt"))) {
            const QString key = o.attribute(QStringLiteral("name"));
            if (!key.isEmpty()) t.muxerOptions.insert(key, o.attribute(QStringLiteral("value")));
        }

        const QDomElement audioElem = d.firstChildElement(QStringLiteral("audio"));
        if (!audioElem.isNull()) {
            t.audioSpec.codec = audioElem.attribute(QStringLiteral("codec"));
            t.audioSpec.bitrate = attrInt(audioElem, QStringLiteral("bitrate"));
            t.audioSpec.channels = attrInt(audioElem, QStringLiteral("channels"));
            t.audioSpec.sampleRate = attrInt(audioElem, QStringLiteral("rate"));
        }
        t.minScalePct = attrInt(d, QStringLiteral("minScalePct"), 25);

        const QDomElement tagElem = d.firstChildElement(QStringLiteral("codecTag"));
        if (!tagElem.isNull()) {
            t.codecTag = tagElem.attribute(QStringLiteral("value"));
            // The optional family qualifier; empty means the tag is unconditional.
            t.codecTagCodec = tagElem.attribute(QStringLiteral("codec"));
        }

        const QDomElement noteElem = d.firstChildElement(QStringLiteral("note"));
        if (!noteElem.isNull() && t.note.isEmpty()) t.note = tableText(oneLine(noteElem.text()));

        out.push_back(t);
    }

    return out;
}

QHash<QString, QualityControl> ExportTargets::loadQualityControl(const QString& aPath) {
    // Parsed once per path: the table is tiny and does not change at runtime.
    static QHash<QString, QHash<QString, QualityControl>> cache;
    const auto it = cache.constFind(aPath);
    if (it != cache.constEnd()) return *it;

    QHash<QString, QualityControl> out;
    QFile file(aPath);
    if (file.open(QIODevice::ReadOnly)) {
        QDomDocument doc;
        const auto parsed = doc.setContent(&file);
        if (parsed) {
            const QDomElement root = doc.firstChildElement(QStringLiteral("export_targets"));
            const QDomElement qc = root.firstChildElement(QStringLiteral("quality_control"));
            for (QDomElement c = qc.firstChildElement(QStringLiteral("codec")); !c.isNull();
                 c = c.nextSiblingElement(QStringLiteral("codec"))) {
                QualityControl q;
                q.codec = c.attribute(QStringLiteral("name"));
                q.option = c.attribute(QStringLiteral("option"));
                q.best = attrInt(c, QStringLiteral("best"));
                q.worst = attrInt(c, QStringLiteral("worst"));
                if (!q.codec.isEmpty() && !q.option.isEmpty()) out.insert(q.codec, q);
            }
        }
    }
    cache.insert(aPath, out);
    return out;
}

QHash<QString, double> ExportTargets::loadBucketPositions(const QString& aPath) {
    static QHash<QString, QHash<QString, double>> cache;
    const auto it = cache.constFind(aPath);
    if (it != cache.constEnd()) return *it;

    QHash<QString, double> out;
    QFile file(aPath);
    if (file.open(QIODevice::ReadOnly)) {
        QDomDocument doc;
        if (doc.setContent(&file)) {
            const QDomElement root = doc.firstChildElement(QStringLiteral("export_targets"));
            const QDomElement bp = root.firstChildElement(QStringLiteral("bucket_positions"));
            for (QDomElement b = bp.firstChildElement(QStringLiteral("bucket")); !b.isNull();
                 b = b.nextSiblingElement(QStringLiteral("bucket"))) {
                out.insert(b.attribute(QStringLiteral("name")),
                           (double)attrDouble(b, QStringLiteral("position")));
            }
        }
    }
    cache.insert(aPath, out);
    return out;
}

ExportTarget ExportTargets::find(const QList<ExportTarget>& aTargets, const QString& aId) {
    for (const ExportTarget& t : aTargets) {
        if (t.id == aId) return t;
    }
    return ExportTarget();
}

// ---------------------------------------------------------------------------
// FrameSelection

FrameSelection FrameSelection::fromRange(int aFirst, int aLast) {
    FrameSelection s;
    if (aLast >= aFirst) s.ranges.append(FrameRange{aFirst, aLast});
    return s;
}

FrameSelection FrameSelection::fromCount(int aCount) {
    return aCount > 0 ? fromRange(0, aCount - 1) : FrameSelection();
}

int FrameSelection::total() const {
    int n = 0;
    for (const FrameRange& r : ranges) n += r.count();
    return n;
}

QList<int> FrameSelection::indices() const {
    QList<int> out;
    out.reserve(total());
    for (const FrameRange& r : ranges) {
        for (int f = r.first; f <= r.last; ++f) out.append(f);
    }
    return out;
}

FrameSelection FrameSelection::firstN(int aCount) const {
    FrameSelection out;
    int remaining = aCount;
    for (const FrameRange& r : ranges) {
        if (remaining <= 0) break;
        const int take = std::min(remaining, r.count());
        if (take <= 0) continue;
        out.ranges.append(FrameRange{r.first, r.first + take - 1});
        remaining -= take;
    }
    return out;
}

int FrameSelection::rangeCount() const {
    int valid = 0;
    for (const FrameRange& r : ranges) {
        if (r.count() > 0) ++valid;
    }
    return valid;
}

QString FrameSelection::describe() const {
    const int n = total();
    if (n == 0)
        return QCoreApplication::translate("ExportTargets", "no frames selected");
    // A backwards range contributes nothing, but is not pretended away: it just
    // does not add to the range count either.
    const int valid = rangeCount();
    if (valid <= 1) {
        // One number, two whole phrases: "frame%2" appended an English "s" to a count,
        // which no translator can move and which is wrong in any language that does not
        // pluralise like English.
        return n == 1 ? QCoreApplication::translate("ExportTargets", "%1 frame").arg(n)
                      : QCoreApplication::translate("ExportTargets", "%1 frames").arg(n);
    }
    return QCoreApplication::translate("ExportTargets",
                                       "%1 frames from %2 separate ranges")
        .arg(n)
        .arg(valid);
}

// ---------------------------------------------------------------------------

QString platformName(const QString& aPlatform) {
    const QString p = aPlatform.trimmed().toLower();
    if (p == QLatin1String("apple")) return QStringLiteral("macOS");
    if (p == QLatin1String("windows")) return QStringLiteral("Windows");
    if (p == QLatin1String("linux")) return QStringLiteral("Linux");
    return aPlatform; // unknown token: show it rather than invent a name
}

QSize applySizeRule(const SizeRule& aRule, const QSize& aSource, QStringList* aNotes) {
    if (!aSource.isValid() || aSource.isEmpty()) return aSource;

    const double sw = aSource.width();
    const double sh = aSource.height();

    switch (aRule.mode) {
    case SizeMode::Free:
        return QSize(evenFloor(sw), evenFloor(sh));

    case SizeMode::Exact:
        if (aNotes) {
            // Crop-or-pad is resolved (FitMode), so the note states which one
            // applies rather than calling it an open question.
            *aNotes << QCoreApplication::translate(
                           "ExportTargets",
                           "Size forced to %1x%2 (required by this destination); the art is %3.")
                           .arg(aRule.w)
                           .arg(aRule.h)
                           .arg(aRule.fit == FitMode::Crop
                                    ? QCoreApplication::translate("ExportTargets", "cropped to fill it")
                                    : QCoreApplication::translate("ExportTargets", "padded to fit it"));
        }
        return QSize(evenFloor(aRule.w), evenFloor(aRule.h));

    case SizeMode::Max: {
        // Capped at 1.0: a MAXIMUM does not enlarge. Without this, exporting a
        // 640x480 animation to TikTok produced 1080x810 — pure interpolation, paid
        // for in bytes and encode time, and every one of the fifteen max-mode rows
        // did it. `exact` and `maxExactSide` are the modes that mean "make it this
        // big" (Telegram requires one side to BE 512), and they still do.
        const double scale = std::min(1.0, std::min(aRule.w / sw, aRule.h / sh));
        return QSize(evenFloor(sw * scale), evenFloor(sh * scale));
    }

    case SizeMode::MaxExactSide: {
        const double scale = aRule.side / std::max(sw, sh);
        return QSize(evenFloor(sw * scale), evenFloor(sh * scale));
    }

    case SizeMode::MaxMinSide: {
        // The first stage is a maximum (no enlargement); the second is a FLOOR
        // ("one side must be at least @side"), which is the one place where
        // growing the art is the requirement rather than a side effect.
        double scale = std::min(1.0, std::min(aRule.w / sw, aRule.h / sh));
        QSize out(evenFloor(sw * scale), evenFloor(sh * scale));
        if (std::max(out.width(), out.height()) < aRule.side) {
            scale = aRule.side / (double)std::max(out.width(), out.height());
            out = QSize(evenFloor(out.width() * scale), evenFloor(out.height() * scale));
        }
        return out;
    }
    }
    return aSource;
}

void applyCaps(const Caps& aCaps, int* aFps, int* aFrames, QStringList* aNotes) {
    if (!aFps || !aFrames) return;

    if (aCaps.maxFps > 0 && *aFps > aCaps.maxFps) {
        if (aNotes)
            *aNotes << QCoreApplication::translate(
                           "ExportTargets", "Frame rate reduced %1 -> %2 fps (destination limit).")
                           .arg(*aFps)
                           .arg(aCaps.maxFps);
        *aFps = aCaps.maxFps;
    }

    if (aCaps.maxDurationSec > 0.0 && *aFps > 0) {
        const double duration = (double)*aFrames / (double)*aFps;
        if (duration > aCaps.maxDurationSec) {
            const int allowed = (int)std::floor(aCaps.maxDurationSec * *aFps);
            if (aNotes) {
                *aNotes << QCoreApplication::translate(
                               "ExportTargets", "Trimmed %1 -> %2 frames (%3s limit).")
                               .arg(*aFrames)
                               .arg(allowed)
                               .arg(aCaps.maxDurationSec);
            }
            *aFrames = allowed;
        }
    }

    if (aCaps.maxFrames > 0 && *aFrames > aCaps.maxFrames) {
        if (aNotes)
            *aNotes << QCoreApplication::translate(
                           "ExportTargets", "Trimmed %1 -> %2 frames (destination limit).")
                           .arg(*aFrames)
                           .arg(aCaps.maxFrames);
        *aFrames = aCaps.maxFrames;
    }

    if (aCaps.minFrames > 0 && *aFrames < aCaps.minFrames) {
        if (aNotes) {
            *aNotes << QCoreApplication::translate(
                               "ExportTargets",
                               "WARNING: destination needs at least %1 frames but only %2 are "
                               "available, so the upload will likely be rejected.")
                           .arg(aCaps.minFrames)
                           .arg(*aFrames);
        }
    }

    // A MINIMUM duration is the mirror of maxDurationSec and cannot be met by
    // exporting more of what the user selected: only they can extend the range.
    // Snapchat Spotlight is the case that needs it (a Spot must be 5 seconds or
    // longer), and without this the dialog said nothing at all about a file the
    // platform will refuse.
    if (aCaps.minDurationSec > 0.0 && *aFps > 0) {
        const double duration = (double)*aFrames / (double)*aFps;
        if (duration < aCaps.minDurationSec) {
            if (aNotes) {
                *aNotes << QCoreApplication::translate(
                               "ExportTargets",
                               "WARNING: destination needs at least %1 s but this export is %2 s, "
                               "so the upload will likely be rejected. Select a longer frame range.")
                               .arg(aCaps.minDurationSec)
                               .arg(duration, 0, 'f', 2);
            }
        }
    }
}

QString codecDisplayName(const QString& aCodec) {
    if (aCodec.isEmpty()) return QString();
    if (aCodec == QLatin1String("libvpx-vp9") || aCodec == QLatin1String("vp9_vaapi")) {
        return QStringLiteral("VP9");
    }
    if (aCodec.contains(QLatin1String("av1"))) return QStringLiteral("AV1");
    if (aCodec.contains(QLatin1String("265")) || aCodec.contains(QLatin1String("hevc"))) {
        return QStringLiteral("H.265");
    }
    if (aCodec.contains(QLatin1String("264"))) return QStringLiteral("H.264");
    if (aCodec == QLatin1String("ffv1")) return QStringLiteral("FFV1");
    if (aCodec.contains(QLatin1String("webp"))) return QStringLiteral("WebP");
    if (aCodec == QLatin1String("gif")) return QStringLiteral("GIF");
    if (aCodec.contains(QLatin1String("prores"))) return QStringLiteral("ProRes");
    return aCodec;
}

QString containerDisplayName(const QString& aContainer) {
    if (aContainer == QLatin1String("mp4") || aContainer == QLatin1String("m4v")) {
        return QStringLiteral("MP4");
    }
    if (aContainer == QLatin1String("webm")) return QStringLiteral("WebM");
    if (aContainer == QLatin1String("matroska") || aContainer == QLatin1String("mkv")) {
        return QStringLiteral("MKV");
    }
    return aContainer.toUpper();
}

QString resolveLadderCodec(DestClass aClass, QualityBucket aBucket, QString* aWhy) {
    const Ladder ladder = ladderFor(aClass, aBucket);
    // Nothing here is timed. The old gate measured each candidate by encoding eight
    // black frames and rejected whatever projected past a fixed 900 s (or 3600 s),
    // which is a number that means nothing at 480p and nothing at 4K, and which turned
    // into "the fastest available, whatever it is" exactly on the long exports where
    // the choice matters most. The choice is a POLICY now: the format comes from the
    // destination's class and the quality bucket, and inside a format the faster kind
    // of encoder is preferred unless the bucket is about fidelity.
    for (const QString& family : ladder.families) {
        // Which of this family's encoders actually run here, in the order this bucket
        // wants them: the declared order is software first (better rate control, which
        // is what Best and Lossless are paying for), and the time-sensitive buckets
        // take a hardware encoder first when one runs. Two groups, each in DECLARED
        // order — `prepend` reversed the hardware subsequence, so the last-declared
        // one won and an Intel+NVIDIA laptop took QSV over NVENC.
        QStringList hardware, software;
        for (const QString& name : familyEncoders(family)) {
            if (!media::ffmpeg::encoderUsable(name.toUtf8().constData())) continue;
            if (isHardwareEncoder(name))
                hardware.append(name);
            else
                software.append(name);
        }
        const QStringList order = ladder.fastFirst ? (hardware + software)
                                                   : (software + hardware);
        if (order.isEmpty()) continue;
        if (aWhy) *aWhy = classReason(aClass, familyLabel(family));
        return order.first();
    }
    if (aWhy)
        *aWhy = QCoreApplication::translate("ExportTargets",
                                               "no suitable encoder found in this build");
    return QString();
}

// I44: accepted ∩ build-capable — the intersection the contract names. A row
// offers a format only when THIS BUILD can produce it into THIS container: an
// encoder that runs here (encoderUsable, not "compiled in" — the ladder's own
// measured trap) and that the muxer takes. The row's pin is the measured,
// working implementation for its format where it has one (safari's
// hevc_videotoolbox: libx265 advertises yuva420p and then refuses it at open).
QList<CodecChoice> offeredCodecs(const ExportTarget& aTarget) {
    QList<CodecChoice> out;
    if (aTarget.artifact != ArtifactKind::Video) return out;

    // GIF is the one artifact media::GifEncoder owns rather than libav: present
    // whenever the app is (writableContainers' own rule) — no encoder to probe.
    if (aTarget.container.compare(QLatin1String("gif"), Qt::CaseInsensitive) == 0) {
        for (const QString& format : aTarget.codecs) {
            if (format != QLatin1String("gif")) continue;
            CodecChoice c;
            c.format = format;
            c.encoder = QStringLiteral("gif");
            c.label = formatLabel(format);
            out.append(c);
        }
        return out;
    }

    for (const QString& format : aTarget.codecs) {
        QStringList candidates;
        if (!aTarget.codec.isEmpty() && formatKeyForEncoder(aTarget.codec) == format)
            candidates.append(aTarget.codec);
        for (const QString& name : formatEncoders(format)) {
            if (!candidates.contains(name)) candidates.append(name);
        }
        for (const QString& encoder : candidates) {
            if (!media::ffmpeg::containerAcceptsUsableCodec(aTarget.container, encoder))
                continue;
            CodecChoice c;
            c.format = format;
            c.encoder = encoder;
            c.label = formatLabel(format);
            c.lossless = (format == QLatin1String("ffv1"));
            out.append(c);
            break;
        }
    }
    return out;
}

// I44: the default codec for a row. Its DECLARED default where it has one (the
// row's own decision needs no advocacy); otherwise the class policy — the
// ladder — constrained to the row's accepted list. The constraint is the schema
// change's whole point: the policy picks among what the destination takes and
// never beyond it (the old ladder would hand YouTube an FFV1 upload at Lossless
// because the policy knew the class but not the row).
CodecChoice resolveDefaultCodec(const ExportTarget& aTarget, QualityBucket aBucket,
                                bool aAlphaOn, QString* aWhy) {
    const QList<CodecChoice> offered = offeredCodecs(aTarget);
    if (offered.isEmpty()) {
        if (aWhy)
            *aWhy = QCoreApplication::translate("ExportTargets",
                                                "no suitable encoder found in this build");
        return CodecChoice();
    }

    // 1. the declared default. A default this build cannot produce falls
    //    through to the policy (still constrained to `codecs`): better the
    //    row's next accepted codec than a dead export.
    if (!aTarget.defaultCodec.isEmpty()) {
        for (const CodecChoice& c : offered) {
            if (c.format == aTarget.defaultCodec) return c;
        }
    }

    // 2. the class policy, list-constrained. Inside a family the ladder's own
    //    order rule applies unchanged (hardware first for the time-sensitive
    //    buckets, declared order for the fidelity ones).
    const Ladder ladder = ladderFor(aTarget.klass, aBucket);
    QStringList families = ladder.families;
    if (aTarget.klass == DestClass::Archive && aBucket != QualityBucket::Lossless) {
        // The archive rule (decided 2026-10-01; alpha capability MEASURED in
        // this build — libsvtav1 accepts yuva420p but stores no alpha at all,
        // libx265 refuses it outright, libvpx-vp9 and ffv1 carry it):
        //   transparency ON  -> VP9, the alpha-capable lossy pick (a MEASURED
        //       capability limit, not a preference: AV1 drops alpha silently
        //       here and H.265 refuses it);
        //   transparency OFF -> AV1 at EVERY non-Lossless bucket (Best,
        //       Balanced and Smaller file alike). Ruled 2026-10-01: "AV1 all
        //       the way makes more sense" — the ladder's family order already
        //       leads with AV1, and the transparency flag no longer reorders
        //       this row's families. H.265 is only the fallback for a build
        //       with no usable AV1 encoder. Lossless stays the ladder's own
        //       FFV1, always.
        //
        // The ladder's fastFirst split does NOT order this row's families any
        // more; it still governs hardware-encoder preference WITHIN a family
        // (the per-family ordering below puts the family's hardware encoders
        // ahead of its software ones when fastFirst is set), exactly as it does
        // for every other class.
        if (aAlphaOn) {
            families = QStringList{QStringLiteral("vp9")};
        }
    }
    for (const QString& family : families) {
        const QString format =
            (family == QLatin1String("lossless")) ? QStringLiteral("ffv1") : family;
        const CodecChoice* here = nullptr;
        for (const CodecChoice& c : offered) {
            if (c.format == format) {
                here = &c;
                break;
            }
        }
        if (!here) continue; // the row does not accept this family — policy yields
        // The family's own encoders in DECLARED order, hardware ahead of software
        // when this bucket asks for speed. NOT by prepending each hardware encoder
        // as it is met, which reversed the hardware subsequence: the LAST-declared
        // usable hardware encoder won (amf > vaapi > qsv > nvenc), so an
        // Intel+NVIDIA laptop took QSV over NVENC — the opposite of the order
        // familyEncoders() declares. Two groups, each in declared order, so the
        // first declared hardware encoder of the family is the one that wins.
        QStringList hardware, software;
        for (const QString& name : familyEncoders(family)) {
            if (!media::ffmpeg::encoderUsable(name.toUtf8().constData())) continue;
            if (isHardwareEncoder(name))
                hardware.append(name);
            else
                software.append(name);
        }
        const QStringList order = ladder.fastFirst ? (hardware + software)
                                                   : (software + hardware);
        if (order.isEmpty()) continue;
        CodecChoice c = *here;
        c.encoder = order.first();
        if (aWhy) *aWhy = classReason(aTarget.klass, formatLabel(format));
        return c;
    }

    // 3. the policy has no preference the row accepts: the row's own order.
    return offered.first();
}

// ---------------------------------------------------------------------------

ExportPlan planExport(const ExportTarget& aTarget, const ExportRequest& aRequest,
                      QualityBucket aBucket) {
    ExportPlan plan;
    plan.target = aTarget;

    if (aTarget.id.isEmpty()) {
        plan.error = QCoreApplication::translate("ExportTargets", "no destination selected");
        return plan;
    }
    if (!aRequest.sourceSize.isValid() || aRequest.sourceSize.isEmpty()) {
        plan.error = QCoreApplication::translate("ExportTargets", "invalid source size");
        return plan;
    }

    // --- capabilities: declared per row, only ever a control when Choice -----
    //
    // ALPHA. Required and Forbidden are the DESTINATION's decision and the plan
    // honours them as they stand. The other three classes are the user's control
    // and differ only in what the checkbox STARTS at — DefaultOn pre-checks it,
    // DefaultOff leaves it clear — so what they resolve to is the answer the
    // caller carries, ExportRequest::allowTransparency. Hardcoding the class's
    // answer here was why the dialog's box lied: the user cleared it on a
    // default-on row and the export still wrote alpha (H42's core half). The
    // struct default stays true, so a bare request still answers "on".
    switch (aTarget.alpha) {
    case Capability::Required:
        plan.alphaEnabled = true;
        plan.notes << QCoreApplication::translate("ExportTargets",
                                                  "%1 is required by this destination.")
                          .arg(QCoreApplication::translate("ExportTargets", "Transparency"));
        break;
    case Capability::Forbidden:
        plan.alphaEnabled = false;
        break;
    default:
        plan.alphaEnabled = aRequest.allowTransparency;
        break;
    }
    // AUDIO is DERIVED, not requested (project has it, the user did not mute, the
    // destination permits it — see the includeAudio computation further down), so
    // the capability's only job at this point is the "forbidden" answer; the
    // non-forbidden classes resolve to their default and every path below
    // overwrites this with the derived decision.
    if (aTarget.audio == Capability::Required) {
        plan.notes << QCoreApplication::translate("ExportTargets",
                                                  "%1 is required by this destination.")
                          .arg(QCoreApplication::translate("ExportTargets", "Audio"));
    }
    plan.audioEnabled = (aTarget.audio == Capability::Required
                         || aTarget.audio == Capability::DefaultOn);

    // --- size, rate and frames: the deterministic, pre-encode half -----------
    //
    // Applied EXACTLY ONCE, and before the codec ladder, because the ladder
    // needs the real frame count to project its time budget. Keeping it in one
    // place, rather than also inside each codec branch, is what keeps the cap
    // notes from being emitted twice.
    if (aRequest.selection.isEmpty()) {
        plan.error = QCoreApplication::translate("ExportTargets", "no frames selected for export");
        return plan;
    }

    // A Free rule means the destination imposes no size, which is NOT the same
    // as deciding one: it leaves the user's own resolution in force (a 1920x1080
    // upload from a 1100x790 project is an ordinary thing to want). Only when the
    // caller named no size does the art's own size apply. This is handled here
    // rather than inside applySizeRule because it is request policy, not part of
    // what the destination's rule says.
    //
    // A MAX rule is a CEILING, not a decision, and the user's own resolution is the
    // other ceiling: the art fits inside the smaller of the two. Treating the
    // destination's box as the answer made every max-mode row (Discord, WhatsApp,
    // Signal, Instagram, TikTok, YouTube) a destination whose size the user could
    // not touch, so "export this at 640x360 for Discord" was not expressible on a
    // destination whose entire rule is "no bigger than 1280x720". The rules that
    // really do pin a size (Exact, MaxExactSide, MaxMinSide) still do.
    const bool hasRequested = aRequest.requestedSize.isValid()
                              && !aRequest.requestedSize.isEmpty();
    if (aTarget.size.mode == SizeMode::Free && hasRequested) {
        plan.size = QSize(evenFloor(aRequest.requestedSize.width()),
                          evenFloor(aRequest.requestedSize.height()));
    } else if (aTarget.size.mode == SizeMode::Max && hasRequested) {
        SizeRule bounded = aTarget.size;
        if (bounded.w > 0) bounded.w = std::min(bounded.w, aRequest.requestedSize.width());
        if (bounded.h > 0) bounded.h = std::min(bounded.h, aRequest.requestedSize.height());
        plan.size = applySizeRule(bounded, aRequest.sourceSize, &plan.notes);
    } else {
        plan.size = applySizeRule(aTarget.size, aRequest.sourceSize, &plan.notes);
    }
    plan.fps = aRequest.fps;
    int allowedFrames = aRequest.selection.total();
    applyCaps(aTarget.caps, &plan.fps, &allowedFrames, &plan.notes);
    plan.selection = aRequest.selection.firstN(allowedFrames);
    // applyCaps has already reported any trim by COUNT. Add the selection detail
    // only when the selection has gaps, because "trimmed to 90 frames" does not
    // say which frames survived, and packing the ranges back-to-back is the
    // non-obvious consequence of choosing several ranges.
    if (plan.selection.rangeCount() > 1) {
        plan.notes << QCoreApplication::translate(
                          "ExportTargets",
                          "%1; the ranges play back to back and the frames between them are not "
                          "exported.")
                          .arg(plan.selection.describe());
    }

    // --- codec (I44: accepted list + declared default) -----------------------
    // The user's choice from the codec row wins WHERE THE DESTINATION ACCEPTS
    // IT — validated against the offered list (accepted ∩ this build can
    // produce), so a stale pick cannot smuggle in a codec the destination
    // refuses. Empty, or refused: the row's DECLARED default, else the class
    // policy constrained to the accepted list. Image artifacts have no codec at
    // all (their branch below writes through Qt's own image writers).
    QString codec;
    if (aTarget.artifact == ArtifactKind::Video) {
        const QList<CodecChoice> offered = offeredCodecs(aTarget);
        if (!aRequest.codec.isEmpty()) {
            for (const CodecChoice& c : offered) {
                if (c.encoder == aRequest.codec
                    || formatKeyForEncoder(aRequest.codec) == c.format) {
                    // The format is accepted. Keep the EXACT encoder asked for
                    // when it runs here and the muxer takes it: the request also
                    // carries the plan's own passthrough (possibly a hardware
                    // encoder of the offered format), and silently swapping it
                    // for the offered software entry would downgrade it.
                    codec = media::ffmpeg::containerAcceptsUsableCodec(aTarget.container,
                                                                       aRequest.codec)
                                ? aRequest.codec
                                : c.encoder;
                    break;
                }
            }
        }
        if (codec.isEmpty()) {
            QString why;
            // The archive codec rule follows the user's transparency toggle (the row's
            // Capability can force or veto it). Read plan.alphaEnabled, resolved by
            // the alpha capability above, and do NOT recompute it from the target and
            // the request here: the ladder's VP9/AV1 branch and the capability's
            // answer are one question, and two implementations of it is how the plan
            // could pick a codec that contradicts the alpha state it reports.
            const CodecChoice def = resolveDefaultCodec(aTarget, aBucket, plan.alphaEnabled, &why);
            if (def.encoder.isEmpty()) {
                plan.error = QCoreApplication::translate(
                    "ExportTargets", "no encoder available for this destination in this build");
                return plan;
            }
            codec = def.encoder;
            if (!why.isEmpty()) plan.notes << why;
        }
    }

    // Exact-size destinations (Discord 320x320, WhatsApp 512x512) need the
    // FILE to be that size while the art keeps its aspect. So render at the
    // fitted size and have the encoder pad the remainder with transparency —
    // never stretch, never crop.
    plan.renderSize = plan.size;
    if (aTarget.size.mode == SizeMode::Exact) {
        const double sw = aRequest.sourceSize.width();
        const double sh = aRequest.sourceSize.height();
        const double scale = std::min((double)aTarget.size.w / sw, (double)aTarget.size.h / sh);
        plan.renderSize = QSize(evenFloor(sw * scale), evenFloor(sh * scale));
    }

    // How the art reaches the destination's size. Resolved HERE because it is not
    // a video-only question: a GIF destination with an exact size needs the same
    // pad-or-crop, and the GIF path never builds a video config to carry it. The
    // video path re-applies it after makeConfig(), which replaces the whole config
    // and would otherwise wipe it (that is exactly how this broke once already).
    const media::FitMode fitMode = (aTarget.size.mode == SizeMode::Exact)
                                       ? (aTarget.size.fit == FitMode::Crop
                                              ? media::FitMode::Crop
                                              : media::FitMode::Pad)
                                       : media::FitMode::None;
    plan.config.fit = fitMode;

    if (!aTarget.note.isEmpty()) plan.notes << aTarget.note;

    if (!aTarget.verified) {
        plan.notes << QCoreApplication::translate(
                          "ExportTargets",
                          "WARNING: this destination's limits are UNVERIFIED; re-check before "
                          "relying on them.");
    }

    // --- image artifacts ----------------------------------------------------
    // A still or a frame sequence is written by Qt's own image writers, not by an
    // encoder, so there is no codec to resolve and no container to mux. What the
    // plan DOES have to settle is how many frames leave: one, or the set.
    //
    // Caps still apply and still mean what they mean. maxFrames trims a sequence
    // (pixiv ugoira accepts at most 150 stills); maxBytes applies to the whole set
    // for a sequence and to the one file for a still.
    if (aTarget.artifact != ArtifactKind::Video) {
        plan.artifact = aTarget.artifact;
        plan.imageSuffix = aTarget.container.toLower();
        // The fit loop writes the artifact through the same path a plain export
        // uses, so the plan has to carry where it goes.
        plan.config.outputPath = aRequest.outputPath;
        plan.config.fit = fitMode;
        if (plan.imageSuffix.isEmpty()) plan.imageSuffix = QStringLiteral("png");
        // An image cannot carry a soundtrack, whatever the row says.
        plan.audioEnabled = false;
        if (aTarget.artifact == ArtifactKind::Still) {
            // One file means one frame, and it is the frame on screen.
            plan.selection = FrameSelection::fromRange(aRequest.playhead, aRequest.playhead);
            plan.notes << QCoreApplication::translate(
                              "ExportTargets",
                              "One still image (frame %1), written as %2. The frame range above "
                              "does not apply.")
                              .arg(aRequest.playhead)
                              .arg(plan.imageSuffix.toUpper());
        } else {
            plan.notes << QCoreApplication::translate(
                              "ExportTargets", "%1 frames written as numbered .%2 files.")
                              .arg(plan.frames())
                              .arg(plan.imageSuffix);
        }
        // The row's note and the UNVERIFIED warning were added above, before this
        // branch: adding them here a second time printed the note twice, which is
        // also how the frame-range controls got pushed out of the dialog.
        return plan;
    }

    // --- build the config ---------------------------------------------------
    // GIF does not go through VideoEncoder (media::GifEncoder owns it). The plan
    // still carries what that encoder needs, because the fit loop re-encodes
    // through the same path as a plain export: without the output path there, a
    // capped GIF destination has no way to be fitted at all.
    if (aTarget.container.compare(QLatin1String("gif"), Qt::CaseInsensitive) == 0) {
        plan.gifPath = true;
        plan.config.fit = fitMode;
        plan.config.outputPath = aRequest.outputPath;
        plan.config.fps = plan.fps;
        // Named so the fit loop finds this codec's quality knob in the table. GIF is
        // not a VideoEncoder codec, but "what trades quality for bytes" is a
        // per-codec fact and the table is where those live.
        plan.config.codec = QStringLiteral("gif");
        return plan;
    }

    QString err;
    plan.config = media::VideoEncoder::makeConfig(aTarget.container, codec, aTarget.pixFmt,
                                                  plan.alphaEnabled, plan.size.width(),
                                                  plan.size.height(), plan.fps, 0, &err);
    if (plan.config.codec.isEmpty()) {
        plan.error = QStringLiteral("%1: %2").arg(aTarget.label, err);
        return plan;
    }

    // The resolved pixel format must be genuinely ENCODABLE, not merely
    // advertised. Codecs list formats they then refuse: libx265 advertises
    // yuva420p and avcodec_open2 fails with ENOSYS, so makeConfig's static
    // compatibility check is not sufficient. Verify by probe, before the config
    // ever reaches open().
    if (!media::ffmpeg::encoderSupportsPixFormatUsable(plan.config.codec, plan.config.pixFmt)) {
        const bool alphaRequired = (aTarget.alpha == Capability::Required);
        QString err2;
        const media::VideoEncoder::Config fallback = media::VideoEncoder::makeConfig(
            aTarget.container, plan.config.codec, QString(), false, plan.size.width(),
            plan.size.height(), plan.fps, 0, &err2);
        const bool fallbackOk = !fallback.codec.isEmpty() &&
                                media::ffmpeg::encoderSupportsPixFormatUsable(fallback.codec,
                                                                              fallback.pixFmt);

        if (alphaRequired) {
            // Dropping alpha here would hand the user a file the platform
            // rejects, with no symptom until upload. Refuse and name the
            // alternative instead.
            plan.error = QCoreApplication::translate(
                             "ExportTargets",
                             "%1 requires transparency, but '%2' cannot encode an alpha channel "
                             "in this build (it advertises %3 and then refuses it). Use a "
                             "destination whose codec carries alpha: WebM/VP9, APNG, or FFV1.")
                             .arg(aTarget.label, plan.config.codec, plan.config.pixFmt);
            return plan;
        }
        if (!fallbackOk) {
            plan.error = QCoreApplication::translate(
                         "ExportTargets",
                         "%1: %2 could not encode %3 and no fallback worked (%4)")
                             .arg(aTarget.id, plan.config.codec, plan.config.pixFmt, err2);
            return plan;
        }
        plan.alphaEnabled = false;
        plan.notes << QCoreApplication::translate(
                          "ExportTargets",
                          "Transparency dropped: '%1' advertises %2 but cannot encode it here, "
                          "so %3 is used instead.")
                          .arg(plan.config.codec, plan.config.pixFmt,
                               fallback.pixFmt.isEmpty()
                                   ? QCoreApplication::translate("ExportTargets",
                                                                 "the encoder default")
                                                         : fallback.pixFmt);
        plan.config = fallback;
    }

    plan.config.outputPath = aRequest.outputPath;
    plan.config.fit = fitMode; // makeConfig() replaced the config; see above
    plan.config.encOptions = aTarget.encOptions;
    plan.config.muxerOptions = aTarget.muxerOptions;
    // The stream codec tag, gated by the optional family qualifier on the
    // element (ruling 2026-10-03: tag hvc1 whenever HEVC lands in an mp4/mov,
    // and nothing else changes).
    //
    // The witness is plan.config.codec — the encoder that will ACTUALLY be
    // encoded, after resolveEncoderName()'s same-family substitution and the
    // pixfmt fallback just above (which replaces the whole config, so reading a
    // local captured earlier would answer for a codec we are no longer using).
    // It is non-empty here: the empty case returned at the makeConfig() error
    // above. formatKeyForEncoder() is the same canonicalizer the codec
    // resolution itself uses (the `c.format` comparison above), so the
    // destination's qualifier and this witness speak one vocabulary.
    plan.config.codecTag = (aTarget.codecTagCodec.isEmpty() ||
                            aTarget.codecTagCodec == formatKeyForEncoder(plan.config.codec))
                               ? aTarget.codecTag
                               : QString();

    // The user's quality intent, applied through the codec's OWN knob.
    //
    // Without this the Quality selector was inert for every destination whose
    // codec is fixed — ten of the fourteen rows — because the bucket only ever
    // chose the codec, and the codec here is dictated by the platform. "Smaller
    // file" and "Best quality" then produced identical instructions.
    //
    // The knob is the private encoder option the table names ('crf' for the
    // VP9/AV1/x264/x265 families, 'quality' for WebP), NOT
    // media::VideoEncoder::Config::quality: that shorthands a 0-100 value onto
    // crf for H.264/H.265 only and is ignored for everything else, so it could
    // never have covered WebP or ProRes.
    applyQualityIntent(plan.config.codec, aBucket, plan.config.encOptions, &plan.notes);
    // Enforce the audio capability here, not by convention. Telegram rejects a
    // video sticker that carries an audio stream, so a forbidden capability has
    // to actively strip the audio config rather than rely on the caller
    // remembering not to call setAudio().
    // Audio presence is DERIVED, not requested: project has it, user didn't mute,
    // destination permits it. Because the only user intent is `mute` (a
    // restriction), it can never conflict with a destination that forbids audio
    // — so there is nothing to enforce and nothing to silently deny.
    const QString acodec = aTarget.audioSpec.codec.isEmpty() ? audioCodecForContainer(aTarget.container)
                                                         : aTarget.audioSpec.codec;
    // Per-destination audio parameters, with defaults where unspecified.
    const int channels = aTarget.audioSpec.channels > 0 ? aTarget.audioSpec.channels : aRequest.audioChannels;
    const int rate = aTarget.audioSpec.sampleRate > 0 ? aTarget.audioSpec.sampleRate : aRequest.audioSampleRate;
    const int bitrate = aTarget.audioSpec.bitrate > 0 ? aTarget.audioSpec.bitrate : 128000;
    // Audio encoders are software, so plain availability is enough here (the
    // "listed but unusable" problem is specific to hardware encoders).
    const bool codecUsable =
        !acodec.isEmpty() && media::ffmpeg::encoderAvailable(acodec.toUtf8().constData());
    const bool projectHasExportableAudio = aRequest.exportableAudioTracks > 0;
    const bool includeAudio = (aTarget.audio != Capability::Forbidden) &&
                              projectHasExportableAudio && !aRequest.mute && codecUsable;
    plan.audioEnabled = includeAudio;

    if (includeAudio) {
        plan.config.audioCodec = acodec;
        plan.config.audioChannels = channels;
        plan.config.audioSampleRate = rate;
        plan.config.audioBitrate = bitrate;
    } else {
        plan.config.audioCodec.clear();
        plan.config.audioChannels = 0;
        plan.config.audioSampleRate = 0;
        // State the reason only when there WAS audio to leave out — otherwise
        // this is noise on every silent project.
        if (projectHasExportableAudio && !aRequest.mute &&
            aTarget.audio == Capability::Forbidden) {
            plan.notes << QCoreApplication::translate(
                              "ExportTargets", "No sound: %1 does not accept an audio track.")
                              .arg(aTarget.label);
        }
        if (aTarget.audio == Capability::Required && !projectHasExportableAudio) {
            plan.notes << QCoreApplication::translate(
                              "ExportTargets",
                              "WARNING: %1 expects sound but this project has no audio track.")
                              .arg(aTarget.label);
        }
    }
    return plan;
}

namespace {

// The frame rate the frames that SURVIVE a stride are written at.
//
// The stride is how the fit's last phase buys bytes: keep every stride-th frame.
// Writing the survivors at the PLAN's rate does not make the animation lighter,
// it makes it SHORTER — a 3-second Telegram sticker capped at stride 2 wrote 45
// of its 90 frames at 30 fps and played 1.5 seconds. The running time the plan
// described is frames/fps, and the only lever on frames/(fps/stride) is the
// RATE, so the survivors are written at fps/stride and the duration stays the
// plan's to within the rounding of that rate.
//
// Both encoders take an INTEGER fps and refuse anything under 1
// (VideoEncoder: time_base {1, fps} and a hard "must be > 0" check;
// GifEncoder: the frame delay is 100/fps centiseconds), so:
//  - stride 1 — the un-degraded case — returns the plan's rate UNCHANGED, not
//    a rounded copy of it: no existing output may move;
//  - a plan rate that is not positive stays what it was (the GIF branch has its
//    own default of 15 for a row that carries no rate);
//  - otherwise the rate is fps/stride, rounded to the nearest integer: at most
//    half a frame of duration error;
//  - a rate that rounds BELOW the format floor of 1 is clamped to 1 and
//    *aBelowFloor says so — the duration is then NOT preserved, which the caller
//    must state rather than let a shorter animation pass silently.
int effectiveFpsForStride(int aPlanFps, int aStride, bool* aBelowFloor = nullptr) {
    if (aBelowFloor) *aBelowFloor = false;
    if (aStride <= 1 || aPlanFps <= 0) return aPlanFps;
    const double exact = double(aPlanFps) / double(aStride);
    if (exact < 1.0) {
        if (aBelowFloor) *aBelowFloor = true;
        return 1; // the format floor; a rate of 0 is refused outright
    }
    const int rounded = int(qRound(exact));
    return rounded < 1 ? 1 : rounded;
}

// One encode attempt at a given degradation, returning the written byte count.
struct Attempt {
    bool wrote = false;
    qint64 bytes = 0;
    QString error;
    // Set when the encoder the plan named could not be opened here and this
    // attempt ran on the family's software encoder instead (see the one-step
    // fallback in encodeAttempt). Empty when no step-down happened.
    QString steppedTo;
};

Attempt encodeAttempt(const ExportPlan& aPlan, const QList<QImage>& aFrames,
                      const QSize& aSize, int aFrameStride, int aQualityValue,
                      const QualityControl* aQuality, const ExportRequest& aRequest,
                      QStringList* aWrittenBefore) {
    Attempt out;

    QList<QImage> frames;
    for (int i = 0; i < aFrames.size(); i += aFrameStride) frames.append(aFrames[i]);
    if (frames.isEmpty()) {
        out.error = QCoreApplication::translate("ExportTargets", "no frames left");
        return out;
    }

    // The colour the pad/crop margins take. Transparent while the plan keeps alpha
    // (the artifact has an alpha plane for the margin to live in); when the plan
    // resolved alpha OFF the margins are the effective viewport colour, so a
    // padded opaque artifact shows the canvas colour instead of black. The PLAN
    // resolves the transparency (the per-target rule), not the raw request field.
    const QColor marginFill = aPlan.alphaEnabled ? QColor() : aRequest.viewportBackground;

    // The rate the SURVIVING frames are written at (see effectiveFpsForStride):
    // fps/stride, so frames/(fps/stride) — the running time — is what the plan
    // described. Computed ONCE here and fed to every branch below, because the
    // image branch (no rate at all), the GIF branch (GifEncoder's fps) and the
    // video branch (VideoEncoder's fps, and the software retry's makeConfig)
    // must not be able to disagree. At stride 1 it is the plan's own rate.
    const int effectiveFps = effectiveFpsForStride(aPlan.config.fps, aFrameStride);

    // An image artifact is written by Qt's image writers, so "encode an attempt"
    // means "write the file(s) this attempt describes and measure them". Without
    // this branch a capped image destination went down the video path and failed
    // with "invalid fps" — the fit loop knew about the video encoder and about GIF,
    // and nothing else.
    if (aPlan.artifact != ArtifactKind::Video) {
        const QSize canvasSize = aSize.isEmpty() ? aPlan.size : aSize;
        const QFileInfo target(aPlan.config.outputPath);
        // Files a PREVIOUS attempt wrote are removed first: an attempt that drops
        // frames (the last degradation phase) would otherwise leave its predecessor's
        // frames behind, and the user would get a folder with more frames in it than
        // the fit decided on. Only files this loop wrote are touched.
        if (aWrittenBefore) {
            for (const QString& p : *aWrittenBefore) QFile::remove(p);
            aWrittenBefore->clear();
        }
        qint64 total = 0;
        const int count = aPlan.artifact == ArtifactKind::Still ? 1 : frames.size();
        for (int i = 0; i < count; ++i) {
            const QImage frame =
                frames[i].size() == canvasSize
                    ? frames[i]
                    : media::fitImage(frames[i], canvasSize, aPlan.config.fit, marginFill);
            const QString path =
                aPlan.artifact == ArtifactKind::Still
                    ? aPlan.config.outputPath
                    // Same naming as Exporter::decideImagePath: stem + 4 digits,
                    // with the stem the caller uses for a plain export.
                    : target.absolutePath() + QLatin1Char('/')
                          + (aRequest.imageStem.isEmpty() ? target.completeBaseName()
                                                          : aRequest.imageStem)
                          + QStringLiteral("%1").arg(i + 1, 4, 10, QChar('0')) + QLatin1Char('.')
                          + aPlan.imageSuffix;
            if (!frame.save(path, nullptr, aQualityValue)) {
                out.error = QCoreApplication::translate("ExportTargets", "could not write %1")
                                .arg(path);
                return out;
            }
            if (aWrittenBefore) aWrittenBefore->append(path);
            total += QFileInfo(path).size();
        }
        if (total <= 0) {
        out.error = QCoreApplication::translate("ExportTargets", "no files produced");
        return out;
    }
        out.wrote = true;
        out.bytes = total;
        return out;
    }

    // GIF is media::GifEncoder's artifact, not VideoEncoder's, and a fit that
    // wrote a different artifact than a plain export would not be a fit: the
    // user's palette and the loop setting have to survive the re-encodes.
    //
    // The frames arrive at the RENDER size, so the pad-or-crop to the output size
    // happens here (the video path gets that from the encoder itself).
    if (aPlan.gifPath) {
        const QSize canvasSize = aSize.isEmpty() ? aPlan.size : aSize;
        QList<QImage> canvas;
        canvas.reserve(frames.size());
        for (const QImage& f : frames) {
            canvas.append(f.size() == canvasSize ? f
                                                 : media::fitImage(f, canvasSize, aPlan.config.fit,
                                                                   marginFill));
        }
        media::GifEncoder::Config gcfg;
        gcfg.outputPath = aPlan.config.outputPath;
        // The stride's survivors play for the plan's duration only because they
        // go out at fps/stride; the row's own default (15) stands for a plan
        // that carries no rate at all.
        gcfg.fps = effectiveFps > 0 ? effectiveFps : 15;
        gcfg.transparent = aPlan.alphaEnabled;
        gcfg.loop = aRequest.gifLoop;
        gcfg.palettePath = aRequest.gifPalettePath;
        // The quality dimension for GIF: fewer palette entries, same pixels. The
        // option NAME in the table ("palette") is not a libav option and is not used
        // here; only its [best, worst] RANGE is, through aQualityValue.
        if (aQuality && aQualityValue > 0) {
            gcfg.maxColours = std::clamp(aQualityValue, 2, 256);
        }
        media::GifEncoder gif;
        QString gerr;
        if (!gif.encode(canvas, gcfg, &gerr)) {
            out.error = gerr.isEmpty() ? gif.error() : gerr;
            return out;
        }
        const QFileInfo info(gcfg.outputPath);
        if (!info.exists()) {
            out.error = QCoreApplication::translate("ExportTargets", "no file produced");
            return out;
        }
        out.wrote = true;
        out.bytes = info.size();
        return out;
    }

    media::VideoEncoder::Config cfg = aPlan.config;
    // The stride's survivors play for the plan's duration because they go out at
    // fps/stride. Every later use of cfg inherits this rate: the open below and
    // the software fallback's makeConfig call alike.
    cfg.fps = effectiveFps;
    cfg.width = aSize.width();
    cfg.height = aSize.height();
    // The encoder pads/crops the rendered frame to the output size itself; its
    // margins take the export's background (see marginFill).
    cfg.fill = marginFill;
    if (aQuality && aQualityValue >= 0) {
        cfg.encOptions.insert(aQuality->option, QString::number(aQualityValue));
    }

    media::VideoEncoder enc;
    if (!enc.open(cfg)) {
        // Step down ONCE, to the same family's software encoder, exactly as
        // Exporter::execute does for a plain export. The capability probe that
        // offered this codec is a 64x64 one-frame encode, which a hardware
        // backend can pass while avcodec_open2 still refuses the real
        // resolution and option set — NVENC on a machine with no capable
        // device, a QSV/VAAPI profile without the residency the resolution
        // needs, an AMF driver that reports the codec and not the profile, or
        // (measured against the linked libavcodec) the family's quality option
        // name, which the hardware encoders spell differently: cq on NVENC, qp
        // on VAAPI, and none on QSV/AMF. Without this the fit loop aborted on
        // exactly the machine whose ladder has just resolved to the GPU.
        //
        // The family mapping is qualityTableNameForFamily() — the SAME one the
        // quality knob is looked up through — so this can never step down to a
        // different family. It is empty when the codec is already software (a
        // software encoder that fails has nothing to step down to, and retrying
        // the same library would only repeat the failure) or when the family has
        // no software encoder of its own (lossless: ffv1). Either way the failure
        // stands exactly as it was reported. There is no second retry.
        const QString softwareCodec =
            isHardwareEncoder(cfg.codec)
                ? qualityTableNameForFamily(formatKeyForEncoder(cfg.codec))
                : QString();
        if (softwareCodec.isEmpty()) {
            out.error = enc.error();
            return out;
        }
        const QString hardwareError = enc.error();
        QString rebuildError;
        media::VideoEncoder::Config retry = media::VideoEncoder::makeConfig(
            cfg.container, softwareCodec, cfg.pixFmt, cfg.transparent, cfg.width,
            cfg.height, cfg.fps, cfg.bitrate > 0 ? cfg.bitrate / 1000 : 0, &rebuildError);
        // Everything encoder-independent carries over untouched, so the retry
        // is the same file with a different library — the private options (the
        // plan's quality knob among them, which the software encoder DOES take),
        // the muxer options, the stream codec tag, the fit mode and the audio
        // stream all survive the swap.
        retry.outputPath = cfg.outputPath;
        retry.encOptions = cfg.encOptions;
        retry.muxerOptions = cfg.muxerOptions;
        retry.codecTag = cfg.codecTag;
        retry.fit = cfg.fit;
        retry.fill = cfg.fill; // the pad margin colour, same as the first attempt
        retry.audioCodec = cfg.audioCodec;
        retry.audioChannels = cfg.audioChannels;
        retry.audioSampleRate = cfg.audioSampleRate;
        retry.audioBitrate = cfg.audioBitrate;
        retry.quality = cfg.quality; // the encoder's own hint, dropped by makeConfig
        if (!retry.codec.isEmpty() && enc.open(retry)) {
            // Not an error: the export is going ahead. The step lines are the
            // user's record of what the loop actually did, so they name the
            // swap rather than leaving a file encoded by a library other than
            // the one the plan resolved.
            out.steppedTo = QCoreApplication::translate(
                                "ExportTargets", " (%1 could not be opened here; written with %2)")
                                .arg(cfg.codec, softwareCodec);
        } else {
            // Both candidates failed. Keep the hardware reason first (it is the
            // one describing the codec the plan resolved) and append the
            // software one, so a genuine failure still reports a reason for each.
            const QString softwareError = retry.codec.isEmpty() ? rebuildError : enc.error();
            out.error = QCoreApplication::translate(
                            "ExportTargets", "%1\nThe %2 encoder could not be opened either (%3).")
                            .arg(hardwareError, softwareCodec, softwareError);
            return out;
        }
    }
    for (const QImage& f : frames) {
        if (!enc.write(f)) { out.error = enc.error(); return out; }
    }
    if (!enc.close()) { out.error = enc.error(); return out; }

    const QFileInfo info(cfg.outputPath);
    if (!info.exists()) {
        out.error = QCoreApplication::translate("ExportTargets", "no file produced");
        return out;
    }
    out.wrote = true;
    out.bytes = info.size();
    return out;
}

} // namespace

FitResult exportWithFit(const ExportTarget& aTarget, const ExportRequest& aRequest,
                        QualityBucket aBucket, const QList<QImage>& aFrames,
                        const std::function<void(const QString&)>& aOnStep,
                        const std::function<bool()>& aShouldCancel) {
    FitResult result;
    result.path = aRequest.outputPath;

    const ExportPlan plan = planExport(aTarget, aRequest, aBucket);
    if (!plan.ok()) { result.error = plan.error; return result; }

    // The plan is authoritative about WHICH frames to write (see FrameSelection).
    // If the caller hands over a different number, the file would not match the
    // plan — and for a capped destination it could exceed the very limit this
    // function exists to enforce, while the fit loop reported success. Fail
    // loudly instead of encoding something the plan never described.
    if (aFrames.size() != plan.frames()) {
        // "frame%2" glued an "s" onto a number, and "%1 frame"/"%1 frames" replaced it
        // with two English plural forms: neither is movable, and Russian needs a third
        // form for 2 to 4. The count is a label here instead of a noun with a number in
        // front of it, which is a phrase every language can render for any count.
        //
        // The parenthetical used to read "(render plan.selection.indices())": that is an
        // instruction to whoever wires a caller, printed to the person waiting for a
        // file. The mismatch itself is what they need to know.
        result.error = QCoreApplication::translate(
                           "ExportTargets",
                           "frames: the plan selected %1, but %2 were supplied")
                           .arg(plan.frames())
                           .arg(aFrames.size());
        return result;
    }

    const qint64 cap = aTarget.caps.maxBytes;

    // Whether the OUTPUT size is a requirement rather than a ceiling. Eight rows
    // demand an exact size AND cap the bytes (telegram.emoji 100x100, signal.sticker
    // 512x512, whatsapp.sticker 512x512, discord.sticker 320x320, twitch.emote
    // 112x112, wechat.sticker 240x240, kakaotalk.sticker 360x360, qq.sticker
    // 300x300), and both constraints are hard upload rejections. The scale phase
    // therefore may not move the output dimensions on those rows: it reduces how
    // much detail the art carries and leaves the size alone. On a Max row the
    // opposite is true and shrinking is the better trade, because a smaller canvas
    // is sharper per byte and the platform accepts anything under its maximum.
    //
    // MaxMinSide belongs with them: it is a FLOOR as well as a ceiling
    // (line.sticker's declared 270 px minimum side, which applySizeRule already
    // grew the output to), so scaling below it is not "smaller", it is below the
    // rule the destination published.
    const bool requiredSize = (aTarget.size.mode == SizeMode::Exact
                               || aTarget.size.mode == SizeMode::MaxExactSide
                               || aTarget.size.mode == SizeMode::MaxMinSide);

    QList<QImage> base;
    for (const QImage& f : aFrames) {
        base.append(f.size() == plan.renderSize
                        ? f
                        : f.scaled(plan.renderSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }

    // The files the last image attempt wrote, so a later attempt can clear them.
    QStringList writtenByLastAttempt;

    // The knob the plan already applied its intent through — looked up the same
    // way applyQualityIntent() looked it up, so a hardware-resolved codec still
    // has one and the loop can degrade quality on a machine whose export the
    // ladder resolved to h264_nvenc/hevc_vaapi/av1_qsv. Null = no knob: this
    // codec trades no quality for bytes (FFV1, ProRes), so the fit loop goes
    // straight to scale and frames.
    const QualityControl quality = qualityControlFor(plan.config.codec);
    const QualityControl* qualityKnob = quality.option.isEmpty() ? nullptr : &quality;

    // No cap: one encode, no loop.
    qint64 smallest = 0; // smallest file seen (only used in the give-up message)
    // What one attempt puts in front of the encoder at a given scale: the output
    // size the file must have, and - written through aArt - the size the ART is
    // drawn at when the scale reduces detail instead of the output. An EMPTY aArt
    // means the frames reach the encoder untouched, at the size the file has.
    //
    // encodeOnce() and the scale phase's search domain both read the answer from
    // here, so the domain cannot drift from what the loop actually does.
    //
    // Test emptiness with isEmpty(), never isNull(): a default QSize is (-1,-1),
    // which isEmpty() reports as empty and isNull() does NOT.
    const auto attemptShape = [&](double aScale, QSize* aArt) {
        const QSize out = requiredSize
                              ? plan.size
                              : QSize(std::max(2, evenFloor(plan.size.width() * aScale)),
                                      std::max(2, evenFloor(plan.size.height() * aScale)));
        QSize art;
        if (aScale != 1.0 && (requiredSize || !plan.gifPath)) {
            art = requiredSize
                      ? QSize(std::max(2, evenFloor(plan.renderSize.width() * aScale)),
                              std::max(2, evenFloor(plan.renderSize.height() * aScale)))
                      : out;
        }
        if (aArt) *aArt = art;
        return out;
    };

    auto encodeOnce = [&](int aCrf, double aScale, int aStride, const QString& aWhat) -> int {
        // The single choke point every attempt goes through, so one check here
        // covers the initial encode and both bisections.
        if (aShouldCancel && aShouldCancel()) {
            result.canceled = true;
            result.error = QCoreApplication::translate("ExportTargets", "canceled");
            return -1;
        }
        // The stride is applied ONCE, in encodeAttempt, which is the encoder
        // boundary. It used to be applied here as well, so asking for every 2nd
        // frame wrote every 4th and the stride in the log was not the stride in
        // the file. Only the first attempt (stride 1) was unaffected, which is
        // why it went unnoticed.
        QSize artSize; // the size the art is drawn at, empty when it is not reduced
        const QSize attemptSize = attemptShape(aScale, &artSize);
        QList<QImage> frames = base;
        if (!artSize.isEmpty()) {
            // The GIF path fits each frame to the output size itself (pad or
            // crop, per the destination's rule); scaling it here would distort
            // the art instead, because this scale ignores the aspect ratio.
            //
            // Which size the scale REDUCES depends on whether the destination
            // requires an exact size. When it does, the scale must not touch the
            // output dimensions: the art is rendered with less detail and placed
            // back at the size the platform demands, so the cap is paid for with
            // softness instead of with the shape. Measured before this: a 512x512
            // Signal sticker under its 300 KB cap came out 296x296, which Signal
            // rejects for its size - the cap was met by breaking the other hard
            // requirement. Detail is reduced in the RENDER space and the encoder's
            // own pad-or-crop then produces the required output size exactly as it
            // does for an uncapped export.
            for (QImage& f : frames) {
                f = f.scaled(artSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                if (requiredSize)
                    f = f.scaled(plan.renderSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
        }
        const Attempt a =
            encodeAttempt(plan, frames, attemptSize, aStride, aCrf, qualityKnob, aRequest,
                          &writtenByLastAttempt);
        ++result.attempts;
        QString step = QStringLiteral("%1: crf=%2 %3x%4 stride=%5 -> %6 bytes")
                           .arg(aWhat)
                           .arg(aCrf)
                           .arg(attemptSize.width())
                           .arg(attemptSize.height())
                           .arg(aStride)
                           .arg(a.wrote ? (qlonglong)a.bytes : (qlonglong)-1);
        if (!artSize.isEmpty() && artSize != attemptSize) {
            // These numbers are the encoder's: `attemptSize` is what the exporter
            // was told to produce. The line used to print plan.size * percent,
            // which on a required-size row is neither the size the encoder was
            // handed nor the size it wrote - a 100x100 emoji row logged "50x50"
            // for a file that is 100x100, in a log the export UI shows the user.
            // The percent is not a pixel count, and the file's size is.
            step += QStringLiteral(" (art drawn at %1x%2)").arg(artSize.width()).arg(artSize.height());
        }
        if (!a.steppedTo.isEmpty()) {
            // The encoder the plan resolved could not be opened here, so this
            // attempt was written by the family's software encoder. The step
            // line is the record of what the loop actually did: a file encoded
            // by a library other than the one the plan named must say so.
            step += a.steppedTo;
        }
        result.steps << step;
        if (aOnStep) aOnStep(step);
        if (!a.wrote) { result.error = a.error; return -1; }
        // `bytes` is the file the caller gets; `smallest` is only for the
        // failure message (the last probe is not necessarily the smallest).
        result.bytes = a.bytes;
        result.finalSize = attemptSize;
        if (smallest == 0 || a.bytes < smallest) smallest = a.bytes;
        return (int)std::min<qint64>(a.bytes, (qint64)INT_MAX);
    };

    if (cap <= 0) {
        if (encodeOnce(qualityKnob ? qualityKnob->best : -1, 1.0, 1, QStringLiteral("initial")) < 0)
            return result;
        result.ok = true;
        return result;
    }

    // ---- model-driven degradation -----------------------------------------
    // Each knob has a predictive model, so we jump to the predicted setting and
    // then correct from the real measurement, rather than walking arbitrary
    // steps. The models are the published/verified relationships:
    //   quality: size ~ 2^(crf/6)  — a change of +-6 CRF halves/doubles the bitrate
    //   scale:   size ~ scale^2    — bytes scale with pixel count
    //   frames:  size ~ frameCount — bytes scale with the number of frames
    // Order comes from the destination's `degrade` list; the default is
    // quality -> scale -> frames because quality changes neither composition nor
    // timing, scale costs detail but keeps both, and dropping frames changes the
    // animation itself.
    double scale = 1.0;
    int stride = 1;
    int crf = qualityKnob ? qualityKnob->best : -1;

    int bytes = encodeOnce(crf, scale, stride, QStringLiteral("initial"));
    if (bytes < 0) return result;
    if (bytes <= cap) { result.ok = true; return result; }

    // ---- searching the degradation knobs -----------------------------------
    // Each knob trades bytes monotonically, so the least destructive setting that
    // fits can be SEARCHED for rather than guessed:
    //   quality: bytes fall as the knob rises
    //   scale:   bytes rise with area
    //   frames:  bytes fall as the stride rises
    // What the search must not do is guess from a MODEL of the curve. The published
    // "+-6 CRF halves/doubles the bitrate" rule measurably fails at the extremes: CRF
    // 24 -> 52 on VP9 gave ~3x here, not the ~25x the rule predicts, and a version
    // driven by it converged worse than a plain bisection.
    //
    // It may, however, use the bytes it MEASURED. Bisection ignores them: it halves
    // its interval whatever the two probes said, and on these knobs that wastes
    // encodes twice over.
    //   * The search domain was 101 percentages while the knob has far fewer values
    //     (H.264 has 16 CRF points between this table's best and worst, VP9 29), so
    //     two probes of "different" percentages encoded the identical setting. In the
    //     measured baseline one H.264 fit probed CRF 29 three times and one VP9 fit
    //     probed 43 and 44 twice each. The domain is now the knob's own values.
    //   * The curve is smooth, so the two measurements bracket the cap closely and
    //     interpolating between them (regula falsi) lands on the crossing instead of
    //     on the midpoint of an interval that is nowhere near it.
    // The interpolation is guarded twice so it can only ever help: the retained
    // endpoint's error is halved when the same side is kept twice (the Illinois step,
    // which stops one-sided crawling on a convex curve), and a step that would not
    // shrink the interval falls back to its midpoint. Worst case is therefore the
    // bisection it replaces, and every step is still decided by a real encode.
    //
    // `lastBytes` carries each probe's result out of the evaluator. -1 = error,
    // -2 = even the extreme end of the range does not fit.
    int lastBytes = bytes;
    const int kMaxAttempts = 16;

    // Searches [0,hi] for the SMALLEST setting whose encode fits, for a bytes()
    // that is NON-INCREASING over the range. `aBytesAtZero` is the size already
    // measured at setting 0; it must be over the cap (it is - a search only starts
    // while the current setting does not fit, and setting 0 IS the current setting).
    // Knowing it is what lets the first step interpolate instead of probing a
    // midpoint. Pass 0 when the knob has already been degraded by an earlier phase,
    // in which case setting 0 was never measured and the first step bisects.
    // Returns the winning index, -1 on an encode error, -2 when even `hi` is too big.
    auto searchSmallest = [&](qint64 aBytesAtZero, int hi, const std::function<int(int)>& eval) -> int {
        if (hi <= 0) return -2;
        if (eval(hi) < 0) return -1;
        if (lastBytes > cap) return -2;
        // g() is what the secant interpolates, and its root is the answer: the crossing
        // of the cap. Plain bytes were measured against log(bytes) - these knobs trade
        // size multiplicatively, so on paper log is the straighter axis - and the log
        // version came out WORSE on the fixture set: WebP improved (10 -> 8 probes on its
        // scale axis) while VP9 and H.264 went 5 -> 6 and the GIF palette axis 9 -> 11,
        // 31 encodes against 29. One mechanism, the one that measured better, with the
        // Illinois step below handling the convexity a straight line gets wrong.
        auto g = [&](qint64 aBytes) {
            return (double)(aBytes - cap);
        };
        int a = 0, b = hi;              // invariant: f(a) > cap >= f(b)
        double ga = aBytesAtZero > 0 ? g(aBytesAtZero) : 0.0;
        double gb = g(lastBytes);                   // <= 0
        int keptLast = 0;               // which side was retained by the previous step
        int atEdge = 0;                 // consecutive steps that only moved one unit
        while (b - a > 1) {
            if (result.attempts >= kMaxAttempts) break;
            int m = -1;
            if (ga > 0.0) {
                const double frac = ga / (ga - gb);
                m = a + (int)std::llround((double)(b - a) * frac);
                // Clamp into the OPEN interval rather than falling back to its
                // midpoint. An interpolation that lands on `a` says the crossing is
                // immediately beside it, and a+1 is then the single most informative
                // probe there is: the midpoint throws that away and needs log2 of the
                // gap to find it (measured: 236, 234, 233 to cover a gap of one).
                if (m <= a) { m = a + 1; ++atEdge; }
                else if (m >= b) { m = b - 1; ++atEdge; }
                else atEdge = 0;
            }
            if (m < 0 || atEdge >= 3) {
                // No measurement at index 0 to interpolate from, or the crossing really
                // is being approached one unit at a time and bisection is the faster
                // way in.
                m = a + (b - a) / 2;
                atEdge = 0;
            }
            if (eval(m) < 0) return -1;
            const double gm = g(lastBytes);
            if (gm <= 0) {
                b = m;
                gb = gm;
                // Illinois: b has now been moved twice in a row, so the endpoint the
                // interval is NOT shrinking from is a, and its weight is what has to
                // drop. Without this the search crawls into the fitting side one step
                // at a time (measured on a WebP scale search: 104, 116, 120, 122, 124,
                // 126 - five probes to move one step), because on a convex curve
                // interpolation keeps landing on the same side of the root.
                if (keptLast > 0) ga /= 2.0;
                keptLast = 1;
            } else {
                a = m;
                ga = gm;
                if (keptLast < 0) gb /= 2.0;
                keptLast = -1;
            }
        }
        return b;
    };

    for (const QString& phase : aTarget.degradeOrder) {
        if (result.ok || result.canceled) break;
        if (phase == QLatin1String("quality") && qualityKnob) {
            // The knob's OWN values, indexed from the table's `best` (index 0, the
            // largest file) to its `worst` (the last index). This used to bisect
            // straight between best and worst, which assumes the range RISES from
            // best to worst: true for CRF, false for WebP's quality (80 -> 30) and
            // for GIF's palette size (256 -> 8), where "best" is the larger number.
            // Passing 80 -> 30 into a bisection that needs lo < hi made it return hi
            // at once, so a capped WebP destination silently took the WORST quality
            // even when a middling one fitted. An index has no direction, so that
            // class of mistake is gone rather than guarded against.
            const int best = qualityKnob->best;
            const int worst = qualityKnob->worst;
            const int n = std::abs(worst - best) + 1;
            auto valueAt = [best, worst, n](int aIndex) {
                if (n <= 1) return best;
                return best + ((worst - best) * aIndex) / (n - 1);
            };
            // f(0) is valueAt(0) == best. The initial encode measured exactly that,
            // but only if no earlier phase already moved the scale or the frames - a
            // row may declare any degrade order, and a stale hint would only cost a
            // step, so it is passed as "unknown" rather than assumed.
            const qint64 atBest = (scale == 1.0 && stride == 1) ? bytes : 0;
            const int win = searchSmallest(
                atBest, n - 1,
                std::function<int(int)>([&](int idx) {
                    lastBytes = encodeOnce(valueAt(idx), scale, stride, QStringLiteral("quality"));
                    return lastBytes;
                }));
            if (win >= 0) {
                crf = valueAt(win);
                result.ok = true;
            } else if (win == -2) {
                // Even the worst quality is too big: KEEP the floor and let the
                // next phase build on it (otherwise later phases silently
                // revert to the best quality and undo this phase's work).
                crf = worst;
            }
        } else if (phase == QLatin1String("scale")) {
            // The floor comes from the data: never shrink past minScalePct, because
            // the cliff is visible. bytes RISE with the scale, so the search starts
            // at the current setting (100%) and walks DOWN.
            //
            // The domain is the SETTINGS this knob can produce, not the percents,
            // for the same reason the quality phase indexes the knob's own values:
            // a percent is a proxy and the encoder is handed PIXELS. The attempt is
            // drawn at evenFloor(size * pct/100), so on a small canvas two percents
            // are one encode - and the search pays for it twice while learning
            // nothing the second time, because encoding is deterministic. Counted
            // over the 25%..100% floor this table uses: a canvas 240 wide or more
            // has 76 percents and 76 distinct settings, 128x96 has 61, 96x64 has 49
            // (27 percents that encode something already paid for). This is not
            // theoretical: the pre-existing discord.emoji case, whose PLAN is
            // 128x128, probed "scale: crf=30 124x124" twice for 32842 bytes both
            // times. Deduplicating by what the encoder receives (attemptShape) makes
            // a repeated probe impossible rather than unlikely.
            const int floorPct = std::max(1, aTarget.minScalePct);
            QList<int> pcts;      // pct 100 first, then strictly smaller settings
            QSize prevOut, prevArt;
            for (int pct = 100; pct >= floorPct; --pct) {
                QSize art;
                const QSize out = attemptShape((double)pct / 100.0, &art);
                if (!pcts.isEmpty() && out == prevOut && art == prevArt) continue;
                pcts.append(pct);
                prevOut = out;
                prevArt = art;
            }
            // pct 100 is unscaled, which is where the previous probe left it: no
            // earlier phase changes the scale, so its size is the last one measured.
            // It is index 0 of the domain by construction above.
            const qint64 bytesAtFullScale = scale == 1.0 ? lastBytes : 0;
            const int win = searchSmallest(
                bytesAtFullScale, (int)pcts.size() - 1,
                std::function<int(int)>([&](int idx) {
                    lastBytes = encodeOnce(crf, (double)pcts[idx] / 100.0, stride, QStringLiteral("scale"));
                    return lastBytes;
                }));
            if (win >= 0) {
                scale = (double)pcts[win] / 100.0;
                result.ok = true;
            } else if (win == -2) {
                scale = (double)floorPct / 100.0; // keep the floor, keep going
            }
        } else if (phase == QLatin1String("frames")) {
            if (base.size() < 2) continue;
            // Stride 1 at the setting the previous phase left is the last probe, so
            // the size at the un-degraded end is already in hand - unless an earlier
            // phase already dropped frames, which is why it is conditional.
            const qint64 atStrideOne = stride == 1 ? lastBytes : 0;
            const int win = searchSmallest(
                atStrideOne, (int)base.size() - 1,
                std::function<int(int)>([&](int idx) {
                    const int st = idx + 1;
                    lastBytes = encodeOnce(crf, scale, st, QStringLiteral("frames"));
                    return lastBytes;
                }));
            if (win >= 0) {
                stride = win + 1;
                result.ok = true;
            } else if (win == -2) {
                stride = (int)base.size(); // nothing left to drop
            }
        }
    }

    if (result.ok) {
        // The search leaves the LAST PROBE's file on disk, and the last probe is
        // not the setting the search decided on. A bisection stops as soon as the
        // interval closes, and the probe that closes it is usually the one that
        // did NOT fit — so the loop could report "fitted at 144x144, 119199 bytes"
        // while the file the user got was the 146x146 probe at 120687 bytes, over
        // the very cap the destination enforces. Re-encoding the winner is what
        // makes the file match the report; without it "fitted" was a claim about
        // an attempt that had already been overwritten.
        const int finalBytes = encodeOnce(crf, scale, stride, QStringLiteral("final"));
        if (finalBytes < 0) {
            result.ok = false;
            return result;
        }
        // result.bytes is the FINAL attempt's size (the file now on disk);
        // lastBytes still holds whatever the search probed last.
        if (result.bytes > cap) {
            // Encoding is deterministic, so this means the winner did not
            // reproduce. Reporting success here would hand over a rejected file.
            result.ok = false;
            result.error = QStringLiteral("%1: the fitted setting wrote %2 bytes, over its %3 byte "
                                          "cap, when it was re-encoded.")
                               .arg(aTarget.label)
                               .arg((qlonglong)result.bytes)
                               .arg((qlonglong)cap);
            return result;
        }
        result.steps << QStringLiteral("fit under the %1 byte cap in %2 attempts")
                            .arg((qlonglong)cap)
                            .arg(result.attempts);
        if (stride > 1) {
            // A stride over 1 means the frames phase won, and that phase is the
            // one knob that changes the animation itself: the file holds every
            // stride-th frame and nothing else. Its running time is preserved
            // all the same — the survivors are written at fps/stride
            // (encodeAttempt's effective rate), so frames/(fps/stride) is the
            // duration the plan described — and this line is where the user
            // learns that, because a reduced frame count on its own would read
            // as "the animation got shorter".
            const int kept = (plan.frames() + stride - 1) / stride;
            bool belowFloor = false;
            const int rate = effectiveFpsForStride(plan.fps, stride, &belowFloor);
            if (belowFloor) {
                // fps/stride is under the format floor of 1 fps, so the rate was
                // clamped there and the animation really is SHORTER than the
                // plan's. This is the one case where dropping frames still costs
                // running time, and it is said out loud rather than passed off
                // as a lighter animation.
                result.steps << QStringLiteral(
                                    "fit kept every %1 frames: %2 of %3 frames at %4 fps, "
                                    "less time than the plan (the format's 1 fps floor)")
                                    .arg(stride)
                                    .arg(kept)
                                    .arg(plan.frames())
                                    .arg(rate);
            } else {
                result.steps << QStringLiteral(
                                    "fit kept every %1 frames: %2 of %3 frames at %4 fps, "
                                    "the same playing time as the plan")
                                    .arg(stride)
                                    .arg(kept)
                                    .arg(plan.frames())
                                    .arg(rate);
            }
        }
        return result;
    }

    // A cancelled fit is not a failed fit: overwriting the reason here would tell
    // the user "even the smallest setting did not fit" about a job THEY stopped.
    if (result.canceled) return result;
    result.error = QStringLiteral(
                       "could not fit under %1's %2 byte cap (best was %3 bytes at %4x%5). "
                       "Shorten the animation or simplify the art; quality and size are "
                       "already at their floors.")
                       .arg(aTarget.label)
                       .arg((qlonglong)cap)
                       .arg((qlonglong)smallest)
                       .arg(result.finalSize.width())
                       .arg(result.finalSize.height());
    return result;
}

} // namespace ctrl
