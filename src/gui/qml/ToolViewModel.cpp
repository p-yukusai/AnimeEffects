#include "gui/qml/ToolViewModel.h"

#include <QCoreApplication>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace gui {

namespace {

// One entry of a {label, icon} list a QML Repeater binds to.
QVariantMap entry(const QString& aLabel, const QString& aIcon) {
    QVariantMap map;
    map.insert(QStringLiteral("label"), aLabel);
    map.insert(QStringLiteral("icon"), aIcon);
    return map;
}

// The mask tool's four targets, in the order the widget's icon row had them. The
// three lanes' names come from the SHARED filter table, so the mask chooser, the
// filter stack's rows and its catalog cannot name one lane three ways.
const core::TimeKeyType kMaskLanes[] = {
    core::TimeKeyType_HSV, core::TimeKeyType_Blur, core::TimeKeyType_Wave};
const int kMaskLaneCount = 3;

// The dock's strings that ALREADY have catalog entries keep their ORIGINAL contexts.
// A context is a catalog key: re-spelling "Add bones" in QML would ship English until
// every catalog is re-extracted AND re-translated, which is what AGENTS.md records
// about the graph panel's frozen context. New strings below use tr() (context
// gui::ToolViewModel) or the scene's own qsTr, and are pending a translation pass.
QString modePanelText(const char* aText) {
    return QCoreApplication::translate("gui::tool::ModePanel", aText);
}

} // namespace

//-------------------------------------------------------------------------------------------------
// ToolBrushModel
//-------------------------------------------------------------------------------------------------
ToolBrushModel::ToolBrushModel(QObject* aParent): QObject(aParent) {}

void ToolBrushModel::retarget(ctrl::BrushParam* aParam) {
    mParam = aParam;
    // values only: a tool switch is not an edit, and the host pushes the new tool's
    // param itself once its editor exists.
    emit valuesChanged();
}

double ToolBrushModel::radius() const { return mParam ? double(mParam->radius) : 100.0; }
double ToolBrushModel::hardness() const { return mParam ? double(mParam->hardness) : 0.5; }
double ToolBrushModel::flow() const { return mParam ? double(mParam->flow) : 1.0; }
double ToolBrushModel::opacity() const { return mParam ? double(mParam->opacity) : 1.0; }
bool ToolBrushModel::pressureSize() const { return mParam && mParam->pressureSize; }
bool ToolBrushModel::pressureFlow() const { return mParam && mParam->pressureFlow; }

void ToolBrushModel::setRadius(double aValue) {
    if (!mParam)
        return;
    const float value = float(std::min(std::max(aValue, radiusMin()), radiusMax()));
    if (mParam->radius == value)
        return;
    mParam->radius = value;
    emit valuesChanged();
    emit edited();
}

void ToolBrushModel::setHardness(double aValue) {
    if (!mParam)
        return;
    const float value = float(std::min(std::max(aValue, 0.0), 1.0));
    if (mParam->hardness == value)
        return;
    mParam->hardness = value;
    emit valuesChanged();
    emit edited();
}

void ToolBrushModel::setFlow(double aValue) {
    if (!mParam)
        return;
    const float value = float(std::min(std::max(aValue, 0.0), 1.0));
    if (mParam->flow == value)
        return;
    mParam->flow = value;
    emit valuesChanged();
    emit edited();
}

void ToolBrushModel::setOpacity(double aValue) {
    if (!mParam)
        return;
    const float value = float(std::min(std::max(aValue, opacityMin() / percentMax()), 1.0));
    if (mParam->opacity == value)
        return;
    mParam->opacity = value;
    emit valuesChanged();
    emit edited();
}

void ToolBrushModel::setPressureSize(bool aValue) {
    if (!mParam || mParam->pressureSize == aValue)
        return;
    mParam->pressureSize = aValue;
    emit valuesChanged();
    emit edited();
}

void ToolBrushModel::setPressureFlow(bool aValue) {
    if (!mParam || mParam->pressureFlow == aValue)
        return;
    mParam->pressureFlow = aValue;
    emit valuesChanged();
    emit edited();
}

// The notches, VERBATIM from the deleted widget panel (BrushPanel::adjustSize /
// adjustHardness): one step is x1.1 on the size and 5 % on the hardness, clamped to
// the same 1..1000 px and 0..1 the fields offer. The rounding is the same too - the
// size keeps a float (a size of 1.1 px is legal) and the panel's slider was the only
// thing that quantised it.
namespace {
// ONE DRAG STEP IS 8 SCREEN PX OF TRAVEL (ctrl::BrushDrag::kSizePxPerStep), AND IT MOVES sqrt(radius)
// BY A CONSTANT: the same hand travel buys the same EFFORT across the whole 1..1000 range - measured
// +11.5 % at radius 20 (22.30) and +2.5 % at 400 (410.06). A linear law (+2 px per step) was built
// and run beside this one (2026-10-31): it crawls at the top (+0.5 % at 400), and the hand picked
// this one. The old multiplicative law was x1.1 at both ends, too coarse at the top and too fine at
// the bottom - the report that started this.
constexpr double kSqrtPerStep = 0.25;
} // namespace

