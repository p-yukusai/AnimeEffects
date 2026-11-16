// THE HEADER SCENE: the Qt Quick Controls MenuBar, its delegates and the
// Action<->MenuCommand bridge.
//
// The models have their own test (header_model). This one loads qml/HeaderBar.qml
// with fake context objects and asks the scene the questions a GUI probe cannot ask
// cheaply: are the five menus there, are the rows Actions with the registry's words,
// does a trigger reach the command, and does a checkable Panels row show the state
// the window owns? It is widget-free on the application side - no MainWindow, no
// docks - so a broken QML binding fails here instead of at a screenshot.
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/header_scene
#include <QApplication>
#include <QFile>
#include <QObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>
#include <QVariantList>

#include "gui/MainViewSetting.h"
#include "gui/qml/DockSlotsModel.h"
#include "gui/qml/MenuCommand.h"
#include "gui/qml/QmlTheme.h"
#include "gui/qml/ViewSettingsModel.h"
#include "gui/theme/Icons.h"

namespace {

int failures = 0;

void check(bool aOk, const QString& aWhat) {
    if (aOk) {
        QTextStream(stdout) << "PASS  " << aWhat << "\n";
        return;
    }
    ++failures;
    QTextStream(stdout) << "FAIL  " << aWhat << "\n";
}

// The registry as the scene sees it. The real one is MainMenuBar, which needs a
// MainWindow; this fake has the same shape (titles, command(id), recentFiles,
// openRecentPath) and records what the rows do.
class FakeMenuHost: public QObject {
    Q_OBJECT
    Q_PROPERTY(QString fileTitle READ fileTitle CONSTANT)
    Q_PROPERTY(QString editTitle READ editTitle CONSTANT)
    Q_PROPERTY(QString panelsTitle READ panelsTitle CONSTANT)
    Q_PROPERTY(QString preferencesTitle READ preferencesTitle CONSTANT)
    Q_PROPERTY(QString helpTitle READ helpTitle CONSTANT)
    Q_PROPERTY(QString openRecentTitle READ openRecentTitle CONSTANT)
    Q_PROPERTY(QVariantList recentFiles READ recentFiles CONSTANT)
public:
    FakeMenuHost() {
        add(QStringLiteral("newProject"), QStringLiteral("New project"));
        add(QStringLiteral("openProject"), QStringLiteral("Open project"));
        add(QStringLiteral("saveProject"), QStringLiteral("Save project"));
        add(QStringLiteral("saveProjectAs"), QStringLiteral("Save project as"));
        add(QStringLiteral("exportProject"), QStringLiteral("Export project as"));
        add(QStringLiteral("quickExport"), QStringLiteral("Quickly export as..."));
        add(QStringLiteral("exportAgain"), QStringLiteral("Export again"));
        add(QStringLiteral("closeProject"), QStringLiteral("Close project"));
        add(QStringLiteral("undo"), QStringLiteral("Undo"));
        add(QStringLiteral("redo"), QStringLiteral("Redo"));
        add(QStringLiteral("projectSettings"), QStringLiteral("Project settings"));
        add(QStringLiteral("assets"), QStringLiteral("Assets"));
        add(QStringLiteral("panelTools"), QStringLiteral("Tools"), true);
        add(QStringLiteral("panelProperties"), QStringLiteral("Properties"), true);
        add(QStringLiteral("panelAnimation"), QStringLiteral("Animation"), true);
        add(QStringLiteral("settings"), QStringLiteral("Settings"));
        add(QStringLiteral("keybindings"), QStringLiteral("Keybindings"));
        add(QStringLiteral("about"), QStringLiteral("About AnimeEffects"));
        add(QStringLiteral("checkUpdates"), QStringLiteral("Check for updates"));
        add(QStringLiteral("telemetry"), QStringLiteral("System telemetry"));

        QVariantMap recent;
        recent.insert(QStringLiteral("text"), QStringLiteral("/tmp/example.anie"));
        recent.insert(QStringLiteral("path"), QStringLiteral("/tmp/example.anie"));
        recent.insert(QStringLiteral("enabled"), true);
        mRecentFiles.push_back(recent);
    }

