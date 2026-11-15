#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QScopedPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>
#include <QUrl>

#include <cmath>
#include <limits>

#include "cmnd/BasicCommands.h"
#include "cmnd/ScopedMacro.h"
#include "core/FormatVersion.h"
#include "ctrl/CmndName.h"
#include "gui/GeneralSettingDialog.h"
#include "gui/KeyBindingDialog.h"
#include "gui/MainMenuBar.h"
#include "gui/MainWindow.h"
#include "gui/ProjectSettingDialog.h"
#include "gui/ResourceDialog.h"
#include "gui/ViaPoint.h"
#include "gui/qml/MenuCommand.h"
#include "media/FFmpegHelpers.h"
#include "util/NetworkUtil.h"
#include "util/TextUtil.h"

#ifdef Q_OS_WINDOWS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#define VENDOR_ID_LEN 13

namespace gui {
//-------------------------------------------------------------------------------------------------
#ifdef Q_PROCESSOR_X86_64
    // This is gonna be rough
    #ifdef _WIN32
    #include <intrin.h> // __cpuid()
    #endif

    typedef unsigned int cpuid_t[4];

    #define EAX 0
    #define EBX 1
    #define ECX 2
    #define EDX 3

    // https://elixir.bootlin.com/linux/latest/source/arch/x86/include/asm/processor.h#L216
    // https://stackoverflow.com/questions/6491566/getting-the-machine-serial-number-and-cpu-id-using-c-c-in-linux
    // https://stackoverflow.com/questions/1666093/cpuid-implementations-in-c
    static void native_cpuid(unsigned int function_id, cpuid_t r) {
        #ifdef _WIN32
        __cpuid((int *) r, (int) function_id);
        #else
        r[EAX] = function_id;
        r[ECX] = 0;

        /* ecx is often an input as well as an output. */
        asm volatile("cpuid"
            : "=a" (r[EAX]),
              "=b" (r[EBX]),
              "=c" (r[ECX]),
              "=d" (r[EDX])
            : "0" (r[EAX]), "2" (r[ECX])
            : "memory");
        #endif
    }

// XXX: you have to make sure the vendor argument is at least lengthed VENDOR_ID_LEN
static void cpuid_vendor_id(char vendor[VENDOR_ID_LEN]) {
    // Always initialize the result in case of buggy CPU (like ES/QS CPUs)
    cpuid_t v = {};
    native_cpuid(0, v);

    // https://learn.microsoft.com/en-us/cpp/intrinsics/cpuid-cpuidx#example
    ((unsigned int *) vendor)[0] = v[EBX];
    ((unsigned int *) vendor)[1] = v[EDX];
    ((unsigned int *) vendor)[2] = v[ECX];
    vendor[VENDOR_ID_LEN - 1] = '\0';
}

#endif
//-------------------------------------------------------------------------------------------------
MainMenuBar::MainMenuBar(MainWindow& aMainWindow,
                         ViaPoint& aViaPoint,
                         GUIResources& aGUIResources,
                         QObject* aParent):
    QObject(aParent),
    mMainWindow(&aMainWindow),
    mViaPoint(aViaPoint),
    mGUIResources(aGUIResources) {
    // ---- File ---------------------------------------------------------------
    addCommand("newProject", tr("New project"), [this]() { mMainWindow->onNewProjectTriggered(); });
    addCommand("openProject", tr("Open project"), [this]() { mMainWindow->onOpenProjectTriggered(); });
    mProjectActions.push_back(
        addCommand("saveProject", tr("Save project"), [this]() { mMainWindow->onSaveProjectTriggered(); }));
    mProjectActions.push_back(addCommand("saveProjectAs", tr("Save project as"), [this]() {
        mMainWindow->onSaveProjectAsTriggered();
    }));
    mProjectActions.push_back(addCommand("exportProject", tr("Export project as"), [this]() {
        mMainWindow->onExportTriggered();
    }));
    // "Quickly export as" is a QUICK action: the destination picker first, then the
    // export with no further dialogs. It is not a fifty-five-row tree.
    mProjectActions.push_back(addCommand("quickExport", tr("Quickly export as..."), [this]() {
        mMainWindow->onQuickExportTriggered();
    }));
    // Re-export: the same export, one click, no dialogs. Disabled until an export
    // has succeeded, because "again" has nothing to repeat before there has been one.
    mExportAgain = addCommand("exportAgain", tr("Export again"), [this]() {
        mMainWindow->onExportAgainTriggered();
    });
    mExportAgain->setEnabled(false);
    mProjectActions.push_back(
        addCommand("closeProject", tr("Close project"), [this]() { mMainWindow->onCloseProjectTriggered(); }));

    // "Open recent" is a submenu. Its rows are the scene's Instantiator over this
    // list, because the paths are dynamic and a menu is the documented place for a
    // dynamic list (Menu docs, "Dynamically Generating Menu Items"): each path is an
    // Action, and the Instantiator's onObjectAdded adds it to the recent Menu.
    {
        QSettings settings;
        settings.sync();
        const QStringList paths = settings.value("projectloader/recents").toStringList();
        for (const QString& path : paths) {
            if (mRecentFiles.size() >= 8)
                break;
            if (path.isEmpty() || path == QStringLiteral("Path placeholder"))
                continue;
            QVariantMap item;
            item.insert(QStringLiteral("text"), path);
            item.insert(QStringLiteral("path"), path);
            item.insert(QStringLiteral("enabled"), true);
            mRecentFiles.push_back(item);
        }
        if (mRecentFiles.isEmpty()) {
            QVariantMap item;
            item.insert(QStringLiteral("text"), tr("No other projects"));
            item.insert(QStringLiteral("path"), QString());
            item.insert(QStringLiteral("enabled"), false);
            mRecentFiles.push_back(item);
        }
    }

    // ---- Edit ---------------------------------------------------------------
    mProjectActions.push_back(
        addCommand("undo", tr("Undo"), [this]() { mMainWindow->onUndoTriggered(); }));
    mProjectActions.push_back(
        addCommand("redo", tr("Redo"), [this]() { mMainWindow->onRedoTriggered(); }));
    mProjectActions.push_back(addCommand("projectSettings", tr("Project settings"), [this]() {
        onProjectSettingsTriggered();
    }));
    mProjectActions.push_back(addCommand("assets", tr("Assets"), [this]() {
        if (mViaPoint.resourceDialog())
            mViaPoint.resourceDialog()->setVisible(!mViaPoint.resourceDialog()->isVisible());
    }));

    // The Panels commands are added by MainWindow once the docks exist: the menu
    // structure is in QML, but the text of a panel row is the dock's own title and
    // the toggle is the window's. They are added before HeaderBar loads.

    // ---- Preferences --------------------------------------------------------
    addCommand("settings", tr("Settings"), [this]() {
        auto* generalSettingsDialog = new GeneralSettingDialog(mGUIResources, mViaPoint, mMainWindow);
        QScopedPointer<GeneralSettingDialog> dialog(generalSettingsDialog);
        if (dialog->exec() == QDialog::DialogCode::Accepted) {
            if (generalSettingsDialog->timeFormatHasChanged())
                onTimeFormatChanged();
            if (generalSettingsDialog->themeHasChanged())
                mGUIResources.setTheme(generalSettingsDialog->theme());
            if (generalSettingsDialog->accentHasChanged())
                mGUIResources.setAccent(generalSettingsDialog->accent());
            // Autosave enable/delay are applied live (start/stop/re-arm the timer)
            // rather than waiting for the next app launch.
            mViaPoint.onGeneralSettingsChanged();
        }
    });
    addCommand("keybindings", tr("Keybindings"), [this]() {
        XC_PTR_ASSERT(mViaPoint.keyCommandMap());
        QScopedPointer<KeyBindingDialog> dialog(new KeyBindingDialog(*mViaPoint.keyCommandMap(), mMainWindow));
        dialog->exec();
    });

    // ---- Help ---------------------------------------------------------------
    addCommand("about", tr("About AnimeEffects"), []() {
        QMessageBox msgBox;
        msgBox.setWindowIcon(QIcon("../src/AnimeEffects.ico"));
        auto versionString = QString::number(AE_MAJOR_VERSION) + "." + QString::number(AE_MINOR_VERSION) + "."
                             + QString::number(AE_MICRO_VERSION);
        auto platform = QSysInfo::productType();
        platform[0] = platform[0].toUpper();
        QString msgStr = tr("### AnimeEffects for ") + platform + tr(" version ") + versionString + "<br />"
                         + tr("An easy to use 2D animation software maintained by the "
                              "[AnimeEffectsDevs](https://github.com/AnimeEffectsDevs)."
                              "<br />Licensed under the GPL v3.0 and powered by various open source libraries.");
        msgStr.append(tr("<br /><br />Video/audio encoding uses the FFmpeg libraries "
                         "(https://ffmpeg.org), which are licensed under the GNU Lesser GPL "
                         "v2.1+ (or GPL, depending on the build). Their corresponding source "
                         "is available as required by that license."));
        msgStr.append(tr("<br /><br />Contributors:<br />"
                         "[Hidefuku](https://github.com/hidefuku), "
                         "[Yukusai](https://github.com/p-yukusai), "
                         "[Gambot](https://github.com/GbotHQ), "
                         "[Arrangemonk](https://github.com/Arrangemonk), "
                         "[OneByStudio](https://onebystudio.com), "
                         "[Larpon](https://github.com/larpon), "
                         "[Herace](https://github.com/herace), "
                         "[Aodaruma](https://github.com/Aodaruma), "
                         "[Azagaya](https://github.com/azagaya), "
                         "[FoxyHawk](https://github.com/FoxyHawk), "
                         "[Nanashia](https://github.com/Nanashia), "
                         "[Henrich](https://github.com/henrich), "
                         "[mcddx330](https://github.com/mcddx330), "
                         "[Freddii](https://github.com/freddii), "
                         "[Aki017](https://github.com/aki017), "
                         "[PicoHz](https://github.com/picoHz), "
                         "[Peter9811](https://github.com/peter9811), "
                         "[GES233](https://github.com/GES233), "
                         "[h-banii](https://github.com/h-banii), "
                         "[henrich](https://github.com/henrich), "
                         "[Nanashia](https://github.com/Nanashia).\n"
                         "If we forgot to include you please let us know or open a PR!"));
        msgBox.setTextFormat(Qt::TextFormat::MarkdownText);
        msgBox.setText(msgStr);
        QFont font = msgBox.font();
        font.setPointSize(10);
        msgBox.setFont(font);
        msgBox.setWindowTitle(tr("About us"));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        msgBox.exec();
    });
    addCommand("checkUpdates", tr("Check for updates"), [this]() {
        const QString url("https://api.github.com/repos/AnimeEffectsDevs/AnimeEffects/tags");
        util::NetworkUtil::checkForUpdate(url, mMainWindow);
    });
    addCommand("telemetry", tr("System telemetry"), [this]() {
        QString system_info = "Unknown";
        #ifdef Q_OS_WIN
        MEMORYSTATUSEX memory_status;
        ZeroMemory(&memory_status, sizeof(MEMORYSTATUSEX));
        memory_status.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memory_status)) {
            system_info = QString(" RAM: %1 MB").arg(memory_status.ullTotalPhys / (1024 * 1024));
        } else {
            system_info = " RAM: Unknown";
        }
        #elif defined(Q_OS_MACOS)
        QProcess p;
        p.start("system_profiler", QStringList() << "SPHardwareDataType" << "| grep" << " Memory:");
        p.waitForFinished();
        system_info = p.readAllStandardOutput();
        p.close();
        #elif defined(Q_OS_LINUX)
        QProcess p;
        p.start("awk", QStringList() << "/MemTotal/ { print $2 }" << "/proc/meminfo");
        p.waitForFinished();
        QString memory = p.readAllStandardOutput();
        system_info = (QString("; RAM: %1 MB").arg(memory.toLong() / 1024));
        p.close();
        #endif
        std::string cpuVendor;
        QMessageBox msgBox;
        msgBox.setWindowIcon(QIcon("../src/AnimeEffects.ico"));
        auto versionString = QString::number(AE_MAJOR_VERSION) + "." + QString::number(AE_MINOR_VERSION) + "."
                             + QString::number(AE_MICRO_VERSION);
        auto formatVersionString = QString::number(AE_PROJECT_FORMAT_MAJOR_VERSION) + "."
                                   + QString::number(AE_PROJECT_FORMAT_MINOR_VERSION);
        auto platform = QSysInfo::productType();
        auto qtVersion = QString::number(QT_VERSION_MAJOR) + "." + QString::number(QT_VERSION_MINOR) + "."
                         + QString::number(QT_VERSION_PATCH);
        platform[0] = platform[0].toUpper();
        QString detail;
        detail += tr("Version: ") + versionString + "\n";
        detail += tr("Format version: ") + formatVersionString + "\n";
        detail += tr("Platform: ") + platform + " " + QSysInfo::productVersion() + "\n";
        detail += tr("Build ABI: ") + QSysInfo::buildAbi() + "\n";
        #ifdef Q_OS_MACOS
        cpuVendor = "Apple";
        #endif

        #ifdef Q_PROCESSOR_X86_64
        char s[VENDOR_ID_LEN];
        cpuid_vendor_id(s);
        cpuVendor = s;

        #else
        cpuVendor = "Unknown";
        #endif

        QString vendor = "Unknown";
        if (cpuVendor == "GenuineIntel") {
            vendor = "Intel";
        } else if (cpuVendor == "AuthenticAMD") {
            vendor = "AMD";
        } else if (cpuVendor == "Unknown" || cpuVendor == "Apple") {
            vendor = "Apple";
        }
        // ---------------------- //
        detail += tr("CPU vendor: ") + vendor + "\n";
        detail += tr("CPU architecture: ") + QSysInfo::currentCpuArchitecture() + "\n";
        detail += tr("CPU threads: ") + QString::number(QThread::idealThreadCount()) + "\n";
        #ifdef Q_OS_APPLE
        QRegularExpression regexp = QRegularExpression("(?<=Memory:)(.*?)(?=\r?\n)");
        QString ram = regexp.match(system_info).captured(0);
        detail += tr("System RAM:") + ram + "\n"; //RAM
        #else
        detail += tr("System ") + system_info + "\n"; //RAM
        #endif
        detail += tr("Current GPU: ") + QString(mViaPoint.glDeviceInfo().renderer.c_str()) + "\n";
        QString vramString;
        int vram = mViaPoint.getVRAM();
        if (vram == -1) {
            vramString = "Unknown VRAM";
        }
        #ifdef Q_OS_APPLE
        if (vram == -2) {
            QRegularExpression captureNumber = QRegularExpression("\\d+");
            vramString = "Roughly "
                         + QString::number(captureNumber.match(ram).captured(0).toInt() * 75 / 100) + ".5 GB";
        }
        #endif
        else {
            int digits = 1 + std::log10(vram);
            QString format = "MB";
            if (digits >= 4) {
                format = "GB";
            }
            #ifdef Q_OS_APPLE
            format = "GB";
            #endif
            int threes = digits / 3;
            bool numNotOdd = digits % 2;
            for (; threes > 0; --threes) {
                int padding = numNotOdd ? 0 : 1;
                if (digits > 3 && threes * 3 + 1 == digits && padding == 1) {
                    vramString = QString::number(vram).insert(digits - (digits - 1), ',');
                } else {
                    vramString = QString::number(vram).insert(threes * 3 + padding, ',');
                }
            }
            vramString.append(format);
        }
        detail += tr("GPU Vendor: ") + QString(mViaPoint.glDeviceInfo().vender.c_str()) + "\n";
        detail += tr("Max VRAM: ") + vramString + "\n";
        detail += tr("OpenGL version: ") + QString(mViaPoint.glDeviceInfo().version.c_str()) + "\n";
        detail += tr("Qt Version: ") + qtVersion + "\n";
        detail += tr("System locale: ") + QLocale::system().name() + "\n";

        // Unicode Check
        auto nonAscii = QRegularExpression("[^\\x00-\\x7F]+");
        QString hasUnicode = QApplication::applicationFilePath().contains(nonAscii) ? "True" : "False";
        detail += tr("Location Has Unicode: ") + hasUnicode + "\n";
        // Write Check: the app's writable storage is the cache (and QSettings);
        // probe the cache dir rather than the old co-located data dir.
        QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
        QFile file(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/.writeTest");
        bool isWritable = true;
        if (!file.open(QIODevice::WriteOnly)) {
            isWritable = false;
        } else {
            file.close();
        }
        const QString isFolderWritable = isWritable ? "True" : "False";
        detail += tr("Location Is Writable: ") + isFolderWritable + "\n";
        // THE MEDIA BACKEND, AS LINKED. There is no ffmpeg process to check for:
        // media/ links libav* directly, so what a bug report needs is the build of
        // those libraries, not the reachability of a binary nothing invokes.
        detail += tr("libav licence: ") + media::ffmpeg::license() + "\n";
        detail += tr("libav configuration: ") + media::ffmpeg::configuration() + "\n";
        msgBox.setText(tr("System specs successfully copied to the clipboard"));
        QClipboard* clipboard = QGuiApplication::clipboard();
        clipboard->setText(detail);
        msgBox.setDetailedText(detail);
        msgBox.setWindowTitle(tr("System telemetry"));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        msgBox.exec();
    });

