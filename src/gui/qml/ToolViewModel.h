#ifndef GUI_QML_TOOLVIEWMODEL_H
#define GUI_QML_TOOLVIEWMODEL_H

#include <QObject>
#include "util/Signaler.h"
#include <QString>
#include <QVariantList>

#include "core/FilterMask.h"
#include "core/TimeKeyType.h"
#include "ctrl/BoneParam.h"
#include "ctrl/Brush.h"
#include "ctrl/FFDParam.h"
#include "ctrl/MaskParam.h"
#include "ctrl/MeshParam.h"
#include "ctrl/PoseParam.h"
#include "ctrl/SRTParam.h"
#include "ctrl/ToolType.h"
#include "gui/MainViewSetting.h"

namespace gui {

// THE SHARED BRUSH, as one Qt Quick object that RETARGETS with the tool.
//
// The brush is one model in ctrl (ctrl/Brush.h): size, hardness, flow, opacity, the
// two pressure switches, and every painting tool owns its own copy so switching from
// the bone brush to the mask brush does not lose either. The dock, though, shows ONE
// set of rows - the active tool's - so this object points at whichever copy is up and
// announces when that pointer moves.
//
// Row VISIBILITY is the tool's decision, not the row's: a tool that cannot honour a
// row hides it rather than changing what the row means (the FFD mesh brush has no
// per-stroke cap, so it shows Flow and hides Opacity). Carrying the flag here is what
// keeps the one QML brush section from needing to know which tool is up.
class ToolBrushModel: public QObject {
    Q_OBJECT

    Q_PROPERTY(double radius READ radius WRITE setRadius NOTIFY valuesChanged)
    Q_PROPERTY(double hardness READ hardness WRITE setHardness NOTIFY valuesChanged)
    Q_PROPERTY(double flow READ flow WRITE setFlow NOTIFY valuesChanged)
    Q_PROPERTY(double opacity READ opacity WRITE setOpacity NOTIFY valuesChanged)
    Q_PROPERTY(bool pressureSize READ pressureSize WRITE setPressureSize NOTIFY valuesChanged)
    Q_PROPERTY(bool pressureFlow READ pressureFlow WRITE setPressureFlow NOTIFY valuesChanged)


    // Field limits, the same ones the widget panel used (Size 1..1000 px, the
    // percentages 0..100 with the opacity floored at 1 so a stroke cannot be
    // invisible). QML must not invent its own.
    Q_PROPERTY(double radiusMin READ radiusMin CONSTANT)
    Q_PROPERTY(double radiusMax READ radiusMax CONSTANT)
    Q_PROPERTY(double opacityMin READ opacityMin CONSTANT)

public:
    explicit ToolBrushModel(QObject* aParent = nullptr);

    // Point at the active tool's brush. aShowOpacity says whether that tool can honour
    // a per-stroke cap (Bone and Pose can; the FFD mesh brush and the mask brush write
    // a value that already converges, so they cannot).
    void retarget(ctrl::BrushParam* aParam);

    double radius() const;
    double hardness() const;
    double flow() const;
    double opacity() const;
    bool pressureSize() const;
    bool pressureFlow() const;

    void setRadius(double aValue);
    void setHardness(double aValue);
    void setFlow(double aValue);
    void setOpacity(double aValue);
    void setPressureSize(bool aValue);
    void setPressureFlow(bool aValue);

    double radiusMin() const { return 1.0; }
    double radiusMax() const { return 1000.0; }
    double opacityMin() const { return 1.0; }
    // the percentage rows' scale, from the SAME constant the tool rows use
    double percentMax() const { return 100.0; }

