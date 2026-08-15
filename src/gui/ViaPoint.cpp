#include "gui/ViaPoint.h"
#include "gui/theme/Colors.h"

#include "gl/Global.h"
#include "gui/ResourceDialog.h"
#include "gui/KeyCommandInvoker.h"
#include <QOpenGLFunctions>

namespace gui {

ViaPoint::ViaPoint(QWidget* aParent):
    mParent(aParent),
    mProject(),
    mResDialog(),
    mLogView(),
    mGLDeviceInfo(),
    mKeyCommandMap(),
    mKeyCommandInvoker(),
    mMainViewSetting(),
    mMouseSetting() {}

void ViaPoint::setProject(core::Project* aProject) { mProject = aProject; }

void ViaPoint::setMainMenuBar(MainMenuBar* aMainMenuBar) { mMainMenuBar = aMainMenuBar; }

void ViaPoint::setResourceDialog(ResourceDialog* aResDialog) { mResDialog = aResDialog; }

img::ResourceNode* ViaPoint::requireOneResource() {
    QScopedPointer<ResourceDialog> dialog(new ResourceDialog(*this, true, mParent));
    dialog->setProject(mProject);
    dialog->exec();

    if (dialog->hasValidNode()) {
        return dialog->nodeList().first();
    }
    return nullptr;
}

void ViaPoint::setLogView(QPlainTextEdit* aLogView) { mLogView = aLogView; }

void ViaPoint::pushLog(const QString& aText, ctrl::UILogType aType) {
    (void)aType;
    if (mLogView) {
        mLogView->appendPlainText(aText);
    }
}

void ViaPoint::pushUndoneLog(const QString& aText) {
    if (mLogView) {
        mLogView->appendHtml("<font color=\"" + theme::Colors::current().textMuted.name() + "\">" + aText + "</font>");
    }
}

void ViaPoint::pushRedoneLog(const QString& aText) {
    if (mLogView) {
        mLogView->appendHtml("<font color=\"" + theme::Colors::current().accentBright.name() + "\">" + aText + "</font>");
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
