#ifndef GUI_QML_INSPECTORVIEWMODEL_H
#define GUI_QML_INSPECTORVIEWMODEL_H

#include <QColor>
#include <QObject>
#include <QImage>
#include <QVariantList>
#include <QVector>

#include "core/ObjectNode.h"
#include "core/Project.h"
#include "util/Signaler.h"
#include "gui/prop/prop_KeyAccessor.h"

namespace gui {

class InspectorRowModel;
class FilterStackModel;
class ViaPoint;

// The widget-free model behind PropertiesPanel.qml. It is deliberately not a
// QAbstractItemModel: the inspector is a fixed set of rows plus one stack, and
// exposing the two as QObject graphs keeps each row's typed values visible to
// QML without role plumbing.
class InspectorViewModel: public QObject {
    Q_OBJECT

    Q_PROPERTY(InspectorRowModel* move READ move CONSTANT)
    Q_PROPERTY(InspectorRowModel* rotate READ rotate CONSTANT)
    Q_PROPERTY(InspectorRowModel* scale READ scale CONSTANT)
    Q_PROPERTY(InspectorRowModel* depth READ depth CONSTANT)
    Q_PROPERTY(InspectorRowModel* opacity READ opacity CONSTANT)
    Q_PROPERTY(InspectorRowModel* image READ image CONSTANT)
    Q_PROPERTY(InspectorRowModel* pose READ pose CONSTANT)
    Q_PROPERTY(InspectorRowModel* ffd READ ffd CONSTANT)
    Q_PROPERTY(FilterStackModel* filters READ filters CONSTANT)

    // ---- the node's MATTE (core::MaskKey) -----------------------------------
    //
    // The card's whole state is these four facts plus a thumbnail. They are
    // DERIVED from the document on every refresh (never cached here), so the card
    // cannot disagree with the lane: `maskKeyed` is "the lane has a keyframe",
    // `maskHasKeyAtFrame` is "there is a key to paint", and the card's between-keys
    // state is `maskKeyed && !maskHasKeyAtFrame`.
    Q_PROPERTY(bool maskPresent READ maskPresent NOTIFY changed)
    Q_PROPERTY(bool maskKeyed READ maskKeyed NOTIFY changed)
    Q_PROPERTY(bool maskHasKeyAtFrame READ maskHasKeyAtFrame NOTIFY changed)
    Q_PROPERTY(int maskKeyCount READ maskKeyCount NOTIFY changed)
    // Bumped on every refresh so `image://maskthumb/...?<revision>` re-asks the
    // provider; a QQuickImage is cached by URL, and a matte that was painted must
    // not keep showing the pixels it had.
    Q_PROPERTY(int maskRevision READ maskRevision NOTIFY changed)
    // The playhead, for the card's "Key at frame N" - the same number the lane and the
    // ruler show, so the button cannot name a different frame from the one it keys.
    Q_PROPERTY(int currentFrame READ frame NOTIFY changed)

    Q_PROPERTY(bool hasTarget READ hasTarget NOTIFY changed)
    Q_PROPERTY(bool playbackActive READ playbackActive NOTIFY changed)
    // The Mask tool is up and its paint target is this node, i.e. the card's Paint button would
    // re-arm what is already armed. The host says so (it is the only place that knows the active
    // tool), and the card disables the button rather than pretending to do something.
    Q_PROPERTY(bool maskPaintActive READ maskPaintActive NOTIFY maskPaintStateChanged)
    // Filter cards need the same fact per field: which lane/channel the Mask tool is
    // painting on THIS node. The layer-mask flag above is the matte half of it.
    Q_PROPERTY(bool maskPaintIsFilter READ maskPaintIsFilter NOTIFY maskPaintStateChanged)
    Q_PROPERTY(int maskPaintLane READ maskPaintLane NOTIFY maskPaintStateChanged)
    Q_PROPERTY(int maskPaintChannel READ maskPaintChannel NOTIFY maskPaintStateChanged)

    Q_PROPERTY(QString blendLabel READ blendLabel CONSTANT)
    Q_PROPERTY(QString clippedLabel READ clippedLabel CONSTANT)
    Q_PROPERTY(QString centroidLabel READ centroidLabel CONSTANT)
    Q_PROPERTY(QString pathLabel READ pathLabel CONSTANT)
    Q_PROPERTY(QString smoothLabel READ smoothLabel CONSTANT)
    Q_PROPERTY(QString smoothTooltip READ smoothTooltip CONSTANT)
    Q_PROPERTY(QString centerLabel READ centerLabel CONSTANT)
    Q_PROPERTY(QString meshCellLabel READ meshCellLabel CONSTANT)
    Q_PROPERTY(QString meshCellTooltip READ meshCellTooltip CONSTANT)
    Q_PROPERTY(QVariantList blendOptions READ blendOptions CONSTANT)
    Q_PROPERTY(int blendIndex READ blendIndex NOTIFY changed)
    Q_PROPERTY(bool blendEnabled READ blendEnabled NOTIFY changed)
    Q_PROPERTY(bool clipped READ clipped NOTIFY changed)
    Q_PROPERTY(bool clippedEnabled READ clippedEnabled NOTIFY changed)

