#include "gui/ToolWidget.h"

#include "gui/KeyCommandMap.h"
#include "gui/QmlHost.h"
#include "gui/qml/FilterNames.h"
#include "gui/qml/QmlTheme.h"
#include "gui/qml/ToolViewModel.h"
#include "gui/theme/Colors.h"

#include "core/MaskKey.h"
#include "core/TimeKeyExpans.h"
#include "ctrl/TimeLineUtil.h"

#include <QQuickWidget>
#include <QQmlContext>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>

namespace gui {

namespace {
// 236 fits the toolbox's seven 26px buttons on ONE row (7*26 + 6*2 = 194 of the 212 the
// 12px margins leave), where the widget dock's 172 wrapped the seventh onto a line of its
// own - the measured "mask brush sits alone at (22,156)". The scene adapts to any width it
// is given; the hint is what the dock opens at, and the minimum is where the row grid
// still holds (76px label + a slider + a 58px field).
const int kToolDockMinWidth = 200;
} // namespace

ToolWidget::ToolWidget(ViaPoint& aViaPoint,
                       KeyCommandMap& aKeyCommandMap,
                       const LocaleParam& aLocale,
                       const QSize& aSizeHint,
                       QWidget* aParent):
    QWidget(aParent),
    mViaPoint(aViaPoint),
    mSizeHint(aSizeHint),
    mDriver(),
    mDriverTool(ctrl::ToolType_TERM),
    meshFromFunction(false),
    mModel(),
    mTheme(),
    mQuick() {
    setFocusPolicy(Qt::NoFocus);
    setObjectName("toolWidget"); // the dock surface's QSS id
    setMinimumWidth(kToolDockMinWidth);
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);

    mModel = new ToolViewModel(this);
    // Ctrl+I with the Mask tool in hand asks for the matte's edit; the properties dock owns it
    mModel->onInvertMaskRequested.connect([this]() { onInvertMaskRequested(); });
    mTheme = new QmlTheme(this);

    // The dock SPEAKS the user's bindings: the view strip's rotation tooltips and the
    // hint under the brush rows name the keys that are actually bound, and every one of
    // them is rebindable.
    ToolKeyHints hints;
    const auto text = [&aKeyCommandMap](const char* aName) {
        auto* key = aKeyCommandMap.get(QString::fromLatin1(aName));
        return key ? key->binding.text() : QString();
    };
    hints.brushSizeDecrease = text("BrushSizeDecrease");
    hints.brushSizeIncrease = text("BrushSizeIncrease");
    hints.brushHardnessDecrease = text("BrushHardnessDecrease");
    hints.brushHardnessIncrease = text("BrushHardnessIncrease");
    hints.swapMaskValue = text("SwapMaskValue");
    mModel->setKeyHints(hints);

    // ---- the scene -----------------------------------------------------------
    mQuick = new KeyForwardingQuickWidget(this);
    mQuick->setResizeMode(QQuickWidget::SizeRootObjectToView);
    mQuick->rootContext()->setContextProperty(QStringLiteral("toolModel"), mModel);
    mQuick->rootContext()->setContextProperty(QStringLiteral("theme"), mTheme);
    // QML's Qt.application.font is the PROCESS font; the dock's face comes from
    // LocaleParam and is applied to widgets through standard.ssa, so the scene needs it
    // explicitly to match 8pt Geist (or the locale override).
    mQuick->rootContext()->setContextProperty(QStringLiteral("uiFont"), qmlUiFont(aLocale));
    mQuick->setClearColor(theme::Colors::current().base);
    mQuick->setSource(QUrl(QStringLiteral("qrc:/AnimeEffects/Dialogs/qml/ToolsPanel.qml")));
    connect(mQuick, &QQuickWidget::statusChanged, this, [this](QQuickWidget::Status aStatus) {
        if (aStatus == QQuickWidget::Error) {
            for (const QQmlError& error : mQuick->errors())
                qWarning().noquote() << error.toString();
        }
    });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(mQuick);

    // ---- the model's two-way wiring -----------------------------------------
    connect(mModel, &ToolViewModel::toolChanged, this, &ToolWidget::onModelToolChanged);
    // the mask editor's TARGET can move without the tool moving (a filter card's Paint switches a
    // Mask tool that is already up), so the "already painting this matte" state has its own signal
    connect(mModel, &ToolViewModel::maskChanged, this, [this]() { onMaskTargetChanged(); });
    connect(mModel, &ToolViewModel::changed, this, &ToolWidget::onParamUpdated);
    connect(mModel, &ToolViewModel::clearMaskRequested, this, [this]() {
        if (mDriver)
            mDriver->clearMask();
    });
    connect(mModel, &ToolViewModel::weightsActionRequested, this, &ToolWidget::onWeightsAction);

