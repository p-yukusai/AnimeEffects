pragma Translator: "AnimeEffects.Tree"

import QtQuick
import QtQuick.Controls

// THE ANIMATION DOCK'S TREE COLUMN.
//
// One flat ListView over gui::ObjectTreeModel: the document row, then every visible node at
// its depth. The rows ARE the timeline's rows (heights included), and the model owns what a
// click, a key, a drag or a rename means - this file owns the ink, the hit rects and the
// pointer, the same split the Properties and Tools docks use.
//
// The pointer is handled here rather than in the delegate because a DRAG crosses rows: the
// press names a row, every move reports the pointer in the scene's own coordinates, and the
// model answers with the drop target. The one thing QML owns in the drag is the autoscroll
// timer - the model cannot tick - and the model still decides whether an edge is armed
// (ctrl::autoscroll's dwell), so the tree scrolls like the timeline's marquee.
Item {
    id: root

    // the host is a QQuickWidget, whose size hint is its root item's: a plain Item has none,
    // and the splitter then hands this column zero width
    implicitWidth: 240
    implicitHeight: 240

    // The scene's pixel ratio: the thumbnails are rendered for the screen they are drawn on.
    Component.onCompleted: {
        treeModel.setPixelRatio(Screen.devicePixelRatio);
        root.syncHBar();
        root.syncViewport();
    }
    // the root is the widget's whole surface, so scene coordinates ARE viewport coordinates;
    // the property states that once instead of three times in the handlers
    readonly property real sceneTop: root.mapToItem(null, 0, 0).y

    // THE HORIZONTAL VIEW of the row content. A deep nesting or a long name is reachable by
    // scrolling; the eye and the matte chip are PINNED (TreeRow's right zone), so the controls
    // that act on a row never move. contentWidth is the widest row's natural width, measured
    // in the dock's own font by the model.
    property real hOffset: 0
    // the pinned right zone of every row (TreeRow's eye + matte chip); the horizontal range is
    // computed against it so scrolling can never push the controls off the edge
    // THE ROW'S PINNED ZONE, at its WIDEST: fade 16 + chip slot 20 + eye 20 + edge 14 (the
    // vertical bar's 12px overlay lane plus 2). Read from TreeRow's own numbers, never restated -
    // a chip appearing widens the zone, and the horizontal range has to reserve the widest case.
    readonly property real kRightZone: 70
    readonly property real maxHOffset: Math.max(0, treeModel.contentWidth - (list.width - kRightZone))
    // did the KEYBOARD move last? A mouse click must not leave a focus ring behind, which is the
    // same rule the dock header's buttons follow (Qt::TabFocus there, focusVisible here)
    property bool focusVisible: false

    function clampHOffset() {
        if (hOffset > maxHOffset)
            hOffset = maxHOffset;
    }

    Rectangle {
        anchors.fill: parent
        color: theme.base
    }

    ListView {
        id: list
        objectName: "aeRowList"
        anchors.fill: parent
        // NO RIGHT INSET: the rows span the column so the selection tint reaches the edge, and
        // the vertical bar OVERLAYS their right 12px (which is why the pinned zone reserves that
        // lane - see kRightZone). The bottom keeps the horizontal bar's own lane - and that lane
        // is kBottomLane, the SAME number the model is told, because a viewport that is 12px
        // taller than the list is a scroll range 12px too long: the timeline could then be panned
        // past the last row and the two panes desynced at the bottom (measured).
        anchors.bottomMargin: root.kBottomLane
        model: treeModel
        // rows have their own heights; the delegate's height IS the model's rowHeight
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: true
        focus: true
        // the tree owns the keyboard while it has focus: Delete, Ctrl+A, F2, the arrows.
        // A key the model does NOT handle is left alone, so the application's own map still
        // gets it (Delete there belongs to the TIMELINE's keys, which is why the tree answers
        // first and only claims what it acts on).
        Keys.onPressed: (event) => {
            // a NON-MODIFIER key means the keyboard is what the user is driving, so the cursor
            // ring shows. Modifiers are deliberately excluded: Shift+wheel scrolls this list
            // sideways, and press-then-release of Shift must not light the ring (measured)
            const k = event.key;
            if (k !== Qt.Key_Shift && k !== Qt.Key_Control && k !== Qt.Key_Alt && k !== Qt.Key_Meta
                    && k !== Qt.Key_AltGr && k !== Qt.Key_CapsLock)
                root.focusVisible = true;
            if (treeModel.handleKey(event.key, event.modifiers)) {
                event.accepted = true;
                const row = treeModel.currentRow();
                if (row >= 0)
                    list.positionViewAtIndex(row, ListView.Contain);
            }
        }
        onActiveFocusChanged: {
            treeModel.focused = activeFocus;
            // the tree losing the keyboard is "clicking away" too: commit the edit in flight
            if (!activeFocus && treeModel.renaming)
                treeModel.commitRenaming();
        }
        // THE WHEEL, BOTH AXES, IN ONE PLACE. A WheelHandler on the list intercepts the event
        // before the Flickable's own handling, so leaving the vertical half to the list meant a
        // handler that only wanted Shift+wheel broke plain wheel-up (measured). One handler now
        // answers both: plain = vertical (three rows a notch, pixel-smooth on a trackpad),
        // Shift = the same gesture on the content's horizontal axis.
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (wheel) => {
                const notches = wheel.angleDelta.y / 120.0;
                if (wheel.modifiers & Qt.ShiftModifier) {
                    root.hOffset = Math.max(0, Math.min(root.maxHOffset, root.hOffset - notches * 40));
                } else {
                    // SMOOTH: a wheel notch glides a short way instead of jumping, and because the
                    // offset is reported per frame the LANES glide with it
                    const target = Math.max(0, Math.min(Math.max(0, list.contentHeight - list.height),
                                                        list.contentY - notches * 66));
                    smoothScroll.stop();
                    smoothScroll.from = list.contentY;
                    smoothScroll.to = target;
                    smoothScroll.start();
                }
                wheel.accepted = true;
            }
        }
        onContentYChanged: {
            if (!scrollProxy.moving)
                treeModel.scrollOffset = Math.max(0, contentY);
        }
        // THE OTHER DIRECTION. A pan of the TIMELINE sets the model's offset, and the rows must
        // follow it exactly like a scroll of this list would - the same number, applied here.
        // (Before this, the tree only ever REPORTED its offset: panning the timeline moved the
        // lanes and left the rows behind.)
        Connections {
            target: treeModel
            function onScrollOffsetChanged() {
                if (scrollProxy.moving)
                    return;
                root.applyScroll();
            }
        }
        ScrollBar.vertical: AeScrollBar { width: 12 }

        delegate: TreeRow {
            id: row
            rowIndex: index
            name: model.name
            depth: model.depth
            rowHeight: model.rowHeight
            isDocument: model.isDocument
            isFolder: model.isFolder
            hasChildren: model.hasChildren
            expanded: model.expanded
            selected: model.selected
            current: model.current
            nodeVisible: model.nodeVisible
            renaming: model.renaming
            treeFocused: treeModel.focused
            dropZone: model.dropZone
            guideMask: model.guideMask
            hOffset: root.hOffset
            focusVisible: root.focusVisible
            singleSelection: treeModel.selectionCount === 1
            glyph: model.glyph
            thumbUrl: model.thumbUrl
            thumbWidth: model.thumbWidth
            thumbHeight: model.thumbHeight
            hasMatte: model.hasMatte
            matteUrl: model.matteUrl

            onPressed: (row, modifiers, sceneX, sceneY) => {
                list.forceActiveFocus();
                root.focusVisible = false;
                treeModel.pressRow(row, modifiers);
                press.row = row;
                press.sceneX = sceneX;
                press.sceneY = sceneY;
                press.dragging = false;
            }
            onMoved: (row, sceneX, sceneY) => {
                if (!press.dragging) {
                    // 4px of travel before a press becomes a drag: the click slop the
                    // timeline's marquee uses, so a click that wobbles stays a click
                    if (Math.abs(sceneY - press.sceneY) + Math.abs(sceneX - press.sceneX) < 4)
                        return;
                    press.dragging = true;
                    treeModel.beginDrag(press.row);
                    dragTimer.start();
                }
                const viewportY = sceneY - root.sceneTop;
                dragLeader.y = viewportY;
                treeModel.updateDrag(viewportY, list.contentY);
            }
            onReleased: (row, modifiers, sceneX, sceneY) => {
                dragTimer.stop();
                if (press.dragging) {
                    press.dragging = false;
                    treeModel.endDrag(true);
                } else {
                    treeModel.releaseRow(row, modifiers);
                }
            }
            onVisibilityToggled: (row) => treeModel.toggleVisibility(row)
            onExpandToggled: (row) => treeModel.toggleExpanded(row)
            onRenameRequested: (row) => treeModel.beginRename(row)
            // (the keyboard returns to the list in TreeRow itself, where the editor hides:
            // a hidden TextInput keeps the active focus, and asking for it here ran before
            // the binding hid the field)
            onRenameCommitted: (row, text) => treeModel.commitRename(text)
            onRenameTextEdited: (row, text) => treeModel.setRenamingText(text)
            onRenameCancelled: (row) => treeModel.cancelRename()
            onMenuRequested: (row, sceneX, sceneY) => {
                list.forceActiveFocus();
                treeModel.showContextMenu(row, sceneX, sceneY);
            }
        }
    }

    NumberAnimation {
        id: smoothScroll
        target: list
        property: "contentY"
        duration: 120
        easing.type: Easing.OutCubic
    }

    // what a press remembers, so a drag can start on it and a click can collapse it
    QtObject {
        id: press
        property int row: -1
        property real sceneX: 0
        property real sceneY: 0
        property bool dragging: false
    }
    QtObject {
        id: scrollProxy
        property bool moving: false
    }

    // THE EDGE AUTOSCROLL. The model decides whether an edge is armed (the shared 24px band,
    // the 16px maximum and the 150ms direction-aware dwell live in ctrl/AutoScroll.h); this
    // timer only applies the step it answers with, then re-answers the drop target for the
    // pointer's unchanged SCREEN position - the content moves under it, which is what keeps
    // the indicator under the cursor.
    Timer {
        id: dragTimer
        interval: 16
        repeat: true
        onTriggered: {
            const viewportY = dragLeader.y;
            const step = treeModel.dragAutoScrollStep(viewportY);
            if (step === 0)
                return;
            const target = Math.max(0, Math.min(list.contentY + step,
                                                Math.max(0, list.contentHeight - list.height)));
            if (target === list.contentY)
                return;
            scrollProxy.moving = true;
            list.contentY = target;
            scrollProxy.moving = false;
            treeModel.scrollOffset = target;
            treeModel.updateDrag(viewportY, list.contentY);
        }
    }

    // The drag's own read-out: one row moves silently, several say how many. It rides the
    // pointer because the drop indicator alone cannot say what is being carried.
    Rectangle {
        id: dragLeader
        visible: treeModel.dragging && treeModel.dragCount > 1
        x: 12
        width: dragLabel.implicitWidth + 14
        height: 20
        radius: 4
        color: theme.floaterBody
        border.width: 1
        border.color: theme.floaterEdge
        Text {
            id: dragLabel
            // centred by hand, on whole pixels: a text origin on a fraction renders soft
            x: Math.round((parent.width - width) / 2)
            y: Math.round((parent.height - height) / 2)
            text: qsTr("%1 layers").arg(treeModel.dragCount)
            color: theme.text
            font: uiFont
        }
    }

    // ---- "Add new assets": the project's resource trees, ticked ----------------------
    //
    // The last hand-built widget dialog in this dock is gone: the LIST is the model's
    // (assetTrees/checked) and the ink is the scene's, so the chooser is the same object
    // vocabulary as the rest of the panel. It covers the column rather than opening a window
    // (the dock is where the layers will appear, so the answer belongs in it).
    Rectangle {
        id: addAssetsLayer
        anchors.fill: parent
        visible: treeModel.addAssetsOpen
        color: Qt.rgba(theme.base.r, theme.base.g, theme.base.b, 0.92)
        focus: visible
        Keys.onEscapePressed: treeModel.closeAddAssets()

        Rectangle {
            anchors.fill: parent
            anchors.margins: 8
            radius: 6
            color: theme.floaterBody
            border.width: 1
            border.color: theme.floaterEdge

            Text {
                id: addAssetsTitle
                x: 12
                y: 10
                width: parent.width - 24
                text: qsTr("Add new assets")
                color: theme.text
                font: uiFont
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
            ListView {
                id: addAssetsList
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: addAssetsTitle.bottom
                anchors.topMargin: 8
                anchors.bottom: addAssetsButtons.top
                anchors.bottomMargin: 12
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                model: treeModel.assetTrees
                clip: true
                ScrollBar.vertical: AeScrollBar { width: 12 }
                delegate: AeCheckBox {
                    width: addAssetsList.width - 12
                    text: modelData.name
                    checked: modelData.checked
                    textFont: uiFont
                    // AeCheckBox IS a CheckBox: the wheel is Qt's own `checked`/`clicked`,
                    // never a re-declared property (its header says why)
                    onToggled: treeModel.setAssetTreeChecked(index, checked)
                }
            }
            Row {
                id: addAssetsButtons
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 10
                spacing: 8
                AeButton {
                    text: qsTr("Cancel")
                    kind: "ghost"
                    onClicked: treeModel.closeAddAssets()
                }
                AeButton {
                    text: qsTr("Add")
                    kind: "primary"
                    onClicked: treeModel.addCheckedAssetTrees()
                }
            }
        }
    }

    // the empty state: a document with no layers says so instead of showing nothing
    Text {
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: parent.width - 32
        visible: treeModel.empty
        text: qsTr("No layers")
        color: theme.textMutedDim
        font: uiFont
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
    }

    // THE HORIZONTAL BAR is the app's own scroll bar (AeScrollBar: 12px lane, 4px pill), used
    // STANDALONE: there is no Flickable under it (the ListView owns the vertical axis), so the
    // two directions are written IMPERATIVELY rather than bound - a binding on `position` would
    // be broken for good by the first drag.
    AeScrollBar {
        id: hBar
        orientation: Qt.Horizontal
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 12
        height: 12
        visible: root.maxHOffset > 0
        onPositionChanged: {
            if (!pressed || !visible)
                return;
            const travel = Math.max(0.0001, 1 - size);
            root.hOffset = Math.max(0, Math.min(root.maxHOffset, (position / travel) * root.maxHOffset));
        }
    }
    // the bar follows the offset (and the offset follows the bar, above)
    function syncHBar() {
        if (!hBar || hBar.pressed)
            return;
        const content = treeModel.contentWidth + root.kRightZone;
        hBar.size = content > 0 ? Math.min(1, list.width / content) : 1;
        hBar.position = root.maxHOffset > 0 ? (root.hOffset / root.maxHOffset) * (1 - hBar.size) : 0;
    }
    Connections {
        target: root
        function onHOffsetChanged() { root.syncHBar() }
        function onMaxHOffsetChanged() { root.syncHBar() }
    }
    Connections {
        target: treeModel
        function onRowsChanged() {
            root.syncHBar();
            // THE RESET DROPS contentY. The model keeps the offset across it (its own rule), so
            // the list is put back where it was - after the new delegates exist, which is why
            // this is deferred one turn. Without it, adding a key scrolled the tree to the top
            // and dragged the timeline with it (reported twice).
            Qt.callLater(root.applyScroll);
        }
    }
    // the model's offset, applied to the list without reporting it back
    function applyScroll() {
        const want = Math.max(0, Math.min(treeModel.scrollOffset,
                                          Math.max(0, list.contentHeight - list.height)));
        if (Math.abs(list.contentY - want) < 0.5)
            return;
        scrollProxy.moving = true;
        list.contentY = want;
        scrollProxy.moving = false;
    }
    onWidthChanged: root.clampHOffset()
    // THE VIEWPORT IS THE LIST, NOT THE PANEL. The panel's height includes the horizontal bar's
    // lane below the list, so reporting the panel height made maxScrollOffset 12px too big and
    // the timeline (which clamps through the model) could scroll past where the tree's rows stop
    // - the reported bottom desync. One number, bound to the list that actually scrolls.
    readonly property int kBottomLane: 12
    onHeightChanged: root.syncViewport()
    // the list's OWN height changes with the panel's margins, so it is the number to follow
    function syncViewport() { treeModel.viewportHeight = Math.round(list.height); }
    Connections {
        target: list
        function onHeightChanged() { root.syncViewport() }
    }
    Component.onDestruction: treeModel.viewportHeight = 0
}
