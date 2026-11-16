#ifndef CTRL_BONE_IMODE
#define CTRL_BONE_IMODE

#include "core/CameraInfo.h"
#include "core/AbstractCursor.h"
#include "core/RenderInfo.h"
#include "ctrl/BoneParam.h"
#include "ctrl/bone/bone_WeightState.h"

namespace ctrl {
namespace bone {

    class IMode {
    public:
        virtual ~IMode() {}
        virtual void updateParam(const BoneParam&) {}
        virtual bool updateCursor(const core::CameraInfo&, const core::AbstractCursor&) = 0;
        virtual void renderQt(const core::RenderInfo&, QPainter&) = 0;

        // Per-frame deferred work (IEditor::flushPendingWork reaches this).
        virtual void flushPendingWork() {}

        // The authored-weight panel's questions. Only PaintWeights answers
        // anything other than "not this mode"; keeping them on the one mode
        // interface is what lets BoneEditor forward a panel request without a
        // dynamic cast or a second editor.
        virtual WeightState weightState() const { return WeightState(); }
        virtual bool bakeWeights(bool, QString*) { return false; }
        virtual bool revertWeights(QString*) { return false; }
        virtual bool clearWeights(QString*) { return false; }
    };

} // namespace bone
} // namespace ctrl

#endif // CTRL_BONE_IMODE
