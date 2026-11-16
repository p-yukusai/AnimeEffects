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
#include <QDomDocument>
#include <QFile>
#include <QHash>
#include <QTextStream>
#include <QTranslator>

#include <algorithm>
#include <cstdint>

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

// What a dataChanged() emission ACTUALLY covered: the rows it named and the roles it
// listed. A test that reads a model's values cannot see the signal a view redraws
// from, and the F36 clamp's "the view must not show a stale field" guarantee is
// exactly about that emission - so it is observed here, not inferred from the pair of
// numbers the test also reads.
class RangeChangeSpy : public QObject {
public:
    explicit RangeChangeSpy(gui::ExportRangeModel* aModel) : QObject(aModel) {
        // The spy is the connection's CONTEXT and a child of the model, so the
        // functor dies with it: a connection left capturing a destroyed object is a
        // crash waiting for the next emission.
        QObject::connect(aModel, &QAbstractItemModel::dataChanged, this,
                         [this](const QModelIndex& aTop, const QModelIndex& aBottom,
                                const QVector<int>& aRoles) {
                             for (int row = aTop.row(); row <= aBottom.row(); ++row) {
                                 if (!mRows.contains(row))
                                     mRows.append(row);
                             }
                             if (aRoles.contains(gui::ExportRangeModel::FirstRole))
                                 mFirst = true;
                             if (aRoles.contains(gui::ExportRangeModel::LastRole))
                                 mLast = true;
                         });
    }
    bool sawRow(int aRow) const { return mRows.contains(aRow); }
    bool sawBothFields() const { return mFirst && mLast; }
private:
    QList<int> mRows;
    bool mFirst = false;
    bool mLast = false;
};

