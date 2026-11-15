#ifndef GUI_VIAPOINT_H
#define GUI_VIAPOINT_H

#include <QWidget>
#include <QPlainTextEdit>
#include <QScopedPointer>
#include "util/NonCopyable.h"
#include "gl/DeviceInfo.h"
#include "core/Project.h"
#include "ctrl/UILogger.h"
namespace img {
class ResourceNode;
}
namespace gui {
class ResourceDialog;
}
namespace gui {
class ObjectTreeWidget;
}
namespace gui {
class MainMenuBar;
}
namespace gui {
class KeyCommandMap;
}
namespace gui {
class KeyCommandInvoker;
}
namespace gui {
class MainViewSetting;
}
namespace gui {
class MouseSetting;
}

namespace gui {

class ViaPoint: private util::NonCopyable, public ctrl::UILogger {
public:
    ViaPoint(QWidget* aParent);

    void setProject(core::Project* aProject);

    void setMainMenuBar(MainMenuBar* aMainMenuBar);
    MainMenuBar* mainMenuBar() const { return mMainMenuBar; }

    void setResourceDialog(ResourceDialog* aResDialog);
    ResourceDialog* resourceDialog() const { return mResDialog; }

    // The object tree's view. The asset removal path needs it to delete the
    // layers that reference a removed asset in the same undo step; it is the one
    // place a res-namespace action can reach the object tree widget from.
    void setObjectTreeWidget(ObjectTreeWidget* aTree);
    ObjectTreeWidget* objectTreeWidget() const { return mObjectTree; }

    img::ResourceNode* requireOneResource();

    void setLogView(QPlainTextEdit* aLogView);
    virtual void pushLog(const QString& aText, ctrl::UILogType aType);
    void pushUndoneLog(const QString& aText);
    void pushRedoneLog(const QString& aText);

    void setGLDeviceInfo(const gl::DeviceInfo&);
    const gl::DeviceInfo& glDeviceInfo() const;
    int getVRAM() const;

    void setKeyCommandMap(KeyCommandMap* aMap);
    KeyCommandMap* keyCommandMap() const { return mKeyCommandMap; }

    void setKeyCommandInvoker(KeyCommandInvoker* aInvoker);
    void throwKeyPressingToKeyCommandInvoker(const QKeyEvent* aEvent);
    void throwKeyReleasingToKeyCommandInvoker(const QKeyEvent* aEvent);

    void setMainViewSetting(MainViewSetting& aSetting);
    MainViewSetting& mainViewSetting();
    const MainViewSetting& mainViewSetting() const;

    void setMouseSetting(MouseSetting& aSetting);
    MouseSetting& mouseSetting();
    const MouseSetting& mouseSetting() const;

    util::Signaler<void()> onVisualUpdated;
    // Fired after the General Settings dialog is accepted, so owners can
    // re-read settings that are applied live (e.g. the autosave timer).
    util::Signaler<void()> onGeneralSettingsChanged;

private:
    QWidget* mParent;
    core::Project* mProject;
    MainMenuBar* mMainMenuBar;
    ResourceDialog* mResDialog;
    ObjectTreeWidget* mObjectTree;
    QPlainTextEdit* mLogView;
    gl::DeviceInfo mGLDeviceInfo;
    KeyCommandMap* mKeyCommandMap;
    KeyCommandInvoker* mKeyCommandInvoker;
    MainViewSetting* mMainViewSetting;
    MouseSetting* mMouseSetting;
};

} // namespace gui

#endif // GUI_VIAPOINT_H
