pragma Translator: "AnimeEffects.Dialogs"

import QtQuick

// A quiet section heading: a small uppercase muted label with a hairline rule
// running out to the right.
//
// The widget form drew its headings as a title between two full-width hairlines
// ("--- WHAT TO EXPORT ---"), which reads as a separator rather than a heading:
// it has the same weight as the rules above and below the frame rows. This is one
// dim word (the role the eye skips) plus one rule (the role the eye follows), so
// the two sections are legible as structure without competing with their own
// contents.
//
// Not a Controls Label because Label has nothing this heading needs: measured
// offscreen against a plain Text, it IS a Text (QQuickLabel -> QQuickText) whose
// only additions are a style-palette colour binding (its color follows
// palette.windowText; Text's does not) that this design replaces with
// theme.textMutedDim anyway, and background/inset chrome slots this heading
// never fills. And no Controls type is a ruled heading — the hairline is ours on
// any rung (docs/export-dialog-implementation-notes.md, "Control inventory").
Item {
    id: root

    property string text: ""
    implicitHeight: 24
    implicitWidth: label.implicitWidth + 12

    Text {
        id: label
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        text: root.text.toUpperCase()
        color: theme.textMutedDim
        font.pixelSize: 10
        font.letterSpacing: 1.1
        font.bold: true
        // Exposed as a real heading, so a screen reader can navigate the form by
        // section instead of reading one flat run of controls.
        Accessible.role: Accessible.Heading
        Accessible.name: root.text
    }
    Rectangle {
        anchors.left: label.right
        anchors.leftMargin: 10
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: 1
        color: theme.hairline
    }
}