    // The familiar notches, shared by the [ ] keys and the Alt+right drag (see
    // ctrl::BrushDrag): one step is x1.1 on the size, 5 % on the hardness. They live
    // here rather than in the QML for the same reason the stroke does - one gesture is
    // one policy - and they write the ACTIVE tool's brush, so a step taken while the
    // mask brush is up does not move the bone brush.
    // CONTINUOUS, for the Alt drag: a step is the same unit the notch keys use (x1.1 size, 5%
    // hardness) but the amount is FRACTIONAL, because a drag rounded to whole notches snapped the
    // hardness in 5% jumps (reported).
    void nudge(double aSizeSteps, double aHardnessSteps);
    void adjustSize(int aSteps);
    void adjustHardness(int aSteps);

signals:
    // Every value AND the visibility flag notify here: a QML binding must re-read them
    // when the ACTIVE BRUSH moves (a tool switch), which is not an edit.
    void valuesChanged();
    // A SETTER actually changed a value. Kept apart from valuesChanged because the host
    // reacts to it by pushing the param to the driver and repainting the viewport, and a
    // tool switch is already followed by an explicit push - one signal for both would
    // push a param for a tool whose editor does not exist yet.
    void edited();

private:
    ctrl::BrushParam* mParam = nullptr;
};

// The live key bindings the dock SPEAKS. Every one of them is rebindable, so the
// tooltips that name a key and the hint under the brush rows read them from the
// application's key map rather than assuming the defaults.
class ToolKeyHints {
public:
    QString rotateCounterClockwise;
    QString resetRotation;
    QString rotateClockwise;
    QString brushSizeDecrease;
    QString brushSizeIncrease;
    QString brushHardnessDecrease;
    QString brushHardnessIncrease;
    // the mask brush's value swap (the painter's X)
    QString swapMaskValue;
};

// The widget-free model behind ToolsPanel.qml, and the counterpart of
// gui::InspectorViewModel on the other side of the dock.
//
// WHAT IT OWNS. One ctrl param object per tool (the same structs the widget panels
// held as members), the current tool, and the view toggles. It owns NO widget, no
// driver and no project: the host (gui::ToolWidget) hands it a MainViewSetting to
// write through and answers its requests with its driver. That is what lets
// tests/tool_view_model.cpp ask every question the dock can answer without a window,
// and it is the split the inspector took.
//
// WHAT IT DECIDES, and why here rather than in the QML:
//   * which rows a tool shows (brush, blur, weight, the SRT key checkboxes, the mask
//     pickers). The widget panels each re-derived this in their own updateTypeParam;
//     one function now owns it, so a tool cannot show a row its editor ignores;
//   * the mode's NAME, because a mode row is icons and something has to say what the
//     highlighted one is. The widget version had tooltips only, which is why four
//     unlabeled glyphs were the only way to tell which field the mask brush writes;
//   * every string, through the ORIGINAL translation contexts. The catalogs carry
//     gui::tool::BonePanel, gui::tool::SRTPanel and so on, and a context is a CATALOG
//     KEY: re-spelling these in QML would silently ship English in five languages
//     until every catalog is re-extracted and re-translated.
class ToolViewModel: public QObject {
    Q_OBJECT

    // ---- the toolbox ---------------------------------------------------------
    // The current ctrl::ToolType as an int (QML has no C++ enums); ToolType_TERM is
    // "no tool", which is the state the dock opens in and the one the toolbox shows
    // as nothing checked.
    Q_PROPERTY(int tool READ tool NOTIFY toolChanged)
    // False until a project's driver exists: the toolbox is disabled then, exactly as
    // the widget's setPanelActivity(false) disabled every button.
    Q_PROPERTY(bool driverActive READ driverActive NOTIFY driverActiveChanged)
    // [{type, name, tip, icon}] for the seven tools, in the fixed order the toolbox has
    // always had.
    Q_PROPERTY(QVariantList tools READ tools CONSTANT)

    // ---- the active tool's panel --------------------------------------------
    // The panel's heading: the tool's own name ("Transform", "Bone editor"), or
    // "Layer mask"/"Filter mask" for the mask tool, which names what it writes.
    Q_PROPERTY(QString title READ title NOTIFY stateChanged)
    // [{label, icon}] the tool's modes; empty for the mask tool, which picks a TARGET
    // and a CHANNEL instead of a mode.
    Q_PROPERTY(QVariantList modes READ modes NOTIFY stateChanged)
    Q_PROPERTY(int modeIndex READ modeIndex NOTIFY modeChanged)
    // The active mode's label, shown at the end of the heading: the mode row is icons,
    // and a caption is what makes it readable without hovering.
    Q_PROPERTY(QString modeName READ modeName NOTIFY modeChanged)

