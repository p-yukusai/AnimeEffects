pragma Translator: "AnimeEffects.Dialogs"

import QtQuick

// The header's layout toggle, matching the widget QToolButton#dockToggle it
// replaces: a quiet 18px glyph in a 25px hit box, and an accent-bright 10%
// hover wash. There is NO checked fill and no brighter checked tint: the state
// is the glyph's FORM. A slot that is showing uses the filled phosphor sidebar;
// an empty slot uses the same glyph hollow. That keeps the whole row a quiet
// group below the viewport settings - the checked tint it used to take made a
// showing slot the brightest glyph in the header - while a filled shape still
// reads as "on" without a second, competing signal behind it.
Item {
    id: root

    property string iconName: ""
    property string label: ""
    property string tipText: ""
    property bool checked: false

    signal clicked()

    implicitWidth: 25
    // 26, the same height as ToolIconButton: the 25px hitbox rounded to the next
    // layout pixel and put the slot glyphs one pixel below the view settings.
    implicitHeight: 26

    readonly property bool nativeToolTip: typeof headerHost !== "undefined" && headerHost !== null
                                          && typeof headerHost.showHeaderToolTip === "function"

    Rectangle {
        anchors.fill: parent
        radius: 2
        antialiasing: true
        // @accentOverlay@ in the sheet is @accentBright@ at 0x1A; the same value
        // here so a hovered slot toggle matches the hovered dock tabs beside it.
        color: !root.enabled ? "transparent"
                             : (mouse.pressed ? theme.active
                                              : (mouse.containsMouse
                                                 ? Qt.rgba(theme.accentBright.r, theme.accentBright.g,
                                                           theme.accentBright.b, 0x1A / 255)
                                                 : "transparent"))
    }
    Image {
        anchors.centerIn: parent
        width: 18
        height: 18
        source: root.iconName === "" ? ""
                : (theme.iconRevision,
                   theme.iconUrl(root.checked ? root.iconName + "-fill" : root.iconName))
        sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
        sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
        smooth: true
        fillMode: Image.PreserveAspectFit
        opacity: root.enabled ? 1.0 : 0.45
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        // HOVER IS NOT DISABLED WITH THE BUTTON: an empty slot's whole message is its
        // "Nothing is docked on the ..." tooltip, and a disabled MouseArea never
        // reports the hover that would show it. The click is gated instead.
        hoverEnabled: true
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: if (root.enabled) root.clicked()
        onContainsMouseChanged: {
            if (root.nativeToolTip && mouse.containsMouse) {
                nativeTipTimer.restart();
            } else {
                nativeTipTimer.stop();
                if (root.nativeToolTip) headerHost.hideHeaderToolTip();
            }
        }
    }
    Timer {
        id: nativeTipTimer
        interval: theme.toolTipDelay
        repeat: false
        onTriggered: if (root.nativeToolTip && mouse.containsMouse)
                         headerHost.showHeaderToolTip(root, root.tipText !== "" ? root.tipText : root.label)
    }
    Component.onDestruction: if (root.nativeToolTip) headerHost.hideHeaderToolTip()

    Accessible.role: Accessible.Button
    Accessible.name: root.label
    Accessible.description: root.checked ? qsTr("On") : qsTr("Off")
    Accessible.onPressAction: if (root.enabled) root.clicked()

    activeFocusOnTab: true
    Keys.onSpacePressed: if (root.enabled) { root.clicked(); event.accepted = true }
    Keys.onReturnPressed: if (root.enabled) { root.clicked(); event.accepted = true }
}
