// The export dialog, headless: what it STATES and what it OFFERS, per destination.
//
// This is the coverage hole the session named twice and never filled. Every bug the
// export form has had is one of two shapes:
//
//   * the facts contradicted the controls ("Audio: no" beside a ticked "Export with
//     audio"; a Format combo beside a stated "MP4"; a frame rate asked for before
//     writing one JPEG);
//   * a control was hidden or frozen that the destination did not decide (width and
//     height locked for every max-size row; Image quality hidden for a still image
//     whose only quality control it is; the Advanced disclosure hidden while a
//     destination was chosen).
//
// Both shapes are questions about WIDGET STATE and TEXT, so both can be asked without
// a project, a GL context, a window manager or a click. The GUI probes drove this
// dialog before, and their assertions were mostly "the screenshot is not black" - which
// is why every one of the bugs above survived them.
//
// The test walks EVERY row of the destination table and asserts the invariants that
// hold for all of them, then checks a handful of destinations by name where the rules
// are specific.
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/export_form
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QTextStream>
#include <QToolButton>
#include <QTranslator>
#include <QWidget>

#include <algorithm>

#include "ctrl/ExportTarget.h"
#include "gui/ExportFacts.h"
#include "gui/GUIResources.h"
#include "gui/exportdiag.h"

static int failures = 0;

