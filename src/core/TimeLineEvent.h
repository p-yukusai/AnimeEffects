#ifndef CORE_TIMELINEEVENT
#define CORE_TIMELINEEVENT

#include <QVector>
#include "core/TimeKeyPos.h"
namespace core {
class Project;
}
namespace core {
class ObjectNode;
}

namespace core {

class TimeLineEvent {
public:
    enum Type {
        Type_PushKey,
        Type_RemoveKey,
        Type_MoveKey,
        Type_CopyKey,
        Type_ChangeKeyValue,
        Type_ChangeKeyProportional,
        // The filter STACK changed (add/remove/reorder/enable). It carries no key
        // targets; PropertyWidget refreshes the filter panel from this type, so an
        // undo/redo of a stack-only edit reaches the UI without inventing a key.
        Type_FilterStack,
        Type_TERM
    };

    struct Target {
        Target(): node(), pos(), subIndex() {}
        Target(ObjectNode& aNode, const TimeKeyPos& aPos, int aSubIndex):
            node(&aNode), pos(aPos), subIndex(aSubIndex) {}

        ObjectNode* node;
        TimeKeyPos pos;
        int subIndex;
    };

    TimeLineEvent();

    void setType(Type aType) { mType = aType; }

    void pushTarget(ObjectNode& aNode, const TimeKeyPos& aPos);
    void pushTarget(ObjectNode& aNode, const TimeKeyPos& aPos, int aSubIndex);
    void pushTarget(ObjectNode& aNode, TimeKeyType aType, int aIndex);
    void pushTarget(ObjectNode& aNode, TimeKeyType aType, int aIndex, int aSubIndex);

    // Announces an edit to an unkeyed value (targets() uses authored frames).
    void pushBaseValueTarget(ObjectNode& aNode, TimeKeyType aType);

    Type type() const { return mType; }
    QVector<Target>& targets() { return mTargets; }
    const QVector<Target>& targets() const { return mTargets; }

    QVector<Target>& baseValueTargets() { return mBaseValueTargets; }
    const QVector<Target>& baseValueTargets() const { return mBaseValueTargets; }

    bool hasAnyTargets() const { return !mTargets.empty() || !mBaseValueTargets.empty(); }

    // set a project by a project
    void setProject(Project& aProject) { mProject = &aProject; }
    Project& project() const {
        XC_PTR_ASSERT(mProject);
        return *mProject;
    }

private:
    Project* mProject;
    Type mType;
    QVector<Target> mTargets;
    QVector<Target> mBaseValueTargets;
};

} // namespace core

#endif // CORE_TIMELINEEVENT
