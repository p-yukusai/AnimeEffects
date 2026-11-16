pragma Translator: "AnimeEffects.Tools"

import QtQuick

// THE BRUSH, once.
//
// Four painting contexts share one brush model (ctrl/Brush.h: size, hardness, flow,
// opacity, the two pressure switches; the envelope brush adds its target Value), and
// the widget dock showed that as four
// separate hand-laid-out blocks - one per panel - each of which could (and did) drift
// from the others. This is the one block now: `toolModel.brush` points at whichever
// tool is up, so the rows are written once and a tool that cannot honour a row HIDES
// it (the FFD mesh brush has no per-stroke cap, so it shows Flow and hides Opacity)
// rather than the row meaning something else per tool.
//
// The percentages are shown as 0..100 and stored as 0..1: one multiplication at the
// boundary, because the model keeps ctrl::BrushParam's own units.
Column {
    id: root

    width: parent ? parent.width : 0

    ToolHeading {
        group: true
        title: qsTr("Brush")
    }

    ToolSliderRow {
        label: qsTr("Size")
        tip: toolModel.sizeKeyTip
        from: toolModel.brush.radiusMin
        to: toolModel.brush.radiusMax
        step: 1
        decimals: 0
        value: toolModel.brush.radius
        onValueEdited: (value) => toolModel.brush.radius = value
    }
    ToolSliderRow {
        label: qsTr("Hardness")
        tip: toolModel.hardnessKeyTip
        from: 0
        to: toolModel.percentMax
        step: 1
        decimals: 0
        suffix: "%"
        value: toolModel.brush.hardness * 100
        onValueEdited: (value) => toolModel.brush.hardness = value / 100
    }
    ToolSliderRow {
        visible: toolModel.envelopeBrushVisible
        label: qsTr("Value")
        tip: qsTr("The envelope range the brush moves toward; Flow limits each stamp")
        from: toolModel.envelopeValueMin
        to: toolModel.envelopeValueMax
        step: 1
        decimals: 0
        value: toolModel.envelopeValue
        onValueEdited: (value) => toolModel.envelopeValue = value
    }
    ToolSliderRow {
        label: qsTr("Flow")
        from: 0
        to: toolModel.percentMax
        step: 1
        decimals: 0
        suffix: "%"
        value: toolModel.brush.flow * 100
        onValueEdited: (value) => toolModel.brush.flow = value / 100
    }
    ToolSliderRow {
        visible: toolModel.opacityVisible
        label: qsTr("Opacity")
        from: toolModel.brush.opacityMin
        to: toolModel.percentMax
        step: 1
        decimals: 0
        suffix: "%"
        value: toolModel.brush.opacity * 100
        onValueEdited: (value) => toolModel.brush.opacity = value / 100
    }

    Item { width: 1; height: 2 }

    ToolCheckRow {
        text: qsTr("Pressure size")
        tip: qsTr("A tablet pen's pressure scales the brush size")
        value: toolModel.brush.pressureSize
        onToggled: (checked) => toolModel.brush.pressureSize = checked
    }
    ToolCheckRow {
        text: qsTr("Pressure flow")
        tip: qsTr("A tablet pen's pressure scales what a stamp deposits")
        value: toolModel.brush.pressureFlow
        onToggled: (checked) => toolModel.brush.pressureFlow = checked
    }

    // The shortcut USED to be spelled out here, on its own muted line. It moved into the
    // Size and Hardness rows' tooltips (`toolModel.sizeKeyTip`): the reported verdict was
    // that a shortcut drawn as text makes the panel busy, and the same words cost no
    // layout in a tooltip.
}
