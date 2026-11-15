pragma Translator: "AnimeEffects.Header"

import QtQuick
import QtQuick.Controls

// The MenuBar's delegate: one top-level menu title.
//
// This is the Qt Quick Controls `MenuBarItem` template, not a hand-rolled button.
// The control supplies the click/hover/keyboard behaviour, the menu it opens, and
// the accessibility role; the file supplies the application's ink (the same 30px
// row, 4px radius and hover fill as the dock's icon buttons) and the locale font.
MenuBarItem {
    id: control

    // Full bar height: the widget QMenuBar's :selected background filled the
    // whole 30px item; a floating pill read as a button.
    implicitHeight: 30
    leftPadding: 9
    rightPadding: 9
    topPadding: 0
    bottomPadding: 0

    contentItem: Text {
        text: control.text
        // THE DOCK FONT, not Qt.application.font: the header is part of the same
        // 8pt Geist chrome as the panels (see QmlHost.h's qmlUiFont).
        font: uiFont
        color: control.enabled ? theme.text : theme.textDisabled
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        Accessible.ignored: true
    }

    background: Rectangle {
        // The HITBOX is the full 30px bar row (MenuBarItem is 30 high), but the
        // VISUAL is a compact 24px pill: a full-height selected block reads as a
        // tall button beside the 26px tool icons. Keep the larger click target and
        // make the ink the size a menu title should be.
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 24
        radius: 4
        antialiasing: true
        // MenuBarItem.highlighted is the open menu or the hover under it. The token
        // is the one the widget QMenuBar's :selected rule used (hairline), not the
        // accent: a menu title is chrome, not a mode.
        color: (control.highlighted || control.down) ? theme.hairline : "transparent"
        border.width: control.visualFocus ? 1 : 0
        border.color: theme.focus
    }
}
