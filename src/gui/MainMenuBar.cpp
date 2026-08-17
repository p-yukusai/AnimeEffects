#include <QFile>
#include <QDomDocument>
#include <qstandardpaths.h>
#include "qprocess.h"
#include "util/TextUtil.h"
#include "cmnd/BasicCommands.h"
#include "cmnd/ScopedMacro.h"
#include "core/FormatVersion.h"
#include "core/ObjectNodeUtil.h"
#include "ctrl/CmndName.h"
#include "gui/MainMenuBar.h"
#include "gui/MainWindow.h"
#include "gui/ResourceDialog.h"
#include "gui/EasyDialog.h"
#include "gui/KeyBindingDialog.h"
#include "gui/GeneralSettingDialog.h"
#include "gui/MouseSettingDialog.h"
#include "util/NetworkUtil.h"
#include <algorithm>
#ifdef Q_OS_WINDOWS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif
#define VENDOR_ID_LEN 13

namespace gui {
//-------------------------------------------------------------------------------------------------
QDomDocument getVideoExportDocument() {
    QFile file("./data/encode/VideoEncode.txt");

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << file.errorString();
        return {};
    }

    QDomDocument prop;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    auto result = prop.setContent(&file, QDomDocument::ParseOption::Default);
    if (!result) {
        qDebug() << "invalid xml file. " << file.fileName() << result.errorMessage << ", line = " << result.errorLine
                 << ", column = " << result.errorColumn;
        return {};
    }
#else
    QString errorMessage;
    int errorLine = 0;
    int errorColumn = 0;
    if (!prop.setContent(&file, false, &errorMessage, &errorLine, &errorColumn)) {
        qDebug() << "invalid xml file. " << file.fileName() << errorMessage << ", line = " << errorLine
                 << ", column = " << errorColumn;
        return {};
    }
#endif
    file.close();

    return prop;
}
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

    // https://learn.microsoft.com/en-us/cpp/intrinsics/cpuid-cpuidex?view=msvc-170#example
    ((unsigned int *) vendor)[0] = v[EBX];
    ((unsigned int *) vendor)[1] = v[EDX];
    ((unsigned int *) vendor)[2] = v[ECX];
    vendor[VENDOR_ID_LEN - 1] = '\0';
}

