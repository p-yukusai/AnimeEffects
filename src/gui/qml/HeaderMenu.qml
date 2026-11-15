import QtQuick
import QtQuick.Controls

// The one menu shell the header uses, so every dropdown shares one popup type, one
// panel and one delegate.
//
// POPUP TYPE IS EXPLICIT. Qt Quick Controls' Menu can be an item popup, a window
// popup or a native menu depending on the style; this app's style is Basic, where
// custom delegates are drawn, and the header scene is a QQuickWidget only 30px
// tall - an in-scene menu would be clipped by its own host. `Popup.Window` is the
// documented type for a customized menu that must keep its QML rendering (Menu
// docs, "Menu types": set it explicitly when customizing).
Menu {
    id: control

    popupType: Popup.Window
    padding: 4
    margins: 0
    overlap: 1
    // CloseMultiple is what makes an outside click close the whole menu hierarchy,
    // not just the top popup. The widget QMenu closed all of its submenus with the
    // menu; without it a click on the canvas left the File menu open behind a
    // dismissed submenu.
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside | Popup.CloseMultiple
    delegate: HeaderMenuItem { }

    // THE WIDTH IS MEASURED HERE, not delegated to the content item. A Menu is
    // populated from Actions, and the Basic style's ListView does not derive an
    // implicit width from the Action texts; without this the popup opened 8px wide
    // (4px padding on each side) with its rows clipped to nothing. The menu is the
    // only object that can see every Action at once, and `actionAt`/`menuAt` are its
    // documented API. The 440px cap keeps a saved path from becoming the width of
    // the screen; HeaderMenuItem elides inside it.
    FontMetrics {
        id: metrics
        font: uiFont
    }
    readonly property real measuredWidth: {
        // The count read is the binding dependency: an Instantiator adding recent
        // files (or hiding an unavailable action) changes it and re-measures.
        var widest = 0;
        for (var i = 0; i < control.count; ++i) {
            var action = control.actionAt(i);
            if (action && action.text !== "") {
                widest = Math.max(widest, metrics.advanceWidth(action.text) + 45);
                continue;
            }
            var submenu = control.menuAt(i);
            if (submenu && submenu.title !== "")
                widest = Math.max(widest, metrics.advanceWidth(submenu.title) + 45);
        }
        return widest;
    }
    implicitWidth: Math.min(440, Math.max(120, measuredWidth + 8))

    Timer {
        id: clearInitialCurrent
        interval: 50
        repeat: false
        onTriggered: if (control.opened) control.currentIndex = -1
    }

    // THE BORDER IS TWO FILLS, AND THE BACKGROUND IS A MULTISAMPLED LAYER.
    // A Rectangle's 1px border is a hard stroke, and in this popup window the
    // ordinary item edge reached the window alpha with binary coverage (MEASURED:
    // an 8px radius corner resolved to alpha 0/255, no partial pixels). The layer
    // renders the background into a multisampled target, so the same shape
    // resolves with partial coverage (32/128/159/191/223 at samples: 8), and two
    // fills inset by 1px keep the border's inner and outer edges independent.
    // The 1-bit window-mask fallback still has binary outer coverage by
    // construction.
    background: Rectangle {
        radius: 8
        antialiasing: true
        color: theme.floaterEdge
        layer.enabled: true
        layer.samples: 8
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: 7
            antialiasing: true
            color: theme.floaterBody
        }
    }

    // Qt hands the popup a transparent window and the Qt::Popup flags; the radius
    // is ours, and a compositor that does not composite a popup's alpha needs a
    // window region instead. The host owns that platform decision (see
    // HeaderBar::configurePopupWindow). Window.window is the popup's own QQuickWindow
    // while it is open.
    onClosed: if (headerHost && headerHost.hideHeaderToolTip) headerHost.hideHeaderToolTip()

    onOpened: {
        // A freshly opened QMenu has NO current row: the first item is highlighted
        // only once the pointer or the keyboard chooses one. QQuickMenu assigns the
        // first row as current once the popup has finalised its focus, AFTER this
        // handler (a Qt.callLater reset is not enough), so a short one-shot clears
        // that initial selection. From then on hover and the keyboard own
        // currentIndex.
        currentIndex = -1;
        clearInitialCurrent.restart();
        // Window.window is an attached property on Item; a Menu is a Popup
        // (QtObject), so ask its content item. When the popup is open that item
        // lives in the popup's own QQuickWindow.
        const popupWindow = contentItem ? contentItem.Window.window : null;
        if (headerHost && typeof headerHost.configurePopupWindow === "function" && popupWindow)
            headerHost.configurePopupWindow(popupWindow, control.title);
    }
}
