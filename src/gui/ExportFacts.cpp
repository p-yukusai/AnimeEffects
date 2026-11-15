#include "gui/ExportFacts.h"

#include <QCoreApplication>
#include <QStringList>

namespace gui {

namespace {
// Why every string below is a full QCoreApplication::translate call with the context
// spelled out, rather than a file-local helper such as TR("..."):
//
// lupdate extracts tr()/translate() calls it can SEE, and it does not expand a helper
// macro or function. A helper therefore produces strings that are not in any catalog
// and that no translator ever sees: this file was written that way, and the effect was
// a facts box that stayed English in all six languages while every string around it
// translated - including the audio sentence that was written specifically to be read.
//
// The context is "MainWindow", where this text lived before it moved to this file, so
// the translations that already exist for it in assets/locale still apply. Changing
// the context would orphan them in five languages for no gain.
//
// Every VALUE below is built from the very same translate() call the flat version
// used, so each existing entry in assets/locale/translation_*.ts still holds: what
// changed is that a row now ALSO carries a label and a detail, which is what lets the
// scene give the pair real typography instead of one run of text.

// The same measurement, in the unit a person reads (see the caps in the table, which
// are all in bytes): MB and GB, and KB below a megabyte.
QString sizeText(qint64 aBytes) {
    if (aBytes >= 1024LL * 1024 * 1024)
        return QCoreApplication::translate("MainWindow", "%1 GB")
            .arg(QString::number(aBytes / (1024.0 * 1024 * 1024), 'g', 3));
    if (aBytes >= 1024LL * 1024)
        return QCoreApplication::translate("MainWindow", "%1 MB")
            .arg(QString::number(aBytes / (1024.0 * 1024), 'g', 3));
    return QCoreApplication::translate("MainWindow", "%1 KB").arg(aBytes / 1024);
}

// "yes"/"no" as translations rather than as literals chosen by a conditional:
// translate() takes its source text as a literal, and a ternary keeps lupdate from
// seeing either word.
QString yesNo(bool aValue) {
    return aValue ? QCoreApplication::translate("MainWindow", "yes")
                  : QCoreApplication::translate("MainWindow", "no");
}

// The words for an audio decision that is NOT simply yes or no. Three different
// things produce a file with no sound, and the form has a control that can look like
// it is being contradicted: a ticked "Export with audio" beside a bare "Audio: no"
// reads as a bug when the truth is that the project has nothing to send. The
// destination refusing sound, the project having none, and the user turning it off are
// three sentences, and only the third is the user's own decision.
//
// When none of the three applies there is no reason to give, and the row says only
// what it will do. Falling through to repeat the plan's own answer printed the
// two-column row as "Audio: yes · yes".
QString audioReason(const ctrl::ExportTarget& aTarget, const ctrl::ExportRequest& aRequest) {
    if (aTarget.audio == ctrl::Capability::Forbidden)
        return QCoreApplication::translate("MainWindow", "not accepted by this destination");
    if (aRequest.exportableAudioTracks == 0)
        return QCoreApplication::translate("MainWindow", "none in this project");
    if (aRequest.mute)
        return QCoreApplication::translate("MainWindow", "off in this dialog");
    // Whatever is left is answered by the row's own value: a file that carries the
    // project's sound needs no reason, and one that cannot carry it says "no".
    return QString();
}
} // namespace

QString ExportFact::severityName(Severity aSeverity) {
    switch (aSeverity) {
    case Severity::Warning: return QStringLiteral("warning");
    case Severity::Error: return QStringLiteral("error");
    case Severity::Normal: break;
    }
    return QStringLiteral("normal");
}

QList<ExportFact> exportFacts(const ctrl::ExportPlan& plan, const ctrl::ExportTarget& target,
                              const ctrl::ExportRequest& request) {
    QList<ExportFact> facts;
    if (!plan.ok()) {
        ExportFact refusal;
        refusal.id = QStringLiteral("error");
        refusal.value = QCoreApplication::translate(
                            "MainWindow", "This destination cannot be exported: %1")
                            .arg(plan.error);
        refusal.severity = ExportFact::Severity::Error;
        facts << refusal;
        return facts;
    }

    // --- the output: size, and how fast -------------------------------------
    // GIF does not go through media::VideoEncoder - media::GifEncoder owns it - so
    // config.codec/container are empty for a GIF row. Naming the format from the path
    // we already know is honest; printing " -  in " is not.
    //
    // The size is appended BEFORE the rate's separate row, which is the order the
    // manual facts panel states the same two rows in: what the artifact is, then how
    // fast it runs. A detail repeats nothing here - "%1 frames" beside a sentence that
    // already ends in "%3 frames (timing is set on the upload page)" was the same fact
    // twice on one line, the furniture the two-column row exists to remove.
    ExportFact size;
    size.id = QStringLiteral("size");
    size.label = QCoreApplication::translate("MainWindow", "Output:");
    ExportFact fps;
    bool haveFps = false;
    if (plan.artifact == ctrl::ArtifactKind::Still) {
        // One image: a frame RATE here would describe a timeline this file does not
        // have. What it does have is one frame, and which one it is - the frame the
        // timeline is showing - so that is what is said.
        size.value = QCoreApplication::translate(
                         "MainWindow", "%1 x %2, frame %3 (the one on the timeline)")
                         .arg(plan.size.width())
                         .arg(plan.size.height())
                         .arg(request.playhead);
    } else if (target.fpsIgnored) {
        // A frame RATE would describe timing this artifact does not carry: pixiv
        // ugoira is numbered stills whose timing is authored on the upload page, so
        // what matters is how many frames there are.
        size.value = QCoreApplication::translate(
                         "MainWindow", "%1 x %2, %3 frames (timing is set on the upload page)")
                         .arg(plan.size.width())
                         .arg(plan.size.height())
                         .arg(plan.frames());
    } else {
        size.value = QCoreApplication::translate("MainWindow", "%1 x %2, %3 fps")
                         .arg(plan.size.width())
                         .arg(plan.size.height())
                         .arg(plan.fps);
        // The rate gets a row of its own so the summary can state it as a fact rather
        // than burying it at the end of the size sentence - but with NO detail, or it
        // would be the third time the number appears on two rows.
        fps.id = QStringLiteral("fps");
        fps.label = QCoreApplication::translate("MainWindow", "Frame rate:");
        fps.value = QCoreApplication::translate("MainWindow", "%1 fps").arg(plan.fps);
        haveFps = true;
    }
    facts << size;
    if (haveFps)
        facts << fps;

    // --- the artifact ---------------------------------------------------------
    ExportFact format;
    format.id = QStringLiteral("format");
    format.label = QCoreApplication::translate("MainWindow", "Format:");
    if (plan.artifact == ctrl::ArtifactKind::Still) {
        format.value = QCoreApplication::translate("MainWindow", "one %1 image")
                           .arg(plan.imageSuffix.toUpper());
    } else if (plan.artifact == ctrl::ArtifactKind::Sequence) {
        // "61 numbered PNG files" states the format already; ".png" after it was the
        // same fact twice.
        format.value = QCoreApplication::translate("MainWindow", "%1 numbered %2 files")
                           .arg(plan.frames())
                           .arg(plan.imageSuffix.toUpper());
    } else if (plan.gifPath) {
        format.value = QCoreApplication::translate("MainWindow", "animated GIF");
    } else {
        format.value = ctrl::codecDisplayName(plan.config.codec);
        format.detail = target.container.isEmpty()
                            ? ctrl::containerDisplayName(plan.config.container)
                            : ctrl::containerDisplayName(target.container);
    }
    facts << format;

    // --- sound and transparency ----------------------------------------------
    ExportFact audio;
    audio.id = QStringLiteral("audio");
    audio.label = QCoreApplication::translate("MainWindow", "Audio:");
    audio.value = yesNo(plan.audioEnabled);
    audio.detail = audioReason(target, request);
    facts << audio;

    ExportFact transparency;
    transparency.id = QStringLiteral("transparency");
    transparency.label = QCoreApplication::translate("MainWindow", "Transparency:");
    transparency.value = yesNo(plan.alphaEnabled);
    facts << transparency;

    // --- the hard limits ------------------------------------------------------
    // Stated as limits. The byte cap is the reason a capped export re-encodes
    // itself (and can end up smaller, or at a lower quality than the Quality
    // setting asked for), so a dialog that omits it makes the result look
    // arbitrary.
    QStringList limits;
    if (target.caps.maxBytes > 0)
        limits << QCoreApplication::translate("MainWindow", "max %1").arg(sizeText(target.caps.maxBytes));
    // The RESOLUTION ceiling belongs in the same statement as the byte cap: it is the
    // other limit on the file. A Max rule deliberately leaves the user's own size in
    // the boxes (the ceiling bounds the OUTPUT - planExport fits the file to it), so a
    // form that omitted it here would hide a real destination rule from the user.
    if (target.size.mode == ctrl::SizeMode::Max && target.size.w > 0 && target.size.h > 0)
        limits << QCoreApplication::translate("MainWindow", "max %1 x %2")
                      .arg(target.size.w).arg(target.size.h);
    if (target.caps.maxDurationSec > 0)
        limits << QCoreApplication::translate("MainWindow", "max %1 s").arg(target.caps.maxDurationSec);
    if (target.caps.minDurationSec > 0)
        limits << QCoreApplication::translate("MainWindow", "min %1 s").arg(target.caps.minDurationSec);
    if (target.caps.maxFrames > 0)
        limits << QCoreApplication::translate("MainWindow", "max %1 frames").arg(target.caps.maxFrames);
    if (target.caps.maxFps > 0)
        limits << QCoreApplication::translate("MainWindow", "max %1 fps").arg(target.caps.maxFps);
    if (!limits.isEmpty()) {
        // The separator is part of what a reader sees, and the ASCII comma is not how
        // Japanese or Chinese separates list items: it is a translated string, not a
        // QStringLiteral, so it can be replaced instead of worked around.
        ExportFact row;
        row.id = QStringLiteral("limits");
        row.label = QCoreApplication::translate("MainWindow", "Limits:");
        row.value = limits.join(QCoreApplication::translate("MainWindow", ", "));
        facts << row;
    }

    // --- the destination's own words -----------------------------------------
    // One row per note, each carrying its own severity. An UNVERIFIED warning is
    // the one a reader has to act on, so it is the warning; the rest are
    // explanation and stay muted.
    for (const QString& note : plan.notes) {
        ExportFact row;
        row.id = QStringLiteral("note");
        row.value = note;
        row.severity = note.contains(QLatin1String("UNVERIFIED")) ? ExportFact::Severity::Warning
                                                                  : ExportFact::Severity::Normal;
        facts << row;
    }
    return facts;
}

QStringList exportFactsText(const QList<ExportFact>& aFacts) {
    QStringList lines;
    lines.reserve(aFacts.size());
    for (const ExportFact& fact : aFacts) {
        lines << fact.text();
    }
    return lines;
}

QList<int> exportFactRoles() {
    return {Qt::UserRole + 1, Qt::UserRole + 2, Qt::UserRole + 3, Qt::UserRole + 4,
            Qt::UserRole + 5};
}

QHash<int, QByteArray> exportFactRoleNames() {
    return {
        {Qt::UserRole + 1, QByteArrayLiteral("factId")},
        {Qt::UserRole + 2, QByteArrayLiteral("factLabel")},
        {Qt::UserRole + 3, QByteArrayLiteral("factValue")},
        {Qt::UserRole + 4, QByteArrayLiteral("factDetail")},
        {Qt::UserRole + 5, QByteArrayLiteral("factSeverity")},
    };
}

} // namespace gui
