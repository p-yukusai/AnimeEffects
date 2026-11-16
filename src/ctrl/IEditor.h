#ifndef CTRL_IEDITOR
#include <QRect>
#define CTRL_IEDITOR

#include "core/Project.h"

namespace ctrl {

class IEditor {
public:
    enum EventType {
        EventType_Frame,
        EventType_TimeKey,
        EventType_Tree,
        EventType_Resource,
        EventType_ProjectAttribute
    };

    virtual ~IEditor() {}

    virtual bool setTarget(core::ObjectNode* aTarget) = 0;

    virtual bool updateCursor(const core::CameraInfo& aCamera, const core::AbstractCursor& aCursor) = 0;

    virtual void updateEvent(EventType aType) = 0;

    virtual void renderQt(const core::RenderInfo& aInfo, QPainter& aPainter) = 0;

    // WORK THE EDITOR DEFERRED TO THE FRAME. A brush sample may be QUEUED (its document work is
    // cheap and its per-sample GL is not), and the viewport calls this once at the start of a
    // paint: one batch per frame, whatever the sample rate. Editors with nothing to defer do
    // nothing.
    virtual void flushPendingWork() {}

};

} // namespace ctrl

#endif // CTRL_IEDITOR