    // ---- key bindings -------------------------------------------------------
    // The tool-selection bindings press the toolbox's own buttons through the model, so
    // a bound key and a click take exactly the same route.
    struct Selection {
        const char* command;
        ctrl::ToolType type;
    };
    static const Selection kSelections[] = {
        {"SelectCursor", ctrl::ToolType_Cursor}, {"SelectSRT", ctrl::ToolType_SRT},
        {"SelectBone", ctrl::ToolType_Bone},     {"SelectPose", ctrl::ToolType_Pose},
        {"SelectMesh", ctrl::ToolType_Mesh},     {"SelectFFD", ctrl::ToolType_FFD},
    };
    for (const Selection& selection : kSelections) {
        auto* key = aKeyCommandMap.get(QString::fromLatin1(selection.command));
        if (!key)
            continue;
        const ctrl::ToolType type = selection.type;
        key->addListener([this, type]() { mModel->activate(int(type)); });
    }
    {
        const auto notch = [this, &aKeyCommandMap](const char* aCommand, int aSizeSteps, int aHardnessSteps) {
            auto* key = aKeyCommandMap.get(QString::fromLatin1(aCommand));
            if (!key)
                return;
            key->addListener([this, aSizeSteps, aHardnessSteps]() { adjustBrush(aSizeSteps, aHardnessSteps); });
        };
        notch("BrushSizeIncrease", +1, 0);
        notch("BrushSizeDecrease", -1, 0);
        notch("BrushHardnessIncrease", 0, +1);
        notch("BrushHardnessDecrease", 0, -1);
    }
    if (auto* key = aKeyCommandMap.get(QStringLiteral("InvertMask"))) {
        // GUARDED ON THE TOOL, like the value swap: Ctrl+I is the image editor's invert and means
        // nothing without a matte in hand.
        key->addListener([this]() {
            if (ctrl::ToolType(mModel->tool()) == ctrl::ToolType_Mask)
                mModel->invertMask();
        });
    }
    if (auto* key = aKeyCommandMap.get(QStringLiteral("SwapMaskValue"))) {
        // GUARDED ON THE TOOL: the mask's value swap is only meaningful while the mask
        // brush is in hand, so X stays free for every other tool (and for anything bound
        // to it later). This is the painter's X - the one key that swaps the two inks.
        key->addListener([this]() {
            if (ctrl::ToolType(mModel->tool()) == ctrl::ToolType_Mask)
                mModel->swapMaskValue();
        });
    }
}

ToolWidget::~ToolWidget() {
    // The scene binds to the model and theme context objects. Tear it down first, as
    // every dialog shell here does, so teardown never re-reads a destroyed context
    // property.
    delete mQuick;
    mQuick = nullptr;
}

void ToolWidget::setDriver(ctrl::Driver* aDriver) {
    // NO DISCONNECT THROUGH THE OLD POINTER: DriverHolder::create destroys the
    // previous driver before this call, so `mDriver` here may already be freed
    // (the crash this code first shipped: setDriver -> Signaler::disconnect on a
    // deleted Driver). The slot was a function inside that driver's own Signaler
    // and died with it; the id is simply replaced.
    mWeightsSlot = util::SlotId();

    mDriver = aDriver;
    mDriverTool = ctrl::ToolType_TERM;
    mModel->setDriverActive(aDriver != nullptr);
    refreshMaskTargets();

    if (mDriver) {
        // Connected before setTool: creating the Bone editor already announces
        // its (empty) selection.
        mWeightsSlot = mDriver->onBoneWeightsStateChanged.connect([this]() { syncWeightsState(); });
        // The tool the model remembers (the dock keeps it across projects, which is what
        // the widget did) is what the new driver starts on.
        mDriverTool = ctrl::ToolType(mModel->tool());
        mDriver->setTool(mDriverTool);
        pushParam();
        syncWeightsState();
        refreshMaskTargets();
    } else {
        mModel->setWeightsState(ctrl::bone::WeightState());
    }
}

void ToolWidget::onSelectionChanged(core::ObjectNode*) { refreshMaskTargets(); }

void ToolWidget::onFrameUpdated() { refreshMaskTargets(); }

