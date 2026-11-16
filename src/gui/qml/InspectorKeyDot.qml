pragma Translator: "AnimeEffects.Properties"

import QtQuick

// The timeline's key dot, made an editing control. Three states, one shape:
//   static   hollow ring       editing changes the base value
//   animated half-filled ring  keys exist elsewhere; this frame is evaluated
//   keyed    filled disc       this frame owns a key
// A press toggles the explicit key at the playhead. The hue is the key TYPE's
// timeline hue, never the accent.
Item {
    id: root

    property string state: "static"
    property color ink: theme.text
    property string label: ""
    signal clicked()

    implicitWidth: 20
    implicitHeight: 20
    Accessible.role: Accessible.Button
    Accessible.ignored: state === ""
    Accessible.name: label !== "" ? qsTr("%1 key").arg(label) : qsTr("Key")
    Accessible.description: state === "keyed" ? qsTr("Key exists at this frame. Click to remove it.")
                                             : state === "animated"
                                               ? qsTr("Animated between keys. Click to add a key at this frame.")
                                               : qsTr("Static. Click to add a key.")
    Accessible.onPressAction: if (root.enabled) root.clicked()

    Rectangle {
        id: hoverPill
        anchors.fill: parent
        radius: 4
        color: hover.hovered ? theme.hover : "transparent"
    }

    Canvas {
        id: dot
        anchors.centerIn: parent
        width: 12
        height: 12
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            const c = width / 2;
            const r = 4.2;
            ctx.strokeStyle = root.enabled ? root.ink : theme.textDisabled;
            ctx.lineWidth = 1.1;
            ctx.beginPath();
            ctx.arc(c, c, r, 0, Math.PI * 2);
            ctx.stroke();

            if (root.state === "keyed") {
                ctx.fillStyle = root.enabled ? root.ink : theme.textDisabled;
                ctx.beginPath();
                ctx.arc(c, c, r, 0, Math.PI * 2);
                ctx.fill();
            } else if (root.state === "animated") {
                ctx.save();
                ctx.beginPath();
                ctx.rect(c - r, c - r, r, 2 * r);
                ctx.clip();
                ctx.fillStyle = root.enabled ? root.ink : theme.textDisabled;
                ctx.beginPath();
                ctx.arc(c, c, r, 0, Math.PI * 2);
                ctx.fill();
                ctx.restore();
            }
        }
    }

    Connections {
        target: root
        function onInkChanged() { dot.requestPaint() }
        function onStateChanged() { dot.requestPaint() }
        function onEnabledChanged() { dot.requestPaint() }
    }

    HoverHandler {
        id: hover
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.enabled
        onClicked: root.clicked()
    }
}