void ToolBrushModel::nudge(double aSizeSteps, double aHardnessSteps) {
    if (!mParam)
        return;
    bool changed = false;
    // The keyboard notch keeps `adjustSize`'s ratio (one press, one x1.1); the DRAG follows the
    // sqrt law above, because it is continuous and the hand asked for equal effort, not equal ratio.
    if (aSizeSteps != 0.0) {
        const double radius = double(mParam->radius);
        const double root = std::sqrt(std::max(radius, 1.0)) + kSqrtPerStep * aSizeSteps;
        const double scaled = root * root;
        mParam->radius = float(std::min(std::max(scaled, double(radiusMin())), double(radiusMax())));
    }
    if (aHardnessSteps != 0.0)
        mParam->hardness = float(std::min(std::max(double(mParam->hardness) + 0.05 * aHardnessSteps, 0.0), 1.0));
    if (aSizeSteps != 0.0 || aHardnessSteps != 0.0)
        changed = true;
    if (changed) {
        emit valuesChanged();
        emit edited();
    }
}

void ToolBrushModel::adjustSize(int aSteps) {
    if (!mParam || aSteps == 0)
        return;
    mParam->radius = std::min(std::max(mParam->radius * std::pow(1.1f, float(aSteps)), 1.0f), 1000.0f);
    emit valuesChanged();
    emit edited();
}

void ToolBrushModel::adjustHardness(int aSteps) {
    if (!mParam || aSteps == 0)
        return;
    mParam->hardness = std::min(std::max(mParam->hardness + 0.05f * float(aSteps), 0.0f), 1.0f);
    emit valuesChanged();
    emit edited();
}

//-------------------------------------------------------------------------------------------------
// ToolViewModel
//-------------------------------------------------------------------------------------------------
ToolViewModel::ToolViewModel(QObject* aParent): QObject(aParent) {
    mMaskEmptyText = tr("Select a layer to paint a mask.");
    mBrush = new ToolBrushModel(this);
    // an EDIT of the brush is an edit of the active tool's param (a retarget is not)
    connect(mBrush, &ToolBrushModel::edited, this, &ToolViewModel::changed);
    retargetBrush();
}

void ToolViewModel::setDriverActive(bool aActive) {
    if (mDriverActive == aActive)
        return;
    mDriverActive = aActive;
    emit driverActiveChanged();
}

void ToolViewModel::setKeyHints(const ToolKeyHints& aHints) {
    mKeyHints = aHints;
    // The tooltips are built from these, so a rebound key re-spells them; the signal is
    // what makes a long-lived scene re-read.
    // No "brush" in the format: the line sits under the section's own BRUSH heading.
    emit keyHintsChanged();
}

const ctrl::BrushParam* ToolViewModel::activeBrush() const {
    switch (mTool) {
    case ctrl::ToolType_Bone: return &mBone.brush;
    case ctrl::ToolType_Pose: return &mPose.brush;
    case ctrl::ToolType_FFD: return &mFFD.brush;
    case ctrl::ToolType_Mask: return &mMask.brush;
    default: return nullptr;
    }
}

void ToolViewModel::retargetBrush() {
    switch (mTool) {
    case ctrl::ToolType_Bone: mBrush->retarget(&mBone.brush); break;
    case ctrl::ToolType_Pose: mBrush->retarget(&mPose.brush); break;
    case ctrl::ToolType_FFD: mBrush->retarget(&mFFD.brush); break;
    case ctrl::ToolType_Mask: mBrush->retarget(&mMask.brush); break;
    default: mBrush->retarget(nullptr); break;
    }
}

//-------------------------------------------------------------------------------------------------
// the toolbox
//-------------------------------------------------------------------------------------------------
QVariantList ToolViewModel::tools() const {
    struct Entry {
        ctrl::ToolType type;
        const char* icon;
        const char* name;
    };
    // The seven tools, in the order the dock has always shown them, with the icons the
    // widget toolbox used. QT_TRANSLATE_NOOP is what keeps these visible to lupdate
    // inside a table (a bare literal in a struct is invisible to the extractor).
    static const Entry kEntries[] = {
        {ctrl::ToolType_Cursor, "hand", QT_TRANSLATE_NOOP("gui::tool::ModePanel", "Pan tool")},
        {ctrl::ToolType_SRT, "arrows-out-cardinal", QT_TRANSLATE_NOOP("gui::tool::ModePanel", "Transform")},
        {ctrl::ToolType_Bone, "bone", QT_TRANSLATE_NOOP("gui::tool::ModePanel", "Bone editor")},
        {ctrl::ToolType_Pose, "pose", QT_TRANSLATE_NOOP("gui::tool::ModePanel", "Pose editor")},
        {ctrl::ToolType_Mesh, "triangle", QT_TRANSLATE_NOOP("gui::tool::ModePanel", "Mesh editor")},
        {ctrl::ToolType_FFD, "ffd", QT_TRANSLATE_NOOP("gui::tool::ModePanel", "Free-form deformation")},
        {ctrl::ToolType_Mask, "paint-brush", QT_TRANSLATE_NOOP("gui::tool::ModePanel", "Mask paint")},
    };
    QVariantList out;
    for (const Entry& entryData : kEntries) {
        QVariantMap map;
        map.insert(QStringLiteral("type"), int(entryData.type));
        map.insert(QStringLiteral("icon"), QString::fromLatin1(entryData.icon));
        const QString name = modePanelText(entryData.name);
        map.insert(QStringLiteral("name"), name);
        map.insert(QStringLiteral("tip"), name);
        out.append(map);
    }
    return out;
}

