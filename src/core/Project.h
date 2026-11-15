#ifndef CORE_PROJECT_H
#define CORE_PROJECT_H

#include <QUuid>
#include <functional>
#include <functional>
#include "util/LifeLink.h"
#include "util/Signaler.h"
#include "util/NonCopyable.h"
#include "util/Range.h"
#include "thr/Paralleler.h"
#include "cmnd/Stack.h"
#include "core/ObjectTree.h"
#include "core/Animator.h"
#include "core/TimeInfo.h"
#include "core/TimeLineEvent.h"
#include "core/ResourceHolder.h"
#include "core/ResourceEvent.h"
#include "core/ObjectTreeEvent.h"
#include "core/ProjectEvent.h"
#include "gui/AudioPlaybackWidget.h"

namespace core {

class Project: private util::NonCopyable {
public:
    class Attribute {
    public:
        Attribute();
        void setImageSize(const QSize& aSize) { mImageSize = aSize; }
        const QSize& imageSize() const { return mImageSize; }
        // The project's frame range (inclusive). Everything playback-related
        // (transport, the playhead, the playback tick) is bounded by it; the
        // temporary playback window (ctrl::TimeLineEditor) overrides it while
        // active.
        void setFrameRange(const util::Range& aRange) { mFrameRange = aRange; }
        const util::Range& frameRange() const { return mFrameRange; }
        void setFps(int aFps) { mFps = aFps; }
        int fps() const { return mFps; }
        void setLoop(bool aLoop) { mLoop = aLoop; }
        bool loop() const { return mLoop; }

    private:
        QSize mImageSize;
        util::Range mFrameRange;
        int mFps;
        bool mLoop;
    };

    class Hook {
    public:
        virtual ~Hook() {}
    };

    Project(QString aFileName, Animator& aAnimator, Hook* aHookGrabbed);
    ~Project();

    util::LifeLink::Pointee<Project> pointee() { return mLifeLink.pointee<Project>(this); }
    util::LifeLink::Pointee<const Project> constPointee() { return mLifeLink.pointee<const Project>(this); }

    void setFileName(const QString& aFileName);
    const QString& fileName() const { return mFileName; }
    bool isNameless() const { return mFileName.isEmpty(); }

    bool isModified() const { return mCommandStack.isEdited(); }

    Attribute& attribute() { return mAttribute; }
    const Attribute& attribute() const { return mAttribute; }

    Animator& animator() { return mAnimator; }
    const Animator& animator() const { return mAnimator; }

    TimeInfo currentTimeInfo() const;

    ResourceHolder& resourceHolder() { return mResourceHolder; }
    const ResourceHolder& resourceHolder() const { return mResourceHolder; }

    cmnd::Stack& commandStack() { return mCommandStack; }
    const cmnd::Stack& commandStack() const { return mCommandStack; }

    ObjectTree& objectTree() { return mObjectTree; }
    const ObjectTree& objectTree() const { return mObjectTree; }

    thr::Paralleler& paralleler() { return *mParalleler; }
    const thr::Paralleler& paralleler() const { return *mParalleler; }

    Hook* hook() { return mHook.data(); }
    // Per-project identity, used for transient cache artifacts (the crash-
    // recovery copies). Stable for the lifetime of this object and unique
    // across sessions, so two nameless tabs never collide and repeated
    // autosaves overwrite the same file.
    const QUuid& id() const { return mId; }

    util::Signaler<void(TimeLineEvent&, bool)> onTimeLineModified;
    util::Signaler<void(ObjectNode&, bool)> onNodeAttributeModified;
    util::Signaler<void(ResourceEvent&, bool)> onResourceModified;
    util::Signaler<void(ObjectTreeEvent&, bool)> onTreeRestructured;
    util::Signaler<void(ProjectEvent&, bool)> onProjectAttributeModified;
    // Audio
    std::vector<audioConfig>* pConf = new std::vector<audioConfig>();
    mediaState* mediaPlayer = new mediaState();
    bool mediaRefresh = false;
    bool uiRefresh = false;

private:
    util::LifeLink mLifeLink;
    QString mFileName;
    Attribute mAttribute;
    QUuid mId;

    QScopedPointer<thr::Paralleler> mParalleler;
    ResourceHolder mResourceHolder;
    cmnd::Stack mCommandStack;
    ObjectTree mObjectTree;
    Animator& mAnimator;
    QScopedPointer<Hook> mHook;
};

} // namespace core

#endif // CORE_PROJECT_H
