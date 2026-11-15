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
//   2. the ACTIVE TOOL's panel: its heading names the tool and the trailing word names
//      the mode, then the mode row, the tool's own rows, and the shared brush.
//
// The view settings that used to sit between those two blocks are WINDOW state, not
// tool state: Show mesh, the cut-outside-frame mask, the canvas rotations and Flip
// canvas describe the viewport, not the tool in hand. They live in the window header
// now (qml/HeaderBar.qml), so they are reachable without opening the Tools dock and
// cannot move when the tool changes. The dock is the TOOL.
//
// The whole content is a ScrollView: the mask tool's panel (target, channel, value,
// four brush rows, two switches, Clear) is taller than a dock on a small window, and the
// widget version simply clipped it - there was no scrollbar anywhere in this dock.
Rectangle {
    id: root

    color: theme.base

    // THE PANEL IS STRUCTURAL, THE VALUES ARE NOT. A row appearing/disappearing
    // needs a fresh subtree because a Column/Flow does not re-layout a child that
    // was invisible when it was built. Read the row VISIBILITY flags into one key
    // (never a value: a slider drag must not rebuild the panel), and rebuild the
    // Loader when it moves.
    property string panelKey: [
        toolModel.panelVisible, toolModel.tool, toolModel.modeIndex,
        toolModel.brushVisible, toolModel.opacityVisible, toolModel.blurVisible,
        toolModel.weightVisible, toolModel.weightHeatmapVisible,
        toolModel.srtCreateVisible, toolModel.srtCentroidVisible,
        toolModel.weightsStateVisible, toolModel.weightsActionsVisible,
        toolModel.weightsBakeVisible, toolModel.weightsRebakeVisible,
        toolModel.maskPickersVisible, toolModel.maskFields.length,
        toolModel.maskPaintable
    ].join("|")
    onPanelKeyChanged: reloadPanel()
    function reloadPanel() {
        if (!toolPanelLoader.active)
            return;
        toolPanelLoader.active = false;
        Qt.callLater(function() { toolPanelLoader.active = toolModel.panelVisible; });
    }

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

            Item { width: 1; height: 10 }

            // THE PANEL IS CREATED WHEN A TOOL IS PICKED, not hidden and shown:
            // a Column does not re-run its layout when an invisible child appears,
            // so a panel built with no tool kept every row at y=0. The Loader's
            // sentinel height keeps it in the outer Column's layout while inactive;
            // its content is then built with the real mode row already populated.
            Loader {
                id: toolPanelLoader
                width: parent.width
                height: active && item ? item.implicitHeight : 0.001
                active: toolModel.panelVisible
                sourceComponent: Component {
                    Column {
                        width: toolPanelLoader.width
                    // ---- 2. the active tool's panel ---------------------------------
                    ToolHeading {
                        visible: toolModel.panelVisible
                        title: toolModel.title
                        detail: toolModel.modeName

                        // Clear lives here in both states, so it never moves.
                        ToolTextAction {
                            visible: toolModel.maskPickersVisible
                            enabled: toolModel.maskPaintable
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

                        // ---- paint weights: the authored field's state and actions ----
                        // A conventional stat row: the label on the dock's grid, the state beside
                        // it, and the count on a muted second line. The verbs are a real primary
                        // button plus the overflow menu; the explanations are tooltips, not
                        // paragraphs (a dock is not a manual).
                        Item {
                            id: weightsPanel
                            visible: toolModel.weightsStateVisible
                            width: parent.width
                            // A plain Item does not follow its implicitHeight, and the Column
                            // below decides the block's height.
                            height: weightsStateColumn.implicitHeight + 10

                            Text {
                                id: weightsLabel
                                x: 12
                                anchors.top: parent.top
                                anchors.topMargin: 7
                                text: qsTr("Weights")
                                color: theme.text
                                font: uiFont
                                Accessible.role: Accessible.StaticText
                                Accessible.name: text
                            }

                            Column {
                                id: weightsStateColumn
                                x: 90
                                width: parent.width - 90 - 16
                                anchors.top: parent.top
                                anchors.topMargin: 5
                                spacing: 1

                                Text {
                                    id: weightsStateValue
                                    width: parent.width
                                    text: toolModel.weightsStateText
                                    color: toolModel.weightsStateRole === "mixed" ? theme.warning
                                         : (toolModel.weightsStateRole === "baked"
                                            || toolModel.weightsStateRole === "envelope") ? theme.text
                                         : theme.textMuted
                                    font: uiFont
                                    elide: Text.ElideRight
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }
                                Text {
                                    id: weightsMeshWarning
                                    width: parent.width
                                    visible: toolModel.weightsMeshWarningVisible
                                    text: qsTr("Mesh changed since bake")
                                    color: theme.warning
                                    font.pixelSize: 10
                                    wrapMode: Text.WordWrap
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }
                                Text {
                                    id: weightsErrorText
                                    width: parent.width
                                    visible: text !== ""
                                    text: toolModel.weightsError
                                    color: theme.warning
                                    font.pixelSize: 10
                                    wrapMode: Text.WordWrap
                                    Accessible.role: Accessible.StaticText
                                    Accessible.name: text
                                }
                            }

                            HoverHandler { id: weightsHover }
                            AeToolTip {
                                ownerItem: weightsPanel
                                text: toolModel.weightsStateTip
                                shown: weightsHover.hovered && toolModel.weightsStateTip !== ""
                            }
                        }

                        // ONE primary verb and the maintenance verbs behind the ellipsis. The
                        // button is the inspector's action shape (height 22, full width) so the
                        // dock has one action look; a row of bare words was not a control.
                        RowLayout {
                            id: weightsActions
                            visible: toolModel.weightsActionsVisible
                            x: 12
                            width: parent.width - 28
                            height: 28
                            spacing: 6

                            AeButton {
                                id: weightsPrimary
                                Layout.fillWidth: true
                                height: 22
                                textFont: uiFont
                                // Envelope/Mixed: Bake fills the live nodes. Baked: Rebake is the
                                // primary verb. Mixed also offers Rebake in the menu.
                                text: toolModel.weightsBakeVisible ? qsTr("Bake") : qsTr("Rebake")
                                enabled: toolModel.weightsBakeVisible ? toolModel.weightsBakeEnabled
                                                                     : toolModel.weightsRebakeEnabled
                                onClicked: toolModel.requestWeightsAction(toolModel.weightsBakeVisible ? 0 : 1)

                                property bool rowHovered: hovered
                                AeToolTip {
                                    ownerItem: weightsPrimary
                                    shown: weightsPrimary.rowHovered
                                    text: toolModel.weightsBakeVisible
                                          ? qsTr("Evaluate the selected bone's live envelope once, so the brush can paint it")
                                          : qsTr("Replace the selected bone's authored field with the current envelope (one undo step)")
                                }
                            }

                            ToolMenu {
                                visible: toolModel.weightsRebakeVisible
                                // The menu mirrors the actions that are NOT the primary verb: on
                                // Mixed the primary is Bake, so Rebake lives here; on Baked Rebake
                                // is the primary and only the two maintenance verbs remain.
                                items: {
                                    let rows = [];
                                    if (toolModel.weightsBakeVisible) {
                                        rows.push({ label: qsTr("Rebake All"), action: 1,
                                                    enabled: toolModel.weightsRebakeEnabled });
                                    }
                                    rows.push({ label: qsTr("Revert to Envelope"), action: 2,
                                                enabled: toolModel.weightsRevertEnabled });
                                    rows.push({ label: qsTr("Clear Weights"), action: 3,
                                                enabled: toolModel.weightsClearEnabled });
                                    return rows;
                                }
                                label: qsTr("More weight actions")
                                onTriggered: (action) => toolModel.requestWeightsAction(action)
                            }
                        }

                        // WHAT a stamp does to the value it covers. The brush's own Size and
                        // Hardness are below; the envelope brush's target Value sits with the
                        // brush rows, and the weight brush has three deposit rules.
                        ToolRow {
                            visible: toolModel.weightsStateVisible
                            label: qsTr("Paint")
                            AeComboBox {
                                Layout.fillWidth: true
                                Layout.preferredHeight: theme.toolFieldHeight
                                textFont: uiFont
                                options: toolModel.weightModes
                                index: toolModel.weightPaintMode
                                // A stable name for a reader/probe: the combo's own name is the
                                // current VALUE ("Add"), which changes as the mode does.
                                Accessible.name: qsTr("Paint mode")
                                onActivated: (index) => toolModel.weightPaintMode = index
                            }
                        }

                        // The bone-scoped brush needs to say how a bone becomes the target:
                        // left is always the stroke, so the target is chosen with the other
                        // button (or, for the first stroke, by the nearest bone).
                        Text {
                            visible: toolModel.weightPaintHintVisible
                            x: 12
                            width: parent.width - 28
                            text: qsTr("Right-click a bone to target it; the brush edits only that bone")
                            color: theme.textMuted
                            font: uiFont
                            wrapMode: Text.WordWrap
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
                        // ONE conventional Target row. The options are DERIVED from this node
                        // and frame, not a catalog: a lane the layer does not have, a disabled
                        // filter, a time-offset field no filter consumes, or a matte between
                        // keys is never offered. When nothing is paintable the explanation below
                        // replaces the Value/brush rows, so a stroke can never silently no-op.
                        ToolRow {
                            visible: toolModel.maskPickersVisible && toolModel.maskFields.length > 0
                            label: qsTr("Target")
                            AeComboBox {
                                objectName: "maskTargetCombo"
                                Layout.fillWidth: true
                                // Use the row's own 24px field height. In a Layout,
                                // `height` does not size the item: the combo kept its
                                // 28px dialog implicit height and stood 5px taller than
                                // the fields under it.
                                Layout.preferredHeight: theme.toolFieldHeight
                                textFont: uiFont
                                options: toolModel.maskFields
                                index: toolModel.maskFieldIndex
                                Accessible.name: qsTr("Mask target")
                                onActivated: (index) => toolModel.setMaskField(index)
                            }
                        }
                        Text {
                            visible: toolModel.maskPickersVisible && !toolModel.maskPaintable
                            x: 12
                            width: parent.width - 28
                            text: toolModel.maskEmptyText
                            color: theme.textMuted
                            font: uiFont
                            wrapMode: Text.WordWrap
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }

                        // WHAT the brush paints toward. The eyedropper (Alt + click on the
                        // canvas) samples it, and the swap key flips it between the two ends -
                        // both in the row's tooltip, because a keyboard or mouse gesture spelled
                        // out on its own line is what made this panel busy.
                        ToolSliderRow {
                            visible: toolModel.maskPickersVisible && toolModel.maskPaintable
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
                                && (!toolModel.maskPickersVisible || toolModel.maskPaintable)
                            // The weight brush is shown but disabled on a live bone; the
                            // state panel above explains the one action that unlocks it.
                            enabled: toolModel.brushEnabled
                        }

                        // The influence modes' second readout: the envelope polygons show
                        // WHERE a bone reaches, this shows how strongly (and only the
                        // influence modes have a bone to read).
                        ToolCheckRow {
                            visible: toolModel.weightHeatmapVisible
                            text: qsTr("Show weights")
                            tip: qsTr("Colour the layers this bone deforms by its influence weight")
                            value: toolModel.weightHeatmap
                            onToggled: (checked) => toolModel.weightHeatmap = checked
                        }

                    }
                    }
                }
            }

            Item { width: 1; height: 12 }
        }
    }
}
