// The QML tools panel's MODEL contract: the tool registry, every tool's parameters,
// which rows each tool shows, the mask targets, the view settings and the signals the
// host acts on.
//
// The dock itself is verified live and by tests/tools_panel.cpp (the scene); this gate
// keeps the DECISIONS from depending on whether a delegate happened to load. It is the
// tools side of tests/inspector_view_model.cpp, and like that one it needs no widget,
// no project and no GL context.
//
// WHAT IS WORTH PINNING HERE, and why each one is a behaviour rather than a tautology:
//   * the ROW VISIBILITY per mode - the widget panels each re-derived it in their own
//     updateTypeParam, so "the FFD mesh brush hides Opacity", "the pose eraser shows a
//     brush" and "the SR centroid mode swaps three checkboxes for one" were three
//     separate code paths that could (and did) drift;
//   * the brush RETARGETING - one object points at the active tool's copy, and a step
//     taken on the bone brush must not move the pose brush;
//   * the mask's target/channel SEEDING, which is ctrl::MaskParam's own rule (an amount
//     mask starts at 0, a time offset at 1, a matte at 0) and the one thing that makes a
//     fresh mask stroke useful;
//   * the SIGNAL ORDER the host depends on: toolChanged carries the OLD tool because the
//     host finalizes it before Driver::setTool destroys the editor.
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/tool_view_model
#include <QCoreApplication>
#include <QTextStream>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>

#include "gui/MainViewSetting.h"
#include "gui/qml/ToolViewModel.h"

namespace {

int failures = 0;

void check(bool aOk, const QString& aWhat) {
    QTextStream(stdout) << (aOk ? "PASS  " : "FAIL  ") << aWhat << "\n";
    if (!aOk)
        ++failures;
}

QString describe(const QVariantList& aList) {
    QStringList out;
    for (const QVariant& value : aList)
        out << value.toMap().value(QStringLiteral("label")).toString();
    return out.join(QStringLiteral(", "));
}

bool near(double aLhs, double aRhs) { return std::fabs(aLhs - aRhs) < 1.0e-5; }

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    gui::MainViewSetting view;
    gui::ToolViewModel model;
    model.setViewSetting(&view);

    // ---- signal bookkeeping ----------------------------------------------------
    int toolChangedCount = 0;
    int lastOld = -1;
    int lastNew = -1;
    int changedCount = 0;
    int stateCount = 0;
    int modeCount = 0;
    int viewCount = 0;
    int viewSettingsCount = 0;
    int clearCount = 0;
    int driverCount = 0;
    QObject::connect(&model, &gui::ToolViewModel::toolChanged, [&](int aOld, int aNew) {
        ++toolChangedCount;
        lastOld = aOld;
        lastNew = aNew;
    });
    QObject::connect(&model, &gui::ToolViewModel::changed, [&]() { ++changedCount; });
    QObject::connect(&model, &gui::ToolViewModel::stateChanged, [&]() { ++stateCount; });
    QObject::connect(&model, &gui::ToolViewModel::modeChanged, [&]() { ++modeCount; });
    QObject::connect(&model, &gui::ToolViewModel::viewChanged, [&]() { ++viewCount; });
    QObject::connect(&model, &gui::ToolViewModel::viewSettingsChanged, [&]() { ++viewSettingsCount; });
    QObject::connect(&model, &gui::ToolViewModel::clearMaskRequested, [&]() { ++clearCount; });
    QObject::connect(&model, &gui::ToolViewModel::driverActiveChanged, [&]() { ++driverCount; });

    // ---- the tool registry -----------------------------------------------------
    const QVariantList tools = model.tools();
    check(tools.size() == 7, QStringLiteral("the toolbox has seven tools (%1)").arg(tools.size()));
    for (int i = 0; i < tools.size(); ++i) {
        const QVariantMap entry = tools.at(i).toMap();
        check(entry.value(QStringLiteral("type")).toInt() == i,
              QStringLiteral("tool %1 is ctrl::ToolType %1 in the dock's order").arg(i));
        check(!entry.value(QStringLiteral("icon")).toString().isEmpty(),
              QStringLiteral("tool %1 has an icon").arg(i));
        check(!entry.value(QStringLiteral("name")).toString().isEmpty(),
              QStringLiteral("tool %1 has a name").arg(i));
    }
    check(tools.at(6).toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Mask paint"),
          QStringLiteral("the seventh tool is the mask paint tool"));