    // ---- which rows are up ---------------------------------------------------
    Q_PROPERTY(bool brushVisible READ brushVisible NOTIFY stateChanged)
    // Opacity is a TOOL decision, not a brush one: exactly one target reads it.
    Q_PROPERTY(bool opacityVisible READ opacityVisible NOTIFY stateChanged)
    Q_PROPERTY(bool blurVisible READ blurVisible NOTIFY stateChanged)
    Q_PROPERTY(bool weightVisible READ weightVisible NOTIFY stateChanged)
    Q_PROPERTY(bool srtCreateVisible READ srtCreateVisible NOTIFY stateChanged)
    Q_PROPERTY(bool srtCentroidVisible READ srtCentroidVisible NOTIFY stateChanged)
    Q_PROPERTY(bool maskPickersVisible READ maskPickersVisible NOTIFY stateChanged)
    Q_PROPERTY(bool panelVisible READ panelVisible NOTIFY stateChanged)

    // ---- the shared brush ----------------------------------------------------
    Q_PROPERTY(ToolBrushModel* brush READ brush CONSTANT)

    // ---- SRT -----------------------------------------------------------------
    // [{label, tip, key}] the four toggle rows, and their state in the same order.
    // A list rather than eight properties because the row set is fixed and the labels
    // are catalog strings; the STATE is a separate list so the binding reads a
    // PROPERTY (`srtCheckValues[index]`) and not a call, which would register no
    // dependency and freeze at its first value (a measured trap in this tree).
    Q_PROPERTY(QVariantList srtChecks READ srtChecks CONSTANT)
    Q_PROPERTY(QVariantList srtCheckValues READ srtCheckValues NOTIFY changed)

    // ---- FFD -----------------------------------------------------------------
    // The pencil's smoothing pass, 0..1 (shown as a percentage).
    Q_PROPERTY(double blur READ blur WRITE setBlur NOTIFY changed)
    Q_PROPERTY(QString blurLabel READ blurLabel CONSTANT)

    // ---- Pose ----------------------------------------------------------------
    // The drawing pressure, 0..1 (shown as a percentage).
    Q_PROPERTY(double weight READ weight WRITE setWeight NOTIFY changed)
    Q_PROPERTY(QString weightLabel READ weightLabel CONSTANT)

    // ---- Mask ----------------------------------------------------------------
    // ONE field chooser. A mask is addressed as (filter lane, channel) or as the node's
    // own matte, so the seven addressable fields are one list: two selectors (a lane and
    // then a channel) asked the same question twice, and the pair only ever named one
    // field. The labels are built with a translatable format, not concatenated with a
    // separator ("Wave Deform amount", never "Wave Deform · amount").
    Q_PROPERTY(QVariantList maskFields READ maskFields CONSTANT)
    Q_PROPERTY(int maskFieldIndex READ maskFieldIndex NOTIFY maskChanged)
    // The current field's own name ("Wave Deform amount", "Layer mask"): what the chooser
    // shows as its value.
    Q_PROPERTY(QString maskFieldLabel READ maskFieldLabel NOTIFY maskChanged)
    // True when a card sent the tool here (a filter's "Paint mask", the mask card's own
    // Paint). The field is then a FACT rather than a question, so the panel shows the
    // caption plus a disclosure and does not ask; picking the Mask tool from the toolbox
    // opens the chooser instead.
    Q_PROPERTY(bool maskPinned READ maskPinned NOTIFY stateChanged)
    // What the brush paints TOWARD, 0..1 (shown as a percentage). A stamp moves a texel
    // toward this, so one value both reduces an amount mask and restores it.
    Q_PROPERTY(double maskValue READ maskValue WRITE setMaskValue NOTIFY maskChanged)
    Q_PROPERTY(QString maskValueHint READ maskValueHint NOTIFY keyHintsChanged)

    // The percentage-derived rows' limits: blur and weight are 0..1, the mask value
    // 0..1, and the field shows them as 0..100.
    Q_PROPERTY(double percentMax READ percentMax CONSTANT)

