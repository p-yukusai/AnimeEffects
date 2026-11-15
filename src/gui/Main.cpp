#include "gui/MSVCMemoryLeakDebugger.h" // first of all
#include <QDir>
#include <QFontDatabase>
#include <QMessageBox>
#include <QStandardPaths>
#include <QScopedPointer>
#include "XC.h"
#include "gl/Global.h"
#include "ctrl/System.h"
#include "gui/MainWindow.h"
#include "gui/GUIResources.h"
#include "gui/MSVCBackTracer.h"
#include "gui/LocaleDecider.h"
#include "util/NetworkUtil.h"
#include <QLoggingCategory>
#include <QtConcurrent>
#include <QApplication>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStyleHints>
#include "gui/qml/QmlTheme.h"

// This MSVC debugger is not useful and only serves to pain me in the ways it unexpectedly fails,
// I'm sure it worked wonderfully before, but it sure doesn't anymore

#if defined(USE_MSVC_MEMORYLEAK_DEBUG)
extern MemoryRegister gMemoryRegister;
#endif // USE_MSVC_MEMORYLEAK_DEBUG

#if defined(USE_MSVC_BACKTRACE)
extern BackTracer gBackTracer;
#endif // USE_MSVC_BACKTRACE

class AEAssertHandler final: public XCAssertHandler {
public:
    void failure() const override {
#if defined(USE_MSVC_BACKTRACE)
        gBackTracer.dumpCurrent();
#endif // USE_MSVC_BACKTRACE
    }
};

#ifndef QT_DEBUG
    #define DISABLE_DEBUG_OUTPUT true
#endif

class AEErrorHandler: public XCErrorHandler {
public:
    void critical(const QString& aText, const QString& aInfo, const QString& aDetail) const override {
        XC_REPORT() << aText << "\n" << aInfo << "\n" << aDetail;
        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setWindowTitle("Fatal Error");

        msgBox.setText(aText);
        msgBox.setInformativeText(aInfo);
        if (!aDetail.isEmpty()) {
            msgBox.setDetailedText(aDetail);
        }

        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        msgBox.exec();

        std::exit(EXIT_FAILURE);
    }
};

XCAssertHandler* gXCAssertHandler = nullptr;
XCErrorHandler* gXCErrorHandler = nullptr;
static AEAssertHandler AEAssertHandler;

#if defined(USE_MSVC_BACKTRACE)
    #define TRY_ACTION_WITH_EXCEPT(action) \
        __try { \
            action; \
        } __except (EXCEPTION_EXECUTE_HANDLER) { \
            qDebug("Exception occurred.(%x)", GetExceptionCode()); \
            gBackTracer.dumpCurrent(); \
            std::abort(); \
        }
#else
    #define TRY_ACTION_WITH_EXCEPT(action) action
#endif // USE_MSVC_BACKTRACE

int entryPoint(int argc, char* argv[]);

int main(const int argc, char* argv[]) {
    #ifdef Q_OS_DARWIN
    rlimit rl{};
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        if (((int)rl.rlim_max) > ((int)rl.rlim_cur)) {
            rl.rlim_cur = rl.rlim_max;
            if (setrlimit(RLIMIT_NOFILE, &rl) != 0) perror("setrlimit");
        }
    }
    else perror("getrlimit");
    #endif
    // Define XC Assert
    gXCAssertHandler = &AEAssertHandler;
    #if defined(USE_MSVC_MEMORYLEAK_DEBUG)
        _CrtSetAllocHook(myAllocHook);
    #endif // USE_MSVC_MEMORYLEAK_DEBUG

    int result = 0;
    // TRY_ACTION_WITH_EXCEPT(result = entryPoint(argc, argv));
    try { result = entryPoint(argc, argv); }
    catch(...){
        QMessageBox::warning(nullptr, "Error code: " + QString::number(result), "An unexpected error has occurred, please restart the application.");
        result = -1;
    }
    #if defined(USE_MSVC_MEMORYLEAK_DEBUG)
        gMemoryRegister.dumpAll();
        gMemoryRegister.final();
    #endif // USE_MSVC_MEMORYLEAK_DEBUG
    return result;
}

