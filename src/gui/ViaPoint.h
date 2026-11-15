#ifndef GUI_VIAPOINT_H
#define GUI_VIAPOINT_H

#include <QWidget>
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
class CommandLogView;
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

    img::ResourceNode* requireOneResource();

    // The Properties panel's command log. It is the concrete view rather than a
    // plain QPlainTextEdit because the log must be TOLD the kind of a line (a
    // warning and an undo are not the same ink) and must re-ink itself on a theme
    // switch - neither of which the old plain-text/HTML appends could do.
    void setLogView(CommandLogView* aLogView);
    CommandLogView* logView() const { return mLogView; }

    // Editor messages (ctrl::UILogger). The type is no longer discarded: a
    // UILogType_Warn line renders with the warning ink and a badge, which is the
    // difference the old appendPlainText() could not show.
    void pushLog(const QString& aText, ctrl::UILogType aType) override;

    // One undo/redo step. aName is cmnd::Base::name(), which plain commands leave
    // empty - the view renders the badge alone in that case instead of the old
    // dangling "Undone : ".
    void pushCommandLog(bool aRedo, const QString& aName);

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
    CommandLogView* mLogView;
    gl::DeviceInfo mGLDeviceInfo;
    KeyCommandMap* mKeyCommandMap;
    KeyCommandInvoker* mKeyCommandInvoker;
    MainViewSetting* mMainViewSetting;
    MouseSetting* mMouseSetting;
};

} // namespace gui

#endif // GUI_VIAPOINT_H