//-------------------------------------------------------------------------------------------------
// the active tool's panel
//-------------------------------------------------------------------------------------------------
QString ToolViewModel::title() const {
    switch (mTool) {
    case ctrl::ToolType_SRT:
        return QCoreApplication::translate("gui::tool::SRTPanel", "Transform");
    case ctrl::ToolType_Bone:
        return QCoreApplication::translate("gui::tool::BonePanel", "Bone editor");
    case ctrl::ToolType_Pose:
        return QCoreApplication::translate("gui::tool::PosePanel", "Pose editor");
    case ctrl::ToolType_Mesh:
        return QCoreApplication::translate("gui::tool::MeshPanel", "Mesh editor");
    case ctrl::ToolType_FFD:
        return QCoreApplication::translate("gui::tool::FFDPanel", "FFD");
    case ctrl::ToolType_Mask:
        // The heading names the TOOL; the Target row below it is the field's own
        // chooser (and shows the current field as its value). A card entry simply
        // preselects that target - it must not turn the label into a caption plus a
        // separate disclosure, which is the control this replaced.
        return tr("Mask");
    default:
        return QString();
    }
}

QVariantList ToolViewModel::modes() const {
    switch (mTool) {
    case ctrl::ToolType_SRT:
        return {entry(QCoreApplication::translate("gui::tool::SRTPanel", "Transform"), QStringLiteral("navigation-arrow")),
                entry(QCoreApplication::translate("gui::tool::SRTPanel", "Move Centroid"), QStringLiteral("crosshair"))};
    case ctrl::ToolType_Bone:
        return {
            entry(QCoreApplication::translate("gui::tool::BonePanel", "Add bones"), QStringLiteral("plus-bold")),
            entry(QCoreApplication::translate("gui::tool::BonePanel", "Remove bones"), QStringLiteral("minus-bold")),
            entry(QCoreApplication::translate("gui::tool::BonePanel", "Move joints"), QStringLiteral("navigation-arrow")),
            entry(QCoreApplication::translate("gui::tool::BonePanel", "Bind bone to node"), QStringLiteral("link")),
            entry(QCoreApplication::translate("gui::tool::BonePanel", "Adjust envelope"), QStringLiteral("envelope")),
            entry(QCoreApplication::translate("gui::tool::BonePanel", "Envelope brush"), QStringLiteral("paint-envelope")),
            entry(QCoreApplication::translate("gui::tool::BonePanel", "Paint weights"), QStringLiteral("paint-brush")),
        };
    case ctrl::ToolType_Pose:
        return {
            entry(QCoreApplication::translate("gui::tool::PosePanel", "Move bone"), QStringLiteral("navigation-arrow")),
            entry(QCoreApplication::translate("gui::tool::PosePanel", "Pull bones"), QStringLiteral("pencil-simple")),
            entry(QCoreApplication::translate("gui::tool::PosePanel", "Erase bone pose"), QStringLiteral("eraser")),
        };
    case ctrl::ToolType_Mesh:
        return {
            entry(QCoreApplication::translate("gui::tool::MeshPanel", "Add vertex"), QStringLiteral("plus-bold")),
            entry(QCoreApplication::translate("gui::tool::MeshPanel", "Delete vertex"), QStringLiteral("minus-bold")),
            entry(QCoreApplication::translate("gui::tool::MeshPanel", "Split polygon"), QStringLiteral("pencil-simple")),
        };
    case ctrl::ToolType_FFD:
        return {
            entry(QCoreApplication::translate("gui::tool::FFDPanel", "Move vertex"), QStringLiteral("navigation-arrow")),
            entry(QCoreApplication::translate("gui::tool::FFDPanel", "Deform mesh"), QStringLiteral("pencil-simple")),
            entry(QCoreApplication::translate("gui::tool::FFDPanel", "Erase deformations"), QStringLiteral("eraser")),
        };
    default:
        // the mask tool picks a TARGET and a CHANNEL instead of a mode
        return {};
    }
}