    // Field ranges. QML must not invent its own limits: these are the same
    // constants the widget inspector used.
    Q_PROPERTY(double translateMin READ translateMin CONSTANT)
    Q_PROPERTY(double translateMax READ translateMax CONSTANT)
    Q_PROPERTY(double rotateMin READ rotateMin CONSTANT)
    Q_PROPERTY(double rotateMax READ rotateMax CONSTANT)
    Q_PROPERTY(double scaleMin READ scaleMin CONSTANT)
    Q_PROPERTY(double scaleMax READ scaleMax CONSTANT)
    Q_PROPERTY(int imageCellMin READ imageCellMin CONSTANT)
    Q_PROPERTY(int imageCellMax READ imageCellMax CONSTANT)
    Q_PROPERTY(double blurMax READ blurMax CONSTANT)
    Q_PROPERTY(double blurAngleMin READ blurAngleMin CONSTANT)
    Q_PROPERTY(double blurAngleMax READ blurAngleMax CONSTANT)
    Q_PROPERTY(int hsvHueMax READ hsvHueMax CONSTANT)
    Q_PROPERTY(int hsvComponentMax READ hsvComponentMax CONSTANT)
    // Wave Deform field ranges. They mirror core::WaveKey::Data's own clamps
    // (wavelength >= 1, mix 0..1, crest 0.25..4, dual scale 0.1..4): a field the
    // key would silently clamp must not be draggable past what it accepts.
    Q_PROPERTY(double waveStrengthMin READ waveStrengthMin CONSTANT)
    Q_PROPERTY(double waveStrengthMax READ waveStrengthMax CONSTANT)
    Q_PROPERTY(double waveWavelengthMin READ waveWavelengthMin CONSTANT)
    Q_PROPERTY(double waveWavelengthMax READ waveWavelengthMax CONSTANT)
    Q_PROPERTY(double wavePhaseMin READ wavePhaseMin CONSTANT)
    Q_PROPERTY(double wavePhaseMax READ wavePhaseMax CONSTANT)
    Q_PROPERTY(double waveMixMax READ waveMixMax CONSTANT)
    Q_PROPERTY(double waveSpeedMin READ waveSpeedMin CONSTANT)
    Q_PROPERTY(double waveSpeedMax READ waveSpeedMax CONSTANT)
    Q_PROPERTY(double waveCrestMin READ waveCrestMin CONSTANT)
    Q_PROPERTY(double waveCrestMax READ waveCrestMax CONSTANT)
    Q_PROPERTY(double waveDualScaleMin READ waveDualScaleMin CONSTANT)
    Q_PROPERTY(double waveDualScaleMax READ waveDualScaleMax CONSTANT)

public:
    explicit InspectorViewModel(ViaPoint* aViaPoint, QObject* aParent = nullptr);

    void setProject(core::Project* aProject);
    void setTarget(core::ObjectNode* aTarget);
    void setPlaybackActive(bool aActive);
    // The host's tool state. Only a target that is this inspector's node counts:
    // switching layers while the Mask tool stays in layer-mask mode must not leave
    // every other layer's Paint button disabled.
    void setMaskPaintState(core::ObjectNode* aNode, bool aLayerMask, bool aFilterInput,
                           core::TimeKeyType aLane, core::FilterMaskChannel aChannel);
    void refresh();
    void refreshAttributes();
    void refreshKeys();
    void refreshFrame();
    void refreshTheme();

    InspectorRowModel* move() const { return mMove; }
    InspectorRowModel* rotate() const { return mRotate; }
    InspectorRowModel* scale() const { return mScale; }
    InspectorRowModel* depth() const { return mDepth; }
    InspectorRowModel* opacity() const { return mOpacity; }
    InspectorRowModel* image() const { return mImage; }
    InspectorRowModel* pose() const { return mPose; }
    InspectorRowModel* ffd() const { return mFFD; }
    FilterStackModel* filters() const { return mFilters; }

    bool hasTarget() const { return mProject && mTarget && mTarget->timeLine(); }
    bool playbackActive() const { return mPlaybackActive; }
    bool maskPaintActive() const { return mMaskPaintActive; }
    bool maskPaintIsFilter() const { return mMaskPaintIsFilter; }
    int maskPaintLane() const { return int(mMaskPaintLane); }
    int maskPaintChannel() const { return int(mMaskPaintChannel); }
    core::TimeLine* timeLine() const;
    int frame() const;
    gui::prop::KeyAccessor& accessor() { return mAccessor; }
    core::Project* project() const { return mProject; }
    core::ObjectNode* target() const { return mTarget; }

