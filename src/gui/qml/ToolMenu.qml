pragma Translator: "AnimeEffects.Tools"

import QtQuick
import QtQuick.Controls

// The dock's OVERFLOW MENU: an ellipsis button and an in-scene list.
//
// A panel with one primary action and two or three maintenance actions puts the primary
// in a real button and the rest behind this control - the convention every desktop
// application follows, and the reason the verbs are no longer a row of bare words under
// the status line. The list is styled like the combo's popup (theme.floaterBody,
// floaterEdge, 24px rows) so the dock has one popup look.
//
// `items` is a list of { label, action, enabled } maps; a chosen row closes the list and
// emits `triggered(action)`. The `action` is the caller's own, never the row index: the
// list is rebuilt from the model's state, so a row's position is not an identity.
Item {
    id: root

    property var items: []
    // The name a screen reader and the button's tooltip use. The glyph is an ellipsis,
    // which says "more" only if something says it in words.
    property string label: ""

    signal triggered(int action)

    implicitWidth: 26
    implicitHeight: 26

    readonly property bool open: popup.visible

    function toggle() {
        if (popup.visible) {
            popup.close();
            return;
        }
        if (root.enabled && items.length > 0) {
            popup.open();
        }
    }

    ToolIconButton {
        anchors.fill: parent
        iconName: "dots-three"
        label: root.label
        onClicked: root.toggle()
    }

    Popup {
        id: popup
        parent: root
        modal: false
        dim: false
        padding: 4
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        // Right-aligned with the button, below it. The list is three short rows at the
        // top of a dock, so the flip/clamp machinery of the big pickers is not needed;
        // the right alignment is what keeps it inside the dock and over the canvas.
        width: 168
        height: content.implicitHeight + 8
        x: root.width - width
        y: root.height + 2

        background: Rectangle {
            radius: 4
            color: theme.floaterBody
            border.width: 1
            border.color: theme.floaterEdge
        }

        contentItem: Column {
            id: content
            spacing: 0

            Repeater {
                model: root.items
                delegate: Item {
                    id: row
                    required property int index
                    required property var modelData

                    width: content.width
                    height: 24

                    Rectangle {
                        anchors.fill: parent
                        radius: 3
                        color: rowMouse.containsMouse && row.modelData.enabled !== false
                               ? theme.raisedHover : "transparent"
                    }
                    Text {
                        x: 8
                        width: parent.width - 16
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.label
                        color: row.modelData.enabled === false ? theme.textDisabled : theme.text
                        font: uiFont
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: row.modelData.enabled !== false
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            popup.close();
                            root.triggered(row.modelData.action);
                        }
                    }

                    // A menu row is a button to a reader as much as to the pointer.
                    Accessible.role: Accessible.Button
                    Accessible.name: row.modelData.label
                    Accessible.description: row.modelData.enabled === false ? qsTr("Unavailable") : ""
                    Accessible.onPressAction: if (row.modelData.enabled !== false) {
                        popup.close();
                        root.triggered(row.modelData.action);
                    }
                }
            }
        }
    }
}
