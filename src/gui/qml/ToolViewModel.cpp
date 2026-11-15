#include "gui/qml/ToolViewModel.h"

#include "gui/qml/FilterNames.h"

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
const int kMaskLayerTarget = kMaskLaneCount; // the fourth target: the node's own matte

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
    mBrush = new ToolBrushModel(this);
    // an EDIT of the brush is an edit of the active tool's param (a retarget is not)
    connect(mBrush, &ToolBrushModel::edited, this, &ToolViewModel::changed);
    retargetBrush();
}

void ToolViewModel::setViewSetting(MainViewSetting* aSetting) {
    mViewSetting = aSetting;
    emit viewChanged();
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
        // A card named the field, so the heading IS the field's own name ("Wave Deform
        // amount", and "Layer mask" for the matte - which is both the kind and the
        // name). Picked from the toolbox the field is still a QUESTION, so the heading
        // names the KIND and the chooser sits under it.
        if (mMaskPinned)
            return mMask.isLayerMask() ? tr("Layer mask") : maskFieldLabel();
        return tr("Filter mask");
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
            entry(QCoreApplication::translate("gui::tool::BonePanel", "Adjust influence"), QStringLiteral("broadcast")),
            entry(QCoreApplication::translate("gui::tool::BonePanel", "Paint influence"), QStringLiteral("paint-brush")),
            entry(QCoreApplication::translate("gui::tool::BonePanel", "Erase influence"), QStringLiteral("eraser")),
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
    case ctrl::ToolType_Bone: return int(mBone.mode);
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
        return mBone.mode == ctrl::BoneEditMode_PaintInfl || mBone.mode == ctrl::BoneEditMode_EraseInfl;
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
    for (int i = 0; i < kMaskLaneCount; ++i) {
        const QString lane = filterDisplayName(kMaskLanes[i]);
        // A translatable FORMAT per channel, never name + separator + name: word order is
        // not the same in every language, and a bare separator is what makes a label read
        // as machine-assembled.
        out.append(entry(tr("%1 amount").arg(lane), QString()));
        out.append(entry(tr("%1 time offset").arg(lane), QString()));
    }
    out.append(entry(tr("Layer mask"), QString()));
    return out;
}

int ToolViewModel::maskFieldIndex() const {
    if (mMask.isLayerMask())
        return kMaskLayerTarget * 2; // the matte sits after the six filter fields
    for (int i = 0; i < kMaskLaneCount; ++i) {
        if (kMaskLanes[i] == mMask.lane)
            return i * 2 + int(mMask.channel);
    }
    return kMaskLayerTarget * 2;
}

QString ToolViewModel::maskFieldLabel() const {
    const QVariantList fields = maskFields();
    const int index = maskFieldIndex();
    if (index < 0 || index >= fields.size())
        return QString();
    return fields.at(index).toMap().value(QStringLiteral("label")).toString();
}

void ToolViewModel::setMaskField(int aIndex) {
    if (aIndex < 0 || aIndex > kMaskLayerTarget * 2 || aIndex == maskFieldIndex())
        return;
    if (aIndex == kMaskLayerTarget * 2) {
        // the node's own matte: no lane, no channel, and a neutral of reveal-all
        mMask.setKind(ctrl::MaskTargetKind_LayerMask);
    } else {
        // setKind FIRST: coming back from the matte, the kind change is what makes the
        // next stroke read a filter input at all. MaskParam only touches the value when
        // the kind or the channel actually changes, exactly as the widget did.
        mMask.setKind(ctrl::MaskTargetKind_FilterInput);
        mMask.lane = kMaskLanes[aIndex / 2];
        // the channel seeds the useful value for its own mask (amount -> 0, offset -> 1)
        mMask.setChannel(core::FilterMaskChannel(aIndex % 2));
    }
    emit stateChanged();   // the heading and the caption follow the target
    emit maskChanged();    // setKind / setChannel may have reset the value
    emit changed();
}

void ToolViewModel::unpinMaskTarget() {
    if (!mMaskPinned)
        return;
    mMaskPinned = false;
    emit stateChanged();
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
    // activate() CLEARS the pin (a toolbox entry asks the question), so the card entry
    // sets it after the switch - which is also the order the user sees: the panel appears
    // already naming the field.
    activate(int(ctrl::ToolType_Mask));
    mMaskPinned = true;
    emit stateChanged();
    emit maskChanged();
    emit changed();
}

void ToolViewModel::beginLayerMaskPaint() {
    mMask.setKind(ctrl::MaskTargetKind_LayerMask);
    activate(int(ctrl::ToolType_Mask));
    mMaskPinned = true;
    emit stateChanged();
    emit maskChanged();
    emit changed();
}