    QString blendLabel() const;
    QString clippedLabel() const;
    QString centroidLabel() const;
    QString pathLabel() const;
    QString smoothLabel() const;
    QString smoothTooltip() const;
    QString centerLabel() const;
    QString meshCellLabel() const;
    QString meshCellTooltip() const;
    QVariantList blendOptions() const { return mBlendOptions; }
    int blendIndex() const { return mBlendIndex; }
    bool blendEnabled() const { return mBlendEnabled; }
    bool clipped() const { return mClipped; }
    bool clippedEnabled() const { return mClippedEnabled; }

    double translateMin() const;
    double translateMax() const;
    double rotateMin() const;
    double rotateMax() const;
    double scaleMin() const;
    double scaleMax() const;
    int imageCellMin() const;
    int imageCellMax() const;
    double blurMax() const;
    double blurAngleMin() const;
    double blurAngleMax() const;
    int hsvHueMax() const;
    int hsvComponentMax() const;
    double waveStrengthMin() const;
    double waveStrengthMax() const;
    double waveWavelengthMin() const;
    double waveWavelengthMax() const;
    double wavePhaseMin() const;
    double wavePhaseMax() const;
    double waveMixMax() const;
    double waveSpeedMin() const;
    double waveSpeedMax() const;
    double waveCrestMin() const;
    double waveCrestMax() const;
    double waveDualScaleMin() const;
    double waveDualScaleMax() const;

    // A filter row asked to paint its mask: the window answers by switching to the
    // Mask tool with that lane and channel (the toolbox tool is where painting
    // happens; the card only points at it).
    util::Signaler<void(core::TimeKeyType, core::FilterMaskChannel)> onMaskPaintRequested;
    void requestMaskPaint(core::TimeKeyType aLane, core::FilterMaskChannel aChannel);
    // The LAYER mask is not a filter input (it has no lane/channel pair), so it asks
    // for the tool on its own signal rather than by a sentinel channel value.
    util::Signaler<void()> onLayerMaskPaintRequested;

    // THE MESH SPACING PREVIEW. The Properties field asks for the inspected
    // node's mesh overlay while it is hovered (after the tooltip delay), being
    // scrubbed or being text-edited; the host hands the node to the viewport.
    // Separate from MainViewSetting::showLayerMesh: this is a transient view of
    // one value, not the user's persistent mesh toggle.
    Q_INVOKABLE void setMeshPreviewActive(bool aActive);
    util::Signaler<void(core::ObjectNode*, bool)> onMeshPreviewRequested;

    bool maskPresent() const;
    bool maskKeyed() const;
    bool maskHasKeyAtFrame() const;
    int maskKeyCount() const;
    int maskRevision() const { return mMaskRevision; }
    // The matte at the playhead as a small image, for the card's thumbnail (and for
    // the tree's chip). Empty when the node has no mask.
    QImage maskThumbnail(const QSize& aRequested = QSize()) const;

    Q_INVOKABLE void addMask();
    Q_INVOKABLE void removeMask();
    Q_INVOKABLE void invertMask();
    Q_INVOKABLE void toggleMaskKey();
    // re-announce the matte facts (present/keyed/key-at-frame/thumbnail revision): every key and
    // frame notification calls it, so the card cannot lag behind the lane
    void notifyMaskState();
    // The card points at the tool, like a filter row's "paint mask" does.
    Q_INVOKABLE void requestLayerMaskPaint();

    Q_INVOKABLE void setBlendIndex(int aIndex);
    Q_INVOKABLE void setClipped(bool aValue);
    Q_INVOKABLE void browseImageAsset();
    Q_INVOKABLE void addImageKey();

signals:
    void changed();
    void maskPaintStateChanged();

private:
    friend class InspectorRowModel;
    friend class FilterRowModel;
    friend class FilterStackModel;

    void updateConstants();
    void assignBlend(img::BlendMode aMode);

    ViaPoint* mViaPoint;
    core::Project* mProject = nullptr;
    core::ObjectNode* mTarget = nullptr;
    bool mPlaybackActive = false;
    bool mMaskPaintActive = false;
    bool mMaskPaintIsFilter = false;
    core::TimeKeyType mMaskPaintLane = core::TimeKeyType_TERM;
    core::FilterMaskChannel mMaskPaintChannel = core::FilterMaskChannel_Amount;
    bool mMeshPreviewActive = false;
    gui::prop::KeyAccessor mAccessor;

    InspectorRowModel* mMove = nullptr;
    InspectorRowModel* mRotate = nullptr;
    InspectorRowModel* mScale = nullptr;
    InspectorRowModel* mDepth = nullptr;
    InspectorRowModel* mOpacity = nullptr;
    InspectorRowModel* mImage = nullptr;
    InspectorRowModel* mPose = nullptr;
    InspectorRowModel* mFFD = nullptr;
    FilterStackModel* mFilters = nullptr;

    QVariantList mBlendOptions;
    int mBlendIndex = 0;
    bool mBlendEnabled = false;
    bool mClipped = false;
    int mMaskRevision = 0;
    bool mClippedEnabled = false;
};

} // namespace gui

#endif // GUI_QML_INSPECTORVIEWMODEL_H
