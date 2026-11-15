#ifndef CTRL_FFD_BRUSHMODE_H
#define CTRL_FFD_BRUSHMODE_H

#include "core/Project.h"
#include "ctrl/Brush.h"
#include "ctrl/ffd/ffd_Target.h"
#include "ctrl/ffd/ffd_IMode.h"
#include "ctrl/ffd/ffd_MoveVertices.h"

namespace ctrl {
namespace ffd {

    class BrushMode: public IMode {
    public:
        BrushMode(core::Project& aProject, Targets& aTargets);
        virtual void updateParam(const FFDParam&);
        virtual bool updateCursor(const core::CameraInfo&, const core::AbstractCursor&);
        virtual void renderQt(const core::RenderInfo& aInfo, QPainter& aPainter);
        // drops a stroke in flight (the tool or the target went away)
        void cancel();

    private:
        enum State {
            State_Idle,
            State_Draw,
        };

        struct Status {
            Status();
            void clear();
            bool isDrawing() const;
            State state;
            MoveVertices* commandRef;
        };

        // one stamp cost of making the command (the first) or extending it
        bool executeStamp(const BrushStamp& aStamp);

        core::Project& mProject;
        Targets& mTargets;
        FFDParam mParam;
        Status mStatus;
        BrushStroke mStroke;
        QVector2D mCursorPos;
    };

} // namespace ffd
} // namespace ctrl

#endif // CTRL_FFD_BRUSHMODE_H