//-------------------------------------------------------------------------------------------------
// view settings
//-------------------------------------------------------------------------------------------------
bool ToolViewModel::showMesh() const { return mViewSetting && mViewSetting->showLayerMesh; }
bool ToolViewModel::cutOutsideFrame() const { return mViewSetting && mViewSetting->cutImagesByTheFrame; }
bool ToolViewModel::flipCanvas() const { return mViewSetting && mViewSetting->flipCanvas; }

void ToolViewModel::setShowMesh(bool aValue) {
    if (!mViewSetting || mViewSetting->showLayerMesh == aValue)
        return;
    mViewSetting->showLayerMesh = aValue;
    emit viewChanged();
    emit viewSettingsChanged();
}

void ToolViewModel::setCutOutsideFrame(bool aValue) {
    if (!mViewSetting || mViewSetting->cutImagesByTheFrame == aValue)
        return;
    mViewSetting->cutImagesByTheFrame = aValue;
    emit viewChanged();
    emit viewSettingsChanged();
}

void ToolViewModel::setFlipCanvas(bool aValue) {
    if (!mViewSetting || mViewSetting->flipCanvas == aValue)
        return;
    mViewSetting->flipCanvas = aValue;
    emit viewChanged();
    emit viewSettingsChanged();
}

QString ToolViewModel::viewTitle() const {
    return QCoreApplication::translate("gui::ToolWidget", "View Settings");
}
QString ToolViewModel::showMeshTip() const {
    return QCoreApplication::translate("gui::ToolWidget", "Show mesh");
}
QString ToolViewModel::cutOutsideFrameTip() const {
    return QCoreApplication::translate("gui::ToolWidget", "Do not draw outside the frame");
}
QString ToolViewModel::rotateCounterClockwiseTip() const {
    return QCoreApplication::translate("gui::ToolWidget", "Rotate the canvas counterclockwise (%1)")
        .arg(mKeyHints.rotateCounterClockwise);
}
QString ToolViewModel::resetRotationTip() const {
    return QCoreApplication::translate("gui::ToolWidget", "Reset rotation of the canvas (%1)")
        .arg(mKeyHints.resetRotation);
}
QString ToolViewModel::rotateClockwiseTip() const {
    return QCoreApplication::translate("gui::ToolWidget", "Rotate the canvas clockwise (%1)")
        .arg(mKeyHints.rotateClockwise);
}
QString ToolViewModel::flipCanvasTip() const {
    return QCoreApplication::translate("gui::ToolWidget", "Flip canvas");
}

QString ToolViewModel::sizeKeyTip() const {
    return tr("Brush size (%1 / %2)").arg(mKeyHints.brushSizeDecrease, mKeyHints.brushSizeIncrease);
}

QString ToolViewModel::hardnessKeyTip() const {
    return tr("Brush hardness (%1 / %2)")
        .arg(mKeyHints.brushHardnessDecrease, mKeyHints.brushHardnessIncrease);
}

// The three rotation verbs are TRIGGERS, not states: the setting is a flag the main
// view consumes on the same call (MainDisplayWidget::onViewSettingChanged), which is
// why it is raised, announced and lowered again - the deleted ViewPanel did exactly
// this and a latched flag would rotate on every later view change.
void ToolViewModel::rotateCounterClockwise() {
    if (!mViewSetting)
        return;
    mViewSetting->rotateViewACW = true;
    emit viewSettingsChanged();
    mViewSetting->rotateViewACW = false;
}

void ToolViewModel::resetRotation() {
    if (!mViewSetting)
        return;
    mViewSetting->resetRotateView = true;
    emit viewSettingsChanged();
    mViewSetting->resetRotateView = false;
}

void ToolViewModel::rotateClockwise() {
    if (!mViewSetting)
        return;
    mViewSetting->rotateViewCW = true;
    emit viewSettingsChanged();
    mViewSetting->rotateViewCW = false;
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
    // Picking the Mask tool from the toolbox ASKS which field, so the card's pin is
    // dropped - a card entry re-pins it after this call (see beginFilterMaskPaint).
    if (type == ctrl::ToolType_Mask)
        mMaskPinned = false;
    retargetBrush();
    // The host finalizes the OLD tool before it switches the driver (see the signal's
    // own note); everything this model derives from the tool is already up to date, so
    // a handler may read any property here.
    emit toolChanged(int(old), int(mTool));
    emit stateChanged();
    emit modeChanged();
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
        if (aIndex >= ctrl::BoneEditMode_TERM)
            return;
        mBone.mode = ctrl::BoneEditMode(aIndex);
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