int ToolViewModel::modeIndex() const {
    switch (mTool) {
    case ctrl::ToolType_SRT: return mSRT.mode;
    case ctrl::ToolType_Bone:
        // The UI order has seven entries; the persisted enum keeps PaintWeights at 7
        // and the retired EraseInfl at 6, so the list index is mapped explicitly.
        if (mBone.mode == ctrl::BoneEditMode_PaintWeights) return 6;
        if (mBone.mode == ctrl::BoneEditMode_EraseInfl) return 5;
        return int(mBone.mode);
    case ctrl::ToolType_Pose: return int(mPose.mode);
    case ctrl::ToolType_Mesh: return mMesh.mode;
    case ctrl::ToolType_FFD: return int(mFFD.type);
    default: return -1;
    }
}

QString ToolViewModel::modeName() const {
    const QVariantList list = modes();
    const int index = modeIndex();
    if (index < 0 || index >= list.size())
        return QString();
    return list.at(index).toMap().value(QStringLiteral("label")).toString();
}

bool ToolViewModel::brushVisible() const {
    switch (mTool) {
    case ctrl::ToolType_Bone:
        return mBone.mode == ctrl::BoneEditMode_PaintInfl || mBone.mode == ctrl::BoneEditMode_EraseInfl
            || mBone.mode == ctrl::BoneEditMode_PaintWeights;
    case ctrl::ToolType_Pose: return mPose.mode == ctrl::PoseEditMode_Erase;
    case ctrl::ToolType_FFD:
        return mFFD.type == ctrl::FFDParam::Type_Pencil || mFFD.type == ctrl::FFDParam::Type_Eraser;
    case ctrl::ToolType_Mask: return true;
    default: return false;
    }
}

bool ToolViewModel::blurVisible() const {
    return mTool == ctrl::ToolType_FFD && mFFD.type == ctrl::FFDParam::Type_Pencil;
}

bool ToolViewModel::weightVisible() const {
    return mTool == ctrl::ToolType_Pose && mPose.mode == ctrl::PoseEditMode_Draw;
}

bool ToolViewModel::weightHeatmapVisible() const {
    if (mTool != ctrl::ToolType_Bone)
        return false;
    return mBone.mode == ctrl::BoneEditMode_Influence || mBone.mode == ctrl::BoneEditMode_PaintInfl
        || mBone.mode == ctrl::BoneEditMode_EraseInfl || mBone.mode == ctrl::BoneEditMode_PaintWeights;
}

bool ToolViewModel::weightPaintHintVisible() const {
    return mTool == ctrl::ToolType_Bone
        && (mBone.mode == ctrl::BoneEditMode_PaintInfl || mBone.mode == ctrl::BoneEditMode_EraseInfl);
}

bool ToolViewModel::envelopeBrushVisible() const {
    return mTool == ctrl::ToolType_Bone
        && (mBone.mode == ctrl::BoneEditMode_PaintInfl || mBone.mode == ctrl::BoneEditMode_EraseInfl);
}

double ToolViewModel::envelopeValue() const { return double(mBone.envelopeValue); }

void ToolViewModel::setEnvelopeValue(double aValue) {
    const float clamped = float(std::min(std::max(aValue, double(envelopeValueMin())),
                                         double(envelopeValueMax())));
    if (std::abs(mBone.envelopeValue - clamped) < 1.0e-6f) {
        return;
    }
    mBone.envelopeValue = clamped;
    emit changed();
}

int ToolViewModel::envelopeValueMin() const { return 0; }

int ToolViewModel::envelopeValueMax() const { return 1000; }

bool ToolViewModel::weightHeatmap() const { return mBone.showWeightHeatmap; }

void ToolViewModel::setWeightHeatmap(bool aValue) {
    if (mBone.showWeightHeatmap == aValue)
        return;
    mBone.showWeightHeatmap = aValue;
    emit changed();
}

//-------------------------------------------------------------------------------------------------
// Paint weights
//-------------------------------------------------------------------------------------------------
namespace {
bool sameWeightsState(const ctrl::bone::WeightState& aLhs, const ctrl::bone::WeightState& aRhs) {
    return aLhs.kind == aRhs.kind && aLhs.boneIndex == aRhs.boneIndex
        && aLhs.bakedNodes == aRhs.bakedNodes && aLhs.liveNodes == aRhs.liveNodes
        && aLhs.bakedVertices == aRhs.bakedVertices && aLhs.meshMismatch == aRhs.meshMismatch
        && aLhs.error == aRhs.error;
}
} // namespace

void ToolViewModel::setWeightsState(const ctrl::bone::WeightState& aState) {
    if (sameWeightsState(mWeights, aState))
        return;
    mWeights = aState;
    emit weightsChanged();
}

bool ToolViewModel::weightsStateVisible() const {
    return mTool == ctrl::ToolType_Bone && mBone.mode == ctrl::BoneEditMode_PaintWeights;
}