void ToolWidget::refreshMaskTargets() {
    core::Project* project = mViaPoint.project();
    core::ObjectNode* target = mDriver ? mDriver->currentTarget() : nullptr;
    core::TimeLine* line = target ? target->timeLine() : nullptr;
    const int frame = project ? project->animator().currentFrame().get() : 0;
    const QSize canvas = project ? project->attribute().imageSize() : QSize();

    QVector<MaskField> fields;
    QString empty = QCoreApplication::translate(
        "gui::ToolViewModel", "Select a layer to paint a mask."
    );
    if (target && line) {
        const bool hasContent = target->type() == core::ObjectType_Layer
                                    ? !line->current().areaTextureSize().isEmpty()
                                    : !canvas.isEmpty();
        if (!hasContent) {
            empty = QCoreApplication::translate(
                "gui::ToolViewModel", "This layer has no image or canvas to mask."
            );
        } else {
            bool hasFilter = false;
            bool hasEnabledFilter = false;
            for (const core::FilterEntry& entry : line->filters()) {
                if (!core::isFilterType(entry.type))
                    continue;
                hasFilter = true;
                if (!entry.enabled)
                    continue;
                hasEnabledFilter = true;
                const QString lane = filterDisplayName(entry.type);
                if (lane.isEmpty())
                    continue;
                MaskField amount;
                amount.kind = ctrl::MaskTargetKind_FilterInput;
                amount.lane = entry.type;
                amount.channel = core::FilterMaskChannel_Amount;
                amount.label = QCoreApplication::translate("gui::ToolViewModel", "%1 amount").arg(lane);
                fields.append(amount);
                // The value is generic; the CONSUMER is not. Wave is the only filter
                // that reads a time-offset field today (LayerNode/FolderNode).
                if (entry.type == core::TimeKeyType_Wave) {
                    MaskField offset = amount;
                    offset.channel = core::FilterMaskChannel_TimeOffset;
                    offset.label = QCoreApplication::translate("gui::ToolViewModel", "%1 time offset").arg(lane);
                    fields.append(offset);
                }
            }

            // A matte is paintable only where a key exists to write (the base value,
            // or a key at this frame). Between keys the field is blended and read-only.
            if (core::MaskKey* key = ctrl::TimeLineUtil::maskKeyForPaint(*target, frame)) {
                if (key->data().isValid()) {
                    MaskField matte;
                    matte.kind = ctrl::MaskTargetKind_LayerMask;
                    matte.lane = core::TimeKeyType_TERM;
                    matte.channel = core::FilterMaskChannel_Amount;
                    matte.label = QCoreApplication::translate("gui::ToolViewModel", "Layer mask");
                    fields.append(matte);
                }
            }

            if (!fields.isEmpty()) {
                empty.clear();
            } else if (line->baseValue(core::TimeKeyType_Mask)
                       || !line->map(core::TimeKeyType_Mask).isEmpty()) {
                empty = QCoreApplication::translate(
                    "gui::ToolViewModel", "Key the mask at this frame to paint it."
                );
            } else if (hasFilter && !hasEnabledFilter) {
                empty = QCoreApplication::translate(
                    "gui::ToolViewModel", "Enable a filter to paint its mask."
                );
            } else if (hasFilter) {
                empty = QCoreApplication::translate(
                    "gui::ToolViewModel", "There is no mask to paint on this layer at this frame."
                );
            } else {
                empty = QCoreApplication::translate(
                    "gui::ToolViewModel", "Add a filter or a mask in Properties first."
                );
            }
        }
    }
    mModel->setMaskFields(fields, empty);
}

void ToolWidget::syncWeightsState() {
    ctrl::bone::WeightState state;
    if (mDriver && mDriver->boneEditor()) {
        state = mDriver->boneEditor()->weightsState();
    }
    mModel->setWeightsState(state);
}

void ToolWidget::onWeightsAction(int aAction) {
    if (!mDriver)
        return;

    QString error;
    switch (ToolViewModel::WeightAction(aAction)) {
    case ToolViewModel::WeightAction_Bake: mDriver->bakeBoneWeights(false, &error); break;
    case ToolViewModel::WeightAction_Rebake: mDriver->bakeBoneWeights(true, &error); break;
    case ToolViewModel::WeightAction_Revert: mDriver->revertBoneWeights(&error); break;
    case ToolViewModel::WeightAction_Clear: mDriver->clearBoneWeights(&error); break;
    default: return;
    }
    syncWeightsState();
    onVisualUpdated();
}

void ToolWidget::onModelToolChanged(int aOldType, int aNewType) {
    if (!mDriver)
        return;

    const ctrl::ToolType oldType = ctrl::ToolType(aOldType);
    const ctrl::ToolType newType = ctrl::ToolType(aNewType);
    // FINALIZE FIRST: that is what commits a gesture still in flight, and setTool
    // destroys the editor that holds it.
    if (oldType != ctrl::ToolType_TERM)
        onFinalizeTool(oldType);

    mDriver->setTool(newType);
    mDriverTool = newType;
    onToolChanged(newType);

    QSettings settings;
    const auto autoshowmesh = settings.value("generalsettings/tools/autoshowmesh");
    if (autoshowmesh.isValid() && autoshowmesh.toBool()) {
        if (newType != ctrl::ToolType_FFD) {
            if (meshFromFunction) {
                meshFromFunction = false;
                showHideMesh(false);
            }
        } else {
            showHideMesh(true);
        }
    }

    // The editor is brand new, so the panel's param has to reach it: this is the same
    // "push it again after the switch" rule the widget version needed for the mask
    // editor, and it applies to every tool for the same reason.
    pushParam();
    syncWeightsState();
    onVisualUpdated();
}

