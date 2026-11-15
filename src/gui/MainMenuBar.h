#ifndef GUI_MAINMENUBAR_H
#define GUI_MAINMENUBAR_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

#include <functional>

#include "core/Project.h"
#include "util/Signaler.h"

namespace gui {

class GUIResources;
class MainWindow;
class MenuCommand;
class ViaPoint;

// THE MENU BAR'S COMMAND REGISTRY.
//
// The bar itself is qml/HeaderBar.qml, built from Qt Quick Controls' MenuBar/Menu
// with this project's delegates and Action objects (HeaderMenuBarItem.qml,
// HeaderMenuItem.qml, HeaderAction.qml, HeaderCheckableAction.qml). There is no
// QMenuBar, QMenu, QAction or QToolButton here: the tree, its order and its
// submenu are declared in QML, where the menu lives, and this class supplies only
// the translated words, the enabled/checked state and the callback behind each
// command.
//
// WHY THE COMMANDS ARE NOT DECLARED IN QML. A menu row is a command the window
// performs (open a project, export, show a dock); the callback needs MainWindow,
// ViaPoint and GUIResources. QML asks for it by a stable id:
//
//     HeaderAction { commandId: "newProject" }
//
// and HeaderAction.qml binds the QML Action's text/enabled to command("newProject")
// and triggers it. The scene never sees a QWidget or a QAction.
//
// THE NAME AND THE TRANSLATION CONTEXT. The class is still called MainMenuBar
// because that is what it is the model of; its tr() context, `gui::MainMenuBar`, is
// a catalog key frozen by assets/locale/translation_*.ts. Renaming the class would
// look like a cleanup and would silently ship English menus in five languages until
// every catalog was re-extracted (AGENTS.md: a lookup in the wrong context returns
// the source with no error).
class MainMenuBar: public QObject {
    Q_OBJECT
    Q_PROPERTY(QString fileTitle READ fileTitle CONSTANT)
    Q_PROPERTY(QString editTitle READ editTitle CONSTANT)
    Q_PROPERTY(QString panelsTitle READ panelsTitle CONSTANT)
    Q_PROPERTY(QString preferencesTitle READ preferencesTitle CONSTANT)
    Q_PROPERTY(QString helpTitle READ helpTitle CONSTANT)
    Q_PROPERTY(QString openRecentTitle READ openRecentTitle CONSTANT)
    Q_PROPERTY(QVariantList recentFiles READ recentFiles CONSTANT)
public:
    MainMenuBar(MainWindow& aMainWindow,
                ViaPoint& aViaPoint,
                GUIResources& aGUIResources,
                QObject* aParent = nullptr);

    // Enable/disable the project-scoped commands. Repeat export is separate: it
    // becomes available after the first successful export, not when a project opens.
    void setProject(core::Project* aProject);
    void setExportAgainEnabled(bool aOn);

    // The scene's lookup. The id is the stable name used by HeaderAction.qml; a
    // missing id is a programming error, not a runtime state, so it is asserted
    // rather than returning a silent no-op row.
    Q_INVOKABLE QObject* command(const QString& aId) const;
    // Optional commands (Donate may be switched off) are asked about before they are
    // used; command() asserts on an unknown id so a missing row cannot be silent.
    Q_INVOKABLE bool hasCommand(const QString& aId) const { return mCommands.contains(aId); }

    // Commands created outside the constructor: the Panels menu's three toggles
    // name docks that do not exist when the menu bar is built. MainWindow adds them
    // before the header scene loads, so the QML Actions bind to a complete registry.
    MenuCommand* addCommand(const QString& aId,
                            const QString& aText,
                            std::function<void()> aCallback,
                            bool aCheckable = false);

    Q_INVOKABLE void openRecentPath(const QString& aPath);

    QString fileTitle() const { return tr("File"); }
    QString editTitle() const { return tr("Edit"); }
    QString panelsTitle() const { return tr("Panels"); }
    QString preferencesTitle() const { return tr("Preferences"); }
    QString helpTitle() const { return tr("Help"); }
    QString openRecentTitle() const { return tr("Open recent"); }
    QVariantList recentFiles() const { return mRecentFiles; }

    // signals
    util::Signaler<void()> onVisualUpdated;
    util::Signaler<void()> onProjectAttributeUpdated;
    util::Signaler<void()> onTimeFormatChanged;

private:
    void onProjectSettingsTriggered();

    MainWindow* mMainWindow = nullptr;
    ViaPoint& mViaPoint;
    GUIResources& mGUIResources;
    QHash<QString, MenuCommand*> mCommands;
    QVector<MenuCommand*> mProjectActions;
    MenuCommand* mExportAgain = nullptr;
    QVariantList mRecentFiles;
};

} // namespace gui

#endif // GUI_MAINMENUBAR_H