bool ToolViewModel::weightsActionsVisible() const {
    return weightsStateVisible() && mWeights.kind != ctrl::bone::WeightState::Kind_None
        && mWeights.kind != ctrl::bone::WeightState::Kind_SelectBone
        && mWeights.kind != ctrl::bone::WeightState::Kind_NoTargets;
}

QString ToolViewModel::weightsStateText() const {
    using Kind = ctrl::bone::WeightState::Kind;
    switch (mWeights.kind) {
    case Kind::Kind_None: return tr("No bone key");
    case Kind::Kind_SelectBone: return tr("Select a bone");
    case Kind::Kind_NoTargets: return tr("No mesh here");
    case Kind::Kind_Envelope: return tr("Envelope");
    // ONE WORD. The count is a diagnostic, not a state: it lives in the state
    // tooltip, where a reader who wants the number can find it, and never in the
    // row ("Baked · 622" made the number the state and left its unit to guess).
    case Kind::Kind_Baked: return tr("Baked");
    case Kind::Kind_Mixed: return tr("Partly baked");
    }
    return QString();
}

QString ToolViewModel::weightsStateRole() const {
    using Kind = ctrl::bone::WeightState::Kind;
    switch (mWeights.kind) {
    case Kind::Kind_None: return QStringLiteral("none");
    case Kind::Kind_SelectBone: return QStringLiteral("select");
    case Kind::Kind_NoTargets: return QStringLiteral("no-targets");
    case Kind::Kind_Envelope: return QStringLiteral("envelope");
    case Kind::Kind_Baked: return QStringLiteral("baked");
    case Kind::Kind_Mixed: return QStringLiteral("mixed");
    }
    return QString();
}

QString ToolViewModel::weightsStateTip() const {
    using Kind = ctrl::bone::WeightState::Kind;
    switch (mWeights.kind) {
    case Kind::Kind_None:
        return tr("Create a bone key at this frame to paint weights.");
    case Kind::Kind_SelectBone:
        return tr("Right-click a bone to target it. Left is always the stroke.");
    case Kind::Kind_NoTargets:
        return tr("The key skins no mesh at this frame.");
    case Kind::Kind_Envelope:
        return tr("This bone's weights are live. Bake evaluates its envelope once, then the brush edits the result.");
    case Kind::Kind_Baked:
        return tr("The field is authored (%1). Rebake replaces it from the envelope.")
            .arg(authoredVertexCountText());
    case Kind::Kind_Mixed:
        return tr("%n layer(s) still use the live envelope; the brush paints the baked ones.", "", mWeights.liveNodes);
    }
    return QString();
}

QString ToolViewModel::authoredVertexCountText() const {
    // `%n` is the Qt numerus form; the English catalog supplies "vertex"/"vertices".
    return tr("%n vertex(s)", "", mWeights.bakedVertices);
}

bool ToolViewModel::weightsMeshWarningVisible() const {
    return weightsStateVisible() && mWeights.meshMismatch;
}

QString ToolViewModel::weightsError() const {
    return weightsStateVisible() ? mWeights.error : QString();
}

bool ToolViewModel::weightsBakeVisible() const {
    using Kind = ctrl::bone::WeightState::Kind;
    return weightsActionsVisible()
        && (mWeights.kind == Kind::Kind_Envelope || mWeights.kind == Kind::Kind_Mixed);
}

bool ToolViewModel::weightsBakeEnabled() const { return weightsBakeVisible() && mWeights.canBake(); }

bool ToolViewModel::weightsRebakeVisible() const {
    using Kind = ctrl::bone::WeightState::Kind;
    return weightsActionsVisible()
        && (mWeights.kind == Kind::Kind_Baked || mWeights.kind == Kind::Kind_Mixed);
}

bool ToolViewModel::weightsRebakeEnabled() const {
    return weightsRebakeVisible() && mWeights.canRebake();
}

bool ToolViewModel::weightsRevertVisible() const { return weightsRebakeVisible(); }

bool ToolViewModel::weightsRevertEnabled() const {
    return weightsRebakeVisible() && mWeights.canRevert();
}

bool ToolViewModel::weightsClearVisible() const { return weightsRebakeVisible(); }

bool ToolViewModel::weightsClearEnabled() const {
    return weightsRebakeVisible() && mWeights.canClear();
}

bool ToolViewModel::weightsPaintEnabled() const {
    return weightsStateVisible() && mWeights.canPaint();
}

bool ToolViewModel::brushEnabled() const {
    if (mTool != ctrl::ToolType_Bone || mBone.mode != ctrl::BoneEditMode_PaintWeights) {
        return true;
    }
    return mWeights.canPaint();
}

QVariantList ToolViewModel::weightModes() const {
    QVariantList out;
    const char* labels[ctrl::WeightPaintMode_TERM] = {QT_TRANSLATE_NOOP("gui::ToolViewModel", "Add"),
                                                      QT_TRANSLATE_NOOP("gui::ToolViewModel", "Subtract"),
                                                      QT_TRANSLATE_NOOP("gui::ToolViewModel", "Replace")};
    for (int i = 0; i < ctrl::WeightPaintMode_TERM; ++i) {
        QVariantMap map;
        map.insert(QStringLiteral("label"), tr(labels[i]));
        map.insert(QStringLiteral("value"), i);
        out.append(map);
    }
    return out;
}

