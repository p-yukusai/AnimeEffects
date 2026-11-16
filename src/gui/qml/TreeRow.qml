pragma Translator: "AnimeEffects.Tree"

import QtQuick

// ONE ROW OF THE ANIMATION DOCK'S TREE COLUMN.
//
// VERTICAL INK, CENTRED. The row's height is the model's (ctrl::TimeLineRow::displayHeight):
// a layer with five animated channels is 90px here and 90px in the lanes beside it. The row's
// CONTENT - caret, icon, name, eye - is centred in that whole height (the widget did this, and
// it reads better than a label floating at the top of a tall block), while the INDENTATION
// GUIDES run the full height so a trunk is continuous from row to row.
//
// The guides are vertical ONLY: no elbows, and a level's trunk appears at all only while that
// level has another row below it (the model publishes the bit per level in `guideMask`, because
// that is a fact about the row list and not about the ink).
//
// THREE ZONES, and the middle one scrolls:
//   [ guides + caret + icon + name ]  <- translated by hOffset, clipped
//   [ eye + matte chip ]              <- PINNED to the right edge, never moves
// so a deep nesting or a long name is reachable by scrolling, and the controls that act on the
// row stay where the hand expects them.
Item {
    id: root

    // ---- the model's row ---------------------------------------------------
    property int rowIndex: 0
    property string name: ""
    property int depth: 0
    property int rowHeight: 22
    property bool isDocument: false
    property bool isFolder: false
    property bool hasChildren: false
    property bool expanded: false
    property bool selected: false
    property bool current: false
    property bool nodeVisible: true
    property bool renaming: false
    property bool treeFocused: false
    // the keyboard moved last, so the focus ring means "the arrows act here" - a mouse click
    // must not leave a ring behind (the same rule the dock header's buttons follow)
    property bool focusVisible: false
    // exactly one row selected: the row the timeline's lanes tint
    property bool singleSelection: false
    property int dropZone: 0
    property int guideMask: 0
    property string glyph: "file"
    property string thumbUrl: ""
    property real thumbWidth: 0
    property real thumbHeight: 0
    property bool hasMatte: false
    property string matteUrl: ""
    // the horizontal scroll of the middle zone, in pixels
    property real hOffset: 0
    // the width of the pinned right zone, from the panel (one number, two files)

    signal pressed(int row, int modifiers, real sceneX, real sceneY)
    signal moved(int row, real sceneX, real sceneY)
    signal released(int row, int modifiers, real sceneX, real sceneY)
    signal renameRequested(int row)
    signal renameCommitted(int row, string text)
    signal renameTextEdited(int row, string text)
    signal renameCancelled(int row)
    signal visibilityToggled(int row)
    signal expandToggled(int row)
    signal menuRequested(int row, real sceneX, real sceneY)

    readonly property int kIndent: 18
    readonly property int kLeftPad: 5
    // THE DOCUMENT ROW IS NOT A LEVEL OF CONTENT: the tree indents from the top-level items, so
    // the document and a top-level layer start at the same x. Without this every row sat 18px in
    // and a guide line ran down the whole tree beside a document that had no child of its own to
    // connect to (measured).
    readonly property int kLevel: Math.max(0, depth - (isDocument ? 0 : 1))
    readonly property int kCaretBox: 14
    readonly property int kIconSlot: 22

    // THE PINNED RIGHT ZONE IS ONLY AS WIDE AS ITS CONTROLS: the eye always, the matte chip only
    // when the node has one (an empty band of row fill beside the eye reads as a dead slot).
    // THE BAR'S LANE IS NOT THE ROWS' TO USE. The vertical scroll bar OVERLAYS the rows' right
    // 12px (a 4px pill centred in a 12px lane, its left edge 8px in), so a control that ends 1px
    // or 3px from that pill reads as overlapping it - reported at 6 ("looks like it overlaps") and
    // again at 8 ("still quite close"). The margin is now THE WHOLE LANE plus 2px of its own room:
    // the eye's glyph ends 17px from the row's edge, 9px clear of the pill.
    readonly property int kBarLane: 12
    readonly property int kEyeSlot: 20
    readonly property int kChipSlot: root.hasMatte ? 20 : 0   // 6px gap + the 14px chip
    readonly property int kEdge: kBarLane + 2
    // THE EYE IS A COLUMN, SO IT IS MEASURED FROM THE ROW'S RIGHT EDGE. The zone grows a chip slot
    // when a row gains a matte; laying the eye out from the zone's LEFT moved it 20px for those
    // rows alone - the reported "the visibility column gets misaligned". These are the zone's own
    // coordinates (it is flush right), so the eye never moves.
    readonly property real kZoneEye: pinnedWidth - kEdge - kEyeSlot
    // the chip sits LEFT of the visibility toggle, 6px clear of it
    readonly property real kZoneChip: kZoneEye - 6 - 14
    // everything left of this is the fade's business, everything right of it is solid fill
    readonly property real kZoneControls: root.hasMatte ? kZoneChip : kZoneEye
    readonly property int kFadeWidth: 16
    // THE FADE IS A BAND OF ITS OWN, LEFT OF THE CONTROLS: pinnedWidth is the fade plus the
    // controls, so the soft edge ends exactly where the eye begins and every control sits on the
    // row's own fill. (Composing it the other way - the fade INSIDE the control band - put the eye
    // on a half-transparent wash, which is what the eye's own hit area looked worst for.)
    readonly property real pinnedWidth: kFadeWidth + kEyeSlot + kChipSlot + kEdge
    // the text is clipped where the CONTROLS begin, so it runs UNDER the fade only
    readonly property real contentClipWidth: Math.max(0, width - pinnedWidth + kZoneControls)

    // THE ROW'S FILL, opaque: the pinned zone covers TEXT, so a translucent tint would let the
    // letters read through the controls. Composited here arithmetically (base blended with the
    // accent at 0.06, or with the text ink at 0.04 for hover) - NOT through a helper function,
    // because a call in a binding registers no dependency on `theme` and would freeze the first
    // palette it saw.
    readonly property color rowBackground: root.selected
        ? Qt.rgba(theme.base.r * 0.94 + theme.accentBright.r * 0.06,
                  theme.base.g * 0.94 + theme.accentBright.g * 0.06,
                  theme.base.b * 0.94 + theme.accentBright.b * 0.06)
        : (root.rowHovered && !root.isDocument
               ? Qt.rgba(theme.base.r * 0.96 + theme.text.r * 0.04,
                         theme.base.g * 0.96 + theme.text.g * 0.04,
                         theme.base.b * 0.96 + theme.text.b * 0.04)
               : theme.base)

    // a HoverHandler, not the row's MouseArea: a child area (the eye) used to take the hover and
    // the row lost its own highlight the moment the pointer reached the control
    readonly property bool rowHovered: rowHover.hovered
    HoverHandler { id: rowHover }
    // the content band is CENTRED in the row: a 130px row puts its name in the middle
    readonly property real bandY: Math.max(0, (height - kIconSlot) / 2)
    // a few pixels of breathing room before the caret: with the document row shifted to the same
    // x as the top-level items, the arrows sat flush against the column's edge
    readonly property real caretX: kLeftPad + kLevel * kIndent
    readonly property real iconX: caretX + kCaretBox + 2
    readonly property real nameX: iconX + kIconSlot + 4

    width: ListView.view ? ListView.view.width : 200
    height: rowHeight

    // THE ROW'S ACCESSIBLE OBJECT COMES FROM THE APPLICATION'S OWN FACTORY
    // (gui/qml/QmlFormAccessibility.cpp, marker "aeAccessibleRow"): a Qt Quick item's default
    // accessible object reports itself INVISIBLE here, which would hide every layer from a
    // screen reader - and the probes address rows as table cells, the role the widget tree
    // published. The factory reads these properties and calls accessiblePress().
    objectName: "aeAccessibleRow"
    property string accessibleName: root.name
    property string accessibleDescription: {
        if (root.isDocument)
            return qsTr("Document");
        const parts = [];
        parts.push(root.isFolder ? qsTr("Folder") : qsTr("Layer"));
        if (!root.nodeVisible)
            parts.push(qsTr("Hidden"));
        if (root.hasChildren && root.expanded)
            parts.push(qsTr("Expanded"));
        if (root.selected)
            parts.push(qsTr("Selected"));
        return parts.join(", ");
    }
    property bool accessibleSelectable: !root.isDocument
    property bool accessibleSelected: root.selected
    property bool accessibleHasChildren: root.hasChildren
    property bool accessibleExpanded: root.expanded
    property bool accessibleCheckable: !root.isDocument
    property bool accessibleChecked: root.nodeVisible
    function accessiblePress() { root.pressed(root.rowIndex, 0, 0, 0) }

    // ---- the row's ink ------------------------------------------------------
    Rectangle {
        anchors.fill: parent
        color: root.rowBackground
        // the row's own fill may BE the floor: drawing base over base is a no-op, and it keeps the
        // row opaque under a translucent pinned gradient
        z: -1
    }
    Rectangle {
        // the selection RAIL: 2px on the row's left edge, for the ONE row the lanes tint (a
        // multi-selection marks no single lane, so it gets no rail - the widget's own rule).
        // It is a SELECTION mark, not a focus mark: a context menu takes the keyboard and the
        // rail used to vanish with it (measured).
        width: 2
        height: parent.height
        color: theme.accentBright
        visible: root.selected && root.singleSelection && !root.isDocument
    }
    Rectangle {
        // the keyboard cursor: only while the KEYBOARD is what moved (focusVisible)
        anchors.fill: parent
        anchors.margins: 1
        radius: 3
        color: "transparent"
        border.width: root.current && root.treeFocused && root.focusVisible ? 1 : 0
        border.color: theme.focus
        visible: border.width > 0
    }

    // ---- the row's own pointer, UNDER the content so the controls keep their presses ------
    MouseArea {
        id: rowMouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        // THE LIST MUST NOT STEAL THIS DRAG. The row lives in a ListView, i.e. a Flickable, and a
        // Flickable takes the mouse away from a child as soon as the pointer moves - so a drag
        // that should reorder the row only ever FLICKED the list (reported: "drag to reorder
        // doesn't work because drag scrolls"). With this, the row keeps the grab for the whole
        // gesture; the wheel is untouched, and the list still scrolls by wheel, bar and keys.
        preventStealing: true
        onPressed: (mouse) => {
            const pt = root.mapToItem(null, mouse.x, mouse.y);
            if (mouse.button === Qt.RightButton)
                root.menuRequested(root.rowIndex, pt.x, pt.y);
            else
                root.pressed(root.rowIndex, mouse.modifiers, pt.x, pt.y);
        }
        onPositionChanged: (mouse) => {
            if (!pressed)
                return;
            const pt = root.mapToItem(null, mouse.x, mouse.y);
            root.moved(root.rowIndex, pt.x, pt.y);
        }
        onReleased: (mouse) => {
            const pt = root.mapToItem(null, mouse.x, mouse.y);
            root.released(root.rowIndex, mouse.modifiers, pt.x, pt.y);
        }
        onDoubleClicked: (mouse) => {
            if (mouse.button !== Qt.LeftButton || root.isDocument)
                return;
            // THE ROW'S VERB IS RENAME, for a folder as much as for a layer (the caret is what
            // expands, and it is a separate target)
            root.renameRequested(root.rowIndex);
        }
    }

    // ---- the scrolling middle zone: guides, caret, icon, name -----------------------------
    Item {
        id: contentClip
        anchors.left: parent.left
        anchors.top: parent.top
        width: root.contentClipWidth
        height: parent.height
        clip: true

        Item {
            x: -root.hOffset
            width: Math.max(contentClip.width, root.nameX + nameText.implicitWidth + 10)
            height: parent.height

            // vertical indentation guides, one per level the model says continues below. The
            // model answers in DEPTH terms and the row draws in LEVEL terms (the document row is
            // level -1), so a top-level item draws nothing and a child of one draws level 0.
            Repeater {
                model: Math.max(0, root.depth - 1)
                delegate: Rectangle {
                    required property int index
                    x: root.kLeftPad + index * root.kIndent + root.kIndent / 2
                    y: 0
                    width: 1
                    height: root.height
                    color: theme.hairline
                    opacity: (root.guideMask & (1 << index)) ? 0.4 : 0
                    visible: opacity > 0
                }
            }

            // the caret: a toggle, and the row's own expander
            Image {
                x: root.caretX
                y: root.bandY + (root.kIconSlot - root.kCaretBox) / 2
                width: root.kCaretBox
                height: root.kCaretBox
                visible: root.hasChildren
                opacity: 0.4
                source: root.hasChildren ? (theme.iconRevision, theme.iconUrl(root.expanded ? "caret-down-bold" : "caret-right-bold")) : ""
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.expandToggled(root.rowIndex)
                }
            }

            // the icon slot: a layer's own pixels, or the folder/file glyph
            Item {
                id: iconSlot
                x: root.iconX
                y: root.bandY
                width: root.kIconSlot
                height: root.kIconSlot
                visible: !root.isDocument
                Image {
                    anchors.centerIn: parent
                    width: root.thumbWidth
                    height: root.thumbHeight
                    visible: root.thumbUrl !== ""
                    source: root.thumbUrl
                    smooth: false
                    opacity: root.nodeVisible ? 1.0 : 0.45
                }
                // THE GLYPH IS THE SLOT'S SIZE, not a hand-picked 16: the widget drew
                // `icon.pixmap(kItemSize, kItemSize)` - a 22px pixmap, whose Phosphor ink lands at
                // ~19px - so a 16px Image shrank the folder by a quarter (measured: glyph ink 19px
                // wide before, 14px after). 22 again, and size the raster to the device footprint
                // so the SVG is not downsampled twice.
                Image {
                    anchors.centerIn: parent
                    width: root.kIconSlot
                    height: root.kIconSlot
                    sourceSize.width: Math.ceil(root.kIconSlot * Screen.devicePixelRatio)
                    sourceSize.height: Math.ceil(root.kIconSlot * Screen.devicePixelRatio)
                    visible: root.thumbUrl === ""
                    source: (theme.iconRevision, theme.iconUrl(root.glyph))
                    opacity: (root.nodeVisible ? 0.66 : 0.3) * (root.selected ? 1.2 : 1.0)
                }
            }

            // the name, in the row's own vertical centre
            Text {
                id: nameText
                objectName: "aeRowName"
                x: root.nameX
                // PIXEL-ALIGNED: the centring arithmetic is fractional (an 8pt line box is not a
                // whole number of pixels), and a text origin on a fraction of a pixel renders
                // every glyph soft - measured on this row against the Properties panel's text at
                // the same font: 8.1% of its ink at full coverage against 18.6%. Rounding the
                // origin is the whole fix; nothing else about the glyphs changes.
                y: Math.round(root.bandY + (root.kIconSlot - height) / 2)
                width: implicitWidth
                visible: !root.renaming
                text: root.name
                color: {
                    const base = root.isDocument ? theme.textMuted : theme.text;
                    if (root.nodeVisible)
                        return base;
                    const t = root.selected ? 0.65 : 0.45;
                    return Qt.rgba(base.r * t + theme.base.r * (1 - t),
                                   base.g * t + theme.base.g * (1 - t),
                                   base.b * t + theme.base.b * (1 - t));
                }
                // THE DOCK'S FONT, PROPERTY BY PROPERTY: `font: uiFont` followed by a
                // `font.weight:` assignment is refused by QML ("Property has already been
                // assigned a value"), which took the whole delegate down - the rows vanished
                // with no runtime error in the log, and the SCENE GATE named it.
                font.family: uiFont.family
                font.pointSize: uiFont.pointSize
                font.pixelSize: uiFont.pixelSize
                font.weight: root.isDocument ? Font.DemiBold : Font.Normal
                // the row is as wide as it needs to be now, so this only fires when the whole
                // name could not fit the visible zone
                TextMetrics {
                    id: nameMetrics
                    font: nameText.font
                    text: root.name
                }
                AeToolTip {
                    ownerItem: nameText
                    text: root.name
                    shown: rowMouse.containsMouse && nameText.x + nameMetrics.advanceWidth > contentClip.width
                }
            }

            // the name's editor EXISTS ONLY WHILE IT IS OPEN (a Loader): a hidden TextInput stays
            // the window's active focus item and eats the tree's keys, and a field that is merely
            // un-hidden is created before it has focus or a layout pass
            Loader {
                id: nameEditor
                x: root.nameX
                y: Math.round(root.bandY + (root.kIconSlot - height) / 2)
                width: Math.max(60, nameText.implicitWidth + 8)
                active: root.renaming
                sourceComponent: Component {
                    TextInput {
                        id: nameEdit
                        text: root.name
                        font: uiFont
                        color: theme.text
                        selectionColor: theme.selection
                        selectedTextColor: theme.selectionText
                        focus: true
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -3
                            z: -1
                            radius: 3
                            color: theme.raised
                            border.width: 1
                            border.color: theme.focus
                        }
                        Component.onCompleted: {
                            forceActiveFocus();
                            selectAll();
                        }
                        onTextChanged: root.renameTextEdited(root.rowIndex, text)
                        onAccepted: root.renameCommitted(root.rowIndex, text)
                        onActiveFocusChanged: {
                            if (!activeFocus && root.renaming)
                                root.renameCommitted(root.rowIndex, text);
                        }
                        // Escape is NOT something a TextInput consumes, so it is caught here;
                        // there is deliberately NO Keys.onPressed: an item's own key FILTER runs
                        // BEFORE the control's handling, so a catch-all handler here stopped the
                        // field from ever inserting a character (measured).
                        Keys.onEscapePressed: root.renameCancelled(root.rowIndex)
                    }
                }
                onActiveChanged: {
                    if (!active && ListView.view)
                        ListView.view.forceActiveFocus();
                }
            }
        }
    }

    // ---- the PINNED right zone: the eye and the matte chip, never scrolled ----------------
    Item {
        id: rightZone
        objectName: "aePinnedZone"
        anchors.right: parent.right
        anchors.top: parent.top
        // only as wide as what it holds: the eye always, the matte chip only when there is one
        // (an empty band of row fill beside the eye reads as a reserved slot that does nothing)
        width: root.pinnedWidth
        height: parent.height

        // THE SOFT EDGE: the row's own background, faded in over the text it covers, so a long
        // name dissolves under the controls instead of being cut by a hard edge.
        Rectangle {
            id: pinnedFade
            objectName: "aePinnedFade"
            x: root.kZoneControls - root.kFadeWidth
            anchors.top: parent.top
            width: root.kFadeWidth
            height: parent.height
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Qt.rgba(root.rowBackground.r, root.rowBackground.g, root.rowBackground.b, 0) }
                GradientStop { position: 1.0; color: root.rowBackground }
            }
        }
        Rectangle {
            x: root.kZoneControls
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height
            color: root.rowBackground
        }

        Item {
            id: visibilityButton
            objectName: "aeEyeButton"
            x: root.kZoneEye
            y: root.bandY + (root.kIconSlot - 20) / 2
            width: root.isDocument ? 0 : root.kEyeSlot
            height: 20
            visible: !root.isDocument
            Accessible.role: Accessible.Button
            Accessible.name: root.nodeVisible ? qsTr("Hide %1").arg(root.name) : qsTr("Show %1").arg(root.name)
            Accessible.onPressAction: root.visibilityToggled(root.rowIndex)

            // NO HOVER FILL: a 20px box of `hairline` inside a row reads as a pressed control
            // (in the dark theme hairline IS the raised fill), and the eye already answers with
            // its own ink. The dock's icon buttons sit in a bar of their own; this one sits in a
            // row whose selection tint is the only background it should ever see.
            Image {
                objectName: "aeEyeGlyph"
                anchors.centerIn: parent
                width: 14
                height: 14
                source: (theme.iconRevision, theme.iconUrl(root.nodeVisible ? "eye-open" : "eye-close"))
                opacity: root.nodeVisible ? (eyeMouse.containsMouse ? 0.9 : 0.55) : (eyeMouse.containsMouse ? 0.7 : 0.35)
            }
            MouseArea {
                id: eyeMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.visibilityToggled(root.rowIndex)
            }
        }

        Item {
            id: matteChip
            objectName: "aeMatteChip"
            x: root.kZoneChip
            y: root.bandY + (root.kIconSlot - 14) / 2
            width: root.hasMatte ? 14 : 0
            height: 14
            visible: root.hasMatte
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: 1
                border.color: theme.hairline
            }
            Image {
                anchors.fill: parent
                anchors.margins: 1
                source: root.matteUrl
                smooth: false
                // the cell is square, the matte usually is not: letterbox it, never squash
                fillMode: Image.PreserveAspectFit
            }
            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("%1 has a layer mask").arg(root.name)
        }
    }

    // ---- the drop indicator -------------------------------------------------
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        height: 2
        y: root.dropZone === 1 ? 0 : root.height - 2
        color: theme.accentBright
        visible: root.dropZone === 1 || root.dropZone === 2
    }
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: 3
        color: "transparent"
        border.width: root.dropZone === 3 ? 1 : 0
        border.color: theme.accentBright
        visible: root.dropZone === 3
    }
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: 3
        color: Qt.rgba(theme.accentBright.r, theme.accentBright.g, theme.accentBright.b, 0.10)
        visible: root.dropZone === 3
    }
}