int entryPoint(int argc, char* argv[]) {
    int result = 0;

    // QT COMBINES MOUSE MOTION BY DEFAULT, and that is documented behaviour rather than a bug: on X11
    // and on Wayland the default of Qt::AA_CompressHighFrequencyEvents is TRUE, and QEvent::MouseMove
    // is "combined whenever they occur more frequently than the application handles them, so that they
    // don't accumulate and overwhelm the application later" (Qt 6, qt.html, the attribute's own entry).
    // A painting brush loses its path exactly there - measured: 40 pointer positions delivered as 6
    // samples while a stroke painted. This is the platform's own switch, and it must be set before the
    // application exists.
    // create qt application
    QApplication app(argc, argv);

    // AND IT IS SET AFTER THE PLATFORM PLUGIN TOO: the plugin (xcb/Wayland) sets this attribute during
    // the application's construction, so a value set BEFORE it is overwritten - measured:
    // `testAttribute` read 1 with the call placed before QApplication, 0 with it here. The compression
    // is consulted per event, so setting it now is what actually takes effect.
    QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, false);
    if (QCoreApplication::testAttribute(Qt::AA_CompressHighFrequencyEvents))
        qWarning("QEasing: AA_CompressHighFrequencyEvents did not take - a brush stroke will lose samples");
    #ifdef DISABLE_DEBUG_OUTPUT
    QLoggingCategory::defaultCategory()->setEnabled(QtInfoMsg, false);
    QLoggingCategory::defaultCategory()->setEnabled(QtDebugMsg, false);
    QLoggingCategory::defaultCategory()->setEnabled(QtWarningMsg, false);
    #else
    QLoggingCategory::defaultCategory()->setEnabled(QtInfoMsg, true);
    QLoggingCategory::defaultCategory()->setEnabled(QtDebugMsg, true);
    QLoggingCategory::defaultCategory()->setEnabled(QtWarningMsg, true);
    #endif
    XC_DEBUG_REPORT() << "exe path =" << app.applicationFilePath();

    // Which Qt Quick Controls style the migrated dialogs (gui/qml) are built on.
    // It MUST be chosen before the first QQuickWidget loads a scene, and it must
    // be a style the design is actually customised against: the default here is
    // Fusion (measured, Qt 6.11.2 on Linux), while Basic is the one the Qt docs
    // recommend basing a customised look on - minimal padding and implicit sizes,
    // identical on every platform. The dialogs replace each control's
    // background/contentItem with the app's own, so the style contributes
    // behaviour and defaults, not paint.
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    // The header owns the view settings and the layout toggles, and they only exist
    // inside the QML MenuBar. On macOS Qt would otherwise move the MenuBar to the
    // native screen-top bar, where the delegates (and therefore the controls beside
    // the menus) are not drawn; force the in-window bar so the header is the same
    // surface on every platform. This is the documented opt-out for a native
    // MenuBar (MenuBar QML docs, "Native menu bars").
    QGuiApplication::setAttribute(Qt::AA_DontUseNativeMenuBar, true);
    // The header's menu popups are Qt Quick controls in their own windows; a
    // rounded QML background is only rounded on screen when the window behind it
    // has an alpha buffer. Qt's own popup window asks for a transparent clear color,
    // but the application format must allow alpha BEFORE the first QQuickWindow is
    // created (QQuickWindow::setDefaultAlphaBuffer docs). The widget popups this
    // replaces set WA_TranslucentBackground for the same reason.
    QQuickWindow::setDefaultAlphaBuffer(true);
    // One tooltip delay for both toolkits: the QML AeToolTip reads QmlTheme's token,
    // and QWidget's QToolTip reads this platform hint.
    QGuiApplication::styleHints()->setToolTipWakeUpDelay(
        std::chrono::milliseconds(gui::QmlTheme::kToolTipDelayMs)
    );

    // application path
    #if defined(Q_OS_DARWIN)
    const QString appDir = app.applicationDirPath();
    QApplication::addLibraryPath(appDir + "/Contents/MacOS");
    QApplication::addLibraryPath(appDir + "/../PlugIns/");
    #else
    const QString appDir = QApplication::applicationDirPath();
    #endif

    // Register the bundled font so the Geist family resolves on every
    // platform instead of falling back to a system font (the digit glyphs
    // have tabular widths baked in, so number fields never shift layout).
    // The fonts are embedded into the binary (see assets/assets.qrc).
    {
        const QDir fontsDir(":/fonts");
        for (const QString& file : fontsDir.entryList(QStringList() << "*.ttf", QDir::Files)) {
            QFontDatabase::addApplicationFont(fontsDir.absoluteFilePath(file));
        }
        QFont appFont(QApplication::font());
        appFont.setFamily("Geist");
        QApplication::setFont(appFont);
    }

    // set organization and application name for the application setting
    // (BEFORE computing paths below: QStandardPaths::CacheLocation derives
    // the dir from these, and QSettings uses them too)
    QCoreApplication::setOrganizationName("AnimeEffectsProject");
    QCoreApplication::setApplicationName("AnimeEffects");

    const QString stdCacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString cacheDir = !stdCacheDir.isEmpty() ? stdCacheDir : (appDir + "/cache");


    // set fatal error handler
    static AEErrorHandler aeErrorHandler;
    gXCErrorHandler = &aeErrorHandler;


    // language: the .qm are embedded under :/i18n; the locale font defaults
    // live in QSettings.
    QScopedPointer<gui::LocaleDecider> locale(new gui::LocaleDecider());
    if (locale->translator()) { QCoreApplication::installTranslator(locale->translator()); }

    {
        // load constant gui resources; the theme reads its icons/stylesheet
        // from the embedded :/ resources (see assets/assets.qrc), not the
        // external data folder.
        QScopedPointer<gui::GUIResources> resources(new gui::GUIResources(QStringLiteral(":")));

        // create system logic core
        QScopedPointer<ctrl::System> system(new ctrl::System(cacheDir));

        // create main window
        QScopedPointer<gui::MainWindow> mainWindow(new gui::MainWindow(*system, *resources, locale->localeParam()));

        qDebug() << "show main window";
        // show main window
        mainWindow->showWithSettings();

        // checkForUpdates
        const QString url("https://api.github.com/repos/AnimeEffectsDevs/AnimeEffects/tags");
        util::NetworkUtil::checkForUpdate(url, mainWindow->window(), false);


#ifdef QT_DEBUG
        qDebug() << "Launching debug project";
        mainWindow->testNewProject(QStringLiteral(AE_SAMPLE_PSD));
#endif
        // assoc handle
        auto arguments = QCoreApplication::arguments();
        if (arguments.last().contains(".anie")) {
            const auto& file = arguments.last();
            if (QFile(file).exists()) {
                mainWindow->onOpenRecentTriggered(file);
            }
        }

        resources->triggerOnThemeChanged();

        // execute application
        result = QCoreApplication::exec();

        // save settings(window status, etc.)
        mainWindow->saveCurrentSettings(result);

        // bind gl context for destructors
        gl::Global::makeCurrent();
        qDebug() << "clearing main window";
        mainWindow.reset();
        qDebug() << "clearing system";
        system.reset();
        qDebug() << "clearing resources";
        resources.reset();
        qDebug() << "core application end";
        #if defined(Q_OS_APPLE) && defined(QT_DEBUG)
        qDebug() << "removing application cache";
        cur = QDir::homePath();
        QDir data = cur + "/.AECache";
        data.removeRecursively();
        #endif

    }

    return result;
}