int main(int argc, char** argv) {
    qputenv("AE_DEBUG_SIZE", "1");
    QCoreApplication app(argc, argv);
    const QList<ctrl::ExportTarget> targets =
        ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH));
    check(targets.size() >= 50,
          QStringLiteral("the destination table loads (%1 rows)").arg(targets.size()));
    if (targets.isEmpty()) return 1;

    // The table's own text, keyed by id, so the byte-cap assertion in the walk below
    // compares the PARSED table against its SOURCE rather than against a number typed
    // into the test.
    QHash<QString, QString> declaredMaxBytes;
    {
        QFile table(QStringLiteral(AE_EXPORT_TARGETS_PATH));
        QDomDocument dom;
        if (table.open(QIODevice::ReadOnly) && dom.setContent(&table)) {
            const QDomElement root = dom.firstChildElement(QStringLiteral("export_targets"));
            for (QDomElement d = root.firstChildElement(QStringLiteral("destination"));
                 !d.isNull(); d = d.nextSiblingElement(QStringLiteral("destination"))) {
                const QDomElement caps = d.firstChildElement(QStringLiteral("caps"));
                if (!caps.isNull() && caps.hasAttribute(QStringLiteral("maxBytes")))
                    declaredMaxBytes.insert(d.attribute(QStringLiteral("id")),
                                            caps.attribute(QStringLiteral("maxBytes")));
            }
        }
    }

    gui::ExportViewModel model(initFor());

    int rows = 0, stills = 0, fpsIgnoredRows = 0, capped = 0, unverified = 0, unbuildable = 0;
    int maxRows = 0, sequenceRows = 0, videoRows = 0, adjacentRows = 0;
    // The ids, not only the count: the assertion below is about WHICH rows ignore the
    // frame rate, so it names them rather than leaving the reader to trust a number.
    QStringList fpsIgnoredIds;
    bool capChecked = false;

    for (const ctrl::ExportTarget& t : targets) {
        // The byte cap must survive parsing exactly as the table declares it. It is a
        // qint64 because a file limit is routinely gigabytes, and reading it with
        // toInt() silently returns 0 the moment it passes INT_MAX - dropping the cap,
        // and with it the destination's limits fact and the fit loop's only bound.
        // Checked against the table's own text for EVERY row, so this catches the class
        // rather than chat.telegram.video's instance: nine rows declare a cap above
        // INT_MAX (telegram.video 2 GB, tiktok/bilibili/douyin/dailymotion 4 GB,
        // niconico 6 GB, x.video 8 GB, weibo 15 GB, vimeo 200 GB) and every one of them
        // parsed as "no cap" before the qint64 fix.
        {
            const QString declared = declaredMaxBytes.value(t.id);
            const qint64 want = declared.isEmpty() ? 0 : declared.toLongLong();
            check(t.caps.maxBytes == want,
                  QStringLiteral("%1: the declared byte cap survives parsing (%2 -> %3)")
                      .arg(t.id,
                           declared.isEmpty() ? QStringLiteral("none") : declared)
                      .arg((qlonglong)t.caps.maxBytes));
        }

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
        if (t.fpsIgnored) {
            ++fpsIgnoredRows;
            fpsIgnoredIds.append(t.id);
        }
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
        check(model.sizeCapAuto() && model.sizeCapValue() == 0.0,
              QStringLiteral("%1: the size cap starts in Auto = no cap enforced").arg(t.id));
        if (!capChecked) {
            capChecked = true;
            // The MB field reaches the fit loop in BYTES (GeneralParams::
            // sizeCapBytes); Auto is the STATE with no cap, and the request encodes it
            // as 0 so the fit loop never runs.
            model.setSizeCap(0.25);
            check(!model.sizeCapAuto(),
                  "I43: naming a cap leaves the Auto state (0.25 MB is a cap, not a word)");
            check(model.readBack().sizeCapBytes == 262144,
                  QStringLiteral("I43: 0.25 MB reaches the fit loop as 262144 bytes (got %1)")
                      .arg((qlonglong)model.readBack().sizeCapBytes));
            // 0 is not a cap anyone means: it CHOOSES Auto, and the state says so.
            model.setSizeCap(0.0);
            check(model.sizeCapAuto() && model.readBack().sizeCapBytes == 0,
                  "I43: 0 in the cap field means Auto (no cap), not a zero-byte cap");
            model.setSizeCap(99999.0);
            check(model.sizeCapValue() == model.sizeCapMaximum(),
                  QStringLiteral("I43: the size cap clamps to its maximum (%1)")
                      .arg(model.sizeCapMaximum()));
            model.setSizeCapToAuto();
            check(model.readBack().sizeCapBytes == 0 && model.sizeCapValue() == 0.0 && model.sizeCapAuto(),
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
        // A Max rule is a CEILING on the FILE, never a bound on the two fields. The
        // fields keep the user's own resolution, so "Reset to native" and the aspect
        // lock work against the PROJECT, and the ceiling is stated in the facts
        // instead. The superseded rule clamped the fields to the ceiling, which made a
        // destination's limit look like the project's resolution: a 1080x1080 project
        // opened on chat.whatsapp.video showing the fitted 720x720, and Reset to native
        // could not land 1080 because the field clamped it back to 720.
        check(model.widthMaximum() == INT32_MAX && model.heightMaximum() == INT32_MAX,
              QStringLiteral("%1: the size fields carry no destination bound (%2x%3)")
                  .arg(t.id).arg(model.widthMaximum()).arg(model.heightMaximum()));
        if (t.size.mode == ctrl::SizeMode::Max && t.size.w > 0 && t.size.h > 0) {
            check(text.contains(QStringLiteral("max %1 x %2").arg(t.size.w).arg(t.size.h)),
                  QStringLiteral("%1: and the ceiling is stated in the facts instead "
                                 "(max %2 x %3)")
                      .arg(t.id).arg(t.size.w).arg(t.size.h));
        }
        check(model.rangeVisible() == !isStill,
              QStringLiteral("%1: the frame range is %2")
                  .arg(t.id).arg(isStill ? QStringLiteral("hidden (one image)")
                                         : QStringLiteral("offered")));
        // The disclosure may not hide a decision, and may not open onto nothing.
        // What lives inside it is what ExportDialog.qml puts in the Advanced
        // subtree: the FRAME-RANGE rows (H41, 2026-10-09), the loop flag, audio, the
        // palette, the quality override, the bitrate and the DESTINATION-mode codec
        // row. Transparency is NOT one of them any more (ruled 2026-10-03: it moved to
        // the regular surface), which is why the model's advancedVisible no longer
        // asks alpha capability.
        const bool anythingInside = model.rangeVisible()
                                    || model.loop()->visible()
                                    || model.audio()->visible()
                                    || model.palette()->visible()
                                    || model.qualityOverrideVisible()
                                    || model.bitrateVisible()
                                    || (model.codecVisible() && !model.manualMode());
        check(!anythingInside || model.advancedVisible(),
              QStringLiteral("%1: a reachable decision is never behind a hidden Advanced").arg(t.id));
        check(!model.advancedVisible() || anythingInside,
              QStringLiteral("%1: Advanced never opens onto nothing").arg(t.id));
        if (!model.qualityCustom()) ++adjacentRows;
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

        // Auto is a STATE (ruled 2026-10-08: "Auto needs to be a special value, not
        // just a visual thing"), and the range is the codec's OWN span. The widget
        // form could not hold a value outside its range, so it carved the sentinel out
        // of the bottom of the span - which meant a user could not pick the codec's
        // best value at all, and the word "Auto" was painted over a number that every
        // other consumer had to re-derive.
        check(model.qualityOverrideAuto()
                  && model.qualityOverrideMinimum() == std::min(control.best, control.worst)
                  && model.qualityOverrideMaximum() == std::max(control.best, control.worst),
              QStringLiteral("and it starts in Auto over the codec's own span "
                             "(range %1..%2, codec span %3..%4)")
                  .arg(model.qualityOverrideMinimum())
                  .arg(model.qualityOverrideMaximum())
                  .arg(control.best)
                  .arg(control.worst));
        check(model.qualityOverrideAutoText() == QStringLiteral("Auto"),
              QStringLiteral("the state is drawn as the word Auto, not as a number"));
        check(model.readBack().qualityOverride == -1,
              QStringLiteral("and Auto asks the export for no override at all"));
        check(!model.qualityCustom(),
              QStringLiteral("so the Quality combo is not reading Custom yet"));

        model.setQualityOverrideValue(std::min(control.best, control.worst));
        check(!model.qualityOverrideAuto() && model.qualityCustom(),
              QStringLiteral("naming a value leaves Auto and the combo reads Custom (%1)")
                  .arg(model.qualityOverrideValue()));
        check(model.readBack().qualityOverride == model.qualityOverrideValue(),
              QStringLiteral("and the number the user named is what the export carries (%1)")
                  .arg(model.readBack().qualityOverride));

        // Choosing an intent hands the decision back: the combo's Custom state cannot
        // outlive the choice that ended it - including the bucket that was ALREADY
        // current, because the combo read "Custom" until that moment.
        model.setQualityIndex(model.qualityIndex());
        check(!model.qualityCustom() && model.qualityOverrideAuto(),
              "choosing a Quality bucket hands the decision back (Custom ends with it)");

        model.setQualityOverrideValue(std::min(control.best, control.worst));
        model.setQualityOverrideToAuto();
        check(model.qualityOverrideAuto() && !model.qualityCustom()
                  && model.readBack().qualityOverride == -1,
              QStringLiteral("and Reset to auto hands it back too (%1)")
                  .arg(model.qualityOverrideValue()));
    }

    check(rows == targets.size(),
          QStringLiteral("every destination was walked (%1 of %2)").arg(rows).arg(targets.size()));
    check(stills > 0 && sequenceRows > 0 && videoRows > 0,
          QStringLiteral("all three artifact kinds were covered (stills %1, sequences %2, video %3)")
              .arg(stills).arg(sequenceRows).arg(videoRows));
    // TWO rows ignore the frame rate now, and the number is the table's, not a typo:
    // the export pass added generic.sequence (artifact="sequence", fps="ignored") to
    // sit alongside the older post.pixiv.ugoira. Both must be walked, and the ids the
    // walk collected are named so the claim is about which rows, not only how many.
    check(fpsIgnoredRows == 2,
          QStringLiteral("every row that ignores the frame rate was covered (%1 of 2: %2)")
              .arg(fpsIgnoredRows)
              .arg(fpsIgnoredIds.join(QStringLiteral(", "))));
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
        // A folder of numbered stills whose timing is authored elsewhere. One PNG
        // codec (no codec choice to make), the frame rate is authored on the upload
        // page, no audio, no loop, no palette, no quality knob - so every resident it
        // has is the FRAME-RANGE row, which H41 moved under Advanced on 2026-10-09.
        // It used to offer the section only because transparency lived there - since
        // 2026-10-03 the transparency row is on the regular surface - and the range
        // move reused the section: with nothing else inside, this is the row that
        // keeps it offered (without it the section is hidden and the destination's
        // ranges are unreachable).
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

    // A destination's audio line must name the destination rather than the project:
    // Telegram is not silent, this project is. (The byte cap this row declares is
    // pinned BY VALUE further down, in the Free-row block that found the truncation.
    // The old check here settled for the word "GB", which this row's own <note>
    // already contains - "2 GB per file" - so it passed with the cap read as 0 and
    // the limits row missing.)
    {
        const ctrl::ExportTarget t =
            ctrl::ExportTargets::find(targets, QStringLiteral("chat.telegram.video"));
        model.setDestinationId(t.id);
        check(factById(model, QStringLiteral("audio")).detail
                  .contains(QStringLiteral("none in this project")),
              "the destination's audio line blames the silent project, not Telegram");
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
        // The ratio the lock HOLDS is the pair the form opened on - here the project's
        // own 720x1172, because that is the pair the model was constructed with. (The
        // case where it is NOT the project's ratio is below.)
        const QSize lock(native.nativeSize);
        const int expectedHeight = evenUp((lock.height() * width) / lock.width());
        check(aspect.setProperty("width", width),
              "a direct width write (the WriteProperty route QML uses) returns");
        check(aspect.width() == width,
              QStringLiteral("and lands (%1)").arg(aspect.width()));
        check(aspect.height() == expectedHeight,
              QStringLiteral("with the aspect-locked height (%1, formula %2)")
                  .arg(aspect.height()).arg(expectedHeight));

        // The mirror: editing Height must still move Width.
        const int height = 680;
        const int expectedWidth = evenUp((lock.width() * height) / lock.height());
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
        const int laterHeight = evenUp((lock.height() * later) / lock.width());
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

        // ---- the lock holds the ratio it was SWITCHED ON with --------------------
        // Ruled 2026-10-08, verbatim: "Aspect ratio should lock aspect ratio to the one
        // used when the lock was last activated." The old derive read the PROJECT's
        // ratio instead, so a locked pair of any other shape snapped back to the
        // project's on the next edit - the report was "the resolution aspect ratio
        // linking is broken".
        {
            gui::ExportViewModel square(native);
            check(square.keepAspectRatio(),
                  "precondition: this one starts with the lock on (the saved preference)");
            square.setKeepAspectRatio(false);
            square.setProperty("width", 1080);
            square.setProperty("height", 1080);
            check(square.width() == 1080 && square.height() == 1080,
                  QStringLiteral("precondition: unlocked, the pair can be any shape (%1x%2)")
                      .arg(square.width()).arg(square.height()));

            square.setKeepAspectRatio(true);
            square.setProperty("height", 540);
            check(square.width() == 540,
                  QStringLiteral("switching the lock ON captures THAT pair, so the next edit "
                                 "holds 1:1 (%1x%2)")
                      .arg(square.width()).arg(square.height()));
            square.setProperty("width", 800);
            check(square.height() == 800,
                  QStringLiteral("and the captured ratio survives editing the other field "
                                 "(%1x%2)")
                      .arg(square.width()).arg(square.height()));

            // Reset ignores the lock and RE-BASES it: "Reset should ignore aspect ratio
            // lock and set a new aspect ratio for it if the lock is enabled."
            square.setSizeToNative();
            check(square.width() == native.nativeSize.width()
                      && square.height() == native.nativeSize.height(),
                  QStringLiteral("Reset lands the project's size exactly, whatever the lock held (%1x%2)")
                      .arg(square.width()).arg(square.height()));
            square.setProperty("width", 360);
            check(square.height() == evenUp((native.nativeSize.height() * 360) / native.nativeSize.width()),
                  QStringLiteral("and from then on the lock holds the ratio Reset landed on, "
                                 "not the 1:1 it held before (%1x%2)")
                      .arg(square.width()).arg(square.height()));
        }

        // A lock that starts ON (the saved preference) holds the pair the form OPENED
        // on - the remembered size, not the project's.
        {
            gui::ExportViewModel::Init remembered = native;
            remembered.width = 1000;
            remembered.height = 1000;
            remembered.keepAspectRatio = true;
            gui::ExportViewModel opened(remembered);
            check(opened.width() == 1000 && opened.height() == 1000,
                  "precondition: the form opens on the remembered pair");
            opened.setProperty("height", 500);
            check(opened.width() == 500,
                  QStringLiteral("and its lock holds that pair, not the project's (%1x%2)")
                      .arg(opened.width()).arg(opened.height()));
            // The project's ratio is what a lock that was OFF captures only when it is
            // switched on over a pair that IS the project's.
            opened.setKeepAspectRatio(false);
            opened.setSizeToNative();
            opened.setKeepAspectRatio(true);
            opened.setProperty("height", 586);
            check(opened.width() == evenUp((native.nativeSize.width() * 586) / native.nativeSize.height()),
                  QStringLiteral("while a lock switched on over the project's own pair holds "
                                 "the project's ratio (%1x%2)")
                      .arg(opened.width()).arg(opened.height()));
        }
    }

    // ---- a Max rule is a CEILING on the FILE, not a decision about the boxes ----
    //
    // The user's report, verbatim: "It should default to project resolution unless a
    // destination enforces a strict resolution limit." chat.whatsapp.video is a
    // surviving ceiling row whose rule is <size mode="max" w="1280" h="720"/> - a
    // ceiling, not a size - so a project LARGER than 1280x720 must still open at its
    // own resolution, with the export fitted down.
    // The superseded code wrote the PLAN's fitted size into the boxes and carried the
    // ceiling as their maximum, so a 1080x1080 project opened showing 720x720 (a
    // 1920x1080 project showing 1280x720) and "Reset to native" could not land the
    // native size: the field clamped it back to the ceiling. Both fail below.
    {
        const ctrl::ExportTarget ceiling =
            ctrl::ExportTargets::find(targets, QStringLiteral("chat.whatsapp.video"));
        check(ceiling.size.mode == ctrl::SizeMode::Max && ceiling.size.w == 1280
                  && ceiling.size.h == 720,
              QStringLiteral("chat.whatsapp.video is the 1280x720 ceiling this rule is about "
                             "(%1x%2)")
                  .arg(ceiling.size.w).arg(ceiling.size.h));

        const QSize natives[] = { QSize(1080, 1080), QSize(1920, 1080) };
        for (const QSize& native : natives) {
            gui::ExportViewModel::Init init = initFor();
            init.nativeSize = native;
            gui::ExportViewModel m(init);
            m.setDestinationId(ceiling.id);

            // The default is the PROJECT's resolution, not the ceiling's fit. Fails on
            // the old code, which showed the fitted plan size here.
            check(m.width() == native.width() && m.height() == native.height(),
                  QStringLiteral("a %1x%2 project over a 1280x720 ceiling opens at its own "
                                 "resolution (%3x%4): the ceiling bounds the FILE, not the "
                                 "boxes - the superseded code showed the fitted size")
                      .arg(native.width()).arg(native.height()).arg(m.width()).arg(m.height()));

            // ... while the FILE is still capped: the boxes are the request, the facts
            // are the file. Asserting the plan too, so the facts check below cannot pass
            // by stating the request.
            const ctrl::ExportPlan plan = m.lastPlan();
            check(plan.size.width() <= ceiling.size.w && plan.size.height() <= ceiling.size.h
                      && plan.size != native,
                  QStringLiteral("a %1x%2 project's plan still fits the ceiling (%3x%4)")
                      .arg(native.width()).arg(native.height())
                      .arg(plan.size.width()).arg(plan.size.height()));
            check(factById(m, QStringLiteral("size")).value.contains(
                      QStringLiteral("%1 x %2").arg(plan.size.width()).arg(plan.size.height()))
                      && !(m.width() == plan.size.width() && m.height() == plan.size.height()),
                  QStringLiteral("a %1x%2 project states the capped output size (%3x%4) in its "
                                 "facts while the boxes hold the request (%5x%6) - the plan is "
                                 "the file, the boxes are the request, and the superseded code "
                                 "made them the same")
                      .arg(native.width()).arg(native.height())
                      .arg(plan.size.width()).arg(plan.size.height())
                      .arg(m.width()).arg(m.height()));

            // Reset to native lands the project's size EXACTLY. Fails on the old code:
            // the ceiling was the field's maximum and clamped the write.
            m.setSizeToNative();
            check(m.width() == native.width() && m.height() == native.height(),
                  QStringLiteral("a %1x%2 project's Reset to native lands the native size "
                                 "exactly over a ceiling (%3x%4)")
                      .arg(native.width()).arg(native.height()).arg(m.width()).arg(m.height()));
        }
    }

    // ---- and the aspect lock keeps the PROJECT's ratio, not the ceiling's --------
    //
    // Contract item 36: with "Keep aspect ratio" on, editing Width derives Height from
    // the PROJECT's ratio. The superseded code clamped that derived partner against the
    // destination ceiling, so a 720x1172 project at 800 wide came back 800x720 - the
    // ceiling's ratio, not the project's. Fails on the old code.
    {
        const ctrl::ExportTarget ceiling =
            ctrl::ExportTargets::find(targets, QStringLiteral("chat.whatsapp.video"));
        gui::ExportViewModel::Init init = initFor();
        init.nativeSize = QSize(720, 1172);
        init.keepAspectRatio = true;
        gui::ExportViewModel m(init);
        m.setDestinationId(ceiling.id);
        check(m.keepAspectRatio() && m.nativeWidth() == 720 && m.nativeHeight() == 1172,
              "the aspect lock is on over a 720x1172 project under the 1280x720 ceiling");

        const int width = 800;
        const int expectedHeight =
            evenUp((init.nativeSize.height() * width) / init.nativeSize.width());
        check(m.setProperty("width", width), "a direct width write returns under the ceiling");
        check(m.height() == expectedHeight && expectedHeight > ceiling.size.h,
              QStringLiteral("and the locked height is the PROJECT's ratio (%1, past the 720 "
                             "ceiling) - a ceiling bounds the file, not the box")
                  .arg(m.height()));
    }

    // ---- a dictated size still reports the plan's size --------------------------
    //
    // MaxExactSide and Exact PIN the size, so the boxes must go on reporting exactly
    // what the plan resolved and stop accepting edits; only the BOUNDING modes (Free,
    // Max) hand the boxes back to the user. Unchanged by the ceiling fix, asserted so
    // that fix cannot leak into the pinned modes.
    {
        const ctrl::ExportTarget pinned[] = {
            ctrl::ExportTargets::find(targets, QStringLiteral("telegram.sticker")),
            ctrl::ExportTargets::find(targets, QStringLiteral("telegram.emoji")),
        };
        check(pinned[0].size.mode == ctrl::SizeMode::MaxExactSide && pinned[0].size.side == 512,
              QStringLiteral("telegram.sticker pins the long side at 512 (%1)")
                  .arg(pinned[0].size.side));
        check(pinned[1].size.mode == ctrl::SizeMode::Exact && pinned[1].size.w == 100
                  && pinned[1].size.h == 100,
              QStringLiteral("telegram.emoji pins 100x100 (%1x%2)")
                  .arg(pinned[1].size.w).arg(pinned[1].size.h));

        for (const ctrl::ExportTarget& t : pinned) {
            gui::ExportViewModel::Init init = initFor();
            init.nativeSize = QSize(1080, 1080);
            gui::ExportViewModel m(init);
            m.setDestinationId(t.id);
            const ctrl::ExportPlan plan = m.lastPlan();
            check(plan.ok(),
                  QStringLiteral("%1: the plan resolves on this build (%2)")
                      .arg(t.id, plan.error));
            check(!m.sizeEditable(),
                  QStringLiteral("%1: a dictated size is still not editable").arg(t.id));
            check(m.width() == plan.size.width() && m.height() == plan.size.height()
                      && !(m.width() == 1080 && m.height() == 1080),
                  QStringLiteral("%1: the boxes still report the plan's size (%2x%3), not the "
                                 "1080x1080 project's")
                      .arg(t.id).arg(m.width()).arg(m.height()));
        }
    }

    // ---- the resolution ceiling is STATED, or the form hides a destination rule -
    {
        gui::ExportViewModel::Init init = initFor();
        gui::ExportViewModel m(init);
        const ctrl::ExportTarget ceiling =
            ctrl::ExportTargets::find(targets, QStringLiteral("chat.whatsapp.video"));
        m.setDestinationId(ceiling.id);
        check(factById(m, QStringLiteral("limits")).value.contains(
                  QStringLiteral("max 1280 x 720")),
              QStringLiteral("a ceiling row states its resolution limit (%1)")
                  .arg(factById(m, QStringLiteral("limits")).value));

        const ctrl::ExportTarget free =
            ctrl::ExportTargets::find(targets, QStringLiteral("chat.telegram.video"));
        check(free.size.mode == ctrl::SizeMode::Free,
              "chat.telegram.video imposes no size, only a byte cap");
        // The cap is 2^31 exactly - one past INT_MAX - so its VALUE is what proves the
        // byte cap is read as a qint64: read as an int it fails and returns 0, the
        // limits row disappears and the fit loop loses its only bound. Pinned against
        // the number, not the word "GB" (which the row's <note> already supplies).
        check(free.caps.maxBytes == 2147483648LL,
              QStringLiteral("chat.telegram.video's 2 GB cap survives parsing as a qint64 (%1)")
                  .arg((qlonglong)free.caps.maxBytes));
        m.setDestinationId(free.id);
        const gui::ExportFact freeLimits = factById(m, QStringLiteral("limits"));
        check(!freeLimits.value.contains(QStringLiteral(" x "))
                  && freeLimits.value.contains(QStringLiteral("2 GB")),
              QStringLiteral("a Free row states its byte cap (2 GB) and no resolution limit (%1)")
                  .arg(freeLimits.value));
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

    // ---- H42: the destination-mode plan is asked about the USER'S answer ----
    //
    // The HIGH the review recorded: `buildRequest()` never wrote
    // `allowTransparency`, so the destination-mode request kept its own `true`
    // default (`ctrl/ExportTarget.h:349`) and the row on screen reached only
    // `readBack()`. With the default standing, a `choice` alpha row's own resolution
    // and the archive ladder's alpha-OFF branch are both unreachable from the app,
    // and a box the user unticked is a control that lies.
    //
    // The assertion is that the REQUEST carries the row, in both states: the answer
    // that used to arrive as the default's own value is the half that passed before
    // the field was wired, so pinning only one state would prove nothing. Fails on
    // the tree where buildRequest() leaves the field to its default.
    {
        const ctrl::ExportTarget t =
            ctrl::ExportTargets::find(targets, QStringLiteral("twitch.emote"));
        check(t.alpha == ctrl::Capability::Choice,
              QStringLiteral("precondition: %1 OFFERS the transparency choice rather than "
                             "deciding it").arg(t.id));
        model.setDestinationId(t.id);
        model.transparency()->setValue(false);
        check(requestFor(model).allowTransparency == false,
              "H42: the destination-mode request says transparency OFF when the row is off");
        model.transparency()->setValue(true);
        check(requestFor(model).allowTransparency == true,
              "H42: and ON when the row is on (the state the request's default already "
              "carried, which is why both are asserted)");
    }

    // ---- F36: the frame-range fields clamp against each other ---------------
    //
    // Contract item 36 (`docs/export-dialog-behavior.md:218`): "the frame-range
    // fields clamp against each other so a range can never invert." Both fields were
    // independent full int32 on the row (AeFrameRangeRow) and the model
    // bound-checked the INDEX only, so a user could set first=100 on a [10..50]
    // range and produce an inverted pair that only the backend noticed (it swaps the
    // pair with a warning, `ctrl/ExportParams.h:229-238`). The form-level guarantee
    // is what is pinned here, and it fails on the tree that took the number as typed.
    {
        gui::ExportRangeModel* ranges = model.ranges();
        // A project of 0..200 holding one remembered range of 10..50: an edit of
        // first to 100 is a real frame of the project and past the row's last, which
        // is the case that inverts.
        ranges->setProjectExtent(0, 200);
        ranges->resetRanges({ctrl::FrameRange{10, 50}});
        check(ranges->ranges().size() == 1 && ranges->ranges().at(0).first == 10
                  && ranges->ranges().at(0).last == 50,
              "precondition: one remembered range of [10..50] inside a 0..200 project");

        RangeChangeSpy spy(ranges);
        ranges->setRangeFirst(0, 100);
        const ctrl::FrameRange dragged = ranges->ranges().at(0);
        check(dragged.first == 100 && dragged.last == 100,
              QStringLiteral("F36: first=100 on a [10..50] range carries the last along, so "
                             "the pair reads %1..%2 and cannot invert")
                  .arg(dragged.first).arg(dragged.last));
        check(spy.sawRow(0) && spy.sawBothFields(),
              "F36: and dataChanged names the row with BOTH fields, so the view redraws "
              "the neighbour it was not told about");

        // The mirror: an edit of last below the row's first drags first down.
        ranges->setRangeLast(0, 3);
        const ctrl::FrameRange mirrored = ranges->ranges().at(0);
        check(mirrored.first == 3 && mirrored.last == 3,
              QStringLiteral("F36: last=3 on a [100..100] range carries the first along "
                             "(%1..%2)")
                  .arg(mirrored.first).arg(mirrored.last));

        // Each field is also clamped into the PROJECT's own extent, so no frame
        // outside the animation can be named: 100 and 3 are outside a 10..50 project,
        // and the extent wins over both the edit and the neighbour.
        ranges->setProjectExtent(10, 50);
        ranges->resetRanges({ctrl::FrameRange{10, 50}});
        ranges->setRangeFirst(0, 100);
        const ctrl::FrameRange high = ranges->ranges().at(0);
        check(high.first <= high.last && high.last == 50,
              QStringLiteral("F36: first=100 on a [10..50] range in a 10..50 project clamps "
                             "to the project's own last (%1..%2), the pair still non-inverted")
                  .arg(high.first).arg(high.last));

        // A project that does not start at 0: its own first is the floor for BOTH
        // fields, and the old code let a field keep a number below it.
        ranges->setRangeLast(0, 3);
        const ctrl::FrameRange lowLast = ranges->ranges().at(0);
        check(lowLast.last == 10 && lowLast.first == 10,
              QStringLiteral("F36: a below-minimum last clamps to the project's own first "
                             "(%1..%2)")
                  .arg(lowLast.first).arg(lowLast.last));
        ranges->setRangeFirst(0, -40);
        const ctrl::FrameRange lowFirst = ranges->ranges().at(0);
        check(lowFirst.first == 10,
              QStringLiteral("F36: and so does a below-minimum first (%1..%2)")
                  .arg(lowFirst.first).arg(lowFirst.last));

        // An out-of-range INDEX is still a no-op rather than a crash, and a write of
        // the value already held moves nothing.
        ranges->setRangeFirst(7, 20);
        check(ranges->ranges().at(0).first == 10,
              "F36: an index that is not there is still a no-op");
        ranges->setRangeFirst(0, 10);
        check(ranges->ranges().at(0).first == 10 && ranges->ranges().at(0).last == 10,
              "F36: re-writing the held value leaves the pair alone");
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
            // archive.master stands in as the translated sample: its label is an
            // ordinary translated name ("Archivo (máxima calidad)"), whereas the row
            // this used to name (chat.generic.video) was removed and the trio that
            // replaced it carries NEW English-only labels.
            const ctrl::ExportTarget esArchive =
                ctrl::ExportTargets::find(translated, QStringLiteral("archive.master"));
            const ctrl::ExportTarget enArchive =
                ctrl::ExportTargets::find(targets, QStringLiteral("archive.master"));
            check(!esArchive.label.isEmpty() && esArchive.label != enArchive.label,
                  QStringLiteral("a destination name arrives translated (%1 -> %2)")
                      .arg(enArchive.label, esArchive.label));
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
            // The table's own translation coverage, read off the LOADED table so the
            // number cannot go stale. On the table this landed with it is 37 of 66:
            // the three removed general rows each had a Spanish label ("Un vídeo para
            // una app de chat", "Sitio web", "Solo un archivo (todavía no lo sé)") and
            // their three replacements ("Video", "Animated image", "Image sequence")
            // are NEW English source strings the catalog does not carry, so the trio
            // neither translates nor counts (three fewer than the 40 the old table
            // yielded). The eight renamed platform labels changed spelling only and
            // were brand words whose Spanish was already the same word, so they move
            // the count by nothing. Brands still do not translate.
            int translatedLabels = 0;
            for (const ctrl::ExportTarget& t : translated) {
                const ctrl::ExportTarget english = ctrl::ExportTargets::find(targets, t.id);
                if (t.label != english.label) ++translatedLabels;
            }
            check(translatedLabels > 20,
                  QStringLiteral("most names are translated, brands are not (%1 of %2 differ; "
                                 "the three new general labels are new English strings and the "
                                 "three removed rows' translations are gone) - the old table's "
                                 "40 was the same read before that swap")
                      .arg(translatedLabels).arg(translated.size()));
            QCoreApplication::removeTranslator(&translator);
        }
    }

    QTextStream(stdout) << (failures == 0
                                ? "=== export_form: OK ===\n"
                                : QStringLiteral("=== export_form: %1 FAILED ===\n").arg(failures));
    return failures == 0 ? 0 : 1;
}