    QString fileTitle() const { return QStringLiteral("File"); }
    QString editTitle() const { return QStringLiteral("Edit"); }
    QString panelsTitle() const { return QStringLiteral("Panels"); }
    QString preferencesTitle() const { return QStringLiteral("Preferences"); }
    QString helpTitle() const { return QStringLiteral("Help"); }
    QString openRecentTitle() const { return QStringLiteral("Open recent"); }
    QVariantList recentFiles() const { return mRecentFiles; }

    Q_INVOKABLE QObject* command(const QString& aId) const { return mCommands.value(aId); }
    Q_INVOKABLE bool hasCommand(const QString& aId) const { return mCommands.contains(aId); }
    Q_INVOKABLE void openRecentPath(const QString& aPath) {
        openedRecent = aPath;
        ++recentOpenCount;
    }

    gui::MenuCommand* menuCommand(const QString& aId) const { return mCommands.value(aId); }
    int triggered = 0;
    int recentOpenCount = 0;
    QString openedRecent;

private:
    void add(const QString& aId, const QString& aText, bool aCheckable = false) {
        auto* command = new gui::MenuCommand(aText, [this, aId]() { ++triggered; last = aId; },
                                            aCheckable, this);
        mCommands.insert(aId, command);
    }

public:
    QString last;

private:
    QHash<QString, gui::MenuCommand*> mCommands;
    QVariantList mRecentFiles;
};

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    gui::QmlTheme theme;
    QTemporaryDir iconDir;
    check(iconDir.isValid(), QStringLiteral("a scratch dir for the tinted icons"));
    theme::tintIcons(QStringLiteral(AE_ICON_SRC), iconDir.path(), theme::Colors::current());
    theme::setIconDir(iconDir.path());

    // The header's slot toggles take their checked state from the glyph's FORM:
    // the filled phosphor sidebar for a showing slot, the outline for an empty
    // one. Both are the same quiet tint, and neither is an -active variant (that
    // brighter tint is what made a showing slot the loudest glyph in the header).
    const auto iconText = [&iconDir](const QString& aName) {
        QFile f(iconDir.path() + QLatin1Char('/') + aName + QStringLiteral(".svg"));
        if (!f.open(QIODevice::ReadOnly)) return QString();
        return QString::fromUtf8(f.readAll());
    };
    const QString dimTint = theme::Colors::current().textMutedDim.name();
    check(iconText(QStringLiteral("sidebar-left-fill")).contains(dimTint),
          QStringLiteral("the filled slot glyph is tinted with the quiet group color"));
    check(iconText(QStringLiteral("sidebar-left")).contains(dimTint),
          QStringLiteral("the outline slot glyph takes the same color"));
    check(!iconText(QStringLiteral("sidebar-left-fill")).isEmpty()
              && !iconText(QStringLiteral("sidebar-right-fill")).isEmpty()
              && !iconText(QStringLiteral("sidebar-bottom-fill")).isEmpty(),
          QStringLiteral("every slot has its filled counterpart"));
    check(iconText(QStringLiteral("sidebar-left-active")).isEmpty(),
          QStringLiteral("and the sidebar toggles no longer generate an -active tint"));
    // The header's view settings sit between the dock's content and the slot
    // toggles: Muted, while the same glyphs stay Text for their dock call sites.
    const QString mutedTint = theme::Colors::current().textMuted.name();
    check(iconText(QStringLiteral("cube-quiet")).contains(mutedTint),
          QStringLiteral("the header's view glyph takes the middle Muted tint"));
    check(iconText(QStringLiteral("cube")).contains(theme::Colors::current().icon.name()),
          QStringLiteral("while the dock's cube keeps the Text tint"));

