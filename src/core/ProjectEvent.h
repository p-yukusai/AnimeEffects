#ifndef CORE_PROJECTEVENT_H
#define CORE_PROJECTEVENT_H

namespace core {
class Project;
}

namespace core {

class ProjectEvent {
public:
    enum Type { Type_ChangeImageSize, Type_ChangeFrameRange, Type_ChangeLoop, Type_ChangeFps,
                Type_ChangeViewportBackground, Type_TERM };

    static ProjectEvent imageSizeChangeEvent(Project& aProject) { return ProjectEvent(aProject, Type_ChangeImageSize); }

    static ProjectEvent frameRangeChangeEvent(Project& aProject) { return ProjectEvent(aProject, Type_ChangeFrameRange); }

    static ProjectEvent loopChangeEvent(Project& aProject) { return ProjectEvent(aProject, Type_ChangeLoop); }

    static ProjectEvent fpsChangeEvent(Project& aProject) { return ProjectEvent(aProject, Type_ChangeFps); }

    static ProjectEvent viewportBackgroundChangeEvent(Project& aProject) { return ProjectEvent(aProject, Type_ChangeViewportBackground); }

    ProjectEvent(Project& aProject, Type aType);

    Project& project() const { return mProject; }
    Type type() const { return mType; }

private:
    Project& mProject;
    Type mType;
};

} // namespace core

#endif // CORE_PROJECTEVENT_H