    // ---- Donate -------------------------------------------------------------
    // A top-level BUTTON in the bar (see HeaderBar.qml), not a menu: it has no
    // submenu and one command. The registry makes it the same kind of command as
    // every row; only the scene's placement differs.
    QSettings settings;
    bool donationAllowed = !settings.value("generalsettings/ui/donationAllowed").isValid()
                           || settings.value("generalsettings/ui/donationAllowed").toBool();
    if (donationAllowed) {
        addCommand("donate", tr("Donate"),
                   []() { QDesktopServices::openUrl(QUrl("https://ko-fi.com/yukusai")); });
    }

    setProject(nullptr);
}

//-------------------------------------------------------------------------------------------------
MenuCommand* MainMenuBar::addCommand(const QString& aId,
                                     const QString& aText,
                                     std::function<void()> aCallback,
                                     bool aCheckable) {
    Q_ASSERT_X(!mCommands.contains(aId), "MainMenuBar::addCommand", "duplicate command id");
    auto* command = new MenuCommand(aText, std::move(aCallback), aCheckable, this);
    mCommands.insert(aId, command);
    return command;
}

QObject* MainMenuBar::command(const QString& aId) const {
    auto it = mCommands.constFind(aId);
    Q_ASSERT_X(it != mCommands.constEnd(), "MainMenuBar::command", "unknown command id");
    return it == mCommands.constEnd() ? nullptr : it.value();
}