void ToolWidget::showHideMesh(bool aShow) {
    if (viewSetting().showLayerMesh && aShow && !meshFromFunction)
        return;
    if (aShow)
        meshFromFunction = true;
    // The view setting is the window's state now (ViewSettingsModel), but this
    // convenience belongs to the FFD tool: it turns the mesh on when the tool is
    // picked and off again when it leaves. Write the one setting, then announce it
    // through the same signal the viewport has always consumed.
    if (viewSetting().showLayerMesh == aShow)
        return;
    viewSetting().showLayerMesh = aShow;
    onViewSettingChanged(viewSetting());
}

void ToolWidget::beginMaskPaint(core::TimeKeyType aLane, core::FilterMaskChannel aChannel) {
    // the model is the authority for the target: select it, then press the tool so the
    // whole switch goes through the normal route
    mModel->beginFilterMaskPaint(aLane, aChannel);
    // ...and PUSH THE PARAM whether or not the tool changed: the editor is created by
    // the switch, so a panel filled in BEFORE it would otherwise leave the freshly
    // created editor on the default parameter - measured once as a stroke that painted
    // nothing at all (the editor still thought it was writing another lane's mask).
    // updateParam is idempotent, so the second push after a switch costs nothing.
    onParamUpdated();
}

bool ToolWidget::maskPaintingLayerMask() const {
    return mModel ? mModel->maskPaintingLayerMask() : false;
}

bool ToolWidget::maskToolActive() const { return mModel && mModel->maskToolActive(); }

core::TimeKeyType ToolWidget::maskLane() const {
    return mModel ? mModel->maskLane() : core::TimeKeyType_TERM;
}

core::FilterMaskChannel ToolWidget::maskChannel() const {
    return mModel ? mModel->maskChannel() : core::FilterMaskChannel_Amount;
}

void ToolWidget::beginLayerMaskPaint() {
    mModel->beginLayerMaskPaint();
    onParamUpdated(); // see beginMaskPaint: the editor is created by the switch
}

void ToolWidget::nudgeBrush(double aSizeSteps, double aHardnessSteps) {
    mModel->nudgeBrush(aSizeSteps, aHardnessSteps);
}

void ToolWidget::adjustBrush(int aSizeSteps, int aHardnessSteps) {
    // the ACTIVE tool's brush: the rows are shared, but each tool owns its own
    // BrushParam, so the step goes to the one on screen (the model decides which)
    mModel->adjustBrush(aSizeSteps, aHardnessSteps);
}

const ctrl::BrushParam* ToolWidget::activeBrush() const {
    return mModel ? mModel->activeBrush() : nullptr;
}

void ToolWidget::setMaskValue(float aValue) {
    if (ctrl::ToolType(mModel->tool()) != ctrl::ToolType_Mask)
        return;
    mModel->sampleMaskValue(double(aValue));
}

void ToolWidget::onParamUpdated() {
    pushParam();
    syncWeightsState();
    onVisualUpdated();
}

void ToolWidget::pushParam() {
    if (!mDriver)
        return;
    // Driver::updateParam ASSERTS that its editor matches its own tool, so a push is
    // only ever made for the tool the driver was last given. They agree by construction
    // (onModelToolChanged switches before anything can be pushed), and this guard is
    // what keeps a future reordering a no-op instead of a crash.
    if (mDriverTool != ctrl::ToolType(mModel->tool()))
        return;

    switch (mDriverTool) {
    case ctrl::ToolType_SRT: mDriver->updateParam(mModel->srtParam()); break;
    case ctrl::ToolType_Bone: mDriver->updateParam(mModel->boneParam()); break;
    case ctrl::ToolType_Pose: mDriver->updateParam(mModel->poseParam()); break;
    case ctrl::ToolType_FFD: mDriver->updateParam(mModel->ffdParam()); break;
    case ctrl::ToolType_Mesh: mDriver->updateParam(mModel->meshParam()); break;
    case ctrl::ToolType_Mask: mDriver->updateParam(mModel->maskParam()); break;
    default: break;
    }
}

void ToolWidget::refreshTheme() {
    mTheme->refresh();
    if (mQuick)
        mQuick->setClearColor(theme::Colors::current().base);
}

} // namespace gui
