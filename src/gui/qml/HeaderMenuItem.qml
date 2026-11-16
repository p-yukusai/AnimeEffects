pragma Translator: "AnimeEffects.Header"

import QtQuick
import QtQuick.Controls

// The Menu's delegate: one command row.
//
// The control is the Qt Quick Controls `MenuItem` template, which owns activation,
// the current/highlighted index, the check state an Action gives it, the submenu
// arrow and the accessibility role. This file supplies the app's row: 24px high,
// the check column reserved on every checkable row, the accent hover, elided text
// and a 440px cap so a saved path cannot become the width of the screen.
MenuItem {
    id: control

    implicitHeight: 24
    padding: 0
    leftPadding: 0
    rightPadding: 0
    font: uiFont

    // The Panels rows are commands with no checkable QML Action (see
    // HeaderCheckableAction.qml): the Action exposes checkableState/checkedState and
    // the command remains the only authority. Every other row falls back to the
    // MenuItem's own action state.
    readonly property bool rowCheckable: control.action && control.action.checkableState !== undefined
                                         ? control.action.checkableState : control.checkable
    readonly property bool rowChecked: control.action && control.action.checkedState !== undefined
                                       ? control.action.checkedState : control.checked

    // The text column starts 21px in from the row's content edge - the QMenu's
    // indicator/icon column plus its item padding. The same offset applies to
    // checkable and plain rows, so a menu's labels line up.
    readonly property int textIndent: 21

    implicitWidth: Math.min(440,
        textIndent + label.implicitWidth + (control.subMenu !== null ? 26 : 14))

    indicator: Image {
        x: 6
        anchors.verticalCenter: parent.verticalCenter
        width: 12
        height: 12
        // A CHECK column is reserved for every checkable row, checked or not: the
        // labels of a group then line up instead of shifting when one is toggled.
        visible: control.rowCheckable
        source: rowChecked ? (theme.iconRevision, theme.iconUrl("check-bold")) : ""
        sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
        sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
        smooth: true
        fillMode: Image.PreserveAspectFit
        Accessible.ignored: true
    }

    // The content item is an Item, not the Text itself: Control sets its content
    // item's x/width, which would overwrite a binding on a Text used directly as
    // the content item. The inside Text then owns the 21px text column.
    contentItem: Item {
        Text {
            id: label
            x: control.textIndent
            width: parent.width - x - (control.subMenu !== null ? 26 : 12)
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            color: !control.enabled ? theme.textDisabled
                                    : (control.highlighted ? theme.accentText : theme.text)
            font: uiFont
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            Accessible.ignored: true
        }
    }

    arrow: Image {
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        width: 12
        height: 12
        visible: control.subMenu !== null
        source: (theme.iconRevision, theme.iconUrl("caret-right"))
        sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
        sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
        smooth: true
        fillMode: Image.PreserveAspectFit
        opacity: control.enabled ? 1.0 : 0.45
        Accessible.ignored: true
    }

    Accessible.checkable: control.rowCheckable
    Accessible.checked: control.rowChecked

    // The menu caps at 440px (long saved paths); the full label must still be
    // reachable, so a truncated row reveals it on the standard tooltip delay. The
    // platform tooltip is used because it can extend past the popup window's edge;
    // a tooltip drawn in the popup would be clipped by that window.
    readonly property bool truncated: label.implicitWidth > label.width
    readonly property bool nativeToolTip: typeof headerHost !== "undefined" && headerHost !== null
                                          && typeof headerHost.showHeaderToolTip === "function"
    Timer {
        id: nativeTipTimer
        interval: theme.toolTipDelay
        repeat: false
        onTriggered: if (control.nativeToolTip && control.hovered && control.enabled && control.truncated)
                         headerHost.showHeaderToolTip(control, control.text)
    }
    onHoveredChanged: {
        if (control.nativeToolTip && control.hovered && control.enabled && control.truncated) {
            nativeTipTimer.restart();
        } else {
            nativeTipTimer.stop();
            if (control.nativeToolTip) headerHost.hideHeaderToolTip();
        }
    }
    Component.onDestruction: if (control.nativeToolTip) headerHost.hideHeaderToolTip()

    background: Rectangle {
        radius: 3
        antialiasing: true
        // A menu's selected row is the accent, matching the QSS QMenu the header
        // replaced and the combo popup beside it. The neutral raised fill is the
        // press/hover of the controls around the menu.
        color: control.enabled && control.highlighted
               ? theme.accentHover
               : (control.enabled && control.down ? theme.raisedHover : "transparent")
    }
}