    // ---- no tool yet -----------------------------------------------------------
    check(model.tool() == int(ctrl::ToolType_TERM), QStringLiteral("the dock opens with no tool"));
    check(!model.panelVisible(), QStringLiteral("and no tool panel"));
    check(model.title().isEmpty(), QStringLiteral("the heading is empty with no tool"));
    check(model.modes().isEmpty(), QStringLiteral("and there are no modes"));
    check(model.modeIndex() == -1, QStringLiteral("modeIndex reports -1 with no tool"));
    check(!model.driverActive(), QStringLiteral("no driver yet, so the toolbox is disabled"));
    model.setDriverActive(true);
    model.setDriverActive(true);
    check(driverCount == 1, QStringLiteral("driverActive notifies ONCE for a repeated value (%1)").arg(driverCount));

    // ---- SR transform ----------------------------------------------------------
    model.activate(int(ctrl::ToolType_SRT));
    check(toolChangedCount == 1 && lastOld == int(ctrl::ToolType_TERM)
              && lastNew == int(ctrl::ToolType_SRT),
          QStringLiteral("activate reports (old, new) = (no tool, SRT)"));
    check(stateCount >= 1, QStringLiteral("and a structural change"));
    check(changedCount == 0, QStringLiteral("activating a tool is not a PARAM change"));
    check(model.title() == QStringLiteral("Transform"), QStringLiteral("the heading names the tool"));
    check(describe(model.modes()) == QStringLiteral("Transform, Move Centroid"),
          QStringLiteral("SRT's two modes: %1").arg(describe(model.modes())));
    check(model.modeIndex() == 0, QStringLiteral("it opens in Transform mode"));
    check(model.modeName() == QStringLiteral("Transform"), QStringLiteral("and the mode caption agrees"));
    check(model.srtCreateVisible() && !model.srtCentroidVisible(),
          QStringLiteral("Transform mode shows the key-creation toggles"));

    const QVariantList checks = model.srtChecks();
    check(checks.size() == 4, QStringLiteral("four toggle rows (%1)").arg(checks.size()));
    check(checks.at(0).toMap().value(QStringLiteral("label")).toString() == QStringLiteral("Create move key"),
          QStringLiteral("the first toggle is Create move key"));
    check(checks.at(3).toMap().value(QStringLiteral("label")).toString() == QStringLiteral("Adjust centroid position"),
          QStringLiteral("the fourth is Adjust centroid position"));
    check(checks.at(3).toMap().value(QStringLiteral("key")).toString() == QStringLiteral("centroid"),
          QStringLiteral("and it is the CENTROID family"));
    check(!checks.at(0).toMap().value(QStringLiteral("tip")).toString().isEmpty(),
          QStringLiteral("every toggle carries its tooltip"));
    QVariantList values = model.srtCheckValues();
    check(values.size() == 4 && !values.at(0).toBool() && !values.at(1).toBool()
              && !values.at(2).toBool() && values.at(3).toBool(),
          QStringLiteral("the defaults are the three unticked key toggles and the ticked centroid offset"));

    const int beforeToggle = changedCount;
    model.setSrtCheck(0, true);
    values = model.srtCheckValues();
    check(values.at(0).toBool() && model.srtParam().necessarilyMove,
          QStringLiteral("ticking a row writes the param"));
    check(changedCount == beforeToggle + 1, QStringLiteral("and notifies once"));
    model.setSrtCheck(0, true);
    check(changedCount == beforeToggle + 1, QStringLiteral("writing the same value notifies nothing"));
    model.setSrtCheck(3, false);
    check(!model.srtParam().adjustPosition, QStringLiteral("only Create move key is on"));

    model.setMode(1);
    check(model.modeIndex() == 1 && model.modeName() == QStringLiteral("Move Centroid"),
          QStringLiteral("the mode switches to Move Centroid"));
    check(model.srtCentroidVisible() && !model.srtCreateVisible(),
          QStringLiteral("and the row set follows the mode"));
    check(model.srtParam().mode == 1, QStringLiteral("the param carries the mode"));
    check(modeCount >= 1, QStringLiteral("a mode change notifies"));
    model.setMode(1);
    check(model.srtParam().mode == 1, QStringLiteral("re-selecting the same mode is harmless"));

