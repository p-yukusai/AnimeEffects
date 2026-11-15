#ifndef CTRL_TIMELINEUTIL_H
#define CTRL_TIMELINEUTIL_H

#include <QVector>
#include "util/Range.h"
#include "cmnd/Stable.h"
#include "cmnd/Listener.h"
#include "core/Project.h"
#include "core/TimeLine.h"
#include "core/TimeLineEvent.h"
#include "core/MoveKey.h"
#include "core/RotateKey.h"
#include "core/ScaleKey.h"
#include "core/BlurKey.h"
#include "core/DepthKey.h"
#include "core/OpaKey.h"
#include "core/HsvKey.h"
#include "core/BoneKey.h"
#include "core/PoseKey.h"
#include "core/FFDKey.h"
#include "core/MeshKey.h"
#include "core/ImageKey.h"

namespace ctrl {

namespace TimeLineUtil {

    //-------------------------------------------------------------------------------------------------
    class MoveFrameOfKey: public cmnd::Stable {
    public:
        MoveFrameOfKey(const core::TimeLineEvent& aCommandEvent);
        bool modifyMove(core::TimeLineEvent& aModEvent, int aAdd, const util::Range& aFrame, int* aClampedAdd);
        static bool lessThan(const core::TimeLineEvent::Target& aLhs, const core::TimeLineEvent::Target& aRhs);
        static bool greaterThan(const core::TimeLineEvent::Target& aLhs, const core::TimeLineEvent::Target& aRhs);
        virtual void undo();
        virtual void redo();

    private:
        bool contains(const core::TimeLine::MapType& aMap, int aIndex);

        QVector<core::TimeLineEvent::Target> mSortedTargets;
        int mCurrent;
        int mMove;
    };

    //-------------------------------------------------------------------------------------------------
    // The VALUE half of the graph's 2-axis key move: rewrites ONE numeric
    // component of ONE key, in place, and puts it back on undo.
    //
    // It is a cmnd::Stable carrying both values, so undo and redo rebuild the
    // document from the command alone (the requirement cmnd/Stable.h states) and
    // a whole drag stays ONE history entry when this command is pushed inside the
    // same cmnd::ScopedMacro as the frame half (MoveFrameOfKey above) - which is
    // the only reason the graph splits one key move into two commands rather than
    // one: the frame is a map operation and the value is a field write, and each
    // already has a command that knows how to replay it.
    //
    // The component is named by (type, component) - the graph's own Channel
    // vocabulary - and the write goes through graph::Channel::writeValue, the one
    // place that knows which field of which key type a component is. The key is
    // held by POINTER, like the easing gesture's cmnd::ModifiableAssign, which is
    // safe here because TimeLine::move re-keys the same key OBJECT: the frame half
    // of the gesture moves the map entry, never the key this command writes.
    class AssignKeyValue: public cmnd::Stable {
    public:
        AssignKeyValue(core::TimeKey& aKey, core::TimeKeyType aType, int aComponent, float aValue);

        // A later step of the same gesture rewrites this command, which is what
        // makes a whole drag one undo step. Mirrors cmnd::ModifiableAssign's own
        // modifyValue, including applying the new value at once while the command
        // is the stack's executed one.
        void modifyValue(float aValue);

        // Captures the value the key holds NOW as the one undo restores, then
        // applies the new value - cmnd::ModifiableAssign's own exec.
        virtual void exec();
        virtual void undo();
        virtual void redo();

    private:
        core::TimeKey* mKey;
        core::TimeKeyType mType;
        int mComponent;
        float mPrev;
        float mNext;
        bool mDone;
    };

    //-------------------------------------------------------------------------------------------------
    class Notifier: public cmnd::Listener {
    public:
        Notifier(core::Project& aProject): mProject(aProject), mEvent() {}

        core::TimeLineEvent& event() { return mEvent; }
        const core::TimeLineEvent& event() const { return mEvent; }

