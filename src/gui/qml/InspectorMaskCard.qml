pragma Translator: "AnimeEffects.Properties"

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// THE NODE'S MASK.
//
// A mask is a channel, not a tree node and not a stack entry - so this card is the
// channel's face: its state (absent / static / keyed / between keys), a thumbnail of
// the field the frame SHOWS, the four edits, and a pointer at the tool that paints it.
// The lane in the Animation dock draws the same facts, and both read them from
// gui::InspectorViewModel, so the card cannot say "static" while the lane carries keys.
//
// The card sits AFTER the effect stack because that is the render order: filters run
// on the node's own content and the matte multiplies the present alpha afterwards.
Item {
    id: root

    width: parent ? parent.width : 0
    // The card is the header plus exactly one of the two bodies - the field with its
    // actions, or the single Add button - so it never leaves dead height behind.
    implicitHeight: header.height + (present ? body.height + 4 : 28)
    height: implicitHeight

    // ---- the state, all derived (never cached here) ---------------------------
    readonly property bool present: inspector.maskPresent
    readonly property bool keyed: inspector.maskKeyed
    readonly property bool onKey: inspector.maskHasKeyAtFrame
    // a matte is paintable when nothing is keyed yet, or when this frame owns a key
    readonly property bool paintable: !keyed || onKey
    // The three-state dot the timeline uses for every channel: hollow = the value is
    // static, half = keys exist elsewhere (this frame is evaluated), filled = this
    // frame owns a key.
    readonly property string dotState: !keyed ? "static" : (onKey ? "keyed" : "animated")
    readonly property string stateText: !present
                                           ? qsTr("none")
                                           : keyed
                                             ? (onKey ? qsTr("key at this frame")
                                                      : qsTr("blended — read only"))
                                             : qsTr("static")

    // ---- header ---------------------------------------------------------------
    Item {
        id: header
        width: parent.width
        height: 32

        Text {
            id: maskTitle
            x: 36
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Mask")
            color: theme.text
            font.family: uiFont.family
            font.pointSize: uiFont.pointSize
            font.bold: true
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        Text {
            id: stateLabel
            anchors.left: maskTitle.right
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: root.stateText
            color: theme.textMuted
            font.family: uiFont.family
            font.pointSize: uiFont.pointSize
            visible: root.present
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        // The key dot is DISABLED with no mask: there is nothing to key yet, and a
        // dot that adds a key to nothing is a trap.
        InspectorKeyDot {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            visible: root.present
            enabled: !inspector.playbackActive
            state: root.dotState
            ink: theme.maskKey
            label: qsTr("Mask")
            onClicked: inspector.toggleMaskKey()
        }
    }

    // ---- body ----------------------------------------------------------------
    Rectangle {
        id: body
        anchors.top: header.bottom
        width: parent.width
        height: visible ? 88 : 0
        visible: root.present
        radius: 6
        color: theme.recessed
        border.width: 1
        border.color: theme.hairline

        // The thumbnail is the FIELD the frame shows, so the between-keys state is
        // legible without a second control: what you see is the blend you cannot paint.
        Rectangle {
            id: thumbFrame
            x: 12
            y: 12
            width: 56
            height: 52
            radius: 4
            color: theme.base
            border.width: 1
            border.color: theme.hairline

            Image {
                anchors.fill: parent
                anchors.margins: 1
                // the revision in the URL is what re-asks the provider after a stroke;
                // an absent mask asks for nothing at all
                source: root.present ? "image://maskthumb/mask?" + inspector.maskRevision : ""
                fillMode: Image.PreserveAspectFit
                smooth: true
                // white texels are "kept"; show them as the artwork's own ink so a
                // reveal-all mask reads as EMPTY rather than as a white card
                opacity: 0.9
                cache: false
            }
        }

        // THREE actions do not fit beside a 56px thumbnail in this dock: measured on a
        // 1500x940 window, the wide row overflowed the panel and clipped Clear. Two
        // compact rows fit at any dock width, and the PRIMARY action keeps the full
        // width because it is the one a masker presses.
        ColumnLayout {
            anchors.left: thumbFrame.right
            anchors.leftMargin: 12
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.top: parent.top
            anchors.topMargin: 12
            spacing: 6

            AeButton {
                Layout.fillWidth: true
                visible: root.paintable
                text: qsTr("Paint mask")
                enabled: !inspector.playbackActive
                onClicked: inspector.requestLayerMaskPaint()
            }

            // Between keys the matte is READ ONLY (a stroke would land on no key), and
            // this is the one press that fixes it: it keys the field you can see, which
            // by definition does not change the picture.
            AeButton {
                Layout.fillWidth: true
                visible: !root.paintable
                text: qsTr("Key at frame %1").arg(inspector.currentFrame)
                enabled: !inspector.playbackActive
                onClicked: inspector.toggleMaskKey()
            }

            // ICONS, not words: AeButton's implicit width has a 72px FLOOR, so two of
            // them in a row demanded 150px inside a dock that is ~220px wide - measured
            // as the whole card overflowing the panel and clipping "Paint mask". A 22px
            // icon button has no such floor, and both actions are unambiguous with a
            // tooltip and an accessible name.
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                AeIconButton {
                    iconName: "arrow-clockwise"
                    label: qsTr("Invert mask")
                    size: 22
                    iconSize: 14
                    showTip: true
                    enabled: !inspector.playbackActive && (!root.keyed || root.onKey)
                    onClicked: inspector.invertMask()
                }
                AeIconButton {
                    iconName: "trash"
                    label: qsTr("Remove mask")
                    size: 22
                    iconSize: 14
                    showTip: true
                    enabled: !inspector.playbackActive
                    onClicked: inspector.removeMask()
                }
                Item { Layout.fillWidth: true }
            }
        }
    }

    // ---- the absent state: one action ---------------------------------------
    AeButton {
        anchors.top: header.bottom
        x: 36
        width: 96
        height: 24
        visible: !root.present
        text: qsTr("Add mask")
        enabled: inspector.hasTarget && !inspector.playbackActive
        onClicked: inspector.addMask()
    }
}