    // ---- bone ------------------------------------------------------------------
    model.activate(int(ctrl::ToolType_Bone));
    check(model.title() == QStringLiteral("Bone editor"), QStringLiteral("bone: heading"));
    check(model.modes().size() == 7, QStringLiteral("bone: seven modes (%1)").arg(model.modes().size()));
    check(model.modeIndex() == int(ctrl::BoneEditMode_Create), QStringLiteral("bone: opens on Add bones"));
    check(!model.brushVisible(), QStringLiteral("bone: Add bones has no brush"));
    check(!model.blurVisible() && !model.weightVisible(), QStringLiteral("bone: no blur and no weight"));
    check(near(model.brush()->radius(), 50.0), QStringLiteral("bone: the brush points at the BONE copy (50)"));

    model.setMode(int(ctrl::BoneEditMode_PaintInfl));
    check(model.brushVisible(), QStringLiteral("bone: Paint influence shows the brush"));
    check(model.modeName() == QStringLiteral("Paint influence"), QStringLiteral("bone: the caption names it"));
    // NOT shown: nothing in ctrl/bone reads BrushParam::opacity (the influence weight is
    // `stamp.amount * falloff` alone), so the row would do nothing.
    check(!model.opacityVisible(), QStringLiteral("bone: no Opacity row (the influence brush ignores it)"));
    model.brush()->setRadius(123.0);
    model.brush()->setHardness(0.6);
    check(near(model.boneParam().brush.radius, 123.0), QStringLiteral("bone: the write lands on the bone param"));
    check(near(model.poseParam().brush.radius, 200.0),
          QStringLiteral("bone: and the pose brush is untouched (%1)").arg(model.poseParam().brush.radius));

    // the [ ] notches, on the ACTIVE tool's brush
    model.adjustBrush(+1, -1);
    check(near(model.brush()->radius(), 123.0 * 1.1),
          QStringLiteral("one size notch is x1.1 (%1)").arg(model.brush()->radius()));

    // THE DRAG DIAL'S LAW, pinned at both ends of the range. One step is 8 screen px of hand travel
    // and moves sqrt(radius) by 0.25, so the SAME travel buys +11.5 % at radius 20 and +2.5 % at
    // 400 - the equal-effort property the hand chose this law for.
    {
        const auto sqrtLaw = [](double aRadius) {
            const double root = std::sqrt(aRadius) + 0.25;   // kSqrtPerStep
            return root * root;
        };
        model.brush()->setRadius(20.0);
        model.brush()->nudge(1.0, 0.0);
        check(near(model.brush()->radius(), sqrtLaw(20.0)),
              QStringLiteral("the dial's sqrt law at 20: %1 (want %2)")
                  .arg(model.brush()->radius()).arg(sqrtLaw(20.0)));
        model.brush()->setRadius(400.0);
        model.brush()->nudge(1.0, 0.0);
        check(near(model.brush()->radius(), sqrtLaw(400.0)),
              QStringLiteral("and at 400: %1 (want %2)")
                  .arg(model.brush()->radius()).arg(sqrtLaw(400.0)));
    }
    check(near(model.brush()->hardness(), 0.55), QStringLiteral("one hardness notch is 5 %% (%1)")
                                                    .arg(model.brush()->hardness()));

    // ---- pose ------------------------------------------------------------------
    model.activate(int(ctrl::ToolType_Pose));
    check(model.title() == QStringLiteral("Pose editor"), QStringLiteral("pose: heading"));
    check(model.modes().size() == 3, QStringLiteral("pose: three modes"));
    check(!model.weightVisible() && !model.brushVisible(),
          QStringLiteral("pose: Move bone shows neither the weight nor the brush"));
    model.setMode(int(ctrl::PoseEditMode_Draw));
    check(model.weightVisible() && !model.brushVisible(),
          QStringLiteral("pose: Pull bones shows the WEIGHT only"));
    check(model.weightLabel() == QStringLiteral("Weight"), QStringLiteral("pose: the row is named Weight"));
    model.setWeight(0.4);
    check(near(model.poseParam().diWeight, 0.4), QStringLiteral("pose: the weight writes the param"));
    model.setWeight(4.0);
    check(near(model.poseParam().diWeight, 1.0), QStringLiteral("pose: and it clamps at 1"));
    model.setMode(int(ctrl::PoseEditMode_Erase));
    check(model.brushVisible() && !model.weightVisible(),
          QStringLiteral("pose: Erase shows the BRUSH only"));
    check(near(model.poseParam().brush.radius, 200.0), QStringLiteral("pose: still its own brush"));
    check(!model.opacityVisible(), QStringLiteral("pose: no Opacity row either (the erase brush ignores it)"));