    // ---- View settings -------------------------------------------------------
    Q_PROPERTY(bool showMesh READ showMesh WRITE setShowMesh NOTIFY viewChanged)
    Q_PROPERTY(bool cutOutsideFrame READ cutOutsideFrame WRITE setCutOutsideFrame NOTIFY viewChanged)
    Q_PROPERTY(bool flipCanvas READ flipCanvas WRITE setFlipCanvas NOTIFY viewChanged)
    Q_PROPERTY(QString viewTitle READ viewTitle NOTIFY viewChanged)
    // EVERY hover text in the dock, with the live key binding folded in where the action
    // has one. They all notify together because they are all rebuilt from one key map:
    // a rebound key re-spells every tooltip at once. NOTHING here is drawn as visible
    // text - the dock has no hint lines, by the reported "too busy" verdict (a shortcut
    // spelled out under the controls is noise; inside a tooltip it costs no layout).
    Q_PROPERTY(QString showMeshTip READ showMeshTip NOTIFY keyHintsChanged)
    Q_PROPERTY(QString cutOutsideFrameTip READ cutOutsideFrameTip NOTIFY keyHintsChanged)
    Q_PROPERTY(QString rotateCounterClockwiseTip READ rotateCounterClockwiseTip NOTIFY keyHintsChanged)
    Q_PROPERTY(QString resetRotationTip READ resetRotationTip NOTIFY keyHintsChanged)
    Q_PROPERTY(QString rotateClockwiseTip READ rotateClockwiseTip NOTIFY keyHintsChanged)
    Q_PROPERTY(QString flipCanvasTip READ flipCanvasTip NOTIFY keyHintsChanged)
    // The brush's two notches, for the Size and Hardness rows' tooltips.
    Q_PROPERTY(QString sizeKeyTip READ sizeKeyTip NOTIFY keyHintsChanged)
    Q_PROPERTY(QString hardnessKeyTip READ hardnessKeyTip NOTIFY keyHintsChanged)

public:
    explicit ToolViewModel(QObject* aParent = nullptr);

    // The host wires these in; nothing here reaches a driver or a project through them.
    void setViewSetting(MainViewSetting* aSetting);
    void setDriverActive(bool aActive);
    void setKeyHints(const ToolKeyHints& aHints);

    int tool() const { return int(mTool); }
    bool driverActive() const { return mDriverActive; }
    QVariantList tools() const;
    QString title() const;
    QVariantList modes() const;
    int modeIndex() const;
    QString modeName() const;
    bool brushVisible() const;
    bool opacityVisible() const;
    bool blurVisible() const;
    bool weightVisible() const;
    bool srtCreateVisible() const;
    bool srtCentroidVisible() const;
    bool maskPickersVisible() const;
    bool panelVisible() const { return mTool != ctrl::ToolType_TERM; }

    ToolBrushModel* brush() const { return mBrush; }
    // the ACTIVE tool's brush parameter, or null when the tool has none: the viewport's adjustment
    // HUD reads it (gui::MainDisplayWidget), and nothing else may write through it
    const ctrl::BrushParam* activeBrush() const;

    QVariantList srtChecks() const;
    QVariantList srtCheckValues() const;
    Q_INVOKABLE void setSrtCheck(int aIndex, bool aValue);
    // The four writers behind setSrtCheck. Private: the row set is the SCENE's, so a
    // single index-addressed verb is the only way in and one list stays the one order.
    void setNecessarilyMove(bool aValue);
    void setNecessarilyRotate(bool aValue);
    void setNecessarilyScale(bool aValue);
    void setAdjustPosition(bool aValue);

    double blur() const { return double(mFFD.blur); }
    void setBlur(double aValue);
    QString blurLabel() const;
    double weight() const { return double(mPose.diWeight); }
    void setWeight(double aValue);
    QString weightLabel() const;

    QVariantList maskFields() const;
    int maskFieldIndex() const;
    Q_INVOKABLE void setMaskField(int aIndex);
    QString maskFieldLabel() const;
    bool maskPinned() const { return mMaskPinned; }
    // The Mask tool is up AND its target is a LAYER's own matte (not a filter lane's input):
    // the card that offers "Paint this layer's matte" is only redundant in that case.
    bool maskPaintingLayerMask() const {
        return mTool == ctrl::ToolType_Mask && mMask.kind == ctrl::MaskTargetKind_LayerMask;
    }
    // The caption's disclosure: open the chooser for a field a card chose.
    Q_INVOKABLE void unpinMaskTarget();
    double maskValue() const { return double(mMask.value); }
    void setMaskValue(double aValue);
    // THE mask keyboard verb: swap the paint value between the two ends, the way every
    // painter swaps its fore/background inks with one key (Photoshop's, Clip Studio's and
    // Krita's X). An in-between value snaps to the far end.
    Q_INVOKABLE void swapMaskValue();
    QString maskValueHint() const;
    double percentMax() const { return 100.0; }