void MainMenuBar::setExportAgainEnabled(bool aOn) {
    if (mExportAgain) mExportAgain->setEnabled(aOn);
}

void MainMenuBar::setProject(core::Project* aProject) {
    for (MenuCommand* action : mProjectActions)
        action->setEnabled(aProject != nullptr);
}

void MainMenuBar::openRecentPath(const QString& aPath) {
    if (!aPath.isEmpty())
        mMainWindow->onOpenRecentTriggered(aPath);
}

void MainMenuBar::onProjectSettingsTriggered() {
    core::Project* project = mViaPoint.project();
    if (!project)
        return;

    auto curSize = project->attribute().imageSize();
    auto curRange = project->attribute().frameRange();
    auto curLoop = project->attribute().loop();
    auto curFPS = project->attribute().fps();
    auto curHasViewportBackground = project->attribute().hasViewportBackground();
    auto curViewportBackground = project->attribute().viewportBackground();

    // create dialog
    QScopedPointer<ProjectSettingDialog> dialog(new ProjectSettingDialog(mViaPoint, *project, mMainWindow));

    // execute dialog
    dialog->exec();
    if (dialog->result() != QDialog::Accepted)
        return;

    // apply each changed attribute as its own undoable command
    {
        // canvas size
        const QSize newSize = dialog->canvasSize();
        XC_ASSERT(!newSize.isEmpty());
        if (curSize != newSize) {
            cmnd::ScopedMacro macro(project->commandStack(), CmndName::tr("Change the canvas size"));

            core::Project* projectPtr = project;
            auto command = new cmnd::Delegatable(
                [=]() {
                    projectPtr->attribute().setImageSize(newSize);
                    this->onProjectAttributeUpdated();
                    this->onVisualUpdated();
                },
                [=]() {
                    projectPtr->attribute().setImageSize(curSize);
                    this->onProjectAttributeUpdated();
                    this->onVisualUpdated();
                });
            project->commandStack().push(command);
        }

        // frame range
        const util::Range newRange = dialog->frameRange();
        if (curRange != newRange) {
            cmnd::ScopedMacro macro(project->commandStack(), CmndName::tr("Change the frame range"));

            core::Project* projectPtr = project;
            auto command = new cmnd::Delegatable(
                [=]() {
                    projectPtr->attribute().setFrameRange(newRange);
                    this->onProjectAttributeUpdated();
                    this->onVisualUpdated();
                },
                [=]() {
                    projectPtr->attribute().setFrameRange(curRange);
                    this->onProjectAttributeUpdated();
                    this->onVisualUpdated();
                });
            project->commandStack().push(command);
        }

        // loop animation
        const bool newLoop = dialog->loop();
        if (curLoop != newLoop) {
            cmnd::ScopedMacro macro(project->commandStack(), CmndName::tr("Change loop settings"));

            core::Project* projectPtr = project;
            auto command = new cmnd::Delegatable(
                [=]() {
                    projectPtr->attribute().setLoop(newLoop);
                    this->onProjectAttributeUpdated();
                    this->onVisualUpdated();
                },
                [=]() {
                    projectPtr->attribute().setLoop(curLoop);
                    this->onProjectAttributeUpdated();
                    this->onVisualUpdated();
                });
            project->commandStack().push(command);
        }

        // frames per second
        const int newFPS = dialog->fps();
        XC_ASSERT(newFPS > 0);
        if (curFPS != newFPS) {
            cmnd::ScopedMacro macro(project->commandStack(), CmndName::tr("Change the FPS"));

            core::Project* projectPtr = project;
            auto command = new cmnd::Delegatable(
                [=]() {
                    projectPtr->attribute().setFps(newFPS);
                    this->onProjectAttributeUpdated();
                    this->onVisualUpdated();
                },
                [=]() {
                    projectPtr->attribute().setFps(curFPS);
                    this->onProjectAttributeUpdated();
                    this->onVisualUpdated();
                });
            project->commandStack().push(command);
        }

        // viewport background: its own command so one undo takes the colour back,
        // separate from the geometry it sits behind. A no-op when the dialog left the
        // override exactly as it found it (same presence AND, when present, the same
        // colour).
        const bool newHasViewportBackground = !dialog->viewportBackgroundIsThemeDefault();
        const QColor newViewportBackground = dialog->viewportBackground();
        const bool viewportBackgroundChanged = (curHasViewportBackground != newHasViewportBackground)
                                               || (newHasViewportBackground
                                                   && curViewportBackground != newViewportBackground);
        if (viewportBackgroundChanged) {
            cmnd::ScopedMacro macro(project->commandStack(),
                                    CmndName::tr("Change the viewport background"));

            core::Project* projectPtr = project;
            auto command = new cmnd::Delegatable(
                [=]() {
                    if (newHasViewportBackground)
                        projectPtr->attribute().setViewportBackground(newViewportBackground);
                    else
                        projectPtr->attribute().clearViewportBackground();
                    this->onProjectAttributeUpdated();
                    this->onVisualUpdated();
                },
                [=]() {
                    if (curHasViewportBackground)
                        projectPtr->attribute().setViewportBackground(curViewportBackground);
                    else
                        projectPtr->attribute().clearViewportBackground();
                    this->onProjectAttributeUpdated();
                    this->onVisualUpdated();
                });
            project->commandStack().push(command);
        }
    }
}

} // namespace gui
