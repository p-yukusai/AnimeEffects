pragma Translator: "AnimeEffects.Tools"

import QtQuick
import QtQuick.Controls

// The dock's slider: a 4px recessed pill, the filled part in the accent, and a solid
// accent head - the same drawing the widget's gui::tool::ToolSlider hand-painted with
// QPainter (a QSS QSlider cannot round a handle: Qt's subcontrol rendering caps
// border-radius below half the box, so a radius >= half draws a plain rectangle).
// Porting it to a delegate is the whole reason that class does not need a QML twin of
// its own input handling: Controls' Slider supplies the drag, the click-to-position,
// the wheel and the focus policy, and the two shapes below are the design.
//
// NO KEYBOARD FOCUS. A slider that takes focus would swallow the arrow keys from the
// viewport after one click, and these rows are tool settings rather than form fields -
// the deleted ToolSlider was Qt::NoFocus for exactly that reason.
Slider {
    id: root

    implicitHeight: 18
    implicitWidth: 72
    focusPolicy: Qt.NoFocus

    // Handle diameter, and the one number both shapes and the fill agree on.
    readonly property int headSize: 15
    // The head's centre, in the control's own coordinates: the head travels the track
    // minus its own diameter, so the value is exact at BOTH ends (the widget's
    // headCenter() is the same function, which is what made its click mapping land
    // where its head was drawn).
    readonly property real headCentre: root.visualPosition * (root.availableWidth - headSize) + headSize / 2

    handle: Rectangle {
        x: Math.round(root.leftPadding + root.headCentre - width / 2)
        y: Math.round((root.height - height) / 2)
        width: root.headSize
        height: root.headSize
        radius: width / 2
        color: root.enabled ? theme.accent : theme.textDisabled
    }
    background: Item {
        x: root.leftPadding
        y: Math.round((root.height - height) / 2)
        width: root.availableWidth
        height: 4

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: root.enabled ? theme.recessed : theme.hairline
        }
        Rectangle {
            width: Math.max(0, Math.round(root.headCentre))
            height: parent.height
            radius: height / 2
            color: root.enabled ? theme.accent : theme.textDisabled
        }
    }
}
