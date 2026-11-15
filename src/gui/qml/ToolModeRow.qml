pragma Translator: "AnimeEffects.Tools"

import QtQuick

// The tool's MODE row: one icon button per mode, the current one filled.
//
// A Flow rather than a fixed grid, because the mode count is not the same for every tool
// and the dock is resizable: the bone editor has SEVEN (which the widget's 172px dock
// wrapped onto a second row, leaving the paint and erase glyphs alone on it), while the
// SR transform has two. Wrapping is therefore the honest layout at any width, and the
// heading's trailing text is what names the highlighted one.
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
