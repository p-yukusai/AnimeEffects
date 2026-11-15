pragma Translator: "AnimeEffects.Dialogs"

import QtQuick
import QtQuick.Layouts

// The muted label in the first column of a form row.
//
// A component rather than an inline `Text { }` per row, so the label column is one
// width everywhere and a row cannot drift out of alignment with its neighbours.
// The Layout attached properties belong here: a section's rows are children of one
// GridLayout, and a label that did not carry its own column width would make every
// row compute its own.
Text {
    id: root

    Layout.preferredWidth: 104
    Layout.minimumWidth: 104
    // Top-aligned with a nudge: the label reads as the field's name, not as a
    // baseline that has to match a 28px control's centre.
    Layout.alignment: Qt.AlignTop | Qt.AlignLeft
    Layout.topMargin: 7

    color: theme.textMuted
    font.pixelSize: 11
    elide: Text.ElideRight
    // A QML Text is invisible to the accessibility tree unless it says otherwise.
    // The field it names carries its own Accessible.name, so this is the same
    // relation a screen reader gets from a QLabel buddy - the words exist in the
    // tree rather than only in pixels.
    Accessible.role: Accessible.StaticText
    Accessible.name: text
}
