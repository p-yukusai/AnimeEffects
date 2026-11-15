// All qsTr() strings in this module live under one stable, module-scoped
// translation context (AnimeEffects.Dialogs) rather than the default filename
// context, so the entries are rename-safe and never collide with the C++
// context of the sibling widget class (gui::AnimationSettingDialog). The
// translation catalog is generated from assets/locale/translation_<locale>.ts
// and the .qm are embedded into the binary at :/i18n.
pragma Translator: "AnimeEffects.Dialogs"

import QtQuick
import QtQuick.Layouts

// The Animation settings popup, rendered as a full-bleed Qt Quick scene inside
// the AnimationSettingDialog QWidget shell. The shell is thin (translucent,
// transparent clear color, rounded window mask); ALL of the visual chrome —
// rounded body, border, header + close, and the form — lives here in Qt Quick.
//
// Reads/writes the "model" (AnimationSettingViewModel) and "theme" (QmlTheme)
// context properties. APPLY-LIVE: no OK/Cancel — each committed change updates
// the model, the shell relays changed(), and the caller applies it. The close
// button and the transparent window corners dismiss the popup.
Rectangle {
    id: root
    radius: dialogRadius
    color: theme.base
    border.color: theme.hairline
    border.width: 1
    implicitWidth: dialogWidth
    implicitHeight: dialogHeight

    // Session undo/redo: step the dialog's own draft history while the popup is
    // open. The whole session still commits to the model as one undo step on
    // close. A focused TextInput consumes Ctrl+Z for text-undo, so these only
    // fire when the user isn't typing.
    Shortcut { sequence: "Ctrl+Z"; onActivated: model.undo() }
    Shortcut { sequence: "Ctrl+Shift+Z"; onActivated: model.redo() }

    // Background layer (below everything): a click anywhere on the empty body
    // drops focus from any value field being edited.
    MouseArea {
        anchors.fill: parent
        onPressed: {
            const fi = Window.activeFocusItem;
            if (fi && fi.objectName === "scrubEdit")
                fi.focus = false;
        }
    }

    // --- header (40px) ---
    RowLayout {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 40
        anchors.leftMargin: 12
        anchors.rightMargin: 8
        Text {
            text: qsTr("Animation settings")
            color: theme.text
            font.pixelSize: 12
            font.bold: true
        }
        Item { Layout.fillWidth: true }
        Rectangle {
            id: closeBtn
            implicitWidth: 22
            implicitHeight: 22
            radius: 4
            color: closeHov.hovered ? theme.raised : "transparent"
            Image {
                anchors.centerIn: parent
                width: 14
                height: 14
                source: theme.closeIcon
                fillMode: Image.PreserveAspectFit
                sourceSize.width: 256
                sourceSize.height: 256
            }
            HoverHandler { id: closeHov }
            TapHandler { onTapped: model.reject() }
        }
    }
    // header bottom hairline
    Rectangle {
        anchors.top: parent.top
        anchors.topMargin: 40
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: theme.hairline
    }

    // --- form ---
    ColumnLayout {
        anchors.top: parent.top
        anchors.topMargin: 57
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        spacing: 14

        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 12

            Text { text: qsTr("Loop animation"); color: theme.text; font.pixelSize: 12 }
            Rectangle {
                id: loopBox
                readonly property bool hover: loopHov.hovered
                implicitWidth: 16
                implicitHeight: 16
                radius: 3
                color: loopDown.pressed ? theme.active : (model.loop ? theme.accent : theme.raised)
                border.color: hover ? theme.hover : (model.loop ? theme.accent : theme.outline)
                border.width: 1
                Canvas {
                    anchors.fill: parent
                    visible: model.loop
                    onPaint: {
                        const c = getContext("2d");
                        c.reset();
                        c.strokeStyle = theme.accentText;
                        c.lineWidth = 2;
                        c.lineCap = "round";
                        c.lineJoin = "round";
                        c.beginPath();
                        c.moveTo(width * 0.22, height * 0.52);
                        c.lineTo(width * 0.43, height * 0.74);
                        c.lineTo(width * 0.78, height * 0.28);
                        c.stroke();
                    }
                }
                HoverHandler { id: loopHov }
                TapHandler { id: loopDown; onTapped: model.loop = !model.loop }
            }

            Text { text: qsTr("Frame range"); color: theme.text; font.pixelSize: 12 }
            RowLayout {
                spacing: 6
                ScrubSpinBox {
                    id: startBox
                    from: -2147483648
                    to: endBox.value
                    step: 1
                    value: model.frameStart
                    mouseGrabber: scrubGrab
                    onChanged: (value) => { model.frameStart = Math.round(value) }
                    // The popup is reused (shown/hidden, never recreated) and a
                    // scrub writes `value` directly, dropping the `value: model.*`
                    // binding; re-sync from the model on every model change (incl.
                    // each showAt) so the field can't show a stale value on reopen.
                    Connections {
                        target: model
                        function onFrameStartChanged() { startBox.value = model.frameStart }
                    }
                }
                Text { text: "-"; color: theme.textMuted; font.pixelSize: 12 }
                ScrubSpinBox {
                    id: endBox
                    from: startBox.value
                    to: 2147483647
                    step: 1
                    value: model.frameEnd
                    mouseGrabber: scrubGrab
                    onChanged: (value) => { model.frameEnd = Math.round(value) }
                    Connections {
                        target: model
                        function onFrameEndChanged() { endBox.value = model.frameEnd }
                    }
                }
            }

            Text { text: qsTr("Frames per second"); color: theme.text; font.pixelSize: 12 }
            ScrubSpinBox {
                id: fpsBox
                from: 1
                to: 2147483647
                step: 1
                value: model.fps
                mouseGrabber: scrubGrab
                onChanged: (value) => { model.fps = Math.round(value) }
                Connections {
                    target: model
                    function onFpsChanged() { fpsBox.value = model.fps }
                }
            }
        }
    }
}
