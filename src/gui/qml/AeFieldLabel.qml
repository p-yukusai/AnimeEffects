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

    // The lane is as wide as the WIDEST label in the column, with the design's 104
    // as the FLOOR rather than the width. A short form (the export form's "Format",
    // "Quality"; the new-project form's rows) keeps the alignment it was measured
    // with, and a long label widens the whole column instead of being ellipsized
    // while the field beside it sits half empty. Measured 2026-10-11 on the settings
    // dialog, which is the first form with long labels: at the fixed 104 it clipped
    // "Language (needs restart)", "Global keybind delay (ms)", "Default keyframe
    // easing" and "Time (in minutes) between autosaves". `implicitWidth` is the
    // Text's natural width even with `elide` set, which is what makes this a
    // measurement of the words rather than of the box.
    //
    // minimumWidth 0 lets the column yield - and the label elide - when a window is
    // genuinely too narrow for the row, which is the only case eliding is for.
    Layout.preferredWidth: Math.max(104, implicitWidth)
    Layout.minimumWidth: 0
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
