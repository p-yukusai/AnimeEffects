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
// Both shapes are questions about STATE and TEXT, so both can be asked without a
// project, a GL context, a window manager or a click - and since the port to Qt Quick
// they are asked of gui::ExportViewModel directly: no QWidget, no offscreen show and
// no isVisible(). "Is this offered" is now the model's own visible flag, which is
// exactly what the scene binds to.
//
// The test walks EVERY row of the destination table and asserts the invariants that
// hold for all of them, then checks a handful of destinations by name where the rules
// are specific.
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/export_form
#include <QCoreApplication>
#include <QTextStream>
#include <QTranslator>

#include <algorithm>

#include "ctrl/ExportTarget.h"
#include "gui/ExportFacts.h"
#include "gui/qml/ExportViewModel.h"

// A translator that answers by CONTEXT, so the test can prove WHERE a string was
// looked up. Qt resolves tr() through the caller's staticMetaObject, so a class
// without Q_OBJECT (or one that hand-rolls a tr() shim) asks a DIFFERENT context and
// its strings stay English while the catalog looks translated - which is exactly how
// the widget export dialog shipped English in all six languages.
class ContextProbe : public QTranslator {
public:
    // mutable: translate() is const, and this is a test probe.
    mutable QStringList seenContexts;
    QString translate(const char* aContext, const char* aSource,
                      const char* aDisambiguation, int aN) const override {
        Q_UNUSED(aDisambiguation);
        Q_UNUSED(aN);
        if (qstrcmp(aSource, "Export with transparency") == 0) {
            seenContexts << QString::fromUtf8(aContext);
            return QStringLiteral("TRANSLATED-BY-CONTEXT");
        }
        return QString();
    }
};

static int failures = 0;