#endif
//-------------------------------------------------------------------------------------------------
MainMenuBar::MainMenuBar(MainWindow& aMainWindow, ViaPoint& aViaPoint, GUIResources& aGUIResources, QWidget* aParent):
    QMenuBar(aParent),
    mProcess(),
    mViaPoint(aViaPoint),
    mProject(),
    mProjectActions(),
    mVideoFormats(),
    mGUIResources(aGUIResources) {
    // load the list of video formats from a setting file.
    loadVideoFormats();

    MainWindow* mainWindow = &aMainWindow;

    auto fileMenu = new QMenu(tr("File"), this);
    {
        auto newProject = new QAction(tr("New project"), this);
        auto openProject = new QAction(tr("Open project"), this);
        auto openRecent = new QMenu(tr("Open recent"), this);
        {
            // Get settings
            QSettings settings;
            settings.sync();
            recentfiles = settings.value("projectloader/recents").toStringList();

            // Path placeholders
            if (recentfiles.length() != 8) {
                while (recentfiles.length() != 8) {
                    recentfiles.append("Path placeholder");
                }
            }
            QString firstPath = recentfiles[0];
            QString secondPath = recentfiles[1];
            QString thirdPath = recentfiles[2];
            QString fourthPath = recentfiles[3];
            QString fifthPath = recentfiles[4];
            QString sixthPath = recentfiles[5];
            QString seventhPath = recentfiles[6];
            QString eigthPath = recentfiles[7];
            QStringList list;
            list << firstPath << secondPath << thirdPath << fourthPath << fifthPath << sixthPath << seventhPath
                 << eigthPath;

            // Path actions
            auto placeholderAction = new QAction(tr("No other projects"), this);
            auto firstPathAction = new QAction(firstPath);
            auto secondPathAction = new QAction(secondPath);
            auto thirdPathAction = new QAction(thirdPath);
            auto fourthPathAction = new QAction(fourthPath);
            auto fifthPathAction = new QAction(fifthPath);
            auto sixthPathAction = new QAction(sixthPath);
            auto seventhPathAction = new QAction(seventhPath);
            auto eigthPathAction = new QAction(eigthPath);
            // Path addition
            // Better than before, but still not very DRY code.
            for (int x = 0; x <= 7; x += 1) {
                if (list[x] == QString("Path placeholder")) {
                    openRecent->addAction(placeholderAction);
                } else {
                    switch (x) {
                    case 0:
                        openRecent->addAction(firstPathAction);
                        break;
                    case 1:
                        openRecent->addAction(secondPathAction);
                        break;
                    case 2:
                        openRecent->addAction(thirdPathAction);
                        break;
                    case 3:
                        openRecent->addAction(fourthPathAction);
                        break;
                    case 4:
                        openRecent->addAction(fifthPathAction);
                        break;
                    case 5:
                        openRecent->addAction(sixthPathAction);
                        break;
                    case 6:
                        openRecent->addAction(seventhPathAction);
                        break;
                    case 7:
                        openRecent->addAction(eigthPathAction);
                        break;
                    default:
                        openRecent->addAction(placeholderAction);
                    }
                }
            }

            // Connections
            connect(placeholderAction, &QAction::triggered, [=]() { qDebug() << "You've earned yourself a cookie!"; });
            connect(firstPathAction, &QAction::triggered, [=]() { mainWindow->onOpenRecentTriggered(firstPath); });
            connect(secondPathAction, &QAction::triggered, [=]() { mainWindow->onOpenRecentTriggered(secondPath); });
            connect(thirdPathAction, &QAction::triggered, [=]() { mainWindow->onOpenRecentTriggered(thirdPath); });
            connect(fourthPathAction, &QAction::triggered, [=]() { mainWindow->onOpenRecentTriggered(fourthPath); });
            connect(fifthPathAction, &QAction::triggered, [=]() { mainWindow->onOpenRecentTriggered(fifthPath); });
            connect(sixthPathAction, &QAction::triggered, [=]() { mainWindow->onOpenRecentTriggered(sixthPath); });
            connect(seventhPathAction, &QAction::triggered, [=]() { mainWindow->onOpenRecentTriggered(seventhPath); });
            connect(eigthPathAction, &QAction::triggered, [=]() { mainWindow->onOpenRecentTriggered(eigthPath); });
        }
        auto saveProject = new QAction(tr("Save project"), this);
        auto saveProjectAs = new QAction(tr("Save project as"), this);
        auto closeProject = new QAction(tr("Close project"), this);
        auto exportWindow = new QAction(tr("Export project as"), this);
        auto exportAs = new QMenu(tr("Legacy exporter (deprecated)"), this);
        {
            ctrl::VideoFormat gifFormat;
            gifFormat.name = "gif";
            gifFormat.label = "GIF";
            gifFormat.icodec = "ppm";

            auto jpgs = new QAction(tr("JPEG Sequence"), this);
            auto pngs = new QAction(tr("PNG Sequence"), this);
            auto gif = new QAction(tr("GIF Animation"), this);
            connect(jpgs, &QAction::triggered, [=]() { mainWindow->onExportImageSeqTriggered("jpg"); });
            connect(pngs, &QAction::triggered, [=]() { mainWindow->onExportImageSeqTriggered("png"); });
            connect(gif, &QAction::triggered, [=]() { mainWindow->onExportVideoTriggered(gifFormat); });
            exportAs->addAction(jpgs);
            exportAs->addAction(pngs);
            exportAs->addAction(gif);

            for (const auto& format : mVideoFormats) {
                auto video = new QAction(format.label + " " + tr("Video"), this);
                connect(video, &QAction::triggered, [=]() { mainWindow->onExportVideoTriggered(format); });
                exportAs->addAction(video);
            }
        }
        auto quickExport = new QMenu(tr("Quickly export as"), this);
        {
            auto pngs = new QAction(tr("PNG Sequence"), this);
            auto gif_transparent = new QAction(tr("GIF Animation (Transparent)"), this);
            auto gif_opaque = new QAction(tr("GIF Animation (Opaque)"), this);
            auto webm_transparent = new QAction(tr("WebM Animation (Transparent)"), this);
            auto webm_opaque = new QAction(tr("WebM Animation (Opaque)"), this);
            auto mp4 = new QAction(tr("MP4 Video"), this);
            auto avi = new QAction(tr("AVI Video"), this);

            QVector<QAction*> actions = {pngs, gif_transparent, gif_opaque, webm_transparent, webm_opaque, mp4, avi};
            QStringList formats = {"png", "gif_t", "gif_o", "webm_t", "webm_o", "mp4", "avi"};
            assert(actions.size() == formats.size());
            int i = 0;
            for (auto action: actions) {
                connect(action, &QAction::triggered, [=]() { mainWindow->onQuickExportTriggered(formats.at(i)); });
                quickExport->addAction(action);
                i++;
            }
        }

        mProjectActions.push_back(saveProject);
        mProjectActions.push_back(saveProjectAs);
        mProjectActions.push_back(closeProject);
        mProjectActions.push_back(exportWindow);
        mProjectActions.push_back(exportAs->menuAction());
        mProjectActions.push_back(quickExport->menuAction());

        connect(newProject, &QAction::triggered, mainWindow, &MainWindow::onNewProjectTriggered);
        connect(openProject, &QAction::triggered, mainWindow, &MainWindow::onOpenProjectTriggered);
        connect(saveProject, &QAction::triggered, mainWindow, &MainWindow::onSaveProjectTriggered);
        connect(saveProjectAs, &QAction::triggered, mainWindow, &MainWindow::onSaveProjectAsTriggered);
        connect(closeProject, &QAction::triggered, mainWindow, &MainWindow::onCloseProjectTriggered);
        connect(exportWindow, &QAction::triggered, mainWindow, &MainWindow::onExportTriggered);

        fileMenu->addAction(newProject);
        fileMenu->addAction(openProject);
        fileMenu->addAction(openRecent->menuAction());
        fileMenu->addSeparator();
        fileMenu->addAction(saveProject);
        fileMenu->addAction(saveProjectAs);
        fileMenu->addAction(exportWindow);
        fileMenu->addAction(quickExport->menuAction());
        fileMenu->addSeparator();
        fileMenu->addAction(exportAs->menuAction());
        fileMenu->addSeparator();
        fileMenu->addAction(closeProject);
    }

    auto editMenu = new QMenu(tr("Edit"), this);
    {
        auto undo = new QAction(tr("Undo"), this);
        auto redo = new QAction(tr("Redo"), this);
        auto projectSettings = new QAction(tr("Project settings"), this);
        auto assets = new QAction(tr("Assets"), this);

        mProjectActions.push_back(undo);
        mProjectActions.push_back(redo);
        mProjectActions.push_back(projectSettings);
        mProjectActions.push_back(assets);

        connect(undo, &QAction::triggered, mainWindow, &MainWindow::onUndoTriggered);
        connect(redo, &QAction::triggered, mainWindow, &MainWindow::onRedoTriggered);
        connect(projectSettings, &QAction::triggered, this, &MainMenuBar::onProjectSettingsTriggered);
        connect(assets, &QAction::triggered, [&] {
            if (aViaPoint.resourceDialog()) {
                aViaPoint.resourceDialog()->setVisible(!aViaPoint.resourceDialog()->isVisible());
            }
        });

        editMenu->addAction(undo);
        editMenu->addAction(redo);
        editMenu->addSeparator();
        editMenu->addAction(projectSettings);
        editMenu->addAction(assets);
    }

    auto preferenceMenu = new QMenu(tr("Preferences"), this);
    {
        auto general = new QAction(tr("General settings"), this);
        auto mouse = new QAction(tr("Mouse settings"), this);
        auto keyBind = new QAction(tr("Keybindings"), this);

        connect(general, &QAction::triggered, [&](bool) {
            auto generalSettingsDialog = new GeneralSettingDialog(mGUIResources, this);
            QScopedPointer<GeneralSettingDialog> dialog(generalSettingsDialog);
            if (dialog->exec() == QDialog::DialogCode::Accepted) {
                if (generalSettingsDialog->timeFormatHasChanged())
                    this->onTimeFormatChanged();
                if (generalSettingsDialog->themeHasChanged())
                    this->mGUIResources.setTheme(generalSettingsDialog->theme());
                if (generalSettingsDialog->accentHasChanged())
                    this->mGUIResources.setAccent(generalSettingsDialog->accent());
            }
        });

        connect(mouse, &QAction::triggered, [&](bool) {
            QScopedPointer<MouseSettingDialog> dialog(new MouseSettingDialog(aViaPoint, this));
            dialog->exec();
        });

        connect(keyBind, &QAction::triggered, [&](bool) {
            XC_PTR_ASSERT(aViaPoint.keyCommandMap());
            QScopedPointer<KeyBindingDialog> dialog(new KeyBindingDialog(*aViaPoint.keyCommandMap(), this));
            dialog->exec();
        });

        preferenceMenu->addAction(general);
        preferenceMenu->addAction(mouse);
        preferenceMenu->addAction(keyBind);
    }

    auto helpMenu = new QMenu(tr("Help"), this);
        {
            auto aboutMe = new QAction(tr("About AnimeEffects"), this);
        connect(aboutMe, &QAction::triggered, [=]() {
            QMessageBox msgBox;
            msgBox.setWindowIcon(QIcon("../src/AnimeEffects.ico"));
            auto versionString = QString::number(AE_MAJOR_VERSION) + "." + QString::number(AE_MINOR_VERSION) + "." +
                QString::number(AE_MICRO_VERSION);
            auto platform = QSysInfo::productType();
            platform[0] = platform[0].toUpper();
            QString msgStr =
                tr("### AnimeEffects for ") + platform + tr(" version ") + versionString + "<br />" +
                tr("An easy to use 2D animation software maintained by the [AnimeEffectsDevs](https://github.com/AnimeEffectsDevs)."
                   "<br />Licensed under the GPL v3.0 and powered by various open source libraries.");
            msgStr.append(
                tr("<br /><br />Contributors:<br />"
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
                "If we forgot to include you please let us know or open a PR!"
                ));
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

        auto checkForUpdates = new QAction(tr("Check for updates"), this);
        connect(checkForUpdates, &QAction::triggered, [=]() {
            const util::NetworkUtil networking;
            const QString url("https://api.github.com/repos/AnimeEffectsDevs/AnimeEffects/tags");
            util::NetworkUtil::checkForUpdate(url, networking, this);
        });
        auto diagnostics = new QAction(tr("System telemetry"), this);
        connect(diagnostics, &QAction::triggered, [=] {
            #ifdef Q_OS_WIN
            MEMORYSTATUSEX memory_status;
            ZeroMemory(&memory_status, sizeof(MEMORYSTATUSEX));
            memory_status.dwLength = sizeof(MEMORYSTATUSEX);
            if (GlobalMemoryStatusEx(&memory_status)) {
              system_info = QString(" RAM: %1 MB").arg(memory_status.ullTotalPhys / (1024 * 1024));
            }
            else {
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
            auto versionString = QString::number(AE_MAJOR_VERSION) + "." + QString::number(AE_MINOR_VERSION) + "." +
                QString::number(AE_MICRO_VERSION);
            auto formatVersionString = QString::number(AE_PROJECT_FORMAT_MAJOR_VERSION) + "." +
                QString::number(AE_PROJECT_FORMAT_MINOR_VERSION);
            auto platform = QSysInfo::productType();
            auto qtVersion = QString::number(QT_VERSION_MAJOR) + "." + QString::number(QT_VERSION_MINOR) + "." +
                QString::number(QT_VERSION_PATCH);
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
            if (cpuVendor == "GenuineIntel"){
                vendor = "Intel";
            }
            else if (cpuVendor == "AuthenticAMD"){
                vendor = "AMD";
            }
            else if (cpuVendor == "Unknown" || cpuVendor == "Apple") {
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
            detail += tr("Current GPU: ") + QString(this->mViaPoint.glDeviceInfo().renderer.c_str()) + "\n";
            QString vramString;
            int vram = this->mViaPoint.getVRAM();
            if (vram == -1) {
                vramString = "Unknown VRAM";
            }
            #ifdef Q_OS_APPLE
            if (vram == -2) {
                QRegularExpression captureNumber = QRegularExpression("\\d+");
                vramString = "Roughly " + QString::number(captureNumber.match(ram).captured(0).toInt() * 75 / 100) + ".5 GB";
            }
            #endif
            else {
                int digits = 1 + log10(vram);
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
            detail += tr("GPU Vendor: ") + QString(this->mViaPoint.glDeviceInfo().vender.c_str()) + "\n";
            detail += tr("Max VRAM: ") + vramString + "\n";
            detail += tr("OpenGL version: ") + QString(this->mViaPoint.glDeviceInfo().version.c_str()) + "\n";
            detail += tr("Qt Version: ") + qtVersion + "\n";
            detail += tr("System locale: ") + QLocale::system().name() + "\n";

            // Unicode Check
            auto nonAscii = QRegularExpression("[^\\x00-\\x7F]+");
            QString hasUnicode = QApplication::applicationFilePath().contains(nonAscii) ? "True" : "False";
            detail += tr("Location Has Unicode: ") + hasUnicode + "\n";
            // Write Check
            QFile file(QApplication::applicationDirPath() + R"(\data\shader\TestBlurVert.glsl)");
            bool isWritable = true;
            if (!file.open(QIODevice::WriteOnly)) {
                isWritable = false;
            } else {
                file.close();
            }
            const QString isFolderWritable = isWritable ? "True" : "False";
            detail += tr("Location Is Writable: ") + isFolderWritable + "\n";
            // FFmpeg Check
            QFileInfo ffmpeg_file;
            QString ffmpeg;
            if (util::NetworkUtil::os() == "win") {
                ffmpeg_file = QFileInfo("./tools/ffmpeg.exe");
            } else {
                ffmpeg_file = QFileInfo("./tools/ffmpeg");
            }
            if (!ffmpeg_file.exists() || !ffmpeg_file.isExecutable()) {
                ffmpeg = "ffmpeg";
            } else {
                ffmpeg = ffmpeg_file.absoluteFilePath();
            }
            bool fExists = util::NetworkUtil::libExists(ffmpeg, "-version");
            QString ffmpegType = ffmpeg == "ffmpeg" ? "Process" : "File";
            QString ffmpegReach = fExists ? "True" : "False";
            detail += tr("FFmpeg Reach Type: ") + ffmpegType + "\n";
            detail += tr(QString("FFmpeg Reachable: " + ffmpegReach).toStdString().c_str());
            msgBox.setText(tr("System specs successfully copied to the clipboard"));
            QClipboard *clipboard = QGuiApplication::clipboard();
            clipboard->setText(detail);
            msgBox.setDetailedText(detail);
            msgBox.setWindowTitle(tr("System telemetry"));
            msgBox.setStandardButtons(QMessageBox::Ok);
            msgBox.setDefaultButton(QMessageBox::Ok);
            msgBox.exec();
        });

        helpMenu->addAction(aboutMe);
        helpMenu->addAction(checkForUpdates);
        helpMenu->addAction(diagnostics);
    }

    auto donateMenu = new QAction(tr("Donate"), this);
    {
        connect(donateMenu, &QAction::triggered, [=](){
            QDesktopServices::openUrl(QUrl("https://ko-fi.com/yukusai"));
        });
    }

    this->addAction(fileMenu->menuAction());
    this->addAction(editMenu->menuAction());
    this->addAction(preferenceMenu->menuAction());
    this->addAction(helpMenu->menuAction());
    QSettings settings;
    bool donationAllowed = !settings.value("generalsettings/ui/donationAllowed").isValid()
                            || settings.value("generalsettings/ui/donationAllowed").toBool();
    if(donationAllowed) { this->addAction(donateMenu); }

    // reset status
    setProject(nullptr);
}

void MainMenuBar::setProject(core::Project* aProject) {
    mProject = aProject;

    if (mProject) {
        for (auto action : mProjectActions) {
            action->setEnabled(true);
        }
    } else {
        for (auto action : mProjectActions) {
            action->setEnabled(false);
        }
    }
}

void MainMenuBar::loadVideoFormats() {
    using util::TextUtil;

    QDomDocument doc = getVideoExportDocument();
    QDomElement domRoot = doc.firstChildElement("video_encode");

    // for each format
    QDomElement domFormat = domRoot.firstChildElement("format");
    while (!domFormat.isNull()) {
        ctrl::VideoFormat format;
        // neccessary attribute
        format.name = domFormat.attribute("name");
        if (format.name.isEmpty())
            continue;
        // optional attributes
        format.label = domFormat.attribute("label");
        format.icodec = domFormat.attribute("icodec");
        format.command = domFormat.attribute("command");
        if (format.label.isEmpty())
            format.label = format.name;
        if (format.icodec.isEmpty())
            format.icodec = "png";
        // add one format
        mVideoFormats.push_back(format);

        // for each codec
        QDomElement domCodec = domFormat.firstChildElement("codec");
        while (!domCodec.isNull()) {
            ctrl::VideoCodec codec;
            // neccessary attribute
            codec.name = domCodec.attribute("name");
            if (codec.name.isEmpty())
                continue;
            // optional attributes
            codec.label = domCodec.attribute("label");
            codec.icodec = domCodec.attribute("icodec");
            codec.command = domCodec.attribute("command");
            if (codec.label.isEmpty())
                codec.label = codec.name;
            if (codec.icodec.isEmpty())
                codec.icodec = format.icodec;
            if (codec.command.isEmpty())
                codec.command = format.command;
            {
                auto hints = TextUtil::splitAndTrim(domCodec.attribute("hint"), ',');
                for (const auto& hint : hints) {
                    if (hint == "lossless")
                        codec.lossless = true;
                    else if (hint == "transparent")
                        codec.transparent = true;
                    else if (hint == "colorspace")
                        codec.colorspace = true;
                    else if (hint == "gpuenc")
                        codec.gpuenc = true;
                }
            }
            codec.pixfmts = TextUtil::splitAndTrim(domCodec.attribute("pixfmt"), ',');

            // add one codec
            mVideoFormats.back().codecs.push_back(codec);

            // to next sibling
            domCodec = domCodec.nextSiblingElement("codec");
        }
        // to next sibling
        domFormat = domFormat.nextSiblingElement("format");
    }
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ProjectSettingDialog::ProjectSettingDialog(ViaPoint& aViaPoint, core::Project& aProject, QWidget* aParent):
    EasyDialog(tr("Project settings"), aParent),
    mViaPoint(aViaPoint),
    mProject(aProject),
    mWidthBox(),
    mHeightBox(),
    mMaxFrameBox(),
    mLoopBox(),
    mFPSBox() {
    // create inner widgets
    auto curSize = mProject.attribute().imageSize();
    auto curMaxFrame = mProject.attribute().maxFrame();
    auto curLoop = mProject.attribute().loop();
    auto curFPS = mProject.attribute().fps();

    {
        auto devInfo = mViaPoint.glDeviceInfo();
        const int maxBufferSize = std::min(devInfo.maxTextureSize, devInfo.maxRenderBufferSize);
        XC_ASSERT(maxBufferSize > 0);

        auto form = new QFormLayout();
        form->setFormAlignment(Qt::AlignHCenter | Qt::AlignTop);
        form->setLabelAlignment(Qt::AlignRight);

        // canvas size
        auto sizeLayout = new QHBoxLayout();
        {
            mWidthBox = new QSpinBox();
            mWidthBox->setRange(1, maxBufferSize);
            mWidthBox->setValue(curSize.width());
            sizeLayout->addWidget(mWidthBox);

            mHeightBox = new QSpinBox();
            mHeightBox->setRange(1, maxBufferSize);
            mHeightBox->setValue(curSize.height());
            sizeLayout->addWidget(mHeightBox);
        }
        form->addRow(tr("Canvas size"), sizeLayout);

        // maximum frame count
        mMaxFrameBox = new QSpinBox();
        mMaxFrameBox->setRange(1, 0x7FFFFFFF);
        mMaxFrameBox->setValue(curMaxFrame);
        form->addRow(tr("Max frame count"), mMaxFrameBox);

        // loop animation
        mLoopBox = new QCheckBox();
        mLoopBox->setChecked(curLoop);
        form->addRow(tr("Loop animation"), mLoopBox);

        // frames per second
        mFPSBox = new QSpinBox();
        mFPSBox->setRange(1, 0x7FFFFFFF);
        mFPSBox->setValue(curFPS);
        form->addRow(tr("Frames per second"), mFPSBox);

        this->setMainLayout(form);
    }

    this->setOkCancel([=](int aIndex) -> bool {
        if (aIndex == 0) {
            return this->confirmMaxFrameUpdating(this->mMaxFrameBox->value());
        }
        return true;
    });
    this->fixSize();
}

bool ProjectSettingDialog::confirmMaxFrameUpdating(int aNewMaxFrame) const {
    XC_ASSERT(aNewMaxFrame > 0);

    auto curMaxFrame = mProject.attribute().maxFrame();
    if (curMaxFrame <= aNewMaxFrame)
        return true;

    if (!core::ObjectNodeUtil::thereAreSomeKeysExceedingFrame(mProject.objectTree().topNode(), aNewMaxFrame)) {
        return true;
    }

    auto message1 = tr("Frame value cannot be set.");
    auto message2 = tr("One or more keys exceed the specified frame value.");
    QMessageBox::warning(nullptr, tr("Operation Error"), message1 + "\n" + message2);
    return false;
}

void MainMenuBar::onProjectSettingsTriggered() {
    if (!mProject)
        return;

    auto curSize = mProject->attribute().imageSize();
    auto curMaxFrame = mProject->attribute().maxFrame();
    auto curLoop = mProject->attribute().loop();
    auto curFPS = mProject->attribute().fps();

    // create dialog
    QScopedPointer<ProjectSettingDialog> dialog(new ProjectSettingDialog(mViaPoint, *mProject, this));

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
            cmnd::ScopedMacro macro(mProject->commandStack(), CmndName::tr("Change the canvas size"));

            core::Project* projectPtr = mProject;
            auto command = new cmnd::Delegatable(
                [=]() {
                    projectPtr->attribute().setImageSize(newSize);
                    auto event = core::ProjectEvent::imageSizeChangeEvent(*projectPtr);
                    projectPtr->onProjectAttributeModified(event, false);
                    this->onProjectAttributeUpdated();
                    this->onVisualUpdated();
                },
                [=]() {
                    projectPtr->attribute().setImageSize(curSize);
                    auto event = core::ProjectEvent::imageSizeChangeEvent(*projectPtr);
                    projectPtr->onProjectAttributeModified(event, true);
                    this->onProjectAttributeUpdated();
                    this->onVisualUpdated();
                }
            );
            mProject->commandStack().push(command);
        }

        // max frame count
        const int newMaxFrame = dialog->maxFrame();
        XC_ASSERT(newMaxFrame > 0);
        if (curMaxFrame != newMaxFrame) {
            cmnd::ScopedMacro macro(mProject->commandStack(), CmndName::tr("Change the max frame"));

            core::Project* projectPtr = mProject;
            auto command = new cmnd::Delegatable(
                [=]() {
                    projectPtr->attribute().setMaxFrame(newMaxFrame);
                    auto event = core::ProjectEvent::maxFrameChangeEvent(*projectPtr);
                    projectPtr->onProjectAttributeModified(event, false);
                    this->onProjectAttributeUpdated();
                    this->onVisualUpdated();
                },
                [=]() {
                    projectPtr->attribute().setMaxFrame(curMaxFrame);
                    auto event = core::ProjectEvent::maxFrameChangeEvent(*projectPtr);
                    projectPtr->onProjectAttributeModified(event, true);
                    this->onProjectAttributeUpdated();
                    this->onVisualUpdated();
                }
            );
            mProject->commandStack().push(command);
        }

        // loop animation
        const bool newLoop = dialog->loop();
        if (curLoop != newLoop) {
            cmnd::ScopedMacro macro(mProject->commandStack(), CmndName::tr("Change loop settings"));

            core::Project* projectPtr = mProject;
            auto command = new cmnd::Delegatable(
                [=]() {
                    projectPtr->attribute().setLoop(newLoop);
                    auto event = core::ProjectEvent::loopChangeEvent(*projectPtr);
                    projectPtr->onProjectAttributeModified(event, false);
                    this->onProjectAttributeUpdated();
                    this->onVisualUpdated();
                },
                [=]() {
                    projectPtr->attribute().setLoop(curLoop);
                    auto event = core::ProjectEvent::loopChangeEvent(*projectPtr);
                    projectPtr->onProjectAttributeModified(event, true);
                    this->onProjectAttributeUpdated();
                    this->onVisualUpdated();
                }
            );
            mProject->commandStack().push(command);
        }

        // frames per second
        const int newFPS = dialog->fps();
        XC_ASSERT(newFPS > 0);
        if (curFPS != newFPS) {
            cmnd::ScopedMacro macro(mProject->commandStack(), CmndName::tr("Change the FPS"));

            core::Project* projectPtr = mProject;
            auto command = new cmnd::Delegatable(
                [=]() {
                    projectPtr->attribute().setFps(newFPS);
                    auto event = core::ProjectEvent::fpsChangeEvent(*projectPtr);
                    projectPtr->onProjectAttributeModified(event, false);
                    this->onProjectAttributeUpdated();
                    this->onVisualUpdated();
                },
                [=]() {
                    projectPtr->attribute().setFps(curFPS);
                    auto event = core::ProjectEvent::fpsChangeEvent(*projectPtr);
                    projectPtr->onProjectAttributeModified(event, true);
                    this->onProjectAttributeUpdated();
                    this->onVisualUpdated();
                }
            );
            mProject->commandStack().push(command);
        }
    }
}

} // namespace gui
