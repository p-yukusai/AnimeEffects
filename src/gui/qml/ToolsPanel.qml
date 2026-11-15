pragma Translator: "AnimeEffects.Tools"

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The Tools dock, now a Qt Quick scene. The scene owns layout and ink; every tool
// decision lives in gui::ToolViewModel, which holds the same ctrl param structs the
// widget panels held as members.
//
// THE ORDER IS THE HIERARCHY, top to bottom:
//   1. the TOOLBOX - the dock's primary control, directly under the dock's own title.
//      It carries no heading, because the QDockWidget already says "Tools" (the widget
//      version's "Toolbox" group title was a QGroupBox artifact);
//   2. the VIEW strip - one fixed row of viewport settings, under its own heading. It
//      sits ABOVE the tool panel on purpose: everything above the panel is fixed, so
//      switching tools cannot move it, and the panel (which changes height with the
//      tool, and is the tallest block by far) takes the rest;
//   3. the ACTIVE TOOL's panel: its heading names the tool and the trailing word names
//      the mode, then the mode row, the tool's own rows, and the shared brush.
//
// The whole content is a ScrollView: the mask tool's panel (target, channel, value,
// four brush rows, two switches, Clear) is taller than a dock on a small window, and the
// widget version simply clipped it - there was no scrollbar anywhere in this dock.
Rectangle {
    id: root

    color: theme.base

    ScrollView {
        id: scroll
        anchors.fill: parent
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical: AeScrollBar { width: 12 }
        contentWidth: width

        Column {
            id: content
            width: scroll.width
            topPadding: 6
            spacing: 0

            // ---- 1. the toolbox ---------------------------------------------
            Flow {
                id: toolFlow
                x: 12
                width: parent.width - 24
                spacing: 2
                Repeater {
                    model: toolModel.tools
                    delegate: ToolIconButton {
                        iconName: modelData.icon
                        label: modelData.tip
                        checkable: true
                        // the accent is the application's MODE and belongs to the one
                        // button that says which tool is in the artist's hand
                        accent: true
                        enabled: toolModel.driverActive
                        checked: toolModel.tool === modelData.type
                        onClicked: toolModel.activate(modelData.type)
                    }
                }
            }

            Item { width: 1; height: 8 }

            // ---- 2. the view strip ------------------------------------------
            ToolHeading { title: toolModel.viewTitle }
            Flow {
                id: viewFlow
                x: 12
                width: parent.width - 24
                spacing: 2
                // the two toggles that change how the ARTWORK is drawn
                ToolIconButton {
                    iconName: "cube"
                    label: toolModel.showMeshTip
                    checkable: true
                    checked: toolModel.showMesh
                    onClicked: toolModel.showMesh = !toolModel.showMesh
                }
                ToolIconButton {
                    iconName: "image"
                    label: toolModel.cutOutsideFrameTip
                    checkable: true
                    checked: toolModel.cutOutsideFrame
                    onClicked: toolModel.cutOutsideFrame = !toolModel.cutOutsideFrame
                }
                // the three canvas rotations: TRIGGERS, not states
                ToolIconButton {
                    iconName: "arrow-counter-clockwise"
                    label: toolModel.rotateCounterClockwiseTip
                    onClicked: toolModel.rotateCounterClockwise()
                }
                ToolIconButton {
                    iconName: "dot-outline"
                    label: toolModel.resetRotationTip
                    onClicked: toolModel.resetRotation()
                }
                ToolIconButton {
                    iconName: "arrow-clockwise"
                    label: toolModel.rotateClockwiseTip
                    onClicked: toolModel.rotateClockwise()
                }
                ToolIconButton {
                    iconName: "flip-horizontal"
                    label: toolModel.flipCanvasTip
                    checkable: true
                    checked: toolModel.flipCanvas
                    onClicked: toolModel.flipCanvas = !toolModel.flipCanvas
                }
            }

            Item { width: 1; height: 6 }

            // ---- 3. the active tool's panel ---------------------------------
            ToolHeading {
                visible: toolModel.panelVisible
                title: toolModel.title
                detail: toolModel.modeName

                // THE MASK'S FIELD, as the heading's own controls. Clear lives here in
                // both states, so it never moves; the disclosure appears only when a card
                // pinned the field (then the heading IS the field's name and there is no
                // chooser row at all).
                ToolIconButton {
                    visible: toolModel.maskPickersVisible && toolModel.maskPinned
                    iconName: "caret-down"
                    iconSize: 14
                    label: qsTr("Change what is painted")
                    onClicked: toolModel.unpinMaskTarget()
                }
                ToolTextAction {
                    visible: toolModel.maskPickersVisible
                    text: qsTr("Clear")
                    tip: qsTr("Clear the field being painted (one undo step)")
                    onClicked: toolModel.requestClearMask()
                }
            }
            Column {
                id: panel
                width: parent.width
                visible: toolModel.panelVisible

                ToolModeRow {
                    visible: toolModel.modes.length > 0
                    modes: toolModel.modes
                    current: toolModel.modeIndex
                    onChosen: (index) => toolModel.setMode(index)
                }

                // ---- SR transform: the key-creation toggles -----------------
                // "Transform" mode offers one toggle per channel it may create, and the
                // "Move Centroid" mode offers the centroid offset instead - which is why
                // the four rows are one data-driven set with a family flag rather than
                // four hand-bound checkboxes.
                Repeater {
                    model: toolModel.srtChecks
                    delegate: ToolCheckRow {
                        visible: modelData.key === "centroid" ? toolModel.srtCentroidVisible
                                                             : toolModel.srtCreateVisible
                        text: modelData.label
                        tip: modelData.tip
                        value: toolModel.srtCheckValues[index]
                        onToggled: (checked) => toolModel.setSrtCheck(index, checked)
                    }
                }

                // ---- pose: the drawing weight ------------------------------
                ToolSliderRow {
                    visible: toolModel.weightVisible
                    label: toolModel.weightLabel
                    from: 0
                    to: toolModel.percentMax
                    step: 1
                    decimals: 0
                    suffix: "%"
                    value: toolModel.weight * 100
                    onValueEdited: (value) => toolModel.weight = value / 100
                }

                // ---- FFD: the pencil's smoothing ---------------------------
                ToolSliderRow {
                    visible: toolModel.blurVisible
                    label: toolModel.blurLabel
                    from: 0
                    to: toolModel.percentMax
                    step: 1
                    decimals: 0
                    suffix: "%"
                    value: toolModel.blur * 100
                    onValueEdited: (value) => toolModel.blur = value / 100
                }

                // ---- mask: WHICH field --------------------------------------
                // ONE chooser, and only when the field is still a QUESTION. Entered from
                // a card the heading already names the field (a filter's "Paint mask", the
                // mask card's Paint) and this row is not drawn at all - the first version
                // asked "Mask" and then "Channel", which named one field in two steps and
                // asked a question the card had answered.
                Item {
                    visible: toolModel.maskPickersVisible && !toolModel.maskPinned
                    width: parent.width
                    height: 26

                    AeComboBox {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        height: 23
                        textFont: uiFont
                        options: toolModel.maskFields
                        index: toolModel.maskFieldIndex
                        onActivated: (index) => toolModel.setMaskField(index)
                    }
                }

                // WHAT the brush paints toward. The eyedropper (Alt + click on the
                // canvas) samples it, and the swap key flips it between the two ends -
                // both in the row's tooltip, because a keyboard or mouse gesture spelled
                // out on its own line is what made this panel busy.
                ToolSliderRow {
                    visible: toolModel.maskPickersVisible
                    label: qsTr("Value")
                    tip: toolModel.maskValueHint
                    from: 0
                    to: toolModel.percentMax
                    step: 1
                    decimals: 0
                    suffix: "%"
                    value: toolModel.maskValue * 100
                    onValueEdited: (value) => toolModel.maskValue = value / 100
                }

                Item { width: 1; height: toolModel.brushVisible ? 2 : 0 }

                ToolBrushSection {
                    visible: toolModel.brushVisible
                }

            }

            Item { width: 1; height: 12 }
        }
    }
}