static void check(bool ok, const QString& what) {
    QTextStream(stdout) << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

// The aspect partner the view model derives is rounded UP to an even number (a codec's
// chroma plane is not half a pixel). Spelled out here independently of the model, so a
// change to that arithmetic has to be a deliberate change to this test as well.
static int evenUp(int aValue) { return aValue % 2 ? aValue + 1 : aValue; }

// The project facts the dialog is constructed with, matching what MainWindow passes:
// a 1000x400 project, MIXED aspect so an exact rule has to pad or crop, frame 0..59,
// the playhead on frame 3, and NO audio, which is the case whose facts used to read
// "Audio: no" beside a ticked "Export with audio".
static gui::ExportViewModel::Init initFor() {
    gui::ExportViewModel::Init init;
    init.nativeSize = QSize(1000, 400);
    init.nativeFps = 24;
    init.projectFirstFrame = 0;
    init.projectLastFrame = 59;
    init.playhead = 3;
    init.ranges = {ctrl::FrameRange{0, 5}}; // six frames, the count the old test used
    init.exportableAudioTracks = 0;
    init.qualityIndex = 1;
    init.keepAspectRatio = true;
    init.transparency = true;
    init.loop = false;
    init.audio = false;
    init.customPalette = false;
    return init;
}

// The request the model itself builds, so the facts the test reads and the facts the
// form shows come from the same inputs.
static ctrl::ExportRequest requestFor(const gui::ExportViewModel& aModel) {
    return aModel.buildRequest();
}

// Find the row with a given id, or a default-constructed one.
static gui::ExportFact factById(const gui::ExportViewModel& aModel, const QString& aId) {
    for (const gui::ExportFact& fact : aModel.facts()->facts()) {
        if (fact.id == aId)
            return fact;
    }
    return gui::ExportFact();
}

int main(int argc, char** argv) {
    qputenv("AE_DEBUG_SIZE", "1");
    QCoreApplication app(argc, argv);
    const QList<ctrl::ExportTarget> targets =
        ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH));
    check(targets.size() >= 50,
          QStringLiteral("the destination table loads (%1 rows)").arg(targets.size()));
    if (targets.isEmpty()) return 1;

    gui::ExportViewModel model(initFor());

    int rows = 0, stills = 0, fpsIgnoredRows = 0, capped = 0, unverified = 0, unbuildable = 0;
    int maxRows = 0, sequenceRows = 0, videoRows = 0, adjacentRows = 0;
    bool capChecked = false;

    for (const ctrl::ExportTarget& t : targets) {
        // The order MainWindow uses, and it matters: the destination's defaults go into
        // the controls first because the plan is computed FROM them.
        model.setDestinationId(t.id);
        const ctrl::ExportPlan plan = model.lastPlan();
        if (!plan.ok()) {
            // A destination THIS BUILD cannot produce is not a dialog defect: the
            // HEVC+alpha row needs Apple's VideoToolbox, which does not exist on Linux.
            // What the form owes the user in that case is the truth rather than a
            // sheet of controls for a plan that cannot run, so that is what is asserted
            // here, and the row is counted as walked.
            const QString said = model.factsText();
            check(said.contains(QStringLiteral("cannot be exported")) && said.contains(plan.error),
                  QStringLiteral("%1: an impossible destination is stated, not silently offered (%2)")
                      .arg(t.id, plan.error));
            ++unbuildable;
            ++rows;
            continue;
        }

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
        const QString text = model.factsText();
        check(text.contains(QStringLiteral("%1 x %2").arg(plan.size.width()).arg(plan.size.height())),
              QStringLiteral("%1: the facts state the output size (%2x%3)")
                  .arg(t.id).arg(plan.size.width()).arg(plan.size.height()));
        if (isStill) {
            check(factById(model, QStringLiteral("format")).value
                      == QStringLiteral("one %1 image").arg(plan.imageSuffix.toUpper()),
                  QStringLiteral("%1: the facts say it is one image").arg(t.id));
        } else if (isSequence) {
            check(factById(model, QStringLiteral("format")).value
                      == QStringLiteral("%1 numbered %2 files")
                             .arg(plan.frames())
                             .arg(plan.imageSuffix.toUpper()),
                  QStringLiteral("%1: the facts say it is a numbered sequence of %2")
                      .arg(t.id).arg(plan.frames()));
        } else {
            // The intent the old "Codec:" line carried: the facts name BOTH the codec
            // (its display name, never the library) and the container it lands in.
            const gui::ExportFact format = factById(model, QStringLiteral("format"));
            if (plan.gifPath) {
                check(format.value == QStringLiteral("animated GIF"),
                      QStringLiteral("%1: the facts name the artifact (GIF)").arg(t.id));
            } else {
                check(!format.value.isEmpty()
                          && format.value == ctrl::codecDisplayName(plan.config.codec)
                          && !format.detail.isEmpty()
                          && format.detail == ctrl::containerDisplayName(plan.config.container),
                      QStringLiteral("%1: the facts name the codec and its container (%2 in %3)")
                          .arg(t.id, format.value, format.detail));
            }
        }
        // The audio line always says WHY there is none, because two different controls
        // on this form can look like they contradict a bare "no".
        const gui::ExportFact audio = factById(model, QStringLiteral("audio"));
        if (t.audio == ctrl::Capability::Forbidden) {
            check(audio.detail.contains(QStringLiteral("not accepted by this destination")),
                  QStringLiteral("%1: the facts say the destination refuses sound").arg(t.id));
        } else {
            check(audio.detail.contains(QStringLiteral("none in this project")),
                  QStringLiteral("%1: the facts say the project has no sound").arg(t.id));
        }
        check(factById(model, QStringLiteral("transparency")).value
                  == (plan.alphaEnabled ? QStringLiteral("yes") : QStringLiteral("no")),
              QStringLiteral("%1: the facts state transparency (%2)")
                  .arg(t.id).arg(plan.alphaEnabled ? QStringLiteral("yes") : QStringLiteral("no")));
        if (t.caps.maxBytes > 0) {
            check(text.contains(QStringLiteral("Limits:")),
                  QStringLiteral("%1: the facts state the destination's limits").arg(t.id));
        }
        check(text.contains(QStringLiteral("UNVERIFIED")) == !t.verified,
              QStringLiteral("%1: the unverified warning matches the row's own flag (%2)")
                  .arg(t.id).arg(t.verified ? QStringLiteral("verified") : QStringLiteral("unverified")));
        // A row's detail EXPLAINS its value; it never repeats one. "Audio: yes · yes",
        // "animated GIF · GIF" and "61 numbered PNG files · .png" were all this defect
        // in a different row, and each of them survived a round of reading the panel -
        // so it is asserted for every row of every destination rather than for the one
        // that was noticed.
        QString repeated;
        for (const gui::ExportFact& fact : model.facts()->facts()) {
            if (!fact.detail.isEmpty() && fact.value.contains(fact.detail)) {
                repeated += (repeated.isEmpty() ? QString() : QStringLiteral("; "))
                            + fact.label + fact.value + QStringLiteral(" · ") + fact.detail;
            }
        }
        check(repeated.isEmpty(),
              QStringLiteral("%1: no row repeats its own value as its explanation%2")
                  .arg(t.id, repeated.isEmpty() ? QString()
                                                : QStringLiteral(" (") + repeated + QStringLiteral(")")));

        // ---- what it OFFERS -----------------------------------------------
        check(!model.formatVisible(),
              QStringLiteral("%1: the destination dictates the format, so there is no format combo")
                  .arg(t.id));
        // I44 + item 42: a destination that DICTATES one codec states it (no
        // combo); one that merely defaults among several accepted codecs leaves
        // the choice open — and that offered row lives UNDER ADVANCED (item 42's
        // strict reading, the user's call 2026-10-01; its QML instance is the
        // destination-mode one, manual mode keeps its own beside Format). The
        // row offers exactly accepted ∩ this build can
        // produce - a list, not a mode - one entry per FORMAT, with the default
        // preselected. This per-row walk is also the destination-switch re-sync
        // check: every iteration above changed the destination under the model.
        const QList<ctrl::CodecChoice> offered = ctrl::offeredCodecs(t);
        const bool wantCodecCombo = offered.size() > 1;
        check(model.codecVisible() == wantCodecCombo,
              QStringLiteral("%1: the codec row is %2 (%3 offerable: %4)")
                  .arg(t.id,
                       wantCodecCombo ? QStringLiteral("offered")
                                      : QStringLiteral("stated, not offered"))
                  .arg(offered.size())
                  .arg([&] {
                      QStringList fs;
                      for (const ctrl::CodecChoice& c : offered) fs << c.format;
                      return fs.join(QLatin1Char(' '));
                  }()));
        if (wantCodecCombo) {
            const QVariantList options = model.codecOptions();
            check(options.size() == offered.size(),
                  QStringLiteral("%1: the codec row lists every accepted codec (%2, got %3)")
                      .arg(t.id).arg(offered.size()).arg(options.size()));
            QStringList labels;
            for (const QVariant& option : options)
                labels << option.toMap().value(QStringLiteral("label")).toString();
            QStringList wantLabels;
            for (const ctrl::CodecChoice& c : offered) wantLabels << c.label;
            check(labels == wantLabels,
                  QStringLiteral("%1: the row offers the accepted FORMATS (%2)")
                      .arg(t.id, labels.join(QLatin1Char('/'))));
            // the default is preselected: the row's DECLARED default, or the
            // class policy where the row declares none - either way exactly what
            // the plan resolved.
            const int idx = model.codecIndex();
            const QString planFormat = ctrl::formatKeyForEncoder(plan.config.codec);
            check(idx >= 0 && idx < offered.size() && offered.at(idx).format == planFormat,
                  QStringLiteral("%1: the row starts on the default (%2 at index %3)")
                      .arg(t.id, planFormat).arg(idx));
        }
        const bool wantFps = !isStill && !t.fpsIgnored;
        check(model.fpsVisible() == wantFps,
              QStringLiteral("%1: the frame rate is %2 (%3)")
                  .arg(t.id)
                  .arg(wantFps ? QStringLiteral("asked for") : QStringLiteral("hidden"))
                  .arg(isStill ? QStringLiteral("one image")
                               : (t.fpsIgnored ? QStringLiteral("the row ignores fps")
                                               : QStringLiteral("it reaches the file"))));
        const bool wantImageQuality = isStill || isSequence;
        check(model.imageQualityVisible() == wantImageQuality,
              QStringLiteral("%1: image quality is %2")
                  .arg(t.id)
                  .arg(wantImageQuality ? QStringLiteral("offered (QImage::save writes this)")
                                        : QStringLiteral("hidden (no still writer)")));
        // I43 (decided 2026-09-30): the size cap is opt-in and settable, and
        // starts on "Auto" = 0 = NO cap — silent fitting is the reviewed defect.
        // A byte cap needs one file: numbered stills have no one size to fit,
        // stills and single files keep the row.
        const bool wantSizeCap = !isSequence;
        check(model.sizeCapVisible() == wantSizeCap,
              QStringLiteral("%1: the size cap is %2")
                  .arg(t.id)
                  .arg(wantSizeCap ? QStringLiteral("offered (opt-in, Auto = no cap)")
                                   : QStringLiteral("hidden (numbered stills, no one size)")));
        check(model.sizeCapValue() == 0.0 && model.sizeCapAutoValue() == 0.0,
              QStringLiteral("%1: the size cap starts on Auto = no cap enforced").arg(t.id));
        if (!capChecked) {
            capChecked = true;
            // The MB field reaches the fit loop in BYTES (GeneralParams::
            // sizeCapBytes); the "Auto" sentinel stays 0 and the loop never runs.
            model.setSizeCap(0.25);
            check(model.readBack().sizeCapBytes == 262144,
                  QStringLiteral("I43: 0.25 MB reaches the fit loop as 262144 bytes (got %1)")
                      .arg((qlonglong)model.readBack().sizeCapBytes));
            model.setSizeCap(99999.0);
            check(model.sizeCapValue() == model.sizeCapMaximum(),
                  QStringLiteral("I43: the size cap clamps to its maximum (%1)")
                      .arg(model.sizeCapMaximum()));
            model.setSizeCapToAuto();
            check(model.readBack().sizeCapBytes == 0 && model.sizeCapValue() == 0.0,
                  QStringLiteral("I43: Reset to auto returns to no cap"));
        }
        const bool sizeIsUsers = t.size.mode == ctrl::SizeMode::Free
                                 || t.size.mode == ctrl::SizeMode::Max;
        check(model.sizeEditable() == sizeIsUsers,
              QStringLiteral("%1: the resolution is %2 (%3 rule)")
                  .arg(t.id)
                  .arg(sizeIsUsers ? QStringLiteral("editable") : QStringLiteral("fixed"))
                  .arg(t.size.mode == ctrl::SizeMode::Free ? QStringLiteral("free")
                       : t.size.mode == ctrl::SizeMode::Max ? QStringLiteral("ceiling")
                                                            : QStringLiteral("pinned")));
        if (t.size.mode == ctrl::SizeMode::Max) {
            check(model.widthMaximum() == t.size.w && model.heightMaximum() == t.size.h,
                  QStringLiteral("%1: and the ceiling is on the control (%2x%3)")
                      .arg(t.id).arg(model.widthMaximum()).arg(model.heightMaximum()));
        }
        check(model.rangeVisible() == !isStill,
              QStringLiteral("%1: the frame range is %2")
                  .arg(t.id).arg(isStill ? QStringLiteral("hidden (one image)")
                                         : QStringLiteral("offered")));
        // The disclosure may not hide a decision, and may not open onto nothing.
        const bool anythingInside = model.transparency()->visible()
                                    || model.loop()->visible()
                                    || model.audio()->visible()
                                    || model.palette()->visible()
                                    || model.qualityOverrideVisible()
                                    || model.bitrateVisible();
        check(!anythingInside || model.advancedVisible(),
              QStringLiteral("%1: a reachable decision is never behind a hidden Advanced").arg(t.id));
        check(!model.advancedVisible() || anythingInside,
              QStringLiteral("%1: Advanced never opens onto nothing").arg(t.id));
        if (model.qualityInCharge()) ++adjacentRows;
    }

    // ---- destination switches re-sync the codec row (I44) ------------------
    // Two rows that merely default their codec; the walk above proved the
    // per-destination state, this proves the SWITCHES: the offered list moves,
    // the selection lands on the new default, and a user's pin does not survive
    // into a destination they did not set it for.
    {
        ctrl::ExportTarget first, second;
        for (const ctrl::ExportTarget& t : targets) {
            if (ctrl::offeredCodecs(t).size() < 2) continue;
            if (first.id.isEmpty()) {
                first = t;
            } else if (second.id.isEmpty() && t.codecs != first.codecs) {
                second = t;
                break;
            }
        }
        if (!first.id.isEmpty() && !second.id.isEmpty()) {
            model.setDestinationId(first.id);
            const QVariantList firstOptions = model.codecOptions();
            const int firstDefault = model.codecIndex();
            check(firstDefault >= 0 && firstOptions.size() > 1,
                  QStringLiteral("precondition: %1 offers a choice (%2)")
                      .arg(first.id).arg(firstOptions.size()));
            // the user pins the other codec
            model.setCodecIndex(firstDefault == 0 ? 1 : 0);
            check(model.codecIndex() != firstDefault,
                  "precondition: the pin moved the row off its default");
            // switching away and back drops the pin and re-syncs
            model.setDestinationId(second.id);
            check(model.codecOptions() != firstOptions,
                  "a destination switch re-syncs the offered list");
            check(ctrl::formatKeyForEncoder(model.selectedCodecKey())
                      == ctrl::formatKeyForEncoder(model.lastPlan().config.codec),
                  "and lands on the NEW destination's default, not the old pin");
            model.setDestinationId(first.id);
            check(model.codecIndex() == firstDefault,
                  "returning re-syncs to the default again: the pin did not survive the switch");
        }
    }

    // ---- the codec's own value, and the sentinel that means "let the Quality setting
    // decide" ---------------------------------------------------------------
    //
    // Asked OUTSIDE the loop on purpose. A destination's plan carries the codec only
    // after MainWindow has resolved it from the container, so inside the loop this
    // control was never offered at all: x.video's plan has no codec, the knob is not
    // usable, the row stays hidden, and the assertions that used to sit there were
    // skipped for every row (measured: 0 rows offered it). The plan here states the
    // container and codec MainWindow resolves for an mp4 destination, which is the
    // input this question needs.
    {
        const ctrl::ExportTarget xv = ctrl::ExportTargets::find(targets, QStringLiteral("x.video"));
        ctrl::ExportPlan forced = ctrl::planExport(xv, requestFor(model), model.qualityBucket());
        forced.config.container = QStringLiteral("mp4");
        forced.config.codec = QStringLiteral("libx264");
        model.setDestinationId(xv.id);
        model.applyDestinationDefaults(xv);
        model.applyPlan(forced, xv);

        const ctrl::QualityControl control =
            ctrl::ExportTargets::loadQualityControl().value(QStringLiteral("libx264"));
        check(!control.option.isEmpty(),
              QStringLiteral("x.video's codec declares a quality knob (%1)").arg(control.option));
        check(model.qualityOverrideVisible(),
              QStringLiteral("a codec with a knob offers its own value control"));

        // The sentinel has to be INSIDE the range. The widget form's setRange CLAMPED
        // it away, so the box always held a number, the Quality combo was disabled as
        // "not in charge", and MainWindow wrote that number into every export: the
        // intent never decided anything - in the shipping build, with the whole suite
        // green.
        check(model.qualityOverrideMinimum() == control.autoValue()
                  && model.qualityOverrideValue() == control.autoValue()
                  && model.qualityOverrideValue() <= model.qualityOverrideMaximum(),
              QStringLiteral("and it starts on Auto, the bottom of its own range "
                             "(value %1, range %2..%3, codec span %4..%5)")
                  .arg(model.qualityOverrideValue())
                  .arg(model.qualityOverrideMinimum())
                  .arg(model.qualityOverrideMaximum())
                  .arg(control.best)
                  .arg(control.worst));
        check(model.qualityOverrideAutoText() == QStringLiteral("Auto"),
              QStringLiteral("the bottom of the range reads as Auto, not as a number"));
        check(model.qualityInCharge(),
              QStringLiteral("so the Quality setting is in charge until the user says otherwise"));

        model.setQualityOverrideValue(control.autoValue() + 1);
        check(!model.qualityInCharge(),
              QStringLiteral("naming a value takes the Quality setting out of charge (%1)")
                  .arg(model.qualityOverrideValue()));
        model.setQualityOverrideToAuto();
        check(model.qualityOverrideValue() == control.autoValue() && model.qualityInCharge(),
              QStringLiteral("and its own Auto hands it back (%1)")
                  .arg(model.qualityOverrideValue()));
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
        model.setDestinationId(t.id);
        const QString id = QString::fromLatin1(e.id);
        check(model.fpsVisible() == e.fps,
              QStringLiteral("%1: frame rate %2").arg(id).arg(e.fps ? "offered" : "hidden"));
        check(model.imageQualityVisible() == e.imageQuality,
              QStringLiteral("%1: image quality %2").arg(id).arg(e.imageQuality ? "offered" : "hidden"));
        check(model.sizeEditable() == e.resolution,
              QStringLiteral("%1: resolution %2").arg(id).arg(e.resolution ? "editable" : "fixed"));
        check(model.advancedVisible() == e.advanced,
              QStringLiteral("%1: Advanced %2").arg(id).arg(e.advanced ? "offered" : "hidden"));
    }

    // ---- the frame-range rows: the trash removes the row it is on, and the last row
    // keeps its trash disabled ----------------------------------------------
    {
        model.setDestinationId(QStringLiteral("chat.discord.file"));
        const int initial = model.ranges()->count();
        check(initial >= 1, QStringLiteral("a range row is there to begin with (%1)").arg(initial));
        model.ranges()->setProjectExtent(0, 59);
        model.addRange();
        model.addRange();
        check(model.ranges()->count() == initial + 2,
              QStringLiteral("adding a range appends a row (%1)").arg(model.ranges()->count()));
        const int before = model.ranges()->ranges().at(0).last;
        // The middle row is the one that goes: the widget form removed the LAST pair
        // whatever button was pressed, which is the bug this pins.
        model.removeRange(1);
        check(model.ranges()->count() == initial + 1
                  && model.ranges()->ranges().at(0).last == before,
              QStringLiteral("the trash on row N removes row N (%1 rows left)")
                  .arg(model.ranges()->count()));
        // Down to one row: no more removals, and the control says so.
        while (model.ranges()->count() > 1)
            model.removeRange(model.ranges()->count() - 1);
        const int one = model.ranges()->count();
        model.removeRange(0);
        check(model.ranges()->count() == one && one == 1 && !model.canRemoveRange(),
              QStringLiteral("the last range cannot be removed, and its trash is disabled"));
    }

    // ---- manual mode: the format and codec become the user's, and the artifact
    // constraints apply here and only here ------------------------------------
    {
        model.setDestinationId(QString());
        check(model.manualMode() && model.formatVisible(),
              "with no destination the format IS the user's to state");
        check(!model.codecVisible() || model.codecOptions().size() > 1,
              "and the codec row is offered exactly when there is a choice to make");
        const int gifIndex = [&]() {
            const QVariantList options = model.formatOptions();
            for (int i = 0; i < options.size(); ++i) {
                if (options.at(i).toMap().value(QStringLiteral("value")).toString()
                    == QLatin1String("gif")) {
                    return i;
                }
            }
            return -1;
        }();
        check(gifIndex >= 0, "GIF is on the manual format list");
        if (gifIndex >= 0) {
            model.setFormatIndex(gifIndex);
            check(model.loop()->value() && !model.loop()->enabled() && model.loop()->visible(),
                  "a manual GIF loops and cannot be told otherwise");
            check(model.palette()->visible(), "and it has a palette of its own");
            check(!model.audio()->visible(), "and it cannot carry sound at all");
        }
        const int pngIndex = [&]() {
            const QVariantList options = model.formatOptions();
            for (int i = 0; i < options.size(); ++i) {
                if (options.at(i).toMap().value(QStringLiteral("value")).toString()
                    == QLatin1String("image/png")) {
                    return i;
                }
            }
            return -1;
        }();
        check(pngIndex >= 0, "PNG sequence is on the manual format list");
        if (pngIndex >= 0) {
            model.setFormatIndex(pngIndex);
            check(model.imageQualityVisible(), "a sequence offers QImage::save's quality");
            check(!model.fpsVisible(), "and does not ask for a frame rate");
            check(!model.codecVisible(), "and has no codec to choose");
            check(!model.loop()->value(), "and does not loop");
        }
        model.setDestinationId(QStringLiteral("chat.discord.file"));
    }

    // ---- the manual rows ARE the destination rows ----------------------------
    // Same shape, same words for the same fact. The widget form's manual branch said
    // "1100 x 790, 24 fps · 24 fps" and "VP9 in WebM · WebM" - the same fact twice in
    // one line - and printed "Audio: no · yes", the checkbox and the project
    // contradicting each other inside the two-column row.
    {
        model.setDestinationId(QString());
        const int webmIndex = [&]() {
            const QVariantList options = model.formatOptions();
            for (int i = 0; i < options.size(); ++i) {
                if (options.at(i).toMap().value(QStringLiteral("value")).toString()
                    == QLatin1String("webm")) {
                    return i;
                }
            }
            return -1;
        }();
        check(webmIndex >= 0, "WebM is on the manual format list");
        if (webmIndex >= 0) {
            model.setFormatIndex(webmIndex);
            const gui::ExportFact size = factById(model, QStringLiteral("size"));
            const gui::ExportFact format = factById(model, QStringLiteral("format"));
            const gui::ExportFact audio = factById(model, QStringLiteral("audio"));
            check(size.detail.isEmpty(),
                  "a manual export does not repeat the rate that has a row of its own");
            check(format.detail == QStringLiteral("WebM")
                      && !format.value.contains(format.detail),
                  QStringLiteral("and names the codec and its container once each (%1 · %2)")
                      .arg(format.value, format.detail));
            check(audio.detail == QStringLiteral("none in this project"),
                  "and its audio row blames the silent project, not a checkbox");
            // And picking a destination next must not inherit the manual codec's number:
            // the field's own minimum is THIS codec's Auto, so a value left over from
            // another encoder's span looks exactly like a value the user named.
            model.setDestinationId(QStringLiteral("chat.discord.file"));
            check(model.readBack().qualityOverride == -1,
                  "and the destination that follows restates quality as Auto, not as the "
                  "manual codec's leftover number");
            model.setDestinationId(QString());
        }
        model.setDestinationId(QStringLiteral("chat.discord.file"));
    }

    // ---- a row explains its answer, it does not repeat it --------------------
    // "Audio: yes · yes" was the destination path's version of the manual branch's
    // "Audio: no · yes": audioReason() fell through to repeating the plan's own answer
    // as though it were the reason for it.
    {
        gui::ExportViewModel::Init audible = initFor();
        audible.exportableAudioTracks = 1;
        audible.audio = true;
        gui::ExportViewModel withSound(audible);
        withSound.setDestinationId(QStringLiteral("chat.discord.file"));
        const gui::ExportFact audio = factById(withSound, QStringLiteral("audio"));
        check(audio.value == QStringLiteral("yes") && audio.detail.isEmpty(),
              "an audio row that carries sound states it once and gives no reason");
    }

    // The facts of a destination that only BOUNDS the size must say what the bound is,
    // and its audio line must name the destination rather than the project.
    {
        const ctrl::ExportTarget t =
            ctrl::ExportTargets::find(targets, QStringLiteral("chat.telegram.video"));
        model.setDestinationId(t.id);
        const QString text = model.factsText();
        check(text.contains(QStringLiteral("GB")),
              "a 2 GB cap is stated in GB, not as a number of bytes");
        check(factById(model, QStringLiteral("audio")).detail
                  .contains(QStringLiteral("none in this project")),
              "and its audio line blames the silent project, not Telegram");
    }

    // ---- the aspect lock writes back ONE WAY --------------------------------
    //
    // The export dialog's "Keep aspect ratio" control crashed the application. QML wrote
    // `height` ONCE; setHeight derived a width and called setWidth, setWidth derived a
    // height and called setHeight, and because the derive block runs on EVERY call - not
    // only when the value changes - the two setters alternated until the stack was gone
    // (coredump: 261291 frames, then SIGSEGV). The equality check at the top cannot stop
    // it, and the pair is not a fixed point of the arithmetic either: rounding the
    // partner up to even makes 680 -> 418 -> 680 a loop that never settles. The guard
    // therefore has to cross the recursive call, so the derived write does not derive
    // again.
    //
    // QML takes the WriteProperty route through moc, so the test writes through
    // setProperty() rather than writeSize(), using the crashed frame's own geometry
    // (native 720x1172, the pair 418/680). Contract item 36 still requires the lock to
    // WORK, so both directions are asserted, not merely that the call returns.
    {
        gui::ExportViewModel::Init native = initFor();
        native.nativeSize = QSize(720, 1172); // the crash's own geometry
        native.keepAspectRatio = true;
        gui::ExportViewModel aspect(native);
        check(aspect.keepAspectRatio() && aspect.nativeWidth() == 720 && aspect.nativeHeight() == 1172,
              "the aspect lock is on over the crash's own 720x1172 native size");

        // Editing Width must still move Height to the formula's number (item 36).
        const int width = 418;
        const int expectedHeight = evenUp((native.nativeSize.height() * width) / native.nativeSize.width());
        check(aspect.setProperty("width", width),
              "a direct width write (the WriteProperty route QML uses) returns");
        check(aspect.width() == width,
              QStringLiteral("and lands (%1)").arg(aspect.width()));
        check(aspect.height() == expectedHeight,
              QStringLiteral("with the aspect-locked height (%1, formula %2)")
                  .arg(aspect.height()).arg(expectedHeight));

        // The mirror: editing Height must still move Width.
        const int height = 680;
        const int expectedWidth = evenUp((native.nativeSize.width() * height) / native.nativeSize.height());
        check(aspect.setProperty("height", height),
              "a direct height write returns too");
        check(aspect.height() == height,
              QStringLiteral("and lands (%1)").arg(aspect.height()));
        check(aspect.width() == expectedWidth,
              QStringLiteral("with the aspect-locked width (%1, formula %2)")
                  .arg(aspect.width()).arg(expectedWidth));

        // Re-write the values already held: the VALUE does not change, but the derive
        // block used to run anyway and start the alternation over again.
        for (int i = 0; i < 3; ++i) {
            check(aspect.setProperty("width", width) && aspect.width() == width
                      && aspect.height() == expectedHeight,
                  QStringLiteral("re-writing the held width returns and stays put (%1)").arg(i));
        }
        for (int i = 0; i < 3; ++i) {
            check(aspect.setProperty("height", height) && aspect.height() == height
                      && aspect.width() == expectedWidth,
                  QStringLiteral("re-writing the held height returns and stays put (%1)").arg(i));
        }

        // The guard has to be put BACK DOWN. If a derived write leaked it (left
        // mWritingSize true), every later direct edit would take the "derived" path and
        // the aspect lock would silently stop working - a quieter, worse bug than the
        // crash. So edit again, AFTER the locked writes, and require the partner to move.
        const int later = 300;
        const int laterHeight = evenUp((native.nativeSize.height() * later) / native.nativeSize.width());
        check(aspect.setProperty("width", later) && aspect.height() == laterHeight,
              QStringLiteral("an edit after the locked ones still moves the partner, so the guard came back down (%1x%2)")
                  .arg(aspect.width()).arg(aspect.height()));

        // writeSize() runs with the guard already up, so it must land BOTH numbers
        // exactly - the native size, not an aspect partner of it. Different from the
        // value just held, so a guard left up by writeSize() would show here.
        aspect.setSizeToNative();
        check(aspect.width() == native.nativeSize.width() && aspect.height() == native.nativeSize.height(),
              QStringLiteral("and writeSize lands the native size exactly (%1x%2)")
                  .arg(aspect.width()).arg(aspect.height()));
    }

    // ---- the read-back MainWindow assembles the export from ----------------
    {
        const ctrl::ExportTarget t =
            ctrl::ExportTargets::find(targets, QStringLiteral("chat.discord.file"));
        model.setDestinationId(t.id);
        const gui::ExportViewModel::ReadBack back = model.readBack();
        check(back.destinationId == t.id && back.qualityBucket == ctrl::QualityBucket::Balanced,
              "the read-back names the destination and the quality intent");
        check(back.width > 0 && back.height > 0 && back.fps > 0,
              QStringLiteral("and carries the size and rate (%1x%2 @ %3)")
                  .arg(back.width).arg(back.height).arg(back.fps));
        check(!back.artifactId.isEmpty() && !back.artifactSuffix.isEmpty(),
              QStringLiteral("and the artifact the plan resolved (%1 -> .%2)")
                  .arg(back.artifactId, back.artifactSuffix));
        check(back.qualityOverride == -1,
              "with no named codec value, the quality intent is what decides");
        check(!back.ranges.isEmpty(), "and at least one frame range");
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
            // The FACTS are built in code (gui/ExportFacts.cpp) through
            // QCoreApplication::translate with the "MainWindow" context, and those
            // strings reach a translator only because the call is written out with its
            // context spelled literally. A file-local helper here made them invisible
            // and the box shipped half in English, so the labels a reader meets first
            // are asserted, not assumed.
            //
            // The string asserted has to be one the code STILL USES. The redesign
            // replaced the single "Codec: %1 in %2" sentence with labelled rows
            // ("Format:" plus the codec and the container), and this check went on
            // passing against the retired sentence until lupdate marked it vanished
            // and lrelease dropped it from the .qm - an obsolete source is not a
            // translation gate.
            const QString formatLabel = QCoreApplication::translate("MainWindow", "Format:");
            check(formatLabel != QStringLiteral("Format:"),
                  QStringLiteral("the facts box translates with the catalog loaded (%1)")
                      .arg(formatLabel));
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
            // The model's own tr() strings have to be looked up in the MODEL's
            // context (gui::ExportViewModel), not the base class's: a QObject without
            // Q_OBJECT files its strings under "QObject" in every catalog while the
            // dialog stays English. The committed .ts are regenerated by the
            // integrator, so this asks the CONTEXT rather than the catalog - which is
            // the part that can rot silently.
            ContextProbe probe;
            QCoreApplication::installTranslator(&probe);
            {
                gui::ExportViewModel translatedModel(initFor());
                // A language change is a retranslate: the shell forwards the event
                // (a QTranslator installed afterwards does not move anything alone).
                translatedModel.retranslateUi();
                check(translatedModel.transparency()->label()
                          == QStringLiteral("TRANSLATED-BY-CONTEXT")
                          && probe.seenContexts.contains(QStringLiteral("gui::ExportViewModel")),
                      QStringLiteral("and the model's own labels are looked up in its own "
                                     "context (%1 -> %2)")
                          .arg(QStringLiteral("Export with transparency"),
                               translatedModel.transparency()->label()));
            }
            QCoreApplication::removeTranslator(&probe);
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
