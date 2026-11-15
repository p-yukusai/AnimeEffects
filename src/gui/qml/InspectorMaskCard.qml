pragma Translator: "AnimeEffects.Properties"

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// THE NODE'S MATTE, as rows on the inspector's own grid.
//
// A mask is a channel, not a tree node and not a stack entry, and this is its whole face:
//   dot | "Mask" | [thumbnail]                     <- state, in the language of every row
//       |          | [trash]                      <- the one destructive action
// Nothing else is said. The dot already carries static / keyed / blended, so a word for it
// would be a second voice saying the same thing (and the first version did: "blended — read
// only", with an em dash in a UI). "Key at frame N" was redundant for the same reason - that
// is what the dot's filled state means. A paint that is impossible at this frame shows as a
// DISABLED Paint and says why in its TOOLTIP, which is where an explanation belongs.
//
// ABSENT is ONE row: nothing exists to describe, so the dot is empty, the label names the
// channel and the field column offers the action. (The previous version drew TWO rows,
// "Mask  none" over "Mask  Add mask", with no key dot - which read as two contradictory
// channels.)
Column {
    id: root

    width: parent ? parent.width : 0
    spacing: 0

    readonly property bool present: inspector.maskPresent
    readonly property bool keyed: inspector.maskKeyed
    readonly property bool onKey: inspector.maskHasKeyAtFrame
    // a matte is paintable when nothing is keyed yet, or when this frame owns a key
    readonly property bool paintable: !keyed || onKey
    readonly property string dotState: !keyed ? "static" : (onKey ? "keyed" : "animated")
    function activateRemove() { if (!inspector.playbackActive) inspector.removeMask(); }

    // ---- absent: ONE row, the action where the value would be -----------------------
    InspectorRow {
        visible: !root.present
        label: qsTr("Mask")
        AeButton {
            // the row is a LAYOUT: `anchors.fill` on a layout-managed item is undefined behaviour
            Layout.fillWidth: true
            height: 22
            text: qsTr("Add mask")
            enabled: inspector.hasTarget && !inspector.playbackActive
            onClicked: inspector.addMask()
        }
    }

    // ---- present: the state, the mode, the removal ---------------------------------
    InspectorRow {
        visible: root.present
        label: qsTr("Mask")
        keyState: root.dotState
        keyColor: theme.maskKey
        keyEnabled: !inspector.playbackActive
        onToggleKey: inspector.toggleMaskKey()

        AeButton {
            height: 22
            // TEXT ONLY: the glyph in a small button was noise next to the toolbox, which already
            // names the tool (reported).
            id: paintButton
            text: qsTr("Paint")
            Layout.fillWidth: true
            // ALREADY PAINTING THIS MATTE IS NOT A REASON TO PRESS IT AGAIN: the tool is up and
            // aimed here, so the button reports that state instead of re-arming it.
            enabled: !inspector.playbackActive && !inspector.maskPaintActive
            // A frame that does not own the key shows a BLEND you cannot paint; the HOVER TIP is
            // where that is said, not a sentence in the row - and the same tip explains the other
            // two disabled states (already painting, playback).
            property string tipText: inspector.playbackActive
                                         ? qsTr("Stop playback to paint")
                                         : !root.paintable
                                               ? qsTr("This frame blends two keys. Key the matte here to paint it.")
                                               : inspector.maskPaintActive
                                                     ? qsTr("The Mask tool is already painting this matte")
                                                     : qsTr("Paint this matte with the Mask tool")
            // THE DECLARATIONS COME FIRST: a binding that reads `parent.tipText` while the parent
            // is still being built throws "Cannot read property ... of null" and writes nothing
            // (measured: two TypeErrors per load). `hovered` is AbstractButton's OWN read-only
            // property - declaring another one is "Cannot override FINAL property".
            property bool rowHovered: false
            HoverHandler { onHoveredChanged: parent.rowHovered = hovered }
            AeToolTip {
                ownerItem: paintButton
                // BY ID, NOT BY `parent`: a `parent.*` binding is evaluated while the tooltip is
                // still unparented and throws "Cannot read property ... of null" (two per load).
                text: paintButton.tipText
                shown: paintButton.rowHovered
            }
            // ONE PRESS, ALWAYS THE SAME OUTCOME: painting. A frame that does not own the key first
            // keys it (which by definition does not change the picture), then arms the tool - so a
            // matte is never one dead button away from the user.
            onClicked: {
                if (!root.paintable)
                    inspector.toggleMaskKey();
                inspector.requestLayerMaskPaint();
            }
        }
        // the FIELD the frame shows, at the row's own height, keeping its aspect
        Image {
            Layout.preferredWidth: 24
            Layout.preferredHeight: 22
            source: root.present ? "image://maskthumb/mask?" + inspector.maskRevision : ""
            fillMode: Image.PreserveAspectFit
            smooth: true
            cache: false
            Accessible.ignored: true
        }
        // THE BIN, IN THE SAME ROW: a single icon on a line of its own was a row of dead height
        AeIconButton {
            Layout.preferredWidth: 22
            Layout.preferredHeight: 22
            iconName: "trash"
            label: qsTr("Remove mask")
            size: 22
            iconSize: 14
            showTip: true
            enabled: !inspector.playbackActive
            onClicked: root.activateRemove()
        }
    }

    // (the Remove action lives IN the row above: one 27px line carries state, the action, the
    // matte itself and the bin, so the card does not grow a second row for a single icon)
}