    // ---- mesh ------------------------------------------------------------------
    model.activate(int(ctrl::ToolType_Mesh));
    check(model.title() == QStringLiteral("Mesh editor") && model.modes().size() == 3,
          QStringLiteral("mesh: heading and three modes"));
    check(!model.brushVisible() && !model.blurVisible() && !model.weightVisible(),
          QStringLiteral("mesh: no parameter rows at all"));
    model.setMode(2);
    check(model.meshParam().mode == 2, QStringLiteral("mesh: the mode writes the param"));

    // ---- FFD -------------------------------------------------------------------
    model.activate(int(ctrl::ToolType_FFD));
    check(model.title() == QStringLiteral("FFD"), QStringLiteral("ffd: heading"));
    check(model.modeIndex() == int(ctrl::FFDParam::Type_Pencil), QStringLiteral("ffd: opens on Deform mesh"));
    check(model.brushVisible() && model.blurVisible(), QStringLiteral("ffd: pencil shows the brush and the blur"));
    check(!model.opacityVisible(),
          QStringLiteral("ffd: the mesh brush has no per-stroke cap, so Opacity is hidden"));
    check(near(model.brush()->radius(), 100.0), QStringLiteral("ffd: its own brush default (100)"));
    model.setMode(int(ctrl::FFDParam::Type_Eraser));
    check(model.ffdParam().type == ctrl::FFDParam::Type_Eraser, QStringLiteral("ffd: eraser mode"));
    check(model.ffdParam().brush.erase, QStringLiteral("ffd: and brush.erase follows the mode"));
    check(model.brushVisible() && !model.blurVisible(),
          QStringLiteral("ffd: the eraser shows the brush and no blur"));
    model.setMode(int(ctrl::FFDParam::Type_Drag));
    check(!model.brushVisible() && !model.blurVisible(),
          QStringLiteral("ffd: Move vertex shows neither"));
    model.setMode(int(ctrl::FFDParam::Type_Pencil));
    check(!model.ffdParam().brush.erase, QStringLiteral("ffd: coming back to the pencil clears erase"));
    model.setBlur(0.35);
    check(near(model.ffdParam().blur, 0.35), QStringLiteral("ffd: the blur writes the param"));
    check(model.blurLabel() == QStringLiteral("Blur"), QStringLiteral("ffd: the row is named Blur"));

    // ---- mask ------------------------------------------------------------------
    model.activate(int(ctrl::ToolType_Mask));
    check(model.title() == QStringLiteral("Filter mask"), QStringLiteral("mask: opens on a filter mask"));
    // ONE field chooser: the six filter inputs and the matte, named with a translatable
    // format ("<filter> amount"), never two selectors or a separator-joined label.
    check(describe(model.maskFields())
              == QStringLiteral("HSV amount, HSV time offset, Blur amount, Blur time offset, "
                                "Wave Deform amount, Wave Deform time offset, Layer mask"),
          QStringLiteral("mask: seven addressable fields: %1").arg(describe(model.maskFields())));
    check(model.maskFieldIndex() == 4, QStringLiteral("mask: the default field is Wave Deform amount (%1)")
                                           .arg(model.maskFieldIndex()));
    check(model.maskFieldLabel() == QStringLiteral("Wave Deform amount"),
          QStringLiteral("mask: and it names itself: %1").arg(model.maskFieldLabel()));
    check(!model.maskPinned(), QStringLiteral("mask: picked from the toolbox, the field is a QUESTION"));
    // MaskEditor.cpp:319 DOES read brush.opacity (`amount * opacity`), so this row is
    // reachable and must be offered - it used to be hidden while working.
    check(model.brushVisible() && model.opacityVisible(),
          QStringLiteral("mask: the brush is up WITH its Opacity row (MaskEditor reads it)"));

    model.setMaskValue(0.42);
    check(near(model.maskParam().value, 0.42), QStringLiteral("mask: the value writes the param"));
    check(model.maskParam().kind == ctrl::MaskTargetKind_FilterInput
              && model.maskParam().lane == core::TimeKeyType_Wave,
          QStringLiteral("mask: still Wave, still a filter input"));