    FakeMenuHost menu;
    gui::MainViewSetting setting;
    gui::ViewSettingsModel view(&setting);
    gui::DockSlotsModel slotModel;
    slotModel.setSlots({{Qt::LeftDockWidgetArea, QStringLiteral("sidebar-left"), QStringLiteral("left side")},
                    {Qt::RightDockWidgetArea, QStringLiteral("sidebar-right"), QStringLiteral("right side")},
                    {Qt::BottomDockWidgetArea, QStringLiteral("sidebar-bottom"), QStringLiteral("bottom")}});
    QObject host; // headerHost: no popup is opened here, so it is never called

    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("mainMenu"), &menu);
    engine.rootContext()->setContextProperty(QStringLiteral("viewSettings"), &view);
    engine.rootContext()->setContextProperty(QStringLiteral("dockSlots"), &slotModel);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    engine.rootContext()->setContextProperty(QStringLiteral("uiFont"), QApplication::font());
    engine.rootContext()->setContextProperty(QStringLiteral("headerHost"), &host);

    QQmlComponent component(&engine, QUrl(QStringLiteral("file:" AE_QML_DIR "/HeaderBar.qml")));
    if (component.isError()) {
        QTextStream(stderr) << "the header scene's QML does not load:\n";
        for (const QQmlError& error : component.errors())
            QTextStream(stderr) << "  " << error.toString() << "\n";
        return 2;
    }
    check(component.status() == QQmlComponent::Ready, QStringLiteral("the header scene's QML is Ready"));

    QScopedPointer<QObject> root(component.create());
    check(!root.isNull(), QStringLiteral("the header scene instantiates"));
    if (root.isNull())
        return 1;

    auto* rootItem = qobject_cast<QQuickItem*>(root.data());
    QQuickWindow window;
    if (rootItem)
        rootItem->setParentItem(window.contentItem());
    window.resize(900, 32);
    window.show();
    QApplication::processEvents();

    // ---- the slot toggle's checked state is the glyph's FORM -----------------
    // The delegates live in the Repeater, so walk the item tree rather than the
    // QObject tree (a Repeater's delegates are not children of its root).
    QQuickItem* leftSlot = nullptr;
    const auto findSlot = [&](auto&& self, QQuickItem* aItem) -> void {
        if (aItem == nullptr) return;
        if (aItem->property("iconName").toString() == QStringLiteral("sidebar-left"))
            leftSlot = aItem;
        for (QQuickItem* child : aItem->childItems()) self(self, child);
    };
    findSlot(findSlot, rootItem);
    check(leftSlot != nullptr, QStringLiteral("the left slot toggle is in the header"));

    const auto slotIconSource = [](QQuickItem* aButton) {
        QString source;
        const auto walk = [&](auto&& self, QQuickItem* aItem) -> void {
            const QVariant value = aItem->property("source");
            if (value.isValid() && value.canConvert<QUrl>())
                source = value.toUrl().toString();
            for (QQuickItem* child : aItem->childItems()) self(self, child);
        };
        walk(walk, aButton);
        return source;
    };
    if (leftSlot) {
        check(!leftSlot->property("checked").toBool(),
              QStringLiteral("the left slot starts unchecked"));
        check(slotIconSource(leftSlot).contains(QStringLiteral("sidebar-left.svg"))
                  && !slotIconSource(leftSlot).contains(QStringLiteral("-fill")),
              QStringLiteral("an empty slot draws the hollow sidebar"));
        slotModel.setSlotState(Qt::LeftDockWidgetArea, true, true,
                               QStringLiteral("Show or hide the left side"), QStringLiteral("tip"));
        QApplication::processEvents();
        check(leftSlot->property("checked").toBool(),
              QStringLiteral("a showing slot takes the checked state"));
        check(slotIconSource(leftSlot).contains(QStringLiteral("sidebar-left-fill.svg")),
              QStringLiteral("and draws the filled sidebar instead of a brighter tint"));
    }

    // ---- the five menus and their titles ------------------------------------
    QObject* fileMenu = root->findChild<QObject*>(QStringLiteral("fileMenu"));
    QObject* panelsMenu = root->findChild<QObject*>(QStringLiteral("panelsMenu"));
    check(fileMenu != nullptr, QStringLiteral("the File menu exists"));
    check(panelsMenu != nullptr, QStringLiteral("the Panels menu exists"));
    if (!fileMenu || !panelsMenu)
        return 1;
    check(fileMenu->property("title").toString() == QStringLiteral("File"),
          QStringLiteral("its title is the registry's word"));
    check(fileMenu->property("count").toInt() > 5, QStringLiteral("the File menu has its rows"));
    check(panelsMenu->property("count").toInt() == 3, QStringLiteral("the Panels menu has three rows"));

    // ---- an ordinary Action is a MenuCommand proxy ---------------------------
    QObject* newProject = fileMenu->findChild<QObject*>(QStringLiteral("action_newProject"));
    check(newProject != nullptr, QStringLiteral("the File menu's first action exists"));
    if (newProject) {
        check(newProject->property("text").toString() == QStringLiteral("New project"),
              QStringLiteral("and carries the command's words"));
        check(newProject->property("enabled").toBool(), QStringLiteral("and its enabled state"));
        check(QMetaObject::invokeMethod(newProject, "trigger"), QStringLiteral("the action can be triggered"));
        QApplication::processEvents(); // HeaderAction defers the command one loop turn
        check(menu.triggered == 1 && menu.last == QStringLiteral("newProject"),
              QStringLiteral("triggering it runs the command registry, once"));

        menu.menuCommand(QStringLiteral("newProject"))->setEnabled(false);
        check(!newProject->property("enabled").toBool(),
              QStringLiteral("disabling the command disables the action"));
        QMetaObject::invokeMethod(newProject, "trigger");
        QApplication::processEvents();
        check(menu.triggered == 1, QStringLiteral("and a disabled action does not run it"));
        menu.menuCommand(QStringLiteral("newProject"))->setEnabled(true);
    }

    // ---- a checkable Panels row follows the window-owned state --------------
    QObject* toolsRow = panelsMenu->findChild<QObject*>(QStringLiteral("action_panelTools"));
    check(toolsRow != nullptr, QStringLiteral("the Panels menu has its Tools row"));
    if (toolsRow) {
        check(toolsRow->property("checkableState").toBool(), QStringLiteral("the row is checkable"));
        check(!toolsRow->property("checkedState").toBool(), QStringLiteral("and starts unchecked"));
        menu.menuCommand(QStringLiteral("panelTools"))->setChecked(true);
        check(toolsRow->property("checkedState").toBool(),
              QStringLiteral("the command's checked state reaches the row"));
        QMetaObject::invokeMethod(toolsRow, "trigger");
        QApplication::processEvents();
        check(menu.triggered == 2 && menu.last == QStringLiteral("panelTools"),
              QStringLiteral("pressing the checkable row triggers the one command"));

        menu.menuCommand(QStringLiteral("panelTools"))->setChecked(false);
        check(!toolsRow->property("checkedState").toBool(),
              QStringLiteral("the window can correct the row after a refused toggle"));
    }

    // ---- the dynamic submenu uses the documented Instantiator + addAction ---
    QObject* recentMenu = fileMenu->findChild<QObject*>(QStringLiteral("recentMenu"));
    check(recentMenu != nullptr, QStringLiteral("the Open recent submenu exists"));
    if (recentMenu) {
        check(recentMenu->property("count").toInt() == 1,
              QStringLiteral("and the Instantiator added the one recent entry"));
        check(recentMenu->property("title").toString() == QStringLiteral("Open recent"),
              QStringLiteral("with the registry's title"));
    }

    // ---- the delegates are the app's, not Basic's ----------------------------
    QVariant menuDelegate = fileMenu->property("delegate");
    check(menuDelegate.isValid() && menuDelegate.value<QObject*>() != nullptr,
          QStringLiteral("the menu carries a delegate component"));
    if (failures) {
        QTextStream(stdout) << "FAILED " << failures << " check(s)\n";
        return 1;
    }
    QTextStream(stdout) << "OK: the header scene's contract holds\n";
    return 0;
}

#include "header_scene.moc"
