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
#include <QVector>

#include <cmath>

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

    gui::ToolViewModel model;

    // ---- signal bookkeeping ----------------------------------------------------
    int toolChangedCount = 0;
    int lastOld = -1;
    int lastNew = -1;
    int changedCount = 0;
    int stateCount = 0;
    int modeCount = 0;
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
    check(model.brushVisible(), QStringLiteral("bone: Envelope brush shows the brush"));
    check(model.modeName() == QStringLiteral("Envelope brush"), QStringLiteral("bone: the caption names it"));
    check(model.envelopeBrushVisible(), QStringLiteral("bone: the Value row is offered"));
    check(near(model.envelopeValue(), 50.0), QStringLiteral("bone: the envelope brush starts at value 50"));
    model.setEnvelopeValue(123.0);
    check(near(model.envelopeValue(), 123.0) && near(model.boneParam().envelopeValue, 123.0),
          QStringLiteral("bone: the Value row reaches the param"));
    model.setEnvelopeValue(2000.0);
    check(near(model.envelopeValue(), model.envelopeValueMax()),
          QStringLiteral("bone: Value clamps at its maximum (%1)").arg(model.envelopeValueMax()));
    model.setEnvelopeValue(50.0);
    // NOT shown: nothing in ctrl/bone reads BrushParam::opacity (the influence weight is
    // `stamp.amount * falloff` alone), so the row would do nothing.
    check(!model.opacityVisible(), QStringLiteral("bone: no Opacity row (the influence brush ignores it)"));
    model.brush()->setRadius(123.0);
    model.brush()->setHardness(0.6);
    check(near(model.boneParam().brush.radius, 123.0), QStringLiteral("bone: the write lands on the bone param"));
    check(near(model.poseParam().brush.radius, 200.0),
          QStringLiteral("bone: and the pose brush is untouched (%1)").arg(model.poseParam().brush.radius));

    // ---- paint weights: the state panel and its four actions --------------------
    model.setMode(6);
    check(model.modeName() == QStringLiteral("Paint weights"), QStringLiteral("bone: Paint weights is the seventh mode"));
    check(model.modeIndex() == 6, QStringLiteral("bone: the weights index is 6 in the seven-entry row"));
    check(!model.envelopeBrushVisible(), QStringLiteral("bone: the envelope Value row is gone on weights"));
    check(model.brushVisible(), QStringLiteral("weights: the shared brush is shown"));
    check(model.weightHeatmapVisible(), QStringLiteral("weights: the heatmap toggle is offered"));
    check(model.weightsStateVisible(), QStringLiteral("weights: the state panel is shown"));
    check(model.weightsState().kind == ctrl::bone::WeightState::Kind_None,
          QStringLiteral("weights: the model starts from the editor's empty state"));
    check(model.weightsStateText() == QStringLiteral("No bone key"),
          QStringLiteral("weights: the empty state is a status word, not a sentence"));
    check(model.weightsStateRole() == QStringLiteral("none"),
          QStringLiteral("weights: the empty state has no status ink"));
    check(!model.brushEnabled() && !model.weightsBakeVisible() && !model.weightsActionsVisible(),
          QStringLiteral("weights: no actions before a bone is selected"));
    {
        ctrl::bone::WeightState state;
        state.kind = ctrl::bone::WeightState::Kind_Envelope;
        state.boneIndex = 2;
        state.liveNodes = 3;
        model.setWeightsState(state);
        check(model.weightsStateText() == QStringLiteral("Envelope"),
              QStringLiteral("weights: the envelope state is named"));
        check(model.weightsStateRole() == QStringLiteral("envelope"),
              QStringLiteral("weights: and gets the neutral status ink"));
        check(model.weightsStateTip().contains(QStringLiteral("Bake")),
              QStringLiteral("weights: the explanation lives in the state tooltip"));
        check(model.weightsBakeVisible() && model.weightsBakeEnabled(),
              QStringLiteral("weights: an envelope bone offers an enabled Bake"));
        check(!model.weightsRebakeVisible() && !model.weightsClearVisible(),
              QStringLiteral("weights: and no baked-field actions"));
        check(!model.brushEnabled() && !model.weightsPaintEnabled(),
              QStringLiteral("weights: the brush is disabled until the bone is baked"));
        const int before = model.weightPaintMode();
        model.setWeightPaintMode(int(ctrl::WeightPaint_Replace));
        check(model.weightPaintMode() == int(ctrl::WeightPaint_Replace)
                  && model.boneParam().weightMode == ctrl::WeightPaint_Replace,
              QStringLiteral("weights: the chooser writes the shared param"));
        model.setWeightPaintMode(before);
    }
    {
        ctrl::bone::WeightState state;
        state.kind = ctrl::bone::WeightState::Kind_Baked;
        state.boneIndex = 0;
        state.bakedNodes = 2;
        state.bakedVertices = 12;
        state.meshMismatch = true;
        model.setWeightsState(state);
        check(model.weightsStateText() == QStringLiteral("Baked"),
              QStringLiteral("weights: the baked state is one word (%1)").arg(model.weightsStateText()));
        check(!model.weightsStateText().contains(QStringLiteral("·")),
              QStringLiteral("weights: the state is not a dot-separated token (%1)")
                  .arg(model.weightsStateText()));
        // The count is a diagnostic, not a state: it belongs to the tooltip, where
        // a reader can ask for it, never to the row.
        check(model.weightsStateTip().contains(QStringLiteral("12"))
                  && model.weightsStateTip().contains(QStringLiteral("vert")),
              QStringLiteral("weights: the authored count is in the state tooltip (%1)")
                  .arg(model.weightsStateTip()));
        check(model.weightsStateRole() == QStringLiteral("baked"),
              QStringLiteral("weights: and gets the authored status ink"));
        check(model.weightsRebakeVisible() && model.weightsRevertVisible() && model.weightsClearVisible(),
              QStringLiteral("weights: a baked bone offers Rebake/Revert/Clear"));
        check(!model.weightsBakeVisible(),
              QStringLiteral("weights: and Bake is not offered (it would do nothing)"));
        check(model.brushEnabled() && model.weightsPaintEnabled(),
              QStringLiteral("weights: the brush is enabled on a baked field"));
        check(model.weightsMeshWarningVisible(),
              QStringLiteral("weights: a mesh-signature mismatch is surfaced"));
    }
    {
        ctrl::bone::WeightState state;
        state.kind = ctrl::bone::WeightState::Kind_Mixed;
        state.bakedNodes = 1;
        state.liveNodes = 2;
        state.bakedVertices = 5;
        model.setWeightsState(state);
        check(model.weightsStateText() == QStringLiteral("Partly baked"),
              QStringLiteral("weights: a mixed rig names the state in words (%1)").arg(model.weightsStateText()));
        check(model.weightsStateTip().contains(QStringLiteral("2"))
                  && model.weightsStateTip().contains(QStringLiteral("layer")),
              QStringLiteral("weights: and counts the nodes still live in its tooltip (%1)")
                  .arg(model.weightsStateTip()));
        check(model.weightsBakeVisible() && model.weightsBakeEnabled(),
              QStringLiteral("weights: a mixed rig offers Bake for its live layers"));
        check(model.weightsRebakeVisible() && model.weightsClearVisible(),
              QStringLiteral("weights: and the baked-field actions"));
    }
    model.setWeightsState(ctrl::bone::WeightState());
    model.setMode(int(ctrl::BoneEditMode_PaintInfl));
    check(!model.weightsStateVisible(), QStringLiteral("weights: leaving the mode hides the state panel"));
    check(model.brushEnabled(), QStringLiteral("weights: the brush enablement does not leak into other modes"));

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
    check(model.title() == QStringLiteral("Mask"), QStringLiteral("mask: the heading names the tool"));
    // The chooser is host-fed: the host derives it from the node/frame, the model
    // owns selection and labels. With no fields nothing is paintable and the empty
    // state says why; a field the document cannot paint is never offered.
    check(model.maskFields().isEmpty(), QStringLiteral("mask: no host fields, no choices"));
    check(!model.maskPaintable(), QStringLiteral("mask: and nothing is paintable"));
    check(model.maskFieldIndex() == -1, QStringLiteral("mask: no field is selected"));
    check(model.maskFieldLabel().isEmpty(), QStringLiteral("mask: and no label is invented"));
    check(model.maskEmptyText() == QStringLiteral("Select a layer to paint a mask."),
          QStringLiteral("mask: the empty state names the missing context (%1)").arg(model.maskEmptyText()));
    // MaskEditor.cpp:319 DOES read brush.opacity (`amount * opacity`), so this row is
    // reachable and must be offered - it used to be hidden while working.
    check(model.brushVisible() && model.opacityVisible(),
          QStringLiteral("mask: the brush is up WITH its Opacity row (MaskEditor reads it)"));

    QVector<gui::MaskField> fields;
    fields.append(gui::MaskField{ctrl::MaskTargetKind_FilterInput, core::TimeKeyType_HSV,
                                 core::FilterMaskChannel_Amount, QStringLiteral("HSV amount")});
    fields.append(gui::MaskField{ctrl::MaskTargetKind_FilterInput, core::TimeKeyType_Wave,
                                 core::FilterMaskChannel_Amount, QStringLiteral("Wave Deform amount")});
    fields.append(gui::MaskField{ctrl::MaskTargetKind_FilterInput, core::TimeKeyType_Wave,
                                 core::FilterMaskChannel_TimeOffset,
                                 QStringLiteral("Wave Deform time offset")});
    model.setMaskFields(fields, QString());
    check(describe(model.maskFields())
              == QStringLiteral("HSV amount, Wave Deform amount, Wave Deform time offset"),
          QStringLiteral("mask: the host's derived list is the chooser: %1")
              .arg(describe(model.maskFields())));
    check(model.maskPaintable() && model.maskEmptyText().isEmpty(),
          QStringLiteral("mask: a non-empty list is paintable and has no empty text"));
    check(model.maskFieldIndex() == 1 && model.maskFieldLabel() == QStringLiteral("Wave Deform amount"),
          QStringLiteral("mask: the current valid field keeps its place (%1)")
              .arg(model.maskFieldLabel()));
    check(model.maskParam().lane == core::TimeKeyType_Wave
              && model.maskParam().channel == core::FilterMaskChannel_Amount,
          QStringLiteral("mask: still Wave amount"));

    model.setMaskValue(0.42);
    check(near(model.maskParam().value, 0.42), QStringLiteral("mask: the value writes the param"));

    model.setMaskField(2);
    check(model.maskParam().channel == core::FilterMaskChannel_TimeOffset,
          QStringLiteral("mask: choosing the time-offset row changes the channel"));
    check(near(model.maskParam().value, 1.0),
          QStringLiteral("mask: a time offset mask starts at 1 (add the delay), not 0 (%1)")
              .arg(model.maskParam().value));
    check(model.maskFieldIndex() == 2, QStringLiteral("mask: and the chooser follows"));
    model.setMaskField(1);
    check(near(model.maskParam().value, 0.0),
          QStringLiteral("mask: an amount mask starts at 0 (reduce the effect)"));

    fields.append(gui::MaskField{ctrl::MaskTargetKind_LayerMask, core::TimeKeyType_TERM,
                                 core::FilterMaskChannel_Amount, QStringLiteral("Layer mask")});
    model.setMaskFields(fields, QString());
    model.setMaskField(3);
    check(model.maskParam().isLayerMask(), QStringLiteral("mask: the matte is the last addressable field"));
    check(near(model.maskParam().value, 0.0), QStringLiteral("mask: a matte starts at 0 (hide)"));

    // A list that no longer contains the current field must not leave the brush aimed
    // at it: the model takes the first remaining target.
    QVector<gui::MaskField> onlyAmount;
    onlyAmount.append(fields.first());
    model.setMaskFields(onlyAmount, QString());
    check(model.maskFieldIndex() == 0 && !model.maskParam().isLayerMask()
              && model.maskParam().lane == core::TimeKeyType_HSV,
          QStringLiteral("mask: a vanished target falls back to the first paintable field"));

    // The host explains an empty list (missing filter, disabled filter, unkeyed matte).
    model.setMaskFields({}, QStringLiteral("Enable a filter to paint its mask."));
    check(!model.maskPaintable() && model.maskFields().isEmpty(),
          QStringLiteral("mask: an empty derived list is not paintable"));
    check(model.maskEmptyText() == QStringLiteral("Enable a filter to paint its mask."),
          QStringLiteral("mask: the host's reason is the visible explanation (%1)")
              .arg(model.maskEmptyText()));
    model.setMaskField(0);
    check(model.maskFields().isEmpty(), QStringLiteral("mask: choosing from an empty list is a no-op"));

    // The card entries select the right lane/channel and seed the channel's useful
    // value even when the host has no target list yet.
    model.activate(int(ctrl::ToolType_SRT));
    model.beginFilterMaskPaint(core::TimeKeyType_Blur, core::FilterMaskChannel_TimeOffset);
    check(model.tool() == int(ctrl::ToolType_Mask), QStringLiteral("beginFilterMaskPaint switches the tool"));
    check(model.maskParam().lane == core::TimeKeyType_Blur
              && model.maskParam().channel == core::FilterMaskChannel_TimeOffset
              && !model.maskParam().isLayerMask(),
          QStringLiteral("mask: beginFilterMaskPaint selects Blur time offset"));
    check(near(model.maskParam().value, 1.0),
          QStringLiteral("mask: a time offset mask starts at 1 (add the delay), not 0 (%1)")
              .arg(model.maskParam().value));
    check(model.title() == QStringLiteral("Mask"),
          QStringLiteral("mask: a card entry keeps the one Target control (%1)").arg(model.title()));

    model.activate(int(ctrl::ToolType_SRT));
    model.beginLayerMaskPaint();
    check(model.maskParam().isLayerMask(), QStringLiteral("mask: the mask card's Paint selects the matte"));
    check(near(model.maskParam().value, 0.0), QStringLiteral("mask: a matte starts at 0 (hide)"));
    check(model.title() == QStringLiteral("Mask"),
          QStringLiteral("mask: and the heading still names the tool (%1)").arg(model.title()));
    model.requestClearMask();
    check(clearCount == 1, QStringLiteral("mask: Clear asks the host"));
    check(!model.maskValueHint().isEmpty(), QStringLiteral("mask: the eyedropper hint exists"));

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

    // ---- the key hints the dock speaks -----------------------------------------
    gui::ToolKeyHints hints;
    hints.brushSizeDecrease = QStringLiteral("[");
    hints.brushSizeIncrease = QStringLiteral("]");
    hints.brushHardnessDecrease = QStringLiteral("Shift+[");
    hints.brushHardnessIncrease = QStringLiteral("Shift+]");
    model.setKeyHints(hints);
    check(model.sizeKeyTip().contains(QStringLiteral("[")) && model.sizeKeyTip().contains(QStringLiteral("]")),
          QStringLiteral("the Size row's tooltip names both notches: %1").arg(model.sizeKeyTip()));
    check(model.hardnessKeyTip().contains(QStringLiteral("Shift+]")),
          QStringLiteral("the Hardness row's tooltip names the shifted pair: %1").arg(model.hardnessKeyTip()));

    if (failures) {
        QTextStream(stdout) << "FAILED " << failures << " check(s)\n";
        return 1;
    }
    QTextStream(stdout) << "OK: the tools model's contract holds\n";
    return 0;
}
