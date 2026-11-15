pragma Translator: "AnimeEffects.Properties"

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The Properties dock, now a Qt Quick scene. The scene owns layout and ink; every
// document decision lives in gui::InspectorViewModel, which is the same
// KeyAccessor/TimeLineUtil path the widget inspector used.
Rectangle {
    id: root

    color: theme.base

    // THE MESH SPACING PREVIEW. Hover arms the overlay after the same delay as
    // the field's tooltip; a scrub or a text edit shows it immediately. It hides
    // immediately when none of hover/edit/scrub holds: this is a live view of the
    // control, not a message, so it must not inherit the tooltip's 10 s timeout.
    property bool meshPreviewArmed: false
    function refreshMeshPreview() {
        const field = meshSpacingField;
        const active = inspector.image.visible && field.enabled
                    && (field.editing || field.scrubbing
                        || (root.meshPreviewArmed && field.hovered));
        inspector.setMeshPreviewActive(active);
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
            topPadding: 5
            spacing: 0
            visible: inspector.hasTarget

            // ---- constants -------------------------------------------------
            InspectorRow {
                label: inspector.blendLabel
                keyState: ""
                enabled: !inspector.playbackActive
                AeComboBox {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 23
                    options: inspector.blendOptions
                    index: inspector.blendIndex
                    textFont: uiFont
                                enabled: inspector.blendEnabled
                    onActivated: (index) => inspector.setBlendIndex(index)
                }
            }
            InspectorRow {
                label: inspector.clippedLabel
                keyState: ""
                enabled: !inspector.playbackActive
                AeCheckBox {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 23
                    checked: inspector.clipped
                    textFont: uiFont
                                enabled: inspector.clippedEnabled
                    Accessible.name: inspector.clippedLabel
                    onToggled: inspector.setClipped(checked)
                }
            }
            Item { width: 1; height: 4 }

            // ---- transform -------------------------------------------------
            InspectorRow {
                visible: inspector.move.visible
                enabled: inspector.move.enabled
                label: inspector.move.label
                keyState: inspector.move.keyState
                keyColor: inspector.move.keyColor
                keyEnabled: inspector.move.keyEnabled
                onToggleKey: inspector.move.toggleKey()
                InspectorNumberField {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 84
                    row: inspector.move
                    channel: 0
                    value: inspector.move.x
                    from: inspector.translateMin
                    to: inspector.translateMax
                    step: 1
                    label: qsTr("Position X")
                    prefix: "X"
                    onValueEdited: (value) => inspector.move.setX(value)
                }
                InspectorNumberField {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 84
                    row: inspector.move
                    channel: 1
                    value: inspector.move.y
                    from: inspector.translateMin
                    to: inspector.translateMax
                    step: 1
                    label: qsTr("Position Y")
                    prefix: "Y"
                    onValueEdited: (value) => inspector.move.setY(value)
                }
            }
            InspectorRow {
                visible: inspector.move.visible
                enabled: inspector.move.enabled
                label: inspector.centroidLabel
                keyState: ""
                InspectorNumberField {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 84
                    row: inspector.move
                    channel: 2
                    value: inspector.move.centroidX
                    from: inspector.translateMin
                    to: inspector.translateMax
                    step: 1
                    label: inspector.centroidLabel + " X"
                    prefix: "X"
                    onValueEdited: (value) => inspector.move.setCentroidX(value)
                }
                InspectorNumberField {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 84
                    row: inspector.move
                    channel: 3
                    value: inspector.move.centroidY
                    from: inspector.translateMin
                    to: inspector.translateMax
                    step: 1
                    label: inspector.centroidLabel + " Y"
                    prefix: "Y"
                    onValueEdited: (value) => inspector.move.setCentroidY(value)
                }
            }
            InspectorRow {
                visible: inspector.move.visible
                enabled: inspector.move.enabled
                label: inspector.pathLabel
                keyState: ""
                AeCheckBox {
                    id: smoothPath
                    Layout.fillWidth: true
                    Layout.preferredHeight: 23
                    text: inspector.smoothLabel
                    textFont: uiFont
                                checked: inspector.move.smooth
                    onToggled: inspector.move.setSmooth(checked)
                    AeToolTip {
                        ownerItem: smoothPath
                        text: inspector.smoothTooltip
                        shown: smoothPath.hovered
                    }
                }
            }
            Item { width: 1; height: inspector.move.visible ? 4 : 0 }

            InspectorRow {
                visible: inspector.rotate.visible
                enabled: inspector.rotate.enabled
                label: inspector.rotate.label
                keyState: inspector.rotate.keyState
                keyColor: inspector.rotate.keyColor
                keyEnabled: inspector.rotate.keyEnabled
                onToggleKey: inspector.rotate.toggleKey()
                InspectorNumberField {
                    Layout.fillWidth: true
                    row: inspector.rotate
                    channel: 0
                    value: inspector.rotate.angle
                    from: inspector.rotateMin
                    to: inspector.rotateMax
                    step: 1
                    label: inspector.rotate.label
                    prefixIcon: "angle"
                    suffix: "°"
                    onValueEdited: (value) => inspector.rotate.setAngle(value)
                }
            }
            Item { width: 1; height: inspector.rotate.visible ? 4 : 0 }
            InspectorRow {
                visible: inspector.scale.visible
                enabled: inspector.scale.enabled
                label: inspector.scale.label
                keyState: inspector.scale.keyState
                keyColor: inspector.scale.keyColor
                keyEnabled: inspector.scale.keyEnabled
                onToggleKey: inspector.scale.toggleKey()
                InspectorNumberField {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 84
                    row: inspector.scale
                    channel: 0
                    value: inspector.scale.scaleX
                    from: inspector.scaleMin
                    to: inspector.scaleMax
                    step: 0.1
                    label: qsTr("Scale X")
                    prefixIcon: "arrows-horizontal"
                    onValueEdited: (value) => inspector.scale.setScaleX(value)
                }
                InspectorNumberField {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 84
                    row: inspector.scale
                    channel: 1
                    value: inspector.scale.scaleY
                    from: inspector.scaleMin
                    to: inspector.scaleMax
                    step: 0.1
                    label: qsTr("Scale Y")
                    prefixIcon: "arrows-vertical"
                    onValueEdited: (value) => inspector.scale.setScaleY(value)
                }
            }
            Item { width: 1; height: inspector.scale.visible ? 4 : 0 }
            InspectorRow {
                visible: inspector.depth.visible
                enabled: inspector.depth.enabled
                label: inspector.depth.label
                keyState: inspector.depth.keyState
                keyColor: inspector.depth.keyColor
                keyEnabled: inspector.depth.keyEnabled
                onToggleKey: inspector.depth.toggleKey()
                InspectorNumberField {
                    Layout.fillWidth: true
                    row: inspector.depth
                    channel: 0
                    value: inspector.depth.depth
                    from: inspector.translateMin
                    to: inspector.translateMax
                    step: 1
                    label: inspector.depth.label
                    prefixIcon: "stack"
                    onValueEdited: (value) => inspector.depth.setDepth(value)
                }
            }
            Item { width: 1; height: inspector.depth.visible ? 4 : 0 }
            InspectorRow {
                visible: inspector.opacity.visible
                enabled: inspector.opacity.enabled
                label: inspector.opacity.label
                keyState: inspector.opacity.keyState
                keyColor: inspector.opacity.keyColor
                keyEnabled: inspector.opacity.keyEnabled
                onToggleKey: inspector.opacity.toggleKey()
                InspectorNumberField {
                    Layout.fillWidth: true
                    row: inspector.opacity
                    channel: 0
                    value: inspector.opacity.opacity
                    from: 0
                    to: 1
                    step: 0.05
                    decimals: 3
                    label: inspector.opacity.label
                    prefixIcon: "circle-half"
                    onValueEdited: (value) => inspector.opacity.setOpacity(value)
                }
            }
            Item { width: 1; height: inspector.opacity.visible ? 4 : 0 }

            // ---- image -----------------------------------------------------
            InspectorRow {
                visible: inspector.image.visible
                enabled: inspector.image.enabled
                label: inspector.image.label
                keyState: inspector.image.keyState
                keyColor: inspector.image.keyColor
                keyEnabled: inspector.image.keyEnabled
                onToggleKey: inspector.image.toggleKey()
                InspectorAssetField {
                    Layout.fillWidth: true
                    name: inspector.image.assetName
                    onClicked: inspector.browseImageAsset()
                }
            }
            InspectorRow {
                visible: inspector.image.visible
                enabled: inspector.image.enabled
                label: inspector.centerLabel
                keyState: ""
                InspectorNumberField {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 84
                    value: inspector.image.centerX
                    from: inspector.translateMin
                    to: inspector.translateMax
                    step: 1
                    label: qsTr("Image center X")
                    prefix: "X"
                    row: inspector.image
                    channel: 0
                    onValueEdited: (value) => inspector.image.setCenterX(value)
                }
                InspectorNumberField {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 84
                    value: inspector.image.centerY
                    from: inspector.translateMin
                    to: inspector.translateMax
                    step: 1
                    label: qsTr("Image center Y")
                    prefix: "Y"
                    row: inspector.image
                    channel: 1
                    onValueEdited: (value) => inspector.image.setCenterY(value)
                }
            }
            InspectorRow {
                visible: inspector.image.visible
                enabled: inspector.image.enabled
                label: inspector.meshCellLabel
                keyState: ""
                InspectorNumberField {
                    id: meshSpacingField
                    Layout.fillWidth: true
                    value: inspector.image.cellSize
                    from: inspector.imageCellMin
                    to: inspector.imageCellMax
                    step: 1
                    decimals: 0
                    label: inspector.meshCellLabel
                    prefixIcon: "grid-four"
                    AeToolTip {
                        ownerItem: meshSpacingField
                        text: inspector.meshCellTooltip
                        shown: meshSpacingField.hovered
                    }
                    onValueEdited: (value) => inspector.image.setCellSize(Math.round(value))
                }
            }
            Item { width: 1; height: inspector.image.visible ? 4 : 0 }

            // ---- structural / optional ------------------------------------
            InspectorRow {
                visible: inspector.pose.visible
                enabled: inspector.pose.enabled
                label: inspector.pose.label
                keyState: inspector.pose.keyState
                keyColor: inspector.pose.keyColor
                keyEnabled: inspector.pose.keyEnabled
                onToggleKey: inspector.pose.toggleKey()
            }
            Item { width: 1; height: inspector.pose.visible ? 4 : 0 }
            InspectorRow {
                visible: inspector.ffd.visible
                enabled: inspector.ffd.enabled
                label: inspector.ffd.label
                keyState: inspector.ffd.keyState
                keyColor: inspector.ffd.keyColor
                keyEnabled: inspector.ffd.keyEnabled
                onToggleKey: inspector.ffd.toggleKey()
            }
            Item { width: 1; height: inspector.ffd.visible ? 4 : 0 }

            // ---- effects --------------------------------------------------
            InspectorFilterStack {
                id: filterStack
                visible: inspector.hasTarget
                enabled: !inspector.playbackActive
                width: parent.width
            }

            InspectorMaskCard {
                id: maskCard
                visible: inspector.hasTarget
                enabled: !inspector.playbackActive
                width: parent.width
            }

            Item { width: 1; height: 12 }
        }
    }

    Timer {
        id: meshPreviewTimer
        interval: theme.toolTipDelay
        repeat: false
        onTriggered: {
            root.meshPreviewArmed = true;
            root.refreshMeshPreview();
        }
    }
    Connections {
        target: meshSpacingField
        function onHoveredChanged() {
            if (meshSpacingField.hovered) {
                if (meshSpacingField.editing || meshSpacingField.scrubbing) {
                    root.meshPreviewArmed = true;
                    root.refreshMeshPreview();
                } else {
                    meshPreviewTimer.restart();
                }
            } else {
                meshPreviewTimer.stop();
                root.meshPreviewArmed = false;
                root.refreshMeshPreview();
            }
        }
        function onScrubbingChanged() {
            if (meshSpacingField.scrubbing) {
                root.meshPreviewArmed = true;
                meshPreviewTimer.stop();
            }
            root.refreshMeshPreview();
        }
        function onEditingChanged() {
            if (meshSpacingField.editing) {
                root.meshPreviewArmed = true;
                meshPreviewTimer.stop();
            }
            root.refreshMeshPreview();
        }
    }
    Connections {
        // A target change is the inspector's `changed`; the field itself can stay
        // alive across nodes, so the preview must follow the row's visibility.
        target: inspector
        function onChanged() {
            if (!inspector.image.visible) {
                meshPreviewTimer.stop();
                root.meshPreviewArmed = false;
            }
            root.refreshMeshPreview();
        }
    }
    Component.onDestruction: inspector.setMeshPreviewActive(false)

}