int ToolViewModel::weightPaintMode() const { return int(mBone.weightMode); }

void ToolViewModel::setWeightPaintMode(int aMode) {
    if (aMode < 0 || aMode >= ctrl::WeightPaintMode_TERM || mBone.weightMode == ctrl::WeightPaintMode(aMode)) {
        return;
    }
    mBone.weightMode = ctrl::WeightPaintMode(aMode);
    emit changed();
}

void ToolViewModel::requestWeightsAction(int aAction) {
    switch (WeightAction(aAction)) {
    case WeightAction_Bake:
        if (!weightsBakeEnabled()) return;
        break;
    case WeightAction_Rebake:
        if (!weightsRebakeEnabled()) return;
        break;
    case WeightAction_Revert:
        if (!weightsRevertEnabled()) return;
        break;
    case WeightAction_Clear:
        if (!weightsClearEnabled()) return;
        break;
    default:
        return;
    }
    emit weightsActionRequested(aAction);
}

bool ToolViewModel::srtCreateVisible() const {
    return mTool == ctrl::ToolType_SRT && mSRT.mode == 0;
}

bool ToolViewModel::srtCentroidVisible() const {
    return mTool == ctrl::ToolType_SRT && mSRT.mode == 1;
}

bool ToolViewModel::maskPickersVisible() const { return mTool == ctrl::ToolType_Mask; }

// WHICH TOOLS ACTUALLY READ Opacity - measured by grepping BrushParam::opacity's
// consumers in ctrl, because a row that changes nothing is worse than a missing row:
//   * MaskEditor.cpp:319 multiplies the stamp's amount by brush.opacity, so the mask
//     brush honours it and the row was WRONGLY HIDDEN (the reported "why is there no
//     opacity?" - the knob works, the panel did not offer it);
//   * nothing in ctrl/ffd reads it (a displacement has no per-stroke cap to blend, as
//     FFDParam's own comment says), so the mesh brush hides it;
//   * nothing in ctrl/bone or ctrl/pose reads it either - the influence and pose brushes
//     use `stamp.amount` alone. Their rows are therefore GONE too: they were shown before
//     this round and did nothing. Teaching those two targets to honour a cap is a change
//     to their painting behaviour, not to this panel.
bool ToolViewModel::opacityVisible() const { return mTool == ctrl::ToolType_Mask; }

//-------------------------------------------------------------------------------------------------
// SRT
//-------------------------------------------------------------------------------------------------
void ToolViewModel::setNecessarilyMove(bool aValue) {
    if (mSRT.necessarilyMove == aValue)
        return;
    mSRT.necessarilyMove = aValue;
    emit changed();
}

void ToolViewModel::setNecessarilyRotate(bool aValue) {
    if (mSRT.necessarilyRotate == aValue)
        return;
    mSRT.necessarilyRotate = aValue;
    emit changed();
}

void ToolViewModel::setNecessarilyScale(bool aValue) {
    if (mSRT.necessarilyScale == aValue)
        return;
    mSRT.necessarilyScale = aValue;
    emit changed();
}

void ToolViewModel::setAdjustPosition(bool aValue) {
    if (mSRT.adjustPosition == aValue)
        return;
    mSRT.adjustPosition = aValue;
    emit changed();
}

//-------------------------------------------------------------------------------------------------
// FFD / Pose
//-------------------------------------------------------------------------------------------------
void ToolViewModel::setBlur(double aValue) {
    const float value = float(std::min(std::max(aValue, 0.0), 1.0));
    if (mFFD.blur == value)
        return;
    mFFD.blur = value;
    emit changed();
}

void ToolViewModel::setWeight(double aValue) {
    const float value = float(std::min(std::max(aValue, 0.0), 1.0));
    if (mPose.diWeight == value)
        return;
    mPose.diWeight = value;
    emit changed();
}

//-------------------------------------------------------------------------------------------------
// Mask
//-------------------------------------------------------------------------------------------------
QVariantList ToolViewModel::maskFields() const {
    QVariantList out;
    for (int i = 0; i < mMaskFieldEntries.size(); ++i) {
        const MaskField& field = mMaskFieldEntries.at(i);
        QVariantMap map;
        map.insert(QStringLiteral("label"), field.label);
        map.insert(QStringLiteral("value"), i);
        out.append(map);
    }
    return out;
}

int ToolViewModel::maskFieldIndex() const {
    return findMaskField(mMask.kind, mMask.lane, mMask.channel);
}

