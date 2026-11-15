#include "gui/ViaPoint.h"
#include "gui/CommandLogView.h"

#include "gl/Global.h"
#include "gui/ResourceDialog.h"
#include "gui/KeyCommandInvoker.h"
#include <QOpenGLFunctions>

namespace gui {

ViaPoint::ViaPoint(QWidget* aParent):
    mParent(aParent),
    mProject(),
    mResDialog(),
    mObjectTree(),
    mLogView(),
    mGLDeviceInfo(),
    mKeyCommandMap(),
    mKeyCommandInvoker(),
    mMainViewSetting(),
    mMouseSetting() {}

void ViaPoint::setProject(core::Project* aProject) { mProject = aProject; }

void ViaPoint::setMainMenuBar(MainMenuBar* aMainMenuBar) { mMainMenuBar = aMainMenuBar; }

void ViaPoint::setResourceDialog(ResourceDialog* aResDialog) { mResDialog = aResDialog; }

void ViaPoint::setObjectTreeWidget(ObjectTreeWidget* aTree) { mObjectTree = aTree; }

img::ResourceNode* ViaPoint::requireOneResource() {
    QScopedPointer<ResourceDialog> dialog(new ResourceDialog(*this, true, mParent));
    dialog->setProject(mProject);
    dialog->exec();

    if (dialog->hasValidNode()) {
        return dialog->nodeList().first();
    }
    return nullptr;
}

void ViaPoint::setLogView(CommandLogView* aLogView) { mLogView = aLogView; }

void ViaPoint::pushLog(const QString& aText, ctrl::UILogType aType) {
    if (!mLogView) return;
    // The type used to be discarded here ((void)aType) because the view could only
    // append plain text: a warning and an informational line were indistinguishable
    // once written. The entry keeps the kind so the view can give a warning its own
    // ink and badge.
    LogEntry entry;
    entry.kind = (aType == ctrl::UILogType_Warn) ? LogKind::Warning : LogKind::Info;
    entry.text = aText;
    mLogView->append(entry);
}

void ViaPoint::pushCommandLog(bool aRedo, const QString& aName) {
    if (mLogView) {
        mLogView->append(LogEntry{aRedo ? LogKind::Redo : LogKind::Undo, aName});
    }
}

void ViaPoint::setGLDeviceInfo(const gl::DeviceInfo& aInfo) { mGLDeviceInfo = aInfo; }

const gl::DeviceInfo& ViaPoint::glDeviceInfo() const {
    XC_ASSERT(mGLDeviceInfo.isValid());
    return mGLDeviceInfo;
}
int ViaPoint::getVRAM() const {
    #ifndef Q_OS_MAC
    XC_ASSERT(mGLDeviceInfo.isValid());
    GLint vram = 0;
    gl::Global::functions().glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &vram);
    gl::Global::functions().glGetIntegerv(GL_ATI_meminfo, &vram);
    gl::Global::functions().glGetError();
    if (vram == 0) {
        return -1;
    }
    vram = vram * 0.001;
    return vram;
    #endif
    #ifdef Q_PROCESSOR_ARM
    return -2;
    #else
    return -1;
    #endif
}

void ViaPoint::setKeyCommandMap(KeyCommandMap* aMap) { mKeyCommandMap = aMap; }

void ViaPoint::setKeyCommandInvoker(KeyCommandInvoker* aInvoker) { mKeyCommandInvoker = aInvoker; }

void ViaPoint::throwKeyPressingToKeyCommandInvoker(const QKeyEvent* aEvent) {
    if (mKeyCommandInvoker) {
        mKeyCommandInvoker->onKeyPressed(aEvent);
    }
}

void ViaPoint::throwKeyReleasingToKeyCommandInvoker(const QKeyEvent* aEvent) {
    if (mKeyCommandInvoker) {
        mKeyCommandInvoker->onKeyReleased(aEvent);
    }
}

void ViaPoint::setMainViewSetting(MainViewSetting& aSetting) { mMainViewSetting = &aSetting; }

MainViewSetting& ViaPoint::mainViewSetting() {
    XC_PTR_ASSERT(mMainViewSetting);
    return *mMainViewSetting;
}

const MainViewSetting& ViaPoint::mainViewSetting() const {
    XC_PTR_ASSERT(mMainViewSetting);
    return *mMainViewSetting;
}

void ViaPoint::setMouseSetting(MouseSetting& aSetting) { mMouseSetting = &aSetting; }

MouseSetting& ViaPoint::mouseSetting() {
    XC_PTR_ASSERT(mMouseSetting);
    return *mMouseSetting;
}

const MouseSetting& ViaPoint::mouseSetting() const {
    XC_PTR_ASSERT(mMouseSetting);
    return *mMouseSetting;
}

} // namespace gui
