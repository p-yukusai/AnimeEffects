pragma Translator: "AnimeEffects.Properties"

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The effect stack: one section header, a card per filter, and a searchable
// picker for adding one. Cards expand in place; there is no detached editor.
Item {
    id: root

    property bool collapsed: false
    property int dragIndex: -1
    property int dropIndex: -1
    property int insertionIndex: -1
    // ---- the catalog picker's keyboard state ---------------------------------
    // The picker is a Repeater of rows in a Column, not a ListView over a model, so
    // there is no currentIndex to lean on: this index IS the chooser's keyboard
    // state, and -1 means nothing is highlighted. It lives on the root because the
    // field that owns the focus and the rows that paint it are different branches of
    // this file.
    property int highlightIndex: -1
    // The popup's height cap. The rows scroll under it instead of being clipped away:
    // before this, any row past the fold was unreachable at all on a short window.
    readonly property int catalogMaxHeight: 420
    // gui::fuzzy::kNoMatch, mirrored so the QML never restates the number: a score of -1
    // is "this row does not answer the query".
    readonly property int fuzzyNoMatch: -1

    width: parent ? parent.width : 0
    implicitHeight: collapsed ? header.height : header.height + body.height
    height: implicitHeight

    function addEntry(entry) {
        picker.close();
        inspector.filters.addFilter(entry.type);
    }
    // One statement of the match rule, shared by the rows' `visible` binding, the
    // highlight walk and resetHighlight(). The query is a PARAMETER rather than a read
    // of search.text inside, so the BINDING that calls this names its dependency at
    // the call site: hasFilter() taught this file that an invokable call is not a
    // dependency, and a binding that reads nothing never updates.
    function queryMatches(index, query) {
        // THE SCORE IS THE MODEL'S (gui::fuzzy over name+description), not a substring
        // test. `notes.md`: "currently if you make a typo, you get an empty list, which is
        // a problem" - and it was, on purpose: every substring/subsequence rule needs the
        // query to be INSIDE a word, so "blurr" (one letter too many) matched nothing and
        // the list went blank. The matcher gained a bounded edit-distance rule for that
        // (gui/qml/FuzzyMatch.h's rule 7), and the catalog reads the same score the
        // destination picker does, so a typo now answers the row it meant.
        //
        // An empty query matches everything, which is the score's own behaviour (0 for
        // every row) and the reason there is no special case here.
        return inspector.filters.catalogScore(index, query) != fuzzyNoMatch;
    }
    // Whether the row can be TAKEN right now: it exists in this build, it is not
    // already in the stack, and the query keeps it. Derived from the catalog and the
    // model, never from the delegate's `visible`: visibility is a binding, and a key
    // that arrives in the same event as the text that changed it cannot be made
    // dependent on that binding having been re-evaluated first.
    function canChoose(index) {
        const entry = inspector.filters.catalog[index];
        if (!entry || !entry.available)
            return false;
        if (inspector.filters.hasFilter(entry.type))
            return false;
        return queryMatches(index, search.text);
    }
    function firstEligible() {
        for (let i = 0; i < catalogRepeater.count; ++i) {
            if (canChoose(i))
                return i;
        }
        return -1;
    }
    // Step the highlight by up to `delta` choosable rows. Rows that cannot be taken
    // are stepped OVER - an `added` or `coming soon` line never holds the highlight -
    // and the ends CLAMP: the press that runs out of rows changes nothing and never
    // wraps around to the other end.
    function step(delta) {
        if (delta === 0)
            return;
        const forward = delta > 0;
        // A highlight that no longer points at a row that can be taken (the stack
        // changed under it) is not a position to step from: drop it first, so the
        // press starts from the matching end like a fresh one.
        if (highlightIndex >= 0 && !canChoose(highlightIndex))
            highlightIndex = -1;
        // Nothing highlighted yet: the first press starts from the matching end, so
        // it lands on a row instead of spending itself doing nothing.
        let next = highlightIndex;
        if (next < 0)
            next = forward ? -1 : catalogRepeater.count;
        for (let n = 0; n < Math.abs(delta); ++n) {
            let candidate = next + (forward ? 1 : -1);
            while (candidate >= 0 && candidate < catalogRepeater.count
                   && !canChoose(candidate))
                candidate += forward ? 1 : -1;
            if (candidate < 0 || candidate >= catalogRepeater.count)
                break;          // the clamp: no further row this way, so stay put
            next = candidate;
        }
        if (next < 0 || next >= catalogRepeater.count || next === highlightIndex)
            return;
        highlightIndex = next;
        revealHighlight();
    }
    // Keep the highlighted row on screen: the popup is capped at catalogMaxHeight, so
    // PageDown can land below the fold. The row's y is its position in the Column,
    // which is the Flickable's content space; nothing renumbers rows while stepping -
    // only a query change does, and that path goes back to the top instead.
    function revealHighlight() {
        const item = catalogRepeater.itemAt(highlightIndex);
        if (!item)
            return;
        const top = item.y;
        const bottom = top + item.height;
        if (top < catalogView.contentY) {
            catalogView.contentY = top;
        } else if (bottom > catalogView.contentY + catalogView.height) {
            const limit = Math.max(0, catalogView.contentHeight - catalogView.height);
            catalogView.contentY = Math.min(bottom - catalogView.height, limit);
        }
    }
    // On open and on every query change: the FIRST row that can be taken becomes the
    // target, so Return always has one and typing re-aims it at the first match
    // instead of leaving it on a row the query just hid. The list goes back to the top
    // with it - an offset that was right for the old result set is not a position in
    // the new one.
    function resetHighlight() {
        highlightIndex = firstEligible();
        catalogView.contentY = 0;
    }
    // The pointer taking over: hover and the keyboard never light two rows at once, so
    // a pointer that reaches a row releases the keyboard's highlight and leaves the
    // hovered row as the one highlight.
    function clearHighlight() {
        highlightIndex = -1;
    }
    // Return: the highlighted row, or - when nothing is highlighted, or what was
    // highlighted can no longer be taken - the first row that can. Eligibility is
    // re-checked rather than trusted: the old handler read an `entry` property no
    // delegate declared (the delegate's is `catalogEntry`), so Return closed the popup
    // and then threw on the missing type instead of adding anything.
    function acceptHighlighted() {
        let index = highlightIndex;
        if (index < 0 || !canChoose(index))
            index = firstEligible();
        if (index < 0)
            return;             // nothing can be taken: Return leaves the popup open
        const entry = inspector.filters.catalog[index];
        if (entry)
            addEntry(entry);
    }
    function insertionAt(sceneY) {
        for (let i = 0; i < cards.count; ++i) {
            const card = cards.itemAt(i);
            if (!card) continue;
            const centre = card.mapToItem(null, 0, card.height / 2).y;
            if (sceneY < centre)
                return i;
        }
        return cards.count;
    }
    function updateDropLine(insertion) {
        insertionIndex = insertion;
        if (cards.count === 0) {
            dropLine.y = 0;
        } else if (insertion < cards.count) {
            const card = cards.itemAt(insertion);
            dropLine.y = card ? card.y : 0;
        } else {
            const card = cards.itemAt(cards.count - 1);
            dropLine.y = card ? card.y + card.height : 0;
        }
    }

    Item {
        id: header
        width: parent.width
        height: 32

        MouseArea {
            id: headerMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.collapsed = !root.collapsed
        }

        Image {
            id: caret
            x: 16
            anchors.verticalCenter: parent.verticalCenter
            width: 14
            height: 14
            source: theme.iconUrl(root.collapsed ? "caret-right" : "caret-down")
            sourceSize.width: 64
            sourceSize.height: 64
            fillMode: Image.PreserveAspectFit
            opacity: headerMouse.containsMouse ? 0.9 : 0.55
        }

        Text {
            x: 36
            anchors.verticalCenter: parent.verticalCenter
            text: inspector.filters.title
            color: theme.text
            font.family: uiFont.family
            font.pointSize: uiFont.pointSize
            font.bold: true
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        AeIconButton {
            id: addButton
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            width: 22
            height: 22
            size: 22
            iconSize: 14
            iconName: "plus-bold"
            label: inspector.filters.addLabel
            showTip: true
            onClicked: {
                picker.open();
                search.forceActiveFocus();
            }
        }
    }

    Item {
        id: body
        y: header.height
        width: parent.width
        height: root.collapsed ? 0 : bodyColumn.implicitHeight + 8
        visible: !root.collapsed

        Column {
            id: bodyColumn
            x: 12
            y: 0
            width: parent.width - 24
            spacing: 2

            Repeater {
                id: cards
                model: inspector.filters.rows.length
                delegate: InspectorFilterCard {
                    required property int index
                    filterRow: inspector.filters.rows[index]
                    cardIndex: index
                    width: bodyColumn.width
                    onDragStarted: (index) => {
                        root.dragIndex = index;
                        root.updateDropLine(index);
                        dropLine.visible = true;
                    }
                    onDragMoved: (index, sceneY) => {
                        if (root.dragIndex < 0) return;
                        const insertion = root.insertionAt(sceneY);
                        root.updateDropLine(insertion);
                        root.dropIndex = insertion > root.dragIndex ? insertion - 1 : insertion;
                        root.dropIndex = Math.max(0, Math.min(cards.count - 1, root.dropIndex));
                        dropLine.visible = true;
                    }
                    onDragEnded: (index, sceneY) => {
                        if (root.dragIndex < 0) return;
                        const source = root.dragIndex;
                        if (root.dropIndex >= 0 && root.dropIndex !== source) {
                            const row = cards.itemAt(source) ? cards.itemAt(source).filterRow : null;
                            if (row)
                                inspector.filters.moveFilter(row.type, root.dropIndex);
                        }
                        root.dragIndex = -1;
                        root.dropIndex = -1;
                        root.insertionIndex = -1;
                        dropLine.visible = false;
                    }
                }
            }

            Text {
                visible: cards.count === 0
                x: 4
                topPadding: 4
                bottomPadding: 4
                text: inspector.filters.noFiltersText
                color: theme.textMuted
                font: uiFont
                }
        }

        Rectangle {
            id: dropLine
            visible: false
            x: 12
            width: bodyColumn.width
            height: 2
            color: theme.accent
            z: 10
        }
    }

    // The catalog chooser. Its keyboard contract is the app's one keyboard contract,
    // the same one tests/picker_keyboard.cpp pins for DestinationPicker: typing
    // filters, Up/Down and PageUp/PageDown walk the visible rows WITHOUT leaving the
    // field (the field keeps the caret), Return takes the highlighted row, Escape
    // closes this popup through the closePolicy below, and Left/Right stay with the
    // caret. Stepping clamps at the ends and skips rows that cannot be taken.
    //
    // Every one of those keys is handled BY THE FIELD, not by a Keys handler here,
    // and that is not a style choice: a Popup's contentItem is parented into the
    // window overlay, outside the item tree under root, so a Keys handler on root
    // would never see a key pressed while the field has focus. The field is where the
    // focus is, so the field is where the handler has to be.
    Popup {
        id: picker
        parent: root
        width: Math.min(320, root.width - 16)
        x: Math.max(8, root.width - width - 12)
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        modal: false
        dim: false
        // Opening aims Return at the first row that can be taken, so the popup is
        // usable the moment a key is pressed rather than only after an arrow.
        onOpened: {
            const top = root.mapToItem(null, 0, 0).y;
            roomBelow = Math.max(0, root.Window.height - (top + header.height) - 8);
            roomAbove = Math.max(0, top - 8);
            root.resetHighlight();
            search.forceActiveFocus();
        }
        onClosed: search.text = ""

        // THE POPUP FITS THE HOST, AND IT OPENS THE WAY THAT FITS.
        //
        // A Popup renders inside the QQuickWidget's own window, so whatever the widget's
        // rectangle does not cover is cut - and this section is the panel's LAST one, so
        // how much room it has below depends entirely on the window height: measured
        // 2026-10-26 on a 1280x662 window, `root` sits at scene y=321 in a 396px host, so
        // below the 32px header there are 43px and the catalog cannot fit down; on a
        // taller window the same section sits higher and it can. So the side is DECIDED,
        // not assumed: down when a usable list fits (kUsableHeight below), otherwise up,
        // otherwise whichever side has more room.
        //
        // The room is measured from the host (`root.Window.height`, an attached property
        // of an Item - writing it on the Popup itself fails with "Window.window only
        // supports types derived from Item", measured) and from where this section
        // actually sits in the scene, which `mapToItem` reports INCLUDING the ScrollView's
        // scroll offset. Scrolling the panel while the list is open can therefore leave an
        // open list where it was; the popup takes the wheel while it is up, so that costs
        // nothing in practice.
        readonly property int kUsableHeight: 160
        // MEASURED AT OPEN, NOT BOUND. `mapToItem` cannot notify, and the thing being
        // measured (where this section sits inside the host widget) changes with the
        // panel's scroll - so a binding over it is evaluated once, often before the item
        // is in a window at all, and then never again: that is how the popup came to open
        // downward with a height clamped by a host edge it had measured as 0. onOpened
        // writes the two numbers, the y/height bindings below read them, and both are
        // fresh exactly when the list appears.
        property real roomBelow: 0
        property real roomAbove: 0
        // HOW TALL the list WANTS to be, computed WITHOUT the delegates. The delegate
        // column's `implicitHeight` is 0 on the first evaluation (the Repeater creates its
        // items while the popup is being opened, after this would have run). The row height
        // is the MODEL's (`catalogRowHeight`), the same number the delegate uses, so this
        // floor is exact and the column can only ever agree with it.
        readonly property real rowsHeight: {
            let total = 0;
            let shown = 0;
            const catalog = inspector.filters.catalog;
            for (let i = 0; i < catalog.length; ++i) {
                if (!root.queryMatches(i, search.text))
                    continue;
                total += inspector.filters.catalogRowHeight(i);
                ++shown;
            }
            return shown > 0 ? total + (shown - 1) * pickerContent.spacing : 0;
        }
        readonly property real wantedHeight: Math.min(root.catalogMaxHeight,
                                                     search.implicitHeight
                                                     + pickerContent.spacing
                                                     + Math.max(rowsHeight,
                                                                catalogColumn.implicitHeight) + 12)
        // Down when a usable list fits there, otherwise up, otherwise the larger side.
        readonly property bool opensUp: roomBelow < Math.min(wantedHeight, kUsableHeight)
                                        && roomAbove > roomBelow
        y: opensUp ? header.height - height - 4 : header.height + 4
        height: Math.max(120, Math.min(wantedHeight, opensUp ? roomAbove : roomBelow))
        // What is left for the rows once the search field has taken its row - the
        // Flickable's height, so a constrained popup SCROLLS instead of hiding rows.
        readonly property real listHeight: Math.max(0, height - 12 - search.implicitHeight
                                                       - pickerContent.spacing)
        background: Rectangle {
            radius: 4
            color: theme.floaterBody
            border.width: 1
            border.color: theme.floaterEdge
        }

        contentItem: Column {
            id: pickerContent
            // NOTE: x and width are overwritten by QQuickPagePrivate::relayout(), which
            // also sets this item's height: a Popup's content item is stretched to the
            // available size, so only its implicitHeight and its spacing mean anything
            // to the popup's own height formula. They are kept because they were here
            // before this picker learned to scroll.
            x: 6
            width: picker.width - 12
            spacing: 4

            AeTextField {
                id: search
                width: parent.width
                implicitHeight: 28
                leadingIcon: "magnifying-glass"
                textFont: uiFont
                        placeholderText: inspector.filters.searchLabel
                accessibleName: inspector.filters.searchLabel
                // The field keeps the caret and hands Up/Down/PageUp/PageDown back as
                // navigate(); Left/Right never come through that signal and stay with
                // the text cursor. That is AeTextField's contract, and the one the
                // destination picker is driven by.
                navigationKeys: true
                onNavigate: (key) => {
                    switch (key) {
                    case Qt.Key_Down: root.step(1); break;
                    case Qt.Key_Up: root.step(-1); break;
                    case Qt.Key_PageDown: root.step(10); break;
                    case Qt.Key_PageUp: root.step(-10); break;
                    default: break;
                    }
                }
                // Return is the field's own accepted(), not a Keys handler. A
                // TextField already turns Return/Enter into accepted() after its own
                // key handling; a Keys handler on top of that fires first, treats the
                // delegate's `entry` as if it existed, and then lets the editor act on
                // the same key - which is exactly how Return came to close this popup
                // and throw instead of adding anything.
                onAccepted: root.acceptHighlighted()
            }

            // Typing re-aims the highlight at the first match. A Connections rather
            // than a binding, because resetHighlight() WRITES highlightIndex: a
            // binding that assigns its own dependency is broken by QML on the first
            // assignment.
            Connections {
                target: search
                function onTextChanged() { root.resetHighlight() }
            }

            // The rows, in a clipped Flickable: the popup's height is capped, and a
            // Column on its own had no way to reach the rows past the cap. It stays a
            // Column of the same delegates - the filtering, the row heights and the
            // "added" state already live there, and a ListView would need a second
            // model beside inspector.filters.catalog, which is a second place for the
            // rows to disagree with the stack.
            Item {
                id: catalogArea
                width: parent.width
                // The cap, minus the popup's own `+ 12` and the search field's row, so
                // the popup never exceeds catalogMaxHeight. When the catalog is
                // shorter, the rows take their own height and the bar below stays
                // hidden.
                // The popup tells us the room it has (picker.listHeight), so a popup
                // clamped by the host widget's edge scrolls its rows instead of
                // drawing them past its own bottom.
                height: picker.listHeight

                Flickable {
                    id: catalogView
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: catalogColumn.implicitHeight
                    flickableDirection: Flickable.VerticalFlick
                    // The ends are the ends: no rubber band, so contentY is always a
                    // position that maps to a row (revealHighlight() writes it).
                    boundsBehavior: Flickable.StopAtBounds
                    // Nothing to drag when everything fits, so the pointer keeps
                    // behaving like a plain list there.
                    interactive: contentHeight > height

                    Column {
                        id: catalogColumn
                        // The right 10 px are the scroll bar's lane, so a highlighted
                        // row stops short of it.
                        width: catalogView.width - 10
                        spacing: 4

                        Repeater {
                            id: catalogRepeater
                            model: inspector.filters.catalog.length
                            delegate: Item {
                                id: catalogItem
                                required property int index
                                property var catalogEntry: inspector.filters.catalog[index]
                                property var modelData: catalogEntry
                                // `rows.length` is read as a dependency: hasFilter() is
                                // a call, and a call registers no binding dependency in
                                // QML.
                                property bool added: modelData.available && inspector.filters.rows.length >= 0
                                                     && inspector.filters.hasFilter(modelData.type)
                                property bool enabledEntry: modelData.available && !added
                                // This row is the popup's keyboard target. The fill
                                // below reads it, and it is announced as the row's
                                // selected state, so the keyboard's position exists for
                                // a screen reader as well as for the eye.
                                readonly property bool keyboardTarget: root.highlightIndex === index
                                width: parent.width
                                height: visible ? inspector.filters.catalogRowHeight(index) : 0
                                // The rule itself is stated once, in root.queryMatches();
                                // search.text is named HERE because that is what this
                                // binding has to depend on.
                                visible: root.queryMatches(index, search.text)
                                opacity: enabledEntry ? 1.0 : 0.55

                                Rectangle {
                                    anchors.fill: parent
                                    radius: 3
                                    // One highlight, never two. The keyboard's target is
                                    // painted with the same raised token hover uses, and
                                    // while the keyboard highlight is live the hover fill
                                    // is suppressed: a pointer that reaches a row releases
                                    // the keyboard's highlight instead (see the MouseArea),
                                    // so the two never show at once.
                                    color: catalogItem.enabledEntry
                                           && (catalogItem.keyboardTarget
                                               || (mouse.containsMouse && root.highlightIndex < 0))
                                           ? theme.raisedHover : "transparent"
                                }
                                Column {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    Text {
                                        width: parent.width
                                        text: modelData.name
                                                 + (!modelData.available ? "  ·  " + inspector.filters.comingSoonLabel
                                                                         : catalogItem.added ? "  ·  " + inspector.filters.addedLabel : "")
                                        color: theme.text
                                        font: uiFont
                                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        width: parent.width
                                        visible: modelData.available
                                        text: modelData.description
                                        color: theme.textMuted
                                        font: uiFont
                                                        elide: Text.ElideRight
                                    }
                                }
                                MouseArea {
                                    id: mouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    enabled: parent.enabledEntry
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.addEntry(parent.catalogEntry)
                                    // The pointer reaching a row takes the highlight over:
                                    // the keyboard's is released so only the row under the
                                    // pointer is lit. Released, not moved - a stray
                                    // mouse-over must not rewrite which row Return takes.
                                    onContainsMouseChanged: {
                                        if (containsMouse)
                                            root.clearHighlight();
                                    }
                                    // A MouseArea that HAS a wheel handler accepts wheel
                                    // events (Qt's qquickmousearea.cpp:
                                    // we.setAccepted(isWheelConnected())), which would leave
                                    // the rows unscrollable by wheel the moment they exceed
                                    // the cap. Declining them hands the wheel back to the
                                    // Flickable.
                                    onWheel: (wheel) => wheel.accepted = false
                                }
                                Accessible.role: Accessible.ListItem
                                Accessible.name: modelData.name
                                Accessible.description: modelData.description
                                Accessible.selected: catalogItem.keyboardTarget
                            }
                        }
                    }

                    // The scroll bar is the app's one scroll affordance, ATTACHED so its
                    // position, size and active state come from the Flickable's
                    // visibleArea (Qt's QQuickScrollBarAttached wires exactly those three
                    // and nothing else). Two things are stated rather than inherited,
                    // both from the Basic style's ScrollBar.qml:
                    //  - the style's contentItem is TRANSIENT (a fade-in that times out
                    //    450 ms after the last interaction). The design has no fade, so
                    //    contentItem is replaced outright, which also removes the state
                    //    that drove it;
                    //  - with the fade gone, the style's `visible: policy !== AlwaysOff`
                    //    would draw an empty track when nothing overflows, so visible is
                    //    the ratio test the design asks for: show it only when the
                    //    content does not fit.
                    // It is parented to the catalog AREA, not to the Flickable: QML
                    // children of a Flickable are stacked into its contentItem and would
                    // scroll with the rows, and Qt only positions a bar whose parentItem
                    // IS the flickable - which is also why the geometry is stated here.
                    ScrollBar.vertical: AeScrollBar {
                        id: catalogBar
                        parent: catalogArea
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        orientation: Qt.Vertical
                        width: 12
                    }
                }
            }

            Text {
                width: parent.width
                visible: {
                    for (let i = 0; i < catalogRepeater.count; ++i) {
                        const item = catalogRepeater.itemAt(i);
                        if (item && item.visible) return false;
                    }
                    return true;
                }
                text: inspector.filters.noMatchLabel
                color: theme.textMuted
                font: uiFont
                    leftPadding: 4
                topPadding: 4
            }
        }
    }
}