int ToolViewModel::findMaskField(ctrl::MaskTargetKind aKind, core::TimeKeyType aLane,
                                 core::FilterMaskChannel aChannel) const {
    for (int i = 0; i < mMaskFieldEntries.size(); ++i) {
        const MaskField& field = mMaskFieldEntries.at(i);
        if (field.kind != aKind)
            continue;
        if (aKind == ctrl::MaskTargetKind_LayerMask || (field.lane == aLane && field.channel == aChannel))
            return i;
    }
    return -1;
}

QString ToolViewModel::maskFieldLabel() const {
    const int index = maskFieldIndex();
    return (0 <= index && index < mMaskFieldEntries.size()) ? mMaskFieldEntries.at(index).label
                                                           : QString();
}

QString ToolViewModel::maskEmptyText() const {
    if (!maskPaintable() && mMaskEmptyText.isEmpty() && !mMaskFieldEntries.isEmpty())
        return tr("Choose a paint target.");
    return mMaskEmptyText;
}

void ToolViewModel::setMaskField(int aIndex) {
    if (aIndex < 0 || aIndex >= mMaskFieldEntries.size() || aIndex == maskFieldIndex())
        return;
    selectMaskField(aIndex, true);
}

void ToolViewModel::selectMaskField(int aIndex, bool aNotify) {
    if (aIndex < 0 || aIndex >= mMaskFieldEntries.size())
        return;
    const MaskField& field = mMaskFieldEntries.at(aIndex);
    if (field.kind == ctrl::MaskTargetKind_LayerMask) {
        mMask.setKind(ctrl::MaskTargetKind_LayerMask);
    } else {
        // setKind FIRST: coming back from the matte, the kind change is what makes the
        // next stroke read a filter input at all.
        mMask.setKind(ctrl::MaskTargetKind_FilterInput);
        mMask.lane = field.lane;
        // The channel seeds the useful value for its own mask (amount -> 0, offset -> 1).
        mMask.setChannel(field.channel);
    }
    if (!aNotify)
        return;
    emit stateChanged();
    emit maskChanged();
    emit changed();
}

void ToolViewModel::setMaskFields(const QVector<MaskField>& aFields, const QString& aEmptyText) {
    if (mMaskFieldEntries == aFields && mMaskEmptyText == aEmptyText)
        return;
    mMaskFieldEntries = aFields;
    mMaskEmptyText = aEmptyText;
    // A target that disappeared (or became unavailable) must not leave the brush
    // aimed at a field the list no longer contains. Pick the first paintable one.
    if (maskFieldIndex() < 0 && !mMaskFieldEntries.isEmpty())
        selectMaskField(0, false);
    emit maskChanged();
}

void ToolViewModel::setMaskValue(double aValue) {
    const float value = float(std::min(std::max(aValue, 0.0), 1.0));
    if (mMask.value == value)
        return;
    mMask.value = value;
    emit maskChanged();
    emit changed();
}

void ToolViewModel::swapMaskValue() {
    // The two ends, not "the other end of the slider": the eyedropper may have sampled
    // 37%, and the key means "now paint the opposite of what I was painting".
    setMaskValue(mMask.value < 0.5f ? 1.0 : 0.0);
}

void ToolViewModel::sampleMaskValue(double aValue) { setMaskValue(aValue); }

void ToolViewModel::requestClearMask() { emit clearMaskRequested(); }

void ToolViewModel::beginFilterMaskPaint(core::TimeKeyType aLane, core::FilterMaskChannel aChannel) {
    mMask.setKind(ctrl::MaskTargetKind_FilterInput);
    for (int i = 0; i < kMaskLaneCount; ++i) {
        if (kMaskLanes[i] == aLane) {
            mMask.lane = aLane;
            break;
        }
    }
    mMask.setChannel(aChannel);
    activate(int(ctrl::ToolType_Mask));
    emit stateChanged();
    emit maskChanged();
    emit changed();
}

void ToolViewModel::beginLayerMaskPaint() {
    mMask.setKind(ctrl::MaskTargetKind_LayerMask);
    activate(int(ctrl::ToolType_Mask));
    emit stateChanged();
    emit maskChanged();
    emit changed();
}

QString ToolViewModel::sizeKeyTip() const {
    return tr("Brush size (%1 / %2)").arg(mKeyHints.brushSizeDecrease, mKeyHints.brushSizeIncrease);
}

QString ToolViewModel::hardnessKeyTip() const {
    return tr("Brush hardness (%1 / %2)")
        .arg(mKeyHints.brushHardnessDecrease, mKeyHints.brushHardnessIncrease);
}

//-------------------------------------------------------------------------------------------------
// the verbs
//-------------------------------------------------------------------------------------------------
void ToolViewModel::activate(int aType) {
    if (aType < 0 || aType >= ctrl::ToolType_TERM)
        return;
    const ctrl::ToolType type = ctrl::ToolType(aType);
    if (type == mTool)
        return;
    const ctrl::ToolType old = mTool;
    mTool = type;
    retargetBrush();
    // The host finalizes the OLD tool before it switches the driver (see the signal's
    // own note); everything this model derives from the tool is already up to date, so
    // a handler may read any property here.
    emit toolChanged(int(old), int(mTool));
    emit stateChanged();
    emit modeChanged();
    emit weightsChanged();
}