        virtual void onExecuted() { mProject.onTimeLineModified(mEvent, false); }

        virtual void onUndone() { mProject.onTimeLineModified(mEvent, true); }

        virtual void onRedone() { mProject.onTimeLineModified(mEvent, false); }

    private:
        core::Project& mProject;
        core::TimeLineEvent mEvent;
    };

    class ResourceModificationNotifier: public cmnd::Listener {
    public:
        ResourceModificationNotifier(core::Project& aProject): mProject(aProject), mEvent(aProject) {}

        core::ResourceEvent& event() { return mEvent; }
        const core::ResourceEvent& event() const { return mEvent; }

        virtual void onExecuted() { mProject.onResourceModified(mEvent, false); }

        virtual void onUndone() { mProject.onResourceModified(mEvent, true); }

        virtual void onRedone() { mProject.onResourceModified(mEvent, false); }

    private:
        core::Project& mProject;
        core::ResourceEvent mEvent;
    };

    //-------------------------------------------------------------------------------------------------
    /// @note you can also set a data to default keys in following assignment functions.
    ///@{
    void assignMoveKeyData(
        core::Project& aProject, core::ObjectNode& aTarget, int aFrame, const core::MoveKey::Data& aNewData
    );

    void assignRotateKeyData(
        core::Project& aProject, core::ObjectNode& aTarget, int aFrame, const core::RotateKey::Data& aNewData
    );

    void assignScaleKeyData(
        core::Project& aProject, core::ObjectNode& aTarget, int aFrame, const core::ScaleKey::Data& aNewData
    );

    void assignDepthKeyData(
        core::Project& aProject, core::ObjectNode& aTarget, int aFrame, const core::DepthKey::Data& aNewData
    );

    void assignOpaKeyData(
        core::Project& aProject, core::ObjectNode& aTarget, int aFrame, const core::OpaKey::Data& aNewData
    );

    void assignHSVKeyData(
        core::Project& aProject, core::ObjectNode& aTarget, int aFrame, const core::HSVKey::Data& aNewData
    );

    void assignBlurKeyData(
        core::Project& aProject, core::ObjectNode& aTarget, int aFrame, const core::BlurKey::Data& aNewData
    );

    void assignPoseKeyEasing(
        core::Project& aProject, core::ObjectNode& aTarget, int aFrame, const util::Easing::Param& aNewData
    );

    void assignFFDKeyEasing(
        core::Project& aProject, core::ObjectNode& aTarget, int aFrame, const util::Easing::Param& aNewData
    );

    void
    assignImageKeyResource(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, img::ResourceNode& aNewData);

    void
    assignImageKeyOffset(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, const QVector2D& aNewData);

    void assignImageKeyCellSize(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, int aNewData);
    ///@}

    //-------------------------------------------------------------------------------------------------
    void pushNewMoveKey(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, core::MoveKey* aKey);

    void pushNewRotateKey(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, core::RotateKey* aKey);

    void pushNewScaleKey(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, core::ScaleKey* aKey);

    void pushNewDepthKey(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, core::DepthKey* aKey);

    void pushNewOpaKey(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, core::OpaKey* aKey);

    void pushNewHSVKey(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, core::HSVKey* aKey);
    void pushNewBlurKey(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, core::BlurKey* aKey);

    void pushNewPoseKey(
        core::Project& aProject, core::ObjectNode& aTarget, int aFrame, core::PoseKey* aKey, core::BoneKey* aParentKey
    );

    void pushNewFFDKey(
        core::Project& aProject, core::ObjectNode& aTarget, int aFrame, core::FFDKey* aKey, core::TimeKey* aParentKey
    );

    void pushNewImageKey(core::Project& aProject, core::ObjectNode& aTarget, int aFrame, core::ImageKey* aKey);

} // namespace TimeLineUtil

} // namespace ctrl

#endif // CTRL_TIMELINEUTIL_H
