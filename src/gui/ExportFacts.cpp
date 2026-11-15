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
} // namespace

QStringList exportFacts(const ctrl::ExportPlan& plan, const ctrl::ExportTarget& target,
                        const ctrl::ExportRequest& request) {
    QStringList lines;
    if (!plan.ok()) {
        lines << QCoreApplication::translate("MainWindow", "This destination cannot be exported: %1")
                     .arg(plan.error);
        return lines;
    }

    // GIF does not go through media::VideoEncoder - media::GifEncoder owns it - so
    // config.codec/container are empty for a GIF row. Naming the format from the path
    // we already know is honest; printing " -  in " is not.
    if (plan.artifact == ctrl::ArtifactKind::Still) {
        // One image: a frame RATE here would describe a timeline this file does not
        // have. What it does have is one frame, and which one it is - the frame the
        // timeline is showing - so that is what is said.
        lines << QCoreApplication::translate(
                     "MainWindow", "%1 x %2, frame %3 (the one on the timeline)")
                     .arg(plan.size.width())
                     .arg(plan.size.height())
                     .arg(request.playhead);
    } else if (target.fpsIgnored) {
        // A frame RATE would describe timing this artifact does not carry: pixiv
        // ugoira is numbered stills whose timing is authored on the upload page, so
        // what matters is how many frames there are.
        lines << QCoreApplication::translate(
                     "MainWindow", "%1 x %2, %3 frames (timing is set on the upload page)")
                     .arg(plan.size.width())
                     .arg(plan.size.height())
                     .arg(plan.frames());
    } else {
        lines << QCoreApplication::translate("MainWindow", "%1 x %2, %3 fps")
                     .arg(plan.size.width())
                     .arg(plan.size.height())
                     .arg(plan.fps);
    }

    if (plan.artifact == ctrl::ArtifactKind::Still) {
        lines << QCoreApplication::translate("MainWindow", "Artifact: one %1 image")
                     .arg(plan.imageSuffix.toUpper());
    } else if (plan.artifact == ctrl::ArtifactKind::Sequence) {
        lines << QCoreApplication::translate("MainWindow", "Artifact: %1 numbered %2 files")
                     .arg(plan.frames())
                     .arg(plan.imageSuffix.toUpper());
    } else {
        lines << (plan.gifPath
                      ? QCoreApplication::translate("MainWindow", "Codec: animated GIF")
                      : QCoreApplication::translate("MainWindow", "Codec: %1 in %2")
                            .arg(ctrl::codecDisplayName(plan.config.codec),
                                 ctrl::containerDisplayName(plan.config.container)));
    }

    // The audio half says WHY, not only whether. Three different things produce a file
    // with no sound, and the form has a control that can look like it is being
    // contradicted: a ticked "Export with audio" beside a bare "Audio: no" reads as a
    // bug when the truth is that the project has nothing to send. The destination
    // refusing sound, the project having none, and the user turning it off are three
    // sentences, and only the third is the user's own decision.
    QString audioText;
    if (target.audio == ctrl::Capability::Forbidden) {
        audioText = QCoreApplication::translate("MainWindow", "not accepted by this destination");
    } else if (request.exportableAudioTracks == 0) {
        audioText = QCoreApplication::translate("MainWindow", "none in this project");
    } else if (request.mute) {
        audioText = QCoreApplication::translate("MainWindow", "off in this dialog");
    } else {
        audioText = yesNo(plan.audioEnabled);
    }
    lines << QCoreApplication::translate("MainWindow", "Transparency: %1   Audio: %2")
                 .arg(yesNo(plan.alphaEnabled), audioText);

    // The hard limits, stated as limits. The byte cap is the reason a capped export
    // re-encodes itself (and can end up smaller, or at a lower quality than the
    // Quality setting asked for), so a dialog that omits it makes the result look
    // arbitrary.
    QStringList limits;
    if (target.caps.maxBytes > 0)
        limits << QCoreApplication::translate("MainWindow", "max %1").arg(sizeText(target.caps.maxBytes));
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
        lines << QCoreApplication::translate("MainWindow", "Limits: %1")
                     .arg(limits.join(QCoreApplication::translate("MainWindow", ", ")));
    }

    for (const QString& note : plan.notes) {
        lines << QStringLiteral("• ") + note;
    }
    return lines;
}

} // namespace gui