static void check(bool ok, const QString& what) {
    QTextStream(stdout) << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

// A request that exercises every branch the form can be asked about: a mismatched
// aspect (so an exact rule has to pad or crop), six frames, 24 fps, and a project with
// NO audio, which is the case whose facts used to read "Audio: no" beside a ticked
// "Export with audio".
static ctrl::ExportRequest requestFor(const ctrl::ExportTarget& aTarget) {
    ctrl::ExportRequest request;
    request.sourceSize = QSize(1000, 400);
    request.requestedSize = QSize(1000, 400);
    request.fps = 24;
    request.selection = ctrl::FrameSelection::fromCount(6);
    request.exportableAudioTracks = 0;
    request.mute = false;
    request.playhead = 3;
    request.imageStem = QStringLiteral("export_form");
    (void)aTarget;
    return request;
}

// Whether the form OFFERS a control, which is what its rules decide. Deliberately
// isHidden() and not isVisible(): isVisible() is false for every child of a hidden
// parent, so asking it of the controls inside the collapsed Advanced section answers
// "not offered" for all of them - the same trap that once hid the section itself
// because its own visibility was computed from isVisible() of its children.
static bool offered(QWidget* aWidget) { return aWidget && !aWidget->isHidden(); }

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    gui::GUIResources resources(QStringLiteral(AE_RESOURCE_DIR));
    const QList<ctrl::ExportTarget> targets =
        ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH));
    check(targets.size() >= 50,
          QStringLiteral("the destination table loads (%1 rows)").arg(targets.size()));
    if (targets.isEmpty()) return 1;

    QWidget* widget = new QWidget;
    auto* ui = new ExportWidgetUI;
    ui->setupUi(widget, resources, /*projectLastFrame*/ 600, /*playhead*/ 3);
    // Offscreen show: isVisible() is false for every child of a hidden parent, so a
    // test that never shows the form would pass while asserting nothing.
    widget->show();
    QApplication::processEvents();
    // The form really is shown: without this, every isHidden() below would still be
    // meaningful but a refused show() would go unnoticed.
    check(widget->isVisible(), "the form is on screen (offscreen platform)");

    int rows = 0, stills = 0, fpsIgnoredRows = 0, capped = 0, unverified = 0, unbuildable = 0;
    int maxRows = 0, sequenceRows = 0, videoRows = 0, adjacentRows = 0;

    for (const ctrl::ExportTarget& t : targets) {
        ctrl::ExportRequest request = requestFor(t);
        // The order MainWindow uses, and it matters: the destination's defaults go into
        // the controls first because the plan is computed FROM them.
        ui->setDestinationId(t.id);
        ui->applyDestinationDefaults(t);
        const ctrl::ExportPlan plan = ctrl::planExport(t, request, ctrl::QualityBucket::Balanced);
        if (!plan.ok()) {
            // A destination THIS BUILD cannot produce is not a dialog defect: the
            // HEVC+alpha row needs Apple's VideoToolbox, which does not exist on Linux.
            // What the form owes the user in that case is the truth rather than a
            // sheet of controls for a plan that cannot run, so that is what is asserted
            // here, and the row is counted as walked.
            const QString said = gui::exportFacts(plan, t, request).join(QLatin1Char(' '));
            check(said.contains(QStringLiteral("cannot be exported")) && said.contains(plan.error),
                  QStringLiteral("%1: an impossible destination is stated, not silently offered (%2)")
                      .arg(t.id, plan.error));
            ++unbuildable;
            ++rows;
            continue;
        }
        const QStringList facts = gui::exportFacts(plan, t, request);
        const QString text = facts.join(QLatin1Char('\n'));
        ui->applyPlan(plan, t);
        ++rows;

        const bool isStill = plan.artifact == ctrl::ArtifactKind::Still;
        const bool isSequence = plan.artifact == ctrl::ArtifactKind::Sequence;
        if (isStill) ++stills;
        if (isSequence) ++sequenceRows;
        if (!isStill && !isSequence) ++videoRows;
        if (t.fpsIgnored) ++fpsIgnoredRows;
        if (t.caps.maxBytes > 0) ++capped;
        if (!t.verified) ++unverified;
        if (t.size.mode == ctrl::SizeMode::Max) ++maxRows;

        // ---- what it SAYS -------------------------------------------------
        check(text.contains(QStringLiteral("%1 x %2").arg(plan.size.width()).arg(plan.size.height())),
              QStringLiteral("%1: the facts state the output size (%2x%3)")
                  .arg(t.id).arg(plan.size.width()).arg(plan.size.height()));
        if (isStill) {
            check(text.contains(QStringLiteral("one %1 image").arg(plan.imageSuffix.toUpper())),
                  QStringLiteral("%1: the facts say it is one image").arg(t.id));
        } else if (isSequence) {
            check(text.contains(QStringLiteral("%1 numbered %2 files")
                                    .arg(plan.frames())
                                    .arg(plan.imageSuffix.toUpper())),
                  QStringLiteral("%1: the facts say it is a numbered sequence of %2")
                      .arg(t.id).arg(plan.frames()));
        } else {
            check(text.contains(QStringLiteral("Codec:")),
                  QStringLiteral("%1: the facts name the codec").arg(t.id));
        }
        // The audio line always says WHY there is none, because two different
        // controls on this form can look like they contradict a bare "no".
        if (t.audio == ctrl::Capability::Forbidden) {
            check(text.contains(QStringLiteral("not accepted by this destination")),
                  QStringLiteral("%1: the facts say the destination refuses sound").arg(t.id));
        } else {
            check(text.contains(QStringLiteral("none in this project")),
                  QStringLiteral("%1: the facts say the project has no sound").arg(t.id));
        }
        check(text.contains(plan.alphaEnabled ? QStringLiteral("Transparency: yes")
                                             : QStringLiteral("Transparency: no")),
              QStringLiteral("%1: the facts state transparency (%2)")
                  .arg(t.id).arg(plan.alphaEnabled ? QStringLiteral("yes") : QStringLiteral("no")));
        if (t.caps.maxBytes > 0) {
            check(text.contains(QStringLiteral("Limits:")),
                  QStringLiteral("%1: the facts state the destination's limits").arg(t.id));
        }
        check(text.contains(QStringLiteral("UNVERIFIED")) == !t.verified,
              QStringLiteral("%1: the unverified warning matches the row's own flag (%2)")
                  .arg(t.id).arg(t.verified ? QStringLiteral("verified") : QStringLiteral("unverified")));

        // ---- what it OFFERS -----------------------------------------------
        check(!offered(ui->formatCombo) && !offered(ui->codecCombo),
              QStringLiteral("%1: the destination dictates the format, so there is no combo")
                  .arg(t.id));
        const bool wantFps = !isStill && !t.fpsIgnored;
        check(offered(ui->fpsSpinBox) == wantFps,
              QStringLiteral("%1: the frame rate is %2 (%3)")
                  .arg(t.id)
                  .arg(wantFps ? QStringLiteral("asked for") : QStringLiteral("hidden"))
                  .arg(isStill ? QStringLiteral("one image")
                               : (t.fpsIgnored ? QStringLiteral("the row ignores fps")
                                               : QStringLiteral("it reaches the file"))));
        const bool wantImageQuality = isStill || isSequence;
        check(offered(ui->imageQualitySpinbox) == wantImageQuality,
              QStringLiteral("%1: image quality is %2")
                  .arg(t.id)
                  .arg(wantImageQuality ? QStringLiteral("offered (QImage::save writes this)")
                                        : QStringLiteral("hidden (no still writer)")));
        const bool sizeIsUsers = t.size.mode == ctrl::SizeMode::Free
                                 || t.size.mode == ctrl::SizeMode::Max;
        check(ui->widthSpinBox->isEnabled() == sizeIsUsers,
              QStringLiteral("%1: the resolution is %2 (%3 rule)")
                  .arg(t.id)
                  .arg(sizeIsUsers ? QStringLiteral("editable") : QStringLiteral("fixed"))
                  .arg(t.size.mode == ctrl::SizeMode::Free ? QStringLiteral("free")
                       : t.size.mode == ctrl::SizeMode::Max ? QStringLiteral("ceiling")
                                                            : QStringLiteral("pinned")));
        if (t.size.mode == ctrl::SizeMode::Max) {
            check(ui->widthSpinBox->maximum() == t.size.w && ui->heightSpinBox->maximum() == t.size.h,
                  QStringLiteral("%1: and the ceiling is on the control (%2x%3)")
                      .arg(t.id).arg(ui->widthSpinBox->maximum()).arg(ui->heightSpinBox->maximum()));
        }
        check(offered(ui->firstFrameSpinBox) == !isStill,
              QStringLiteral("%1: the frame range is %2")
                  .arg(t.id).arg(isStill ? QStringLiteral("hidden (one image)")
                                         : QStringLiteral("offered")));
        // The disclosure may not hide a decision, and may not open onto nothing.
        const bool anythingInside = offered(ui->transparencyCheckBox) || offered(ui->loopCheckBox)
                                    || offered(ui->exportWithAudioCheckBox)
                                    || offered(ui->customPaletteCheckBox)
                                    || offered(ui->qualityOverrideSpin)
                                    || offered(ui->bitrateLineEdit);
        check(!anythingInside || offered(ui->advancedToggle),
              QStringLiteral("%1: a reachable decision is never behind a hidden Advanced").arg(t.id));
        check(!offered(ui->advancedToggle) || anythingInside,
              QStringLiteral("%1: Advanced never opens onto nothing").arg(t.id));
        if (offered(ui->qualityCombo)) ++adjacentRows;

    }

    // ---- the codec's own value, and the sentinel that means "let the Quality setting
    // decide" ---------------------------------------------------------------
    //
    // Asked OUTSIDE the loop on purpose. A destination's plan carries the codec only
    // after MainWindow has resolved it from the container, so inside the loop this
    // control was never offered at all: x.video's plan has no codec, knobUsable is
    // false, the spin stays hidden, and the two assertions that used to sit there were
    // skipped for every one of the 56 rows (measured: 0 rows offered it). The plan here
    // states the container and codec MainWindow resolves for an mp4 destination, which
    // is the input this question needs.
    {
        const ctrl::ExportTarget xv = ctrl::ExportTargets::find(targets, QStringLiteral("x.video"));
        ctrl::ExportRequest request = requestFor(xv);
        ctrl::ExportPlan forced = ctrl::planExport(xv, request, ctrl::QualityBucket::Balanced);
        forced.config.container = QStringLiteral("mp4");
        forced.config.codec = QStringLiteral("libx264");
        ui->setDestinationId(xv.id);
        ui->applyDestinationDefaults(xv);
        ui->applyPlan(forced, xv);
        ui->advancedToggle->setChecked(true);

        const ctrl::QualityControl control =
            ctrl::ExportTargets::loadQualityControl().value(QStringLiteral("libx264"));
        check(!control.option.isEmpty(),
              QStringLiteral("x.video's codec declares a quality knob (%1)").arg(control.option));
        check(offered(ui->qualityOverrideSpin),
              QStringLiteral("a codec with a knob offers its own value control"));

        // The sentinel has to be INSIDE the range. QSpinBox::setRange CLAMPS the current
        // value, so a sentinel of -1 against a span starting at 19 was erased the moment
        // the constraints were applied: the box always held a number, the Quality combo
        // was disabled as "not in charge", and MainWindow wrote that number into every
        // export, so the intent never decided anything - in the shipping build, with the
        // whole suite green.
        check(ui->qualityOverrideSpin->minimum() == control.autoValue()
                  && ui->qualityOverrideSpin->value() == control.autoValue(),
              QStringLiteral("and it starts on Auto, the bottom of its own range "
                             "(value %1, range %2..%3, codec span %4..%5)")
                  .arg(ui->qualityOverrideSpin->value())
                  .arg(ui->qualityOverrideSpin->minimum())
                  .arg(ui->qualityOverrideSpin->maximum())
                  .arg(control.best)
                  .arg(control.worst));
        check(ui->qualityOverrideSpin->specialValueText() == QStringLiteral("Auto"),
              QStringLiteral("the bottom of the range reads as Auto, not as a number"));
        check(ui->qualityCombo->isEnabled(),
              QStringLiteral("so the Quality setting is in charge until the user says otherwise"));

        ui->qualityOverrideSpin->setValue(control.autoValue() + 1);
        check(!ui->qualityCombo->isEnabled(),
              QStringLiteral("naming a value takes the Quality setting out of charge (%1)")
                  .arg(ui->qualityOverrideSpin->value()));
        ui->setQualityOverrideAuto->click();
        check(ui->qualityOverrideSpin->value() == control.autoValue()
                  && ui->qualityCombo->isEnabled(),
              QStringLiteral("and its own Auto button hands it back (%1)")
                  .arg(ui->qualityOverrideSpin->value()));
    }

    check(rows == targets.size(),
          QStringLiteral("every destination was walked (%1 of %2)").arg(rows).arg(targets.size()));
    check(stills > 0 && sequenceRows > 0 && videoRows > 0,
          QStringLiteral("all three artifact kinds were covered (stills %1, sequences %2, video %3)")
              .arg(stills).arg(sequenceRows).arg(videoRows));
    check(fpsIgnoredRows == 1,
          QStringLiteral("the one row that ignores the frame rate was covered (%1)").arg(fpsIgnoredRows));
    check(capped > 20, QStringLiteral("capped rows were covered (%1)").arg(capped));
    check(unbuildable >= 1,
          QStringLiteral("rows this build cannot produce were covered (%1)").arg(unbuildable));
    check(unverified > 5, QStringLiteral("unverified rows were covered (%1)").arg(unverified));
    check(maxRows > 10, QStringLiteral("ceiling rows were covered (%1)").arg(maxRows));
    check(adjacentRows > 30, QStringLiteral("rows offering the quality intent (%1)").arg(adjacentRows));

    // ---- the destinations whose rules are specific, by name ----------------
    struct Expect {
        const char* id;
        bool fps;          // the frame rate is asked for
        bool imageQuality; // QImage::save's quality is offered
        bool resolution;   // width/height are editable
        bool advanced;     // the disclosure is offered
    };
    const Expect named[] = {
        // Discord's chat row: a CEILING (so the resolution is the user's), sound
        // allowed, and the encoder's own value reachable.
        { "chat.discord.file", true, false, true, true },
        // An animated image: same ceiling, no audio, alpha on by default.
        { "discord.animation", true, false, true, true },
        // One image: no frame rate, no range, and image quality IS the control.
        { "still.instagram.feed", false, true, true, false },
        // A folder of numbered stills whose timing is authored elsewhere.
        { "post.pixiv.ugoira", false, true, true, true },
        // An exact-size sticker: the destination owns the size.
        { "discord.sticker", true, false, false, true },
        // A GIF: the loop flag and the palette are real decisions.
        { "twitch.emote", true, false, false, true },
    };
    for (const Expect& e : named) {
        const ctrl::ExportTarget t =
            ctrl::ExportTargets::find(targets, QString::fromLatin1(e.id));
        if (t.id.isEmpty()) {
            check(false, QStringLiteral("found %1").arg(QString::fromLatin1(e.id)));
            continue;
        }
        ctrl::ExportRequest request = requestFor(t);
        ui->setDestinationId(t.id);
        ui->applyDestinationDefaults(t);
        const ctrl::ExportPlan plan = ctrl::planExport(t, request, ctrl::QualityBucket::Balanced);
        ui->applyPlan(plan, t);
        const QString id = QString::fromLatin1(e.id);
        check(offered(ui->fpsSpinBox) == e.fps,
              QStringLiteral("%1: frame rate %2").arg(id).arg(e.fps ? "offered" : "hidden"));
        check(offered(ui->imageQualitySpinbox) == e.imageQuality,
              QStringLiteral("%1: image quality %2").arg(id).arg(e.imageQuality ? "offered" : "hidden"));
        check(ui->widthSpinBox->isEnabled() == e.resolution,
              QStringLiteral("%1: resolution %2").arg(id).arg(e.resolution ? "editable" : "fixed"));
        check(offered(ui->advancedToggle) == e.advanced,
              QStringLiteral("%1: Advanced %2").arg(id).arg(e.advanced ? "offered" : "hidden"));
    }

    // The facts of a destination that only BOUNDS the size must say what the bound is,
    // and its audio line must name the destination rather than the project.
    {
        const ctrl::ExportTarget t =
            ctrl::ExportTargets::find(targets, QStringLiteral("chat.telegram.video"));
        ctrl::ExportRequest request = requestFor(t);
        ui->setDestinationId(t.id);
        ui->applyDestinationDefaults(t);
        const ctrl::ExportPlan plan = ctrl::planExport(t, request, ctrl::QualityBucket::Balanced);
        const QString text = gui::exportFacts(plan, t, request).join(QLatin1Char('\n'));
        check(text.contains(QStringLiteral("GB")),
              "a 2 GB cap is stated in GB, not as a number of bytes");
        check(text.contains(QStringLiteral("none in this project")),
              "and its audio line blames the silent project, not Telegram");
    }

    // ---- the table's strings actually arrive translated ---------------------
    //
    // The mechanism (a generated QT_TRANSLATE_NOOP list plus a lookup through the same
    // context) could rot silently in either half: drop the lookup and every string is
    // English again with nothing failing. So install a real built catalog and assert the
    // TABLE comes back in another language, brands intact.
    {
        QTranslator translator;
        const bool loaded = translator.load(QStringLiteral("translation_es"),
                                            QStringLiteral(AE_I18N_QM_DIR));
        check(loaded, "a built catalog loads (the .qm are build artifacts)");
        if (loaded) {
            QCoreApplication::installTranslator(&translator);
            const QList<ctrl::ExportTarget> translated =
                ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH));
            const ctrl::ExportTarget esGeneric =
                ctrl::ExportTargets::find(translated, QStringLiteral("chat.generic.video"));
            const ctrl::ExportTarget enGeneric =
                ctrl::ExportTargets::find(targets, QStringLiteral("chat.generic.video"));
            check(!esGeneric.label.isEmpty() && esGeneric.label != enGeneric.label,
                  QStringLiteral("a destination name arrives translated (%1 -> %2)")
                      .arg(enGeneric.label, esGeneric.label));
            // The FACTS box is not a table: it is built in code (gui/ExportFacts.cpp)
            // through QCoreApplication::translate with the "MainWindow" context, and
            // those strings reach a translator only because the call is written out
            // with its context spelled literally. A file-local helper here made them
            // invisible and the box shipped half in English, so the two that a reader
            // meets first are asserted, not assumed.
            const QString codecLine =
                QCoreApplication::translate("MainWindow", "Codec: %1 in %2")
                    .arg(QStringLiteral("libx264"), QStringLiteral("mp4"));
            check(codecLine != QStringLiteral("Codec: libx264 in mp4"),
                  QStringLiteral("the facts box translates with the catalog loaded (%1)")
                      .arg(codecLine));
            // The facts name the FORMAT, never the library that produced it: "AV1", not
            // "libsvtav1". Which implementation we run is ours to pick and not the
            // user's to adjudicate.
            check(ctrl::codecDisplayName(QStringLiteral("libsvtav1")) == QStringLiteral("AV1")
                      && ctrl::codecDisplayName(QStringLiteral("hevc_nvenc")) == QStringLiteral("H.265")
                      && ctrl::codecDisplayName(QStringLiteral("libvpx-vp9")) == QStringLiteral("VP9")
                      && ctrl::containerDisplayName(QStringLiteral("matroska")) == QStringLiteral("MKV"),
                  QStringLiteral("and it names the format and the container, not the library"));
            const ctrl::ExportTarget esAnimation =
                ctrl::ExportTargets::find(translated, QStringLiteral("discord.animation"));
            const ctrl::ExportTarget enAnimation =
                ctrl::ExportTargets::find(targets, QStringLiteral("discord.animation"));
            check(esAnimation.note != enAnimation.note && esAnimation.note.contains(QLatin1String("WebP")),
                  QStringLiteral("and so does its note, with the format names intact (%1 chars)")
                      .arg(esAnimation.note.size()));
            int translatedLabels = 0;
            for (const ctrl::ExportTarget& t : translated) {
                const ctrl::ExportTarget english = ctrl::ExportTargets::find(targets, t.id);
                if (t.label != english.label) ++translatedLabels;
            }
            check(translatedLabels > 20,
                  QStringLiteral("most names are translated, brands are not (%1 of %2 differ)")
                      .arg(translatedLabels).arg(translated.size()));
            QCoreApplication::removeTranslator(&translator);
        }
    }

    QTextStream(stdout) << (failures == 0
                                ? "=== export_form: OK ===\n"
                                : QStringLiteral("=== export_form: %1 FAILED ===\n").arg(failures));
    return failures == 0 ? 0 : 1;
}