    // the SAME list addresses a channel change: index 5 is Wave Deform's time offset
    model.setMaskField(5);
    check(model.maskParam().channel == core::FilterMaskChannel_TimeOffset,
          QStringLiteral("mask: field 5 is a channel change"));
    check(near(model.maskParam().value, 1.0),
          QStringLiteral("mask: a time offset mask starts at 1 (add the delay), not 0 (%1)")
              .arg(model.maskParam().value));
    model.setMaskField(4);
    check(near(model.maskParam().value, 0.0),
          QStringLiteral("mask: and an amount mask starts at 0 (reduce the effect)"));

    model.setMaskField(6);
    check(model.maskParam().isLayerMask(), QStringLiteral("mask: field 6 is the layer's own matte"));
    // UNPINNED the heading names the KIND and the chooser carries the field; a card entry
    // pins it and the heading becomes the field's own name (asserted further down).
    check(model.title() == QStringLiteral("Filter mask"),
          QStringLiteral("mask: unpinned, the heading names the kind (%1)").arg(model.title()));
    check(model.maskFieldLabel() == QStringLiteral("Layer mask"), QStringLiteral("mask: the label names it"));
    check(near(model.maskParam().value, 0.0), QStringLiteral("mask: a matte starts at 0 (hide)"));
    model.setMaskField(6);
    check(stateCount >= 1, QStringLiteral("mask: re-picking the same field is harmless"));
    model.setMaskField(0);
    check(model.maskParam().lane == core::TimeKeyType_HSV && !model.maskParam().isLayerMask(),
          QStringLiteral("mask: back to an HSV filter input"));
    check(model.title() == QStringLiteral("Filter mask"), QStringLiteral("mask: heading follows"));

    // ---- the value: the keyboard swap is the painter's X ----------------------
    model.setMaskValue(0.0);
    model.swapMaskValue();
    check(near(model.maskParam().value, 1.0),
          QStringLiteral("swap: from 0%% the value goes to 100%% (%1)").arg(model.maskParam().value));
    model.swapMaskValue();
    check(near(model.maskParam().value, 0.0), QStringLiteral("swap: and back"));
    model.setMaskValue(0.37);
    model.swapMaskValue();
    check(near(model.maskParam().value, 1.0),
          QStringLiteral("swap: an in-between value goes to the FAR end, not to a mirrored "
                         "number (%1)").arg(model.maskParam().value));

    // a filter card's "paint mask" and the card's own Paint: both name what is painted,
    // PIN it (the card chose it, so the panel must not ask) and switch the tool
    model.activate(int(ctrl::ToolType_SRT)); // leave the mask tool first
    model.beginFilterMaskPaint(core::TimeKeyType_Blur, core::FilterMaskChannel_TimeOffset);
    check(model.tool() == int(ctrl::ToolType_Mask), QStringLiteral("beginFilterMaskPaint switches the tool"));
    check(model.maskParam().lane == core::TimeKeyType_Blur
              && model.maskParam().channel == core::FilterMaskChannel_TimeOffset
              && !model.maskParam().isLayerMask(),
          QStringLiteral("mask: beginFilterMaskPaint selects Blur time offset"));
    check(model.maskPinned(), QStringLiteral("mask: and PINS it (the card answered the question)"));
    check(model.title() == QStringLiteral("Blur time offset"),
          QStringLiteral("mask: pinned, the HEADING is the field's name: %1").arg(model.title()));
    model.unpinMaskTarget();
    check(!model.maskPinned(), QStringLiteral("mask: the caption's disclosure re-opens the chooser"));

    model.activate(int(ctrl::ToolType_SRT));
    model.beginLayerMaskPaint();
    check(model.maskParam().isLayerMask() && model.maskPinned(),
          QStringLiteral("mask: the mask card's Paint pins the matte too"));
    check(model.title() == QStringLiteral("Layer mask"),
          QStringLiteral("mask: and the matte names itself (%1)").arg(model.title()));
    // a TOOLBOX entry clears the pin: picking the tool means asking which field
    model.activate(int(ctrl::ToolType_SRT));
    model.activate(int(ctrl::ToolType_Mask));
    check(!model.maskPinned(), QStringLiteral("mask: entering from the toolbox asks the question again"));
    model.requestClearMask();
    check(clearCount == 1, QStringLiteral("mask: Clear asks the host"));
    check(!model.maskValueHint().isEmpty(), QStringLiteral("mask: the eyedropper hint exists"));

    // ---- tool memory -----------------------------------------------------------
    model.activate(int(ctrl::ToolType_SRT));
    check(model.modeIndex() == 1, QStringLiteral("each tool remembers its own mode"));
    check(model.srtParam().necessarilyMove && !model.srtParam().adjustPosition,
          QStringLiteral("and its own toggles"));

