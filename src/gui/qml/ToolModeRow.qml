pragma Translator: "AnimeEffects.Tools"

import QtQuick

// The tool's MODE row: one icon button per mode, the current one filled.
//
// ONE ROW at the dock's width. The bone editor had EIGHT modes when the paint/erase
// pair was two tools; a four-column grid was used then so the eighth did not sit alone
// on a second line. The pair is now ONE Envelope brush, so seven modes fit on one row
// again (7*26 + 6*2 = 194 inside the 212px content), and the grid was forcing a second
// row with an empty cell for no reason. A Flow wraps only if the dock is made narrower.
// The order still groups the modes: four rig modes, then Adjust envelope, Envelope
// brush, Paint weights. The heading's trailing text names the highlighted mode.
Item {
    id: root

    // [{label, icon}] from gui::ToolViewModel
    property var modes: []
    property int current: -1
    signal chosen(int index)

    implicitHeight: flow.implicitHeight + 4
    width: parent ? parent.width : 0

    Flow {
        id: flow
        x: 12
        y: 2
        width: parent.width - 24
        spacing: 2
        Repeater {
            model: root.modes
            delegate: ToolIconButton {
                iconName: modelData.icon
                label: modelData.label
                checkable: true
                checked: index === root.current
                onClicked: root.chosen(index)
            }
        }
    }
}
