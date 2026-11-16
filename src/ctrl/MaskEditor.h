#ifndef CTRL_MASKEDITOR_H
#define CTRL_MASKEDITOR_H

#include "core/AbstractCursor.h"
#include "core/CameraInfo.h"
#include "ctrl/AssignPixels.h"
#include "ctrl/IEditor.h"
#include "core/MaskKey.h"
#include "ctrl/MaskParam.h"
#include "ctrl/UILogger.h"

namespace core {
class FilterMask;
class MaskKey;
}

namespace ctrl {

// The PAINT tool for a mask field. ONE editor for BOTH kinds (MaskParam::kind):
// a filter lane's input (core/FilterMask.h) and the node's own matte
// (core/MaskKey.h). The brush, the falloff, the stroke spacing and the eyedropper are
// the same code for both; only the field's ADDRESS and its undo command differ.
//
// A filter input is a per-lane side table with a per-channel neutral, so the first
// stamp allocates it. A layer mask is a KEY's own field: it exists only where the
// document has one (a key at the playhead, or the base value) and the tool writes it
// in place, pushing ONE command on the release.
//
// The stroke is the shared BrushStroke (ctrl/Brush.h): stamps on the press, spaced
// along every drag, ended on the release. Each stamp writes the mask's texels
// through the shared falloff and records the box it touched, so the whole stroke
// is ONE undo step (ctrl/AssignPixels.h).
class MaskEditor: public IEditor {
public:
    // No UILogger: this editor is a brush over a field and never logs, so the parameter
    // would be a dead dependency on the driver's logger.
    explicit MaskEditor(core::Project& aProject);

    void setParam(const MaskParam& aParam);
    const MaskParam& param() const { return mParam; }

    // from IEditor
    virtual bool setTarget(core::ObjectNode* aTarget) override;
    virtual bool updateCursor(const core::CameraInfo& aCamera, const core::AbstractCursor& aCursor) override;
    // Records the stroke's accumulated damage (called at the end of updateCursor) and, separately,
    // ANNOUNCES it once per painted frame: the viewport calls announcePendingPaint() at the start of
    // a paint, so a mouse event never re-blends the document (see flushPaint).
    bool flushPaint();
    void announcePendingPaint();
    virtual void updateEvent(EventType aType) override;
    virtual void renderQt(const core::RenderInfo& aInfo, QPainter& aPainter) override;

    // the mask the tool would write, or null when the target cannot carry one
    core::FilterMask* currentMask();
    // the layer-mask key the tool would write, or null (no mask / between keys)
    core::MaskKey* currentMaskKey();
    // the field's CONTENT rect (both kinds): a layer's image box, a folder's canvas
    QRectF fieldRect() const;
    // clears the field being edited, as one undo step
    void clearMask();
    // EXTENDS THE STROKE FROM A POINTER POSITION THAT IS NOT A MOUSE EVENT. The viewport polls the
    // pointer while a stroke is down (see MainDisplayWidget::pollStrokePointer) because a platform
    // hands over one motion per frame while the view paints, so the events alone lose most of the
    // path. Returns false when no stroke is being painted.
    bool paintTo(const QVector2D& aWorldPos, float aPressure);
    bool isPainting() const { return mPainting; }

    // THE TEMPORARY EYEDROPPER: the field's value under a world position, or a
    // negative number when there is no field there. The caller (the viewport, which
    // owns the modifier state) puts it back into the panel.
    float sampleValue(const QVector2D& aWorldPos) const;
    // the preview the viewport draws while this tool is active (null when the target
    // carries no field for the selected lane/channel)
    const core::ObjectNode* previewTarget() const { return mTarget; }
    const core::FilterMask* previewMask() const;
    // the matte being painted, when the target is a LAYER MASK (see RenderInfo)
    const core::MaskKey* previewMaskKey() const;

private:
    // The node's CONTENT rectangle: a layer's image box, a folder's canvas-shaped
    // local box. Empty when the node cannot carry a mask.
    QRectF contentRect() const;
    // world (canvas) position -> content pixels, through the same world matrix the
    // renderer draws the node with
    bool toContent(const QVector2D& aWorldPos, QVector2D& aContentPos) const;
    // the world-space length of one content pixel along the node's X axis
    float contentScale() const;
    // the filter lane's mask, allocated on first use at the node's content rect
    core::FilterMask* ensureMask();
    // WHAT THE BRUSH WRITES, resolved once per stroke: the image, where it lives in
    // content space, and which kind it is (for the undo command and the event).
    struct Field {
        QImage* image = nullptr;
        QRectF rect;
        core::FilterMask* filter = nullptr;
        core::MaskKey* key = nullptr;
        bool valid() const { return image != nullptr && !image->isNull(); }
    };
    Field resolveField();      // allocates / creates as the kind allows
    Field currentField() const;  // never mutates: null when there is nothing yet
    bool paintStamp(const BrushStamp& aStamp);
    void beginStroke();
    void endStroke();

    core::Project& mProject;
    core::ObjectNode* mTarget;
    MaskParam mParam;
    BrushStroke mStroke;
    QVector2D mCursorPos;
    // the camera the last cursor event carried: the content mapping needs it, and
    // it is the same one the shader is drawn with
    core::CameraInfo mCamera;
    bool mPainting;
    // true while the editor is emitting its own damage: updateEvent must not react to it
    bool mAnnouncing;
    // stamps accumulate their damage here; flushPaint announces it once
    bool mDirty = false;
    bool mDirtyKey = false;
    bool mDirtyFilter = false;
    AssignPixels* mCommand;
    // the layer-mask stroke: the key it writes and its field before the first stamp
    core::MaskKey* mStrokeKey = nullptr;
    core::MaskKey::Data mStrokeBefore;
};

} // namespace ctrl

#endif // CTRL_MASKEDITOR_H