    // ---- activating the ACTIVE tool does nothing --------------------------------
    {
        const int count = toolChangedCount;
        model.activate(model.tool());
        check(toolChangedCount == count,
              QStringLiteral("re-pressing the active tool emits nothing (the widget rebuilt the "
                             "editor, throwing its state away)"));
    }
    check(lastOld == int(ctrl::ToolType_Mask) && lastNew == int(ctrl::ToolType_SRT),
          QStringLiteral("the last change still carried its (old, new) pair"));

    // ---- view settings ---------------------------------------------------------
    {
        // the three ROTATION verbs are triggers: the flag is up while the signal runs and
        // down again afterwards (a latched flag would rotate on every later view change)
        bool sawAcw = false;
        bool sawReset = false;
        bool sawCw = false;
        QObject::connect(&model, &gui::ToolViewModel::viewSettingsChanged, [&]() {
            sawAcw = sawAcw || view.rotateViewACW;
            sawReset = sawReset || view.resetRotateView;
            sawCw = sawCw || view.rotateViewCW;
        });
        model.rotateCounterClockwise();
        model.resetRotation();
        model.rotateClockwise();
        check(sawAcw && sawReset && sawCw, QStringLiteral("view: each rotation verb raises its flag"));
        check(!view.rotateViewACW && !view.resetRotateView && !view.rotateViewCW,
              QStringLiteral("view: and lowers it again"));
    }
    model.setShowMesh(true);
    check(view.showLayerMesh && model.showMesh(), QStringLiteral("view: Show mesh writes the setting"));
    const int afterMesh = viewSettingsCount;
    model.setShowMesh(true);
    check(viewSettingsCount == afterMesh, QStringLiteral("view: writing the same value notifies nothing"));
    model.setCutOutsideFrame(true);
    model.setFlipCanvas(true);
    check(view.cutImagesByTheFrame && view.flipCanvas,
          QStringLiteral("view: the other two toggles write their settings"));
    check(!model.viewTitle().isEmpty() && !model.showMeshTip().isEmpty()
              && !model.cutOutsideFrameTip().isEmpty() && !model.flipCanvasTip().isEmpty(),
          QStringLiteral("view: every strip action has its words"));

    // ---- the key hints the dock speaks -----------------------------------------
    gui::ToolKeyHints hints;
    hints.rotateCounterClockwise = QStringLiteral("Alt+Q");
    hints.resetRotation = QStringLiteral("F1");
    hints.rotateClockwise = QStringLiteral("Alt+E");
    hints.brushSizeDecrease = QStringLiteral("[");
    hints.brushSizeIncrease = QStringLiteral("]");
    hints.brushHardnessDecrease = QStringLiteral("Shift+[");
    hints.brushHardnessIncrease = QStringLiteral("Shift+]");
    model.setKeyHints(hints);
    check(model.rotateCounterClockwiseTip().contains(QStringLiteral("Alt+Q")),
          QStringLiteral("the rotation tooltip names the inspired binding: %1")
              .arg(model.rotateCounterClockwiseTip()));
    check(model.resetRotationTip().contains(QStringLiteral("F1")), QStringLiteral("reset: F1"));
    check(model.rotateClockwiseTip().contains(QStringLiteral("Alt+E")), QStringLiteral("clockwise: Alt+E"));
    check(model.sizeKeyTip().contains(QStringLiteral("[")) && model.sizeKeyTip().contains(QStringLiteral("]")),
          QStringLiteral("the Size row's tooltip names both notches: %1").arg(model.sizeKeyTip()));
    check(model.hardnessKeyTip().contains(QStringLiteral("Shift+]")),
          QStringLiteral("the Hardness row's tooltip names the shifted pair: %1").arg(model.hardnessKeyTip()));

    // ---- a link-free view setting is a no-op, not a crash -----------------------
    {
        gui::ToolViewModel offline;
        offline.setShowMesh(true);
        offline.rotateClockwise();
        offline.adjustBrush(+1, +1);
        check(!offline.showMesh(), QStringLiteral("a model with no view setting answers safely"));
    }

    if (failures) {
        QTextStream(stdout) << "FAILED " << failures << " check(s)\n";
        return 1;
    }
    QTextStream(stdout) << "OK: the tools model's contract holds\n";
    return 0;
}