void ToolViewModel::setMode(int aIndex) {
    if (aIndex < 0 || aIndex == modeIndex())
        return;
    switch (mTool) {
    case ctrl::ToolType_SRT:
        if (aIndex > 1)
            return;
        mSRT.mode = aIndex;
        break;
    case ctrl::ToolType_Bone:
        if (aIndex < 0 || aIndex > 6)
            return;
        mBone.mode = aIndex == 6 ? ctrl::BoneEditMode_PaintWeights : ctrl::BoneEditMode(aIndex);
        break;
    case ctrl::ToolType_Pose:
        if (aIndex >= ctrl::PoseEditMode_TERM)
            return;
        mPose.mode = ctrl::PoseEditMode(aIndex);
        break;
    case ctrl::ToolType_Mesh:
        if (aIndex > 2)
            return;
        mMesh.mode = aIndex;
        break;
    case ctrl::ToolType_FFD:
        if (aIndex >= ctrl::FFDParam::Type_TERM)
            return;
        // setType, not the bare field: the struct documents that it keeps
        // brush.erase in step with the mode, and the editor's BrushMode does the same
        // when the param arrives - one spelling, both readers.
        mFFD.setType(ctrl::FFDParam::Type(aIndex));
        break;
    default:
        return;
    }
    retargetBrush();
    emit stateChanged();
    emit modeChanged();
    emit weightsChanged();
    emit changed();
}

void ToolViewModel::invertMask() { onInvertMaskRequested(); }

void ToolViewModel::nudgeBrush(double aSizeSteps, double aHardnessSteps) {
    mBrush->nudge(aSizeSteps, aHardnessSteps);
}

void ToolViewModel::adjustBrush(int aSizeSteps, int aHardnessSteps) {
    if (aSizeSteps == 0 && aHardnessSteps == 0)
        return;
    mBrush->adjustSize(aSizeSteps);
    mBrush->adjustHardness(aHardnessSteps);
}

//-------------------------------------------------------------------------------------------------
// SRT's key checkboxes and the FFD / Pose labels (catalog strings)
//-------------------------------------------------------------------------------------------------
QVariantList ToolViewModel::srtChecks() const {
    QVariantList out;
    const auto add = [&out](const char* aLabel, const char* aTip, const char* aKey) {
        QVariantMap map;
        map.insert(QStringLiteral("label"), QCoreApplication::translate("gui::tool::SRTPanel", aLabel));
        map.insert(QStringLiteral("tip"), QCoreApplication::translate("gui::tool::SRTPanel", aTip));
        map.insert(QStringLiteral("key"), QString::fromLatin1(aKey));
        out.append(map);
    };
    add(QT_TRANSLATE_NOOP("gui::tool::SRTPanel", "Create move key"),
        QT_TRANSLATE_NOOP("gui::tool::SRTPanel", "The transformation will create a move key regardless of its type."),
        "move");
    add(QT_TRANSLATE_NOOP("gui::tool::SRTPanel", "Create rotate key"),
        QT_TRANSLATE_NOOP("gui::tool::SRTPanel", "The transformation will create a rotate key regardless of its type."),
        "rotate");
    add(QT_TRANSLATE_NOOP("gui::tool::SRTPanel", "Create scale key"),
        QT_TRANSLATE_NOOP("gui::tool::SRTPanel", "The transformation will create a scale key regardless of its type."),
        "scale");
    add(QT_TRANSLATE_NOOP("gui::tool::SRTPanel", "Adjust centroid position"),
        QT_TRANSLATE_NOOP("gui::tool::SRTPanel",
                          "Move the centroid in relation to the image, the opposite will be true when not toggled."),
        "centroid");
    return out;
}

QVariantList ToolViewModel::srtCheckValues() const {
    return {mSRT.necessarilyMove, mSRT.necessarilyRotate, mSRT.necessarilyScale, mSRT.adjustPosition};
}

void ToolViewModel::setSrtCheck(int aIndex, bool aValue) {
    switch (aIndex) {
    case 0: setNecessarilyMove(aValue); break;
    case 1: setNecessarilyRotate(aValue); break;
    case 2: setNecessarilyScale(aValue); break;
    case 3: setAdjustPosition(aValue); break;
    default: break;
    }
}

QString ToolViewModel::blurLabel() const {
    return QCoreApplication::translate("gui::tool::FFDPanel", "Blur");
}

QString ToolViewModel::weightLabel() const {
    return QCoreApplication::translate("gui::tool::PosePanel", "Weight");
}

QString ToolViewModel::maskValueHint() const {
    // Both facts the row cannot show: the eyedropper gesture, and the swap key (which the
    // panel used to render as visible text).
    return tr("Alt + click samples the value under the pointer; %1 swaps between 0% and 100%")
        .arg(mKeyHints.swapMaskValue);
}

} // namespace gui
