pragma Translator: "AnimeEffects.Properties"

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One filter is one card. Its header is the row; expanding it grows the card
// under that same row instead of moving the settings into a detached editor.
Item {
    id: root

    // A delegate is torn down while its model role is cleared; every binding
    // below then runs once against an undefined object and writes a TypeError to
    // the log. The empty row keeps that teardown silent without hiding a real
    // missing role (the delegate always assigns the actual row in FilterStack).
    QtObject {
        id: emptyRow
        property string kind: ""
        property string name: ""
        property bool enabled: true
        property bool expanded: false
        property string keyState: ""
        property color keyColor: "transparent"
        property string summary: ""
        property int hue: 0
        property int saturation: 100
        property int value: 100
        property bool absolute: false
        property double amount: 0
        property bool directional: false
        property double blurX: 0
        property double blurY: 0
        property double blurAngle: 0
        property double waveStrength: 0
        property double waveWavelength: 140
        property double waveDirection: 0
        property double wavePhase: 0
        property double waveMix: 0.35
        property double waveSpeed: 0.4
        property double waveCrest: 1
        property double waveDualAngle: 62
        property double waveDualScale: 0.61
        property bool waveRipple: true
        property bool hasAmountMask: false
        property bool hasTimeMask: false
        property string maskLabel: ""
        property string timeMaskLabel: ""
        property string paintMaskLabel: ""
        property string clearMaskLabel: ""
        property string paintTimeMaskLabel: ""
        property string clearTimeMaskLabel: ""
        function toggleExpanded() {}
        function toggleKey() {}
        function toggleEnabled() {}
        function remove() {}
        function setHue(v) {}
        function setSaturation(v) {}
        function setValue(v) {}
        function setAbsolute(v) {}
        function setAmount(v) {}
        function setDirectional(v) {}
        function setBlurX(v) {}
        function setBlurY(v) {}
        function setBlurAngle(v) {}
        function setWaveStrength(v) {}
        function setWaveWavelength(v) {}
        function setWaveDirection(v) {}
        function setWavePhase(v) {}
        function setWaveMix(v) {}
        function setWaveSpeed(v) {}
        function setWaveCrest(v) {}
        function setWaveDualAngle(v) {}
        function setWaveDualScale(v) {}
        function setWaveRipple(v) {}
        function paintMask(c) {}
        function clearMask(c) {}
    }
    property var filterRow: emptyRow
    property int cardIndex: 0
    readonly property int cardRadius: 6
    signal dragStarted(int index)
    signal dragMoved(int index, real sceneY)
    signal dragEnded(int index, real sceneY)

    // The summary stays ONE line of one size. Only the axis/field labels are
    // dimmer; values keep the original muted summary ink.
    function summaryHtml() {
        const segments = filterRow.summarySegments;
        if (!segments || segments.length === 0)
            return filterRow.summary;
        const parts = [];
        for (let i = 0; i < segments.length; ++i) {
            const segment = segments[i];
            parts.push('<font color="' + String(theme.textMutedDim) + '">'
                       + segment.label + '</font> ' + segment.value);
        }
        // StyledText does not give U+2003 usable width here; use explicit
        // non-breaking spaces so item gaps are visibly wider than the one
        // ordinary space between a label and its value.
        return parts.join("&nbsp;&nbsp;&nbsp;");
    }

    width: parent ? parent.width : 0
    // Expansion may change INK, never GEOMETRY: the height is the two parts and
    // nothing else. The old formula added 2 while expanded because both children
    // were inset by the frame's 1px stroke; the children now sit at 0 and the
    // stroke is painted OVER them, so there is nothing left to reserve.
    height: headerItem.height + body.height
    Accessible.role: Accessible.Grouping
    Accessible.name: filterRow.name

    // One card, one quiet outline. Painting follows declaration order, and this
    // frame is declared first, so it carries z: 1 to land OVER the header and
    // body fills - otherwise the stroke sat under them and only the ring's outer
    // half survived. A Rectangle without a MouseArea does not take pointer
    // events, so the header's drag and click still reach it.
    Rectangle {
        id: cardFrame
        anchors.fill: parent
        visible: filterRow.expanded
        z: 1
        radius: root.cardRadius
        color: "transparent"
        border.width: 1
        border.color: theme.hairline
    }

    Rectangle {
        id: headerItem
        x: 0
        y: 0
        width: parent.width
        height: 30
        // The corners are the CARD's radius in both states so the arcs meet the
        // frame's stroke; a smaller inner radius was what made expanding look
        // like the header shrank into the border.
        radius: root.cardRadius
        color: filterRow.expanded || headerMouse.containsMouse ? theme.raised : "transparent"

        // Expanded: the header's top corners stay rounded, its bottom edge
        // becomes square so it joins the body as one card. Without this patch
        // the radius leaves a second rounded edge over the body.
        Rectangle {
            visible: filterRow.expanded
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: root.cardRadius
            color: headerItem.color
        }

        MouseArea {
            id: headerMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            // A filter drag is vertical and the panel is a Flickable; without
            // this the page steals the gesture after a few pixels.
            preventStealing: true
            property real pressY: 0
            property bool dragging: false
            onPressed: (mouse) => {
                pressY = mouse.y;
                dragging = false;
            }
            onPositionChanged: (mouse) => {
                if (!pressed) return;
                if (!dragging && Math.abs(mouse.y - pressY) > 4) {
                    dragging = true;
                    root.dragStarted(root.cardIndex);
                }
                if (dragging)
                    root.dragMoved(root.cardIndex, headerItem.mapToItem(null, mouse.x, mouse.y).y);
            }
            onReleased: (mouse) => {
                if (dragging)
                    root.dragEnded(root.cardIndex, headerItem.mapToItem(null, mouse.x, mouse.y).y);
                else
                    filterRow.toggleExpanded();
                dragging = false;
            }
            onCanceled: {
                if (dragging)
                    root.dragEnded(root.cardIndex, headerItem.mapToItem(null, width / 2, height / 2).y);
                dragging = false;
            }
        }

        // THE CARD CAN BE EXPANDED WITHOUT A MOUSE. The header is a MouseArea, so it is
        // not a keyboard stop and has no accessibility protocol of its own; this marker
        // (see gui/qml/QmlFormAccessibility) gives it a Press action and a rectangle, so
        // a screen reader - and scripts/gui/ae_ui.py - can expand and collapse a card.
        // It carries no pointer handlers, so hover, the drag-reorder gesture and the
        // click pass through to headerMouse below.
        Item {
            objectName: "aeAccessibleDisclosure"
            anchors.fill: parent
            property string accessibleName: filterRow.expanded
                                            ? qsTr("Collapse %1").arg(filterRow.name)
                                            : qsTr("Expand %1").arg(filterRow.name)
            property string accessibleValue: filterRow.expanded
                                             ? qsTr("Expanded") : qsTr("Collapsed")
            function accessiblePress() { filterRow.toggleExpanded() }
            Accessible.role: Accessible.Button
            Accessible.name: accessibleName
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 6
            spacing: 4

            Image {
                Layout.preferredWidth: 14
                Layout.preferredHeight: 14
                source: theme.iconUrl(filterRow.expanded ? "caret-down" : "caret-right")
                sourceSize.width: 64
                sourceSize.height: 64
                fillMode: Image.PreserveAspectFit
                opacity: headerMouse.containsMouse ? 0.9 : 0.5
            }

            AeIconButton {
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                size: 20
                iconSize: 14
                iconName: filterRow.enabled ? "eye-open" : "eye-close"
                label: filterRow.enabled ? qsTr("Disable %1").arg(filterRow.name)
                                         : qsTr("Enable %1").arg(filterRow.name)
                showTip: true
                opacity: headerMouse.containsMouse || filterRow.expanded ? 1.0 : 0.5
                onClicked: filterRow.toggleEnabled()
            }

            InspectorKeyDot {
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                state: filterRow.keyState
                ink: filterRow.keyColor
                label: filterRow.name
                onClicked: filterRow.toggleKey()
            }

            Text {
                text: filterRow.name
                color: filterRow.enabled ? theme.text : theme.textDisabled
                font.family: uiFont.family
                font.pointSize: uiFont.pointSize
                font.bold: true
                elide: Text.ElideRight
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 20

                Text {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.StyledText
                    text: root.summaryHtml()
                    color: theme.textMuted
                    font: uiFont
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                }
            }

            AeIconButton {
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                size: 20
                iconSize: 16
                iconName: "trash"
                label: qsTr("Remove %1").arg(filterRow.name)
                showTip: true
                // Always present so revealing it cannot reflow the header; it
                // brightens with the pointer and the expanded card.
                opacity: headerMouse.containsMouse || filterRow.expanded ? 0.9 : 0.45
                onClicked: filterRow.remove()
            }
        }
    }

    Rectangle {
        id: body
        // Same rule as the header: the body is full width at 0 in both states.
        // Its only expansion-dependent property is the height that reveals it.
        x: 0
        y: headerItem.height
        width: parent.width
        height: filterRow.expanded ? bodyColumn.implicitHeight + 18 : 0
        visible: filterRow.expanded
        color: theme.base
        radius: root.cardRadius

        // Round the BOTTOM of the body only: the header joins it squarely and
        // the header's own bottom patch does the same at the junction. The patch
        // is as tall as the radius so it covers the arcs it has to square off.
        Rectangle {
            visible: filterRow.expanded
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: root.cardRadius
            color: body.color
        }

        Column {
            id: bodyColumn
            y: 8
            width: parent.width
            spacing: 0

            // HSV
            InspectorRow {
                visible: filterRow.kind === "hsv"
                label: filterRow.hueLabel
                InspectorNumberField {
                    Layout.fillWidth: true
                    value: filterRow.hue
                    from: 0
                    to: inspector.hsvHueMax
                    step: 1
                    decimals: 0
                    label: filterRow.hueLabel
                    prefix: "H"
                    onValueEdited: (value) => filterRow.setHue(Math.round(value))
                }
            }
            InspectorRow {
                visible: filterRow.kind === "hsv"
                label: filterRow.saturationLabel
                InspectorNumberField {
                    Layout.fillWidth: true
                    value: filterRow.saturation
                    from: 0
                    to: inspector.hsvComponentMax
                    step: 1
                    decimals: 0
                    label: filterRow.saturationLabel
                    prefix: "S"
                    onValueEdited: (value) => filterRow.setSaturation(Math.round(value))
                }
            }
            InspectorRow {
                visible: filterRow.kind === "hsv"
                label: filterRow.valueLabel
                InspectorNumberField {
                    Layout.fillWidth: true
                    value: filterRow.value
                    from: 0
                    to: inspector.hsvComponentMax
                    step: 1
                    decimals: 0
                    label: filterRow.valueLabel
                    prefix: "V"
                    onValueEdited: (value) => filterRow.setValue(Math.round(value))
                }
            }
            InspectorRow {
                visible: filterRow.kind === "hsv"
                label: filterRow.colorLabel
                AeCheckBox {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 23
                    text: filterRow.absoluteLabel
                    textFont: uiFont
                                checked: filterRow.absolute
                    onToggled: filterRow.setAbsolute(checked)
                }
            }

            // Blur
            InspectorRow {
                visible: filterRow.kind === "blur"
                label: filterRow.amountLabel
                InspectorNumberField {
                    Layout.fillWidth: true
                    value: filterRow.amount
                    from: 0
                    to: inspector.blurMax
                    step: 0.5
                    decimals: 1
                    label: qsTr("Blur amount")
                    onValueEdited: (value) => filterRow.setAmount(value)
                }
            }
            InspectorRow {
                visible: filterRow.kind === "blur"
                label: filterRow.modeLabel
                AeCheckBox {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 23
                    text: filterRow.directionalLabel
                    textFont: uiFont
                                checked: filterRow.directional
                    onToggled: filterRow.setDirectional(checked)
                }
            }
            InspectorRow {
                visible: filterRow.kind === "blur" && filterRow.directional
                label: filterRow.blurXLabel
                InspectorNumberField {
                    Layout.fillWidth: true
                    value: filterRow.blurX
                    from: 0
                    to: inspector.blurMax
                    step: 0.5
                    decimals: 1
                    label: filterRow.blurXLabel
                    prefix: "X"
                    onValueEdited: (value) => filterRow.setBlurX(value)
                }
            }
            InspectorRow {
                visible: filterRow.kind === "blur" && filterRow.directional
                label: filterRow.blurYLabel
                InspectorNumberField {
                    Layout.fillWidth: true
                    value: filterRow.blurY
                    from: 0
                    to: inspector.blurMax
                    step: 0.5
                    decimals: 1
                    label: filterRow.blurYLabel
                    prefix: "Y"
                    onValueEdited: (value) => filterRow.setBlurY(value)
                }
            }
            InspectorRow {
                visible: filterRow.kind === "blur" && filterRow.directional
                label: filterRow.angleLabel
                InspectorNumberField {
                    Layout.fillWidth: true
                    value: filterRow.blurAngle
                    from: inspector.blurAngleMin
                    to: inspector.blurAngleMax
                    step: 1
                    decimals: 1
                    label: qsTr("Blur angle")
                    prefixIcon: "angle"
                    suffix: "°"
                    onValueEdited: (value) => filterRow.setBlurAngle(value)
                }
            }

            // Wave Deform. The five graph channels first (they are what the
            // animation graph draws), then the shape decisions that are
            // step-held between keys - the same split core::WaveKey documents.
            InspectorRow {
                visible: filterRow.kind === "wave"
                label: filterRow.strengthLabel
                InspectorNumberField {
                    Layout.fillWidth: true
                    value: filterRow.waveStrength
                    from: inspector.waveStrengthMin
                    to: inspector.waveStrengthMax
                    step: 0.5
                    decimals: 1
                    label: qsTr("Wave strength")
                    onValueEdited: (value) => filterRow.setWaveStrength(value)
                }
            }
            InspectorRow {
                visible: filterRow.kind === "wave"
                label: filterRow.wavelengthLabel
                InspectorNumberField {
                    Layout.fillWidth: true
                    value: filterRow.waveWavelength
                    from: inspector.waveWavelengthMin
                    to: inspector.waveWavelengthMax
                    step: 1
                    decimals: 0
                    label: qsTr("Wave wavelength")
                    onValueEdited: (value) => filterRow.setWaveWavelength(value)
                }
            }
            InspectorRow {
                visible: filterRow.kind === "wave"
                label: filterRow.directionLabel
                InspectorNumberField {
                    Layout.fillWidth: true
                    value: filterRow.waveDirection
                    from: inspector.blurAngleMin
                    to: inspector.blurAngleMax
                    step: 1
                    decimals: 1
                    label: qsTr("Wave direction")
                    prefixIcon: "angle"
                    suffix: "\u00b0"
                    onValueEdited: (value) => filterRow.setWaveDirection(value)
                }
            }
            InspectorRow {
                visible: filterRow.kind === "wave"
                label: filterRow.phaseLabel
                InspectorNumberField {
                    Layout.fillWidth: true
                    value: filterRow.wavePhase
                    from: inspector.wavePhaseMin
                    to: inspector.wavePhaseMax
                    step: 0.05
                    decimals: 2
                    label: qsTr("Wave phase")
                    onValueEdited: (value) => filterRow.setWavePhase(value)
                }
            }
            InspectorRow {
                visible: filterRow.kind === "wave"
                label: filterRow.mixLabel
                InspectorNumberField {
                    Layout.fillWidth: true
                    value: filterRow.waveMix
                    from: 0
                    to: inspector.waveMixMax
                    step: 0.05
                    decimals: 2
                    label: qsTr("Dual wave amount")
                    onValueEdited: (value) => filterRow.setWaveMix(value)
                }
            }
            InspectorRow {
                visible: filterRow.kind === "wave"
                label: filterRow.deformLabel
                AeCheckBox {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 23
                    text: filterRow.rippleLabel
                    textFont: uiFont
                    checked: filterRow.waveRipple
                    onToggled: filterRow.setWaveRipple(checked)
                }
            }
            InspectorRow {
                visible: filterRow.kind === "wave"
                label: filterRow.speedLabel
                InspectorNumberField {
                    Layout.fillWidth: true
                    value: filterRow.waveSpeed
                    from: inspector.waveSpeedMin
                    to: inspector.waveSpeedMax
                    step: 0.05
                    decimals: 2
                    label: qsTr("Wave speed")
                    onValueEdited: (value) => filterRow.setWaveSpeed(value)
                }
            }
            InspectorRow {
                visible: filterRow.kind === "wave"
                label: filterRow.crestLabel
                InspectorNumberField {
                    Layout.fillWidth: true
                    value: filterRow.waveCrest
                    from: inspector.waveCrestMin
                    to: inspector.waveCrestMax
                    step: 0.05
                    decimals: 2
                    label: qsTr("Wave crest")
                    onValueEdited: (value) => filterRow.setWaveCrest(value)
                }
            }
            InspectorRow {
                visible: filterRow.kind === "wave"
                label: filterRow.dualAngleLabel
                InspectorNumberField {
                    Layout.fillWidth: true
                    value: filterRow.waveDualAngle
                    from: inspector.blurAngleMin
                    to: inspector.blurAngleMax
                    step: 1
                    decimals: 1
                    label: qsTr("Dual wave direction")
                    prefixIcon: "angle"
                    suffix: "\u00b0"
                    onValueEdited: (value) => filterRow.setWaveDualAngle(value)
                }
            }
            InspectorRow {
                visible: filterRow.kind === "wave"
                label: filterRow.dualScaleLabel
                InspectorNumberField {
                    Layout.fillWidth: true
                    value: filterRow.waveDualScale
                    from: inspector.waveDualScaleMin
                    to: inspector.waveDualScaleMax
                    step: 0.01
                    decimals: 2
                    label: qsTr("Dual wave scale")
                    onValueEdited: (value) => filterRow.setWaveDualScale(value)
                }
            }

            // MASK: the filter's painted field. Paint hands the lane and channel to the
            // Mask tool (the toolbox tool is where painting happens - the card only
            // points at it); Clear returns the field to its neutral value in ONE undo
            // step and is offered only while a field exists.
            InspectorRow {
                visible: filterRow.kind !== ""
                label: filterRow.maskLabel
                RowLayout {
                    spacing: 4
                    AeIconButton {
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        size: 20
                        iconSize: 16
                        iconName: "paint-brush"
                        label: filterRow.paintMaskLabel
                        showTip: true
                        opacity: headerMouse.containsMouse ? 1.0 : 0.75
                        onClicked: filterRow.paintMask(0)
                    }
                    AeIconButton {
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        size: 20
                        iconSize: 16
                        iconName: "eraser"
                        label: filterRow.clearMaskLabel
                        showTip: true
                        enabled: filterRow.hasAmountMask
                        opacity: filterRow.hasAmountMask ? 0.75 : 0.3
                        onClicked: filterRow.clearMask(0)
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }
            }
            InspectorRow {
                visible: filterRow.kind === "wave"
                label: filterRow.timeMaskLabel
                RowLayout {
                    spacing: 4
                    AeIconButton {
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        size: 20
                        iconSize: 16
                        iconName: "paint-brush"
                        label: filterRow.paintTimeMaskLabel
                        showTip: true
                        opacity: headerMouse.containsMouse ? 1.0 : 0.75
                        onClicked: filterRow.paintMask(1)
                    }
                    AeIconButton {
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        size: 20
                        iconSize: 16
                        iconName: "eraser"
                        label: filterRow.clearTimeMaskLabel
                        showTip: true
                        enabled: filterRow.hasTimeMask
                        opacity: filterRow.hasTimeMask ? 0.75 : 0.3
                        onClicked: filterRow.clearMask(1)
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }
}
