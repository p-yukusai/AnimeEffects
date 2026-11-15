import QtQuick

// A bare text action in the menu bar - the Donate command, which has a command and
// no menu. It is deliberately not a MenuBarItem: that control requires a Menu.
Item {
    id: root

    property string text: ""
    signal clicked()

    implicitWidth: label.implicitWidth + 16
    implicitHeight: 30

    Rectangle {
        // Same hitbox/visual split as HeaderMenuBarItem: the 30px row is the
        // click target, the 24px pill is the ink.
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 24
        radius: 4
        antialiasing: true
        // The QMenuBar item's hover/pressed fill was @hairline@; the Donate action
        // was a QMenuBar item, not a QToolButton, so it shares that ink.
        color: (mouse.pressed || mouse.containsMouse) ? theme.hairline : "transparent"
    }
    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.enabled ? theme.text : theme.textDisabled
        font: uiFont
        Accessible.ignored: true
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: root.enabled
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    Accessible.role: Accessible.Button
    Accessible.name: root.text
    Accessible.onPressAction: if (root.enabled) root.clicked()
}
