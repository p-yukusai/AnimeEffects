#include "gui/qml/InspectorViewModel.h"

#include "cmnd/BasicCommands.h"
#include "cmnd/Listener.h"
#include "cmnd/ScopedMacro.h"
#include "core/BlurKey.h"
#include "core/Constant.h"
#include "core/DepthKey.h"
#include "core/FFDKey.h"
#include "core/FilterFrame.h"
#include "core/HsvKey.h"
#include "core/ImageKey.h"
#include "core/MoveKey.h"
#include "core/OpacityKey.h"
#include "core/PoseKey.h"
#include "core/RotateKey.h"
#include "core/ScaleKey.h"
#include "core/TimeKeyExpans.h"
#include "ctrl/TimeLineUtil.h"
#include "gui/ViaPoint.h"
#include "gui/qml/FilterStackModel.h"
#include "gui/qml/InspectorRowModel.h"
#include "gui/theme/Colors.h"
#include "img/BlendMode.h"
#include "img/ResourceNode.h"
#include "util/MathUtil.h"

#include <QCoreApplication>
#include <QVariantMap>

namespace gui {

namespace {
QString trInspector(const char* aText) {
    return QCoreApplication::translate("gui::prop::InspectorViewModel", aText);
}

class ObjectNodeAttrNotifier: public cmnd::Listener {
public:
    ObjectNodeAttrNotifier(core::Project& aProject, core::ObjectNode& aNode):
        mProject(aProject), mNode(aNode) {}
    void onExecuted() override { mProject.onNodeAttributeModified(mNode, false); }
    void onUndone() override { mProject.onNodeAttributeModified(mNode, true); }
    void onRedone() override { mProject.onNodeAttributeModified(mNode, false); }

private:
    core::Project& mProject;
    core::ObjectNode& mNode;
};

QVector<img::BlendMode> blendModes() {
    return {
        img::BlendMode_Normal,
        img::BlendMode_Darken,
        img::BlendMode_Multiply,
        img::BlendMode_ColorBurn,
        img::BlendMode_LinearBurn,
        img::BlendMode_DarkerColor,
        img::BlendMode_Lighten,
        img::BlendMode_Screen,
        img::BlendMode_ColorDodge,
        img::BlendMode_GlowDodge,
        img::BlendMode_LinearDodge,
        img::BlendMode_AddGlow,
        img::BlendMode_LighterColor,
        img::BlendMode_Overlay,
        img::BlendMode_SoftLight,
        img::BlendMode_HardLight,
        img::BlendMode_VividLight,
        img::BlendMode_LinearLight,
        img::BlendMode_PinLight,
        img::BlendMode_HardMix,
        img::BlendMode_Difference,
        img::BlendMode_Exclusion,
        img::BlendMode_Subtract,
        img::BlendMode_Divide,
        img::BlendMode_Hue,
        img::BlendMode_Saturation,
        img::BlendMode_Color,
        img::BlendMode_Luminosity,
    };
}

// The five group dividers the widget-era dropdown carried as
// QComboBox::insertSeparator() items: after Normal, after DarkerColor, after
// LighterColor, after HardMix, after Divide (prop/prop_ConstantPanel.cpp, before
// the QML port). Named by MODE, never by row index: the delegate draws the rule
// from `separatorAfter`, the name the export and destination tables already use
// for a rule drawn after a block, so an index table would go stale the day
// blendModes() is reordered or a mode is inserted - which img/BlendMode.h
// explicitly allows, since that order is a UI convenience and not a persisted
// contract.
bool endsBlendGroup(img::BlendMode aMode) {
    switch (aMode) {
    case img::BlendMode_Normal:
    case img::BlendMode_DarkerColor:
    case img::BlendMode_LighterColor:
    case img::BlendMode_HardMix:
    case img::BlendMode_Divide:
        return true;
    default:
        return false;
    }
}
} // namespace

InspectorViewModel::InspectorViewModel(ViaPoint* aViaPoint, QObject* aParent): QObject(aParent), mViaPoint(aViaPoint) {
    mMove = new InspectorRowModel(InspectorRowModel::Kind::Move, *this, this);
    mRotate = new InspectorRowModel(InspectorRowModel::Kind::Rotate, *this, this);
    mScale = new InspectorRowModel(InspectorRowModel::Kind::Scale, *this, this);
    mDepth = new InspectorRowModel(InspectorRowModel::Kind::Depth, *this, this);
    mOpacity = new InspectorRowModel(InspectorRowModel::Kind::Opacity, *this, this);
    mImage = new InspectorRowModel(InspectorRowModel::Kind::Image, *this, this);
    mPose = new InspectorRowModel(InspectorRowModel::Kind::Pose, *this, this);
    mFFD = new InspectorRowModel(InspectorRowModel::Kind::FFD, *this, this);
    mFilters = new FilterStackModel(*this, this);

    for (img::BlendMode mode : blendModes()) {
        QVariantMap option;
        option.insert(QStringLiteral("label"), img::getBlendNameFromBlendMode(mode));
        option.insert(QStringLiteral("value"), int(mode));
        // Decoration for the QML delegate's 9px separator band - the only trace
        // of the old control's insertSeparator() rows. A FLAG on the option, not
        // an extra list entry, keeps options[index] 1:1 with the modes: `value`
        // and `label` keep the meaning refreshAttributes and setBlendIndex read,
        // and activated(index) keeps meaning "options[index]".
        option.insert(QStringLiteral("separatorAfter"), endsBlendGroup(mode));
        mBlendOptions.append(option);
    }
}

core::TimeLine* InspectorViewModel::timeLine() const { return mTarget ? mTarget->timeLine() : nullptr; }

int InspectorViewModel::frame() const { return mProject ? mProject->animator().currentFrame().get() : 0; }

QString InspectorViewModel::blendLabel() const {
    return QCoreApplication::translate("gui::prop::ConstantPanel", "Blend");
}

QString InspectorViewModel::clippedLabel() const {
    return QCoreApplication::translate("gui::prop::ConstantPanel", "Clipped");
}
QString InspectorViewModel::centroidLabel() const {
    return QCoreApplication::translate("gui::prop::MoveKeyGroup", "Centroid");
}
QString InspectorViewModel::pathLabel() const {
    return QCoreApplication::translate("gui::prop::MoveKeyGroup", "Path");
}
QString InspectorViewModel::smoothLabel() const {
    return QCoreApplication::translate("gui::prop::MoveKeyGroup", "Smooth");
}
QString InspectorViewModel::smoothTooltip() const {
    return QCoreApplication::translate("gui::prop::MoveKeyGroup",
        "Curve the position path between keys instead of straight segments.");
}
QString InspectorViewModel::centerLabel() const {
    return QCoreApplication::translate("gui::prop::ImageKeyGroup", "Center");
}
QString InspectorViewModel::meshCellLabel() const {
    // The internal value is a grid CELL size, but the user authors the distance
    // between mesh vertices. "Cell" exposed the implementation and named nothing
    // they could see; "spacing" is the picture, and smaller = denser.
    return QCoreApplication::translate("gui::prop::ImageKeyGroup", "Mesh spacing");
}
QString InspectorViewModel::meshCellTooltip() const {
    return QCoreApplication::translate("gui::prop::ImageKeyGroup",
        "Distance between mesh vertices, in pixels. Smaller values create a denser mesh.");
}

void InspectorViewModel::setProject(core::Project* aProject) {
    setMeshPreviewActive(false);
    setMaskPaintState(nullptr, false, false, core::TimeKeyType_TERM, core::FilterMaskChannel_Amount);
    mProject = aProject;
    mTarget = nullptr;
    mAccessor.setProject(aProject);
    mAccessor.setTarget(nullptr);
    mFilters->setProject(aProject);
    mFilters->setTarget(nullptr);
    refresh();
}

void InspectorViewModel::setTarget(core::ObjectNode* aTarget) {
    // A preview belongs to the node the field was hovering. Do not let it follow
    // a selection change onto a different mesh.
    setMeshPreviewActive(false);
    setMaskPaintState(nullptr, false, false, core::TimeKeyType_TERM, core::FilterMaskChannel_Amount);
    mTarget = aTarget;
    mAccessor.setTarget(aTarget);
    mFilters->setTarget(aTarget);
    refresh();
}

void InspectorViewModel::setPlaybackActive(bool aActive) {
    if (mPlaybackActive == aActive)
        return;
    mPlaybackActive = aActive;
    refresh();
    emit changed();
}

void InspectorViewModel::setMeshPreviewActive(bool aActive) {
    if (!aActive) {
        if (!mMeshPreviewActive)
            return;
        mMeshPreviewActive = false;
        onMeshPreviewRequested(nullptr, false);
        return;
    }
    if (mMeshPreviewActive || !mTarget || !mTarget->timeLine())
        return;
    mMeshPreviewActive = true;
    onMeshPreviewRequested(mTarget, true);
}

void InspectorViewModel::setMaskPaintState(
    core::ObjectNode* aNode, bool aLayerMask, bool aFilterInput, core::TimeKeyType aLane,
    core::FilterMaskChannel aChannel
) {
    const bool onTarget = aNode && aNode == mTarget;
    const bool layer = onTarget && aLayerMask;
    const bool filter = onTarget && aFilterInput;
    const auto lane = filter ? aLane : core::TimeKeyType_TERM;
    const auto channel = filter ? aChannel : core::FilterMaskChannel_Amount;
    if (mMaskPaintActive == layer && mMaskPaintIsFilter == filter && mMaskPaintLane == lane
        && mMaskPaintChannel == channel) {
        return;
    }
    mMaskPaintActive = layer;
    mMaskPaintIsFilter = filter;
    mMaskPaintLane = lane;
    mMaskPaintChannel = channel;
    mFilters->refreshMaskPaintState();
    emit maskPaintStateChanged();
}


// ---- the node's MATTE -------------------------------------------------------
//
// The card's state is read from the DOCUMENT here rather than stored: the mask can
// also change from the lane's key menu, the tool's stroke or an undo, and a cached
// flag would go stale in exactly those cases.

namespace {

const core::TimeLine* maskLine(const core::ObjectNode* aTarget) {
    return aTarget ? aTarget->timeLine() : nullptr;
}

} // namespace

bool InspectorViewModel::maskPresent() const {
    const core::TimeLine* line = maskLine(mTarget);
    return line && (line->baseValue(core::TimeKeyType_Mask) || !line->map(core::TimeKeyType_Mask).isEmpty());
}

bool InspectorViewModel::maskKeyed() const {
    const core::TimeLine* line = maskLine(mTarget);
    return line && !line->map(core::TimeKeyType_Mask).isEmpty();
}

bool InspectorViewModel::maskHasKeyAtFrame() const {
    const core::TimeLine* line = maskLine(mTarget);
    return line && line->map(core::TimeKeyType_Mask).value(frame()) != nullptr;
}

int InspectorViewModel::maskKeyCount() const {
    const core::TimeLine* line = maskLine(mTarget);
    return line ? line->map(core::TimeKeyType_Mask).size() : 0;
}

QImage InspectorViewModel::maskThumbnail(const QSize& aRequested) const {
    const core::TimeLine* line = maskLine(mTarget);
    if (!line)
        return QImage();

    // The field the frame SHOWS: the key at the playhead when there is one,
    // otherwise the base value. A blended frame's thumbnail is the blended field,
    // which is what makes the card's between-keys state legible ("this is what you
    // are looking at, and it is read-only").
    const core::MaskKey* key = nullptr;
    if (const core::TimeKey* atFrame = line->map(core::TimeKeyType_Mask).value(frame()))
        key = static_cast<const core::MaskKey*>(atFrame);
    else
        key = static_cast<const core::MaskKey*>(line->baseValue(core::TimeKeyType_Mask));
    if (!key || !key->data().isValid())
        return QImage();

    QImage field = key->data().field();
    if (aRequested.isValid() && !aRequested.isEmpty())
        field = field.scaled(aRequested, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // THE THUMBNAIL IS THE MATTE ITSELF: white = visible, black = hidden, which is how
    // an alpha channel reads anywhere. (The first version showed only what was HIDDEN,
    // in the rubylith ink - red on grey - which invented a meaning the reader had to be
    // told, and made an untouched matte look like a filled control.)
    return field;
}

void InspectorViewModel::addMask() {
    if (!mProject || !mTarget)
        return;
    ctrl::TimeLineUtil::addMask(*mProject, *mTarget);
    refresh();
    // CREATING A MATTE IS ASKING TO PAINT IT: switch to the Mask tool with this node targeted, so
    // the next stroke lands on the matte you just made (reported: "adding a mask doesn't
    // immediately go into painting even though presumably if you create a mask, you want to paint
    // it").
    onLayerMaskPaintRequested();
}

void InspectorViewModel::removeMask() {
    if (!mProject || !mTarget)
        return;
    ctrl::TimeLineUtil::removeMask(*mProject, *mTarget);
    refresh();
}

void InspectorViewModel::invertMask() {
    if (!mProject || !mTarget)
        return;
    ctrl::TimeLineUtil::invertMask(*mProject, *mTarget, frame());
    refresh();
}

void InspectorViewModel::toggleMaskKey() {
    if (!mProject || !mTarget)
        return;
    ctrl::TimeLineUtil::toggleMaskKey(*mProject, *mTarget, frame());
    refresh();
}

void InspectorViewModel::requestLayerMaskPaint() {
    if (!mTarget)
        return;
    onLayerMaskPaintRequested();
}

void InspectorViewModel::refresh() {
    refreshAttributes();
    for (InspectorRowModel* row : {mMove, mRotate, mScale, mDepth, mOpacity, mImage, mPose, mFFD})
        row->refresh();
    mFilters->refresh();
    // a mask thumbnail is an IMAGE: its URL has to change for QML to re-ask the
    // provider, and the state flags ride the same signal
    ++mMaskRevision;
    emit changed();
}

void InspectorViewModel::refreshAttributes() {
    const bool hadBlend = mBlendEnabled;
    const bool hadClip = mClippedEnabled;
    const int oldIndex = mBlendIndex;
    const bool oldClipped = mClipped;

    const core::Renderer* renderer = mTarget ? mTarget->renderer() : nullptr;
    mBlendEnabled = renderer && renderer->hasBlendMode();
    mClippedEnabled = renderer != nullptr;
    if (mBlendEnabled) {
        const int mode = int(renderer->blendMode());
        int index = 0;
        for (int i = 0; i < mBlendOptions.size(); ++i) {
            if (mBlendOptions.at(i).toMap().value(QStringLiteral("value")).toInt() == mode) {
                index = i;
                break;
            }
        }
        mBlendIndex = index;
    } else {
        mBlendIndex = 0;
    }
    mClipped = renderer && renderer->isClipped();

    if (hadBlend != mBlendEnabled || hadClip != mClippedEnabled || oldIndex != mBlendIndex
        || oldClipped != mClipped)
        emit changed();
}

void InspectorViewModel::refreshKeys() {
    for (InspectorRowModel* row : {mMove, mRotate, mScale, mDepth, mOpacity, mImage, mPose, mFFD})
        row->refresh();
    mFilters->refresh();
    // THE MATTE'S STATE IS A LANE FACT, not a value the rows hold: `maskPresent`/`maskKeyed`/
    // `maskHasKeyAtFrame` (and the thumbnail's revision) are read by the card and must be
    // re-announced when a key anywhere in the node lands. They used to ride `refresh()` only, which
    // a key event does not call - so the dot, the thumbnail and the actions stayed as they were
    // until the SELECTION changed (reported). One call covers every mutation path.
    notifyMaskState();
}

void InspectorViewModel::notifyMaskState() {
    ++mMaskRevision;
    emit changed();
}

void InspectorViewModel::refreshFrame() {
    for (InspectorRowModel* row : {mMove, mRotate, mScale, mDepth, mOpacity, mImage, mPose, mFFD})
        row->refresh();
    mFilters->refresh();
    // a matte can be ANIMATED: the frame decides which field the card shows and whether it can be
    // painted, so a frame step re-announces it too
    notifyMaskState();
}

void InspectorViewModel::refreshTheme() {
    for (InspectorRowModel* row : {mMove, mRotate, mScale, mDepth, mOpacity, mImage, mPose, mFFD})
        row->refreshTheme();
    mFilters->refreshTheme();
    emit changed();
}

double InspectorViewModel::translateMin() const { return core::Constant::transMin(); }
double InspectorViewModel::translateMax() const { return core::Constant::transMax(); }
double InspectorViewModel::rotateMin() const {
    return util::MathUtil::getDegreeFromRadian(core::Constant::rotateMin());
}
double InspectorViewModel::rotateMax() const {
    return util::MathUtil::getDegreeFromRadian(core::Constant::rotateMax());
}
double InspectorViewModel::scaleMin() const { return core::Constant::scaleMin(); }
double InspectorViewModel::scaleMax() const { return core::Constant::scaleMax(); }
int InspectorViewModel::imageCellMin() const { return core::Constant::imageCellSizeMin(); }
int InspectorViewModel::imageCellMax() const { return core::Constant::imageCellSizeMax(); }
double InspectorViewModel::blurMax() const { return 512.0; }
double InspectorViewModel::blurAngleMin() const { return -100000.0; }
double InspectorViewModel::blurAngleMax() const { return 100000.0; }
int InspectorViewModel::hsvHueMax() const { return 360; }
int InspectorViewModel::hsvComponentMax() const { return 200; }
double InspectorViewModel::waveStrengthMin() const { return -512.0; }
double InspectorViewModel::waveStrengthMax() const { return 512.0; }
double InspectorViewModel::waveWavelengthMin() const { return 1.0; }
double InspectorViewModel::waveWavelengthMax() const { return 4096.0; }
double InspectorViewModel::wavePhaseMin() const { return -1000.0; }
double InspectorViewModel::wavePhaseMax() const { return 1000.0; }
double InspectorViewModel::waveMixMax() const { return 1.0; }
double InspectorViewModel::waveSpeedMin() const { return -8.0; }
double InspectorViewModel::waveSpeedMax() const { return 8.0; }
double InspectorViewModel::waveCrestMin() const { return 0.25; }
double InspectorViewModel::waveCrestMax() const { return 4.0; }
double InspectorViewModel::waveDualScaleMin() const { return 0.1; }
double InspectorViewModel::waveDualScaleMax() const { return 4.0; }

void InspectorViewModel::requestMaskPaint(core::TimeKeyType aLane, core::FilterMaskChannel aChannel) {
    onMaskPaintRequested(aLane, aChannel);
}

void InspectorViewModel::setBlendIndex(int aIndex) {
    if (!mProject || !mTarget || !mTarget->renderer() || !mBlendEnabled)
        return;
    if (aIndex < 0 || aIndex >= mBlendOptions.size())
        return;
    const auto mode = img::BlendMode(mBlendOptions.at(aIndex).toMap().value(QStringLiteral("value")).toInt());
    if (mode == mTarget->renderer()->blendMode())
        return;
    assignBlend(mode);
}

void InspectorViewModel::assignBlend(img::BlendMode aMode) {
    core::Renderer* renderer = mTarget->renderer();
    if (!renderer)
        return;
    const img::BlendMode prev = renderer->blendMode();
    cmnd::ScopedMacro macro(mProject->commandStack(), trInspector("update a blending mode"));
    macro.grabListener(new ObjectNodeAttrNotifier(*mProject, *mTarget));
    const auto exec = [renderer, aMode]() { renderer->setBlendMode(aMode); };
    const auto undo = [renderer, prev]() { renderer->setBlendMode(prev); };
    mProject->commandStack().push(new cmnd::Delegatable(exec, undo));
    refreshAttributes();
}

void InspectorViewModel::setClipped(bool aValue) {
    if (!mProject || !mTarget || !mTarget->renderer() || !mClippedEnabled)
        return;
    if (aValue == mTarget->renderer()->isClipped())
        return;
    core::Renderer* renderer = mTarget->renderer();
    const bool prev = renderer->isClipped();
    cmnd::ScopedMacro macro(mProject->commandStack(), trInspector("update a clipping flag"));
    macro.grabListener(new ObjectNodeAttrNotifier(*mProject, *mTarget));
    const auto exec = [renderer, aValue]() { renderer->setClipped(aValue); };
    const auto undo = [renderer, prev]() { renderer->setClipped(prev); };
    mProject->commandStack().push(new cmnd::Delegatable(exec, undo));
    refreshAttributes();
}

void InspectorViewModel::browseImageAsset() {
    if (!mProject || !mTarget || !mViaPoint)
        return;
    if (img::ResourceNode* node = mViaPoint->requireOneResource()) {
        mAccessor.assignImageResource(*node);
        refreshKeys();
    }
}

void InspectorViewModel::addImageKey() {
    if (!mProject || !mTarget || !mViaPoint)
        return;
    if (img::ResourceNode* node = mViaPoint->requireOneResource()) {
        mAccessor.knockNewImage(node->handle());
        refreshKeys();
    }
}

} // namespace gui
