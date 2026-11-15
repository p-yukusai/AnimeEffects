#ifndef GUI_PROP_KEYACCESSOR_H
#define GUI_PROP_KEYACCESSOR_H

#include "util/Easing.h"
#include "img/ResourceHandle.h"
#include "core/Project.h"
#include "core/ObjectNode.h"
#include "core/BlurKey.h"
#include "core/TimeLineEvent.h"

namespace gui {
namespace prop {

    class KeyAccessor {
    public:
        KeyAccessor();
        void setProject(core::Project* aProject);
        void setTarget(core::ObjectNode* aTarget);

        // default values
        void assignDefaultDepth(float);
        void assignDefaultOpacity(float);
        void assignDefaultHSV(QList<int>);
        void assignDefaultImageResource(img::ResourceNode&);
        void assignDefaultImageOffset(const QVector2D&);
        void assignDefaultImageCellSize(int);

        // move
        void assignMoveEasing(util::Easing::Param);
        void assignMoveSpline(int);
        void assignMovePosition(const QVector2D&);
        void assignMoveCentroid(const QVector2D&);

        // rotate
        void assignRotateEasing(util::Easing::Param);
        void assignRotateAngle(float);

        // scale
        void assignScaleEasing(util::Easing::Param);
        void assignScaleRate(const QVector2D&);

        // depth
        void assignDepthEasing(util::Easing::Param);
        void assignDepthPosition(float);

        // opa
        void assignOpaEasing(util::Easing::Param);
        void assignOpacity(float);

        // hsv
        void assignHSVEasing(util::Easing::Param);
        void assignHSV(int aValue, QString aType);

        // blur
        void assignBlurEasing(util::Easing::Param);
        void assignBlur(float aAmount);
        void assignBlurEllipse(float aBlurX, float aBlurY, float aAngleDeg);
        void assignBlurDirectional(bool aIsDirectional);
        core::BlurKey::Data blurData() const;

        // pose
        void assignPoseEasing(util::Easing::Param);

        // ffd
        void assignFFDEasing(util::Easing::Param);

        // image
        void assignImageResource(img::ResourceNode&);
        void assignImageOffset(const QVector2D&);
        void assignImageCellSize(int);

        // new keys
        void knockNewMove();
        void knockNewRotate();
        void knockNewScale();
        void knockNewDepth();
        void knockNewOpacity();
        void knockNewHSV();
        void knockNewBlur();
        void knockNewPose();
        void knockNewFFD();
        void knockNewImage(const img::ResourceHandle& aHandle);

    private:
        bool isValid() const { return mProject && mTarget && mTarget->timeLine(); }
        int getFrame() const { return mProject->animator().currentFrame().get(); }
        core::TimeLine& currline() { return *mTarget->timeLine(); }

        core::Project* mProject;
        core::ObjectNode* mTarget;
    };

    // The Auto context of one component of a KEY - not of the frame the playhead
    // happens to be on, which is the frame KeyAccessor itself is bound to: the
    // picker and the header's chip edit the keys the caller SELECTED, and a
    // selection is not at the playhead. aComponent is the component being edited
    // (an Auto Move x segment is shaped by x values, its y segment by y values).
    // The derivation is the blender's and the graph panel's (see
    // core::autoEasingContext): the key's successor in its own lane ends the
    // segment it shapes, and the keys either side of that pair shape its handles.
    // Empty when the lane has no such pair, which leaves an Auto curve the neutral
    // straight line.
    //
    // A key's Auto context is a DOCUMENT fact, so it is here rather than inside
    // the widget that happens to open the picker: this is the one walk every reader of it
    // (the header's chip and the picker) is given, so a second copy cannot drift from
    // the shape the blender plays.
    util::Easing::AutoContext autoContextOf(
        const core::TimeLineEvent::Target& aTarget, const core::TimeKey& aKey, int aComponent);

} // namespace prop
} // namespace gui

#endif // GUI_PROP_KEYACCESSOR_H
