#ifndef GUI_HEADERBAR_H
#define GUI_HEADERBAR_H

#include "gui/QmlHost.h"

class QWidget;

namespace gui {

class DockSlotsModel;
class KeyCommandMap;
class LocaleParam;
class MainMenuBar;
class QmlTheme;
class ViaPoint;
class ViewSettingsModel;

// The window's header: the menu bar, the viewport settings and the layout toggles,
// as ONE Qt Quick scene.
//
// This is a QQuickWidget only because the main window is a QMainWindow; the scene
// itself owns every pixel and every interaction (qml/HeaderBar.qml and its
// components). There is no QMenuBar, QMenu, QAction, QToolButton or corner widget on
// the widget side: the menu is QML's MenuBar over gui::MainMenuBar's MenuCommand
// registry, the view settings are gui::ViewSettingsModel, and the slot toggles are
// gui::DockSlotsModel.
// The host's whole job is to put those three models, the theme and the locale's font
// into the scene's context, to give the scene its height, and to pass the scene's
// requests back to the window.
//
// THE KEYS STILL BELONG TO THE WINDOW. The header takes no keyboard focus
// (Qt::NoFocus): a menu opens in its own popup window, which has its own focus while
// it is up, and the moment it closes the rebindable key map must be live again. A
// focusable QQuickWidget would keep an active focus item that eats Ctrl+S/Ctrl+Z the
// way QQuickWidget's key forwarding exists to prevent (see QmlHost.h).
class HeaderBar: public KeyForwardingQuickWidget {
    Q_OBJECT
public:
    HeaderBar(MainMenuBar& aMenu,
              ViaPoint& aViaPoint,
              KeyCommandMap& aKeyCommandMap,
              const LocaleParam& aLocale,
              QWidget* aParent);
    ~HeaderBar() override;

    // Re-read the palette and the tinted glyphs after a theme switch.
    void refreshTheme();

    // Shape a QML popup window after QML has opened it. Qt already gives a
    // Popup.Window a transparent clear color and the Qt::Popup flags; what it
    // cannot know is this application's 8px panel radius, or that a compositor
    // which does not composite Qt::Popup translucency needs a real window region
    // (the same fallback the widget menus had). The menu calls this from its
    // `onOpened` with its content item's Window.window.
    Q_INVOKABLE void configurePopupWindow(QObject* aWindow, const QString& aTitle);

    // The header scene is only 30px tall, so an in-scene QML tooltip (even one
    // mounted on the window overlay) is clipped at the widget's bottom edge. The
    // header's controls therefore ask the host for the platform tooltip, the same
    // one the widget controls had; it can sit anywhere on screen and carries the
    // application's QToolTip stylesheet.
    Q_INVOKABLE void showHeaderToolTip(QObject* aOwner, const QString& aText);
    Q_INVOKABLE void hideHeaderToolTip();

    ViewSettingsModel* viewSettings() const { return mViewSettings; }
    DockSlotsModel* dockSlots() const { return mDockSlots; }

signals:
    // The viewport must consume the setting now (a toggle, or one of the three
    // rotation pulses whose flag is already lowered again).
    void viewSettingChanged();
    // A slot button was pressed. The area is Qt::DockWidgetArea's value; the window
    // owns the panels and performs the toggle.
    void dockSlotToggleRequested(int aArea);

private:
    QmlTheme* mTheme = nullptr;
    ViewSettingsModel* mViewSettings = nullptr;
    DockSlotsModel* mDockSlots = nullptr;
};

} // namespace gui

#endif // GUI_HEADERBAR_H