    bool showMesh() const;
    bool cutOutsideFrame() const;
    bool flipCanvas() const;
    void setShowMesh(bool aValue);
    void setCutOutsideFrame(bool aValue);
    void setFlipCanvas(bool aValue);
    QString viewTitle() const;
    QString showMeshTip() const;
    QString cutOutsideFrameTip() const;
    QString rotateCounterClockwiseTip() const;
    QString resetRotationTip() const;
    QString rotateClockwiseTip() const;
    QString flipCanvasTip() const;
    QString sizeKeyTip() const;
    QString hardnessKeyTip() const;

    // The params, for the host's push to the driver. One accessor per tool rather than
    // a variant, so the host's switch stays a switch and each updateParam overload keeps
    // its own type.
    const ctrl::SRTParam& srtParam() const { return mSRT; }
    const ctrl::FFDParam& ffdParam() const { return mFFD; }
    const ctrl::BoneParam& boneParam() const { return mBone; }
    const ctrl::PoseParam& poseParam() const { return mPose; }
    const ctrl::MeshParam& meshParam() const { return mMesh; }
    const ctrl::MaskParam& maskParam() const { return mMask; }

    // ---- the scene's verbs ---------------------------------------------------
    // A press on a toolbox button. Doing nothing when the type is already up is
    // deliberate: the widget version re-ran setTool() (which RECREATES the editor) when
    // the active tool was pressed again, throwing the editor's state away for no reason.
    // A real change still finalizes the OLD tool and then switches - see ToolWidget.
    Q_INVOKABLE void activate(int aType);
    Q_INVOKABLE void setMode(int aIndex);
    // (setMaskTarget / setMaskChannel are above, beside the properties they write;
    // both are Q_INVOKABLE for the scene's combos.)
    // The temporary eyedropper: Alt+click on the canvas samples the field.
    Q_INVOKABLE void sampleMaskValue(double aValue);
    // "Clear mask" - the host owns the driver, so this is a request.
    Q_INVOKABLE void requestClearMask();
    Q_INVOKABLE void rotateCounterClockwise();
    Q_INVOKABLE void resetRotation();
    Q_INVOKABLE void rotateClockwise();
    // A filter card's "paint mask" (InspectorViewModel::onMaskPaintRequested) and the
    // card's own Paint: select what is painted, PIN it (the card named it) and switch to
    // the Mask tool in one step, so no caller has to remember the order.
    void beginFilterMaskPaint(core::TimeKeyType aLane, core::FilterMaskChannel aChannel);
    void beginLayerMaskPaint();
    // The [ ] / Alt+right-drag notches, for the ACTIVE tool's brush.
    void adjustBrush(int aSizeSteps, int aHardnessSteps);
    // the Alt drag's continuous form: fractional steps of the same units the keys use
    void nudgeBrush(double aSizeSteps, double aHardnessSteps);
    // Ctrl+I while the Mask tool is in hand: ASK for the matte to be inverted. The inspector owns
    // the document edit, so this is a request and the two routes cannot drift.
    void invertMask();
    util::Signaler<void()> onInvertMaskRequested;

signals:
    // The tool changed. The OLD type comes along because the host must finalize it
    // BEFORE it calls Driver::setTool - finalizing is what commits a gesture still in
    // flight, and setTool destroys the editor that holds it.
    void toolChanged(int aOldType, int aNewType);
    // the tool's structure changed (title, modes, which rows are up)
    void stateChanged();
    // the active mode changed (also a param write, so `changed` follows it)
    void modeChanged();
    // a parameter changed: the host pushes the active tool's param and repaints
    void changed();
    // the mask's target/channel/value changed, or its chooser's labels did
    void maskChanged();
    void viewChanged();
    void viewSettingsChanged();
    void driverActiveChanged();
    // every tooltip (they all read the same key map)
    void keyHintsChanged();
    void clearMaskRequested();

private:
    void retargetBrush();

    ctrl::ToolType mTool = ctrl::ToolType_TERM;
    bool mDriverActive = false;
    MainViewSetting* mViewSetting = nullptr;
    ToolKeyHints mKeyHints;

    // One param object per tool, exactly as the widget panels held them: switching
    // tools and coming back keeps the mode, the checkbox states and the brush.
    ctrl::SRTParam mSRT;
    ctrl::FFDParam mFFD;
    ctrl::BoneParam mBone;
    ctrl::PoseParam mPose;
    ctrl::MeshParam mMesh;
    ctrl::MaskParam mMask;

    ToolBrushModel* mBrush = nullptr;
    bool mMaskPinned = false;
};

} // namespace gui

#endif // GUI_QML_TOOLVIEWMODEL_H
