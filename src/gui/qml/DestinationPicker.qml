// The destination chooser scene, rendered full-bleed inside the DestinationPicker
// QDialog's QQuickWidget.
//
// It mixes four things deliberately, per element:
//   * the list is a ListView over a flat row model (QtQuick core), and its ROWS are
//     QtQuick.Controls ItemDelegates that carry this design's own background, caret,
//     badge and words - the control supplies activation and the focus policy, the row
//     supplies how it looks, what a click means, and (through its HoverHandler, not
//     the control's own hover state - measured, see the delegate) where the pointer is;
//   * its scroll bar is a QtQuick.Controls ScrollBar with this design's two visual
//     items;
//   * the search field, the buttons and the tooltip are the app's own Ae* components;
//   * and everything a person can decide - which rows exist, what is open, what Enter
//     means - lives in the model, so the scene is only ever a view.
// An earlier version of this comment claimed a QQuickWidget
// cannot render a Controls popup outside its own bounds; that is false - a
// `Popup.Window` draws outside the scene, though on Wayland whether it maps from
// a QQuickWidget host depends on the compositor's backend (measured 2026-10-03;
// notes, "The Wayland limitation is a backend limitation"). This scene is
// in-scene by design, not by impossibility.
//
// The scene is a pure VIEW. Everything a person can decide - which rows exist, what
// is open, what the search reveals, what is selected, what Enter means - is in the
// "picker" context property (gui::DestinationPickerModel), which the headless test
// (tests/destination_picker.cpp) drives with no QML engine, so the rules have one
// implementation and one place to break.
//
// The dialog's own words come from the model as translated strings
// (picker.searchPlaceholder, picker.okText, ...): they live in the catalog under
// the picker class's own context (gui::DestinationPicker), which is the one the
// shipped catalogs have already translated. The module pragma is kept so a future
// scene-local string has a home; there is deliberately none today.
pragma Translator: "AnimeEffects.Dialogs"

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: root
    // Context properties: picker (DestinationPickerModel) and theme (QmlTheme).

    // A QML item only has ACTIVE focus inside an ACTIVE window, and this scene is
    // built before the dialog is shown - so the search field's initial
    // forceInputFocus() lands on an inactive window and the field opens caret-less.
    // Re-assert it the moment the window becomes active. (The C++ shell also
    // activates the window on show; this is the QML half of the same fix.)
    readonly property bool windowActive: Window.active
    onWindowActiveChanged: if (windowActive) search.forceInputFocus()

    Rectangle {
        anchors.fill: parent
        color: theme.base
    }

    // A3/B11 (contract items 3 and 11): a click on the scene's own empty space
    // removes the focus from the search field, and the field's TEXT and the list's
    // SELECTION are unchanged by that click. The picker had no background MouseArea
    // at all until 2026-10-09, so a click beside the list left the field focused
    // with a caret in it while the pointer was elsewhere. It sits UNDER the content
    // in z-order, so it only ever sees the presses the controls, the list and the
    // buttons do not take - which is what "outside" means here. It clears the focus
    // and nothing else: the one thing the handler must not do is touch the text or
    // the model, which is the other half of item 11.
    MouseArea {
        anchors.fill: parent
        onPressed: {
            const fi = root.Window ? root.Window.activeFocusItem : null;
            if (fi) fi.focus = false;
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 8

        // ---- the search field ----
        // Typing filters; Up/Down and PageUp/PageDown move the list WITHOUT leaving
        // the field. The field keeps the text cursor; the list keeps the highlight.
        // Left/Right stay with the caret (contract B9) - a tree action driven from
        // inside a text field is a tree action nobody asked for.
        AeTextField {
            id: search
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            placeholderText: picker.searchPlaceholder
            accessibleName: picker.searchAccessibleName
            leadingIcon: "magnifying-glass"
            clearButton: true
            navigationKeys: true
            Component.onCompleted: forceInputFocus()
            onNavigate: (key) => {
                switch (key) {
                case Qt.Key_Down: picker.step(1); break;
                case Qt.Key_Up: picker.step(-1); break;
                case Qt.Key_PageDown: picker.step(10); break;
                case Qt.Key_PageUp: picker.step(-10); break;
                // Left/Right are NOT here: they belong to the caret (contract B9).
                // The field never forwards them, so a platform cannot be collapsed
                // by an arrow pressed while typing.
                default: break;
                }
            }
            onAccepted: picker.activateCurrent()
        }
        // The filter follows the TEXT, not the edited() signal: a clear button and
        // the accessibility layer both write the text without a keystroke, and the
        // widget version filtered on QLineEdit::textChanged for the same reason.
        Connections {
            target: search
            function onTextChanged() { picker.setQuery(search.text) }
        }

        // ---- the list ----
        Item {
            id: listArea
            Layout.fillWidth: true
            Layout.fillHeight: true

            property int hoveredRow: -1
            property string hoveredTooltip: ""

            ListView {
                id: list
                // What a test or a probe addresses this view by (the rows have
                // their own accessible names; the view itself is this).
                objectName: "pickerList"
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                // Reserve the lane the ScrollBar below stands in, so a row's
                // hover highlight stops short of it. (The combo dropdown derives
                // this from the bar's visibility; here the list is a page-height
                // Flickable whose height is the popup's, so an unconditional lane
                // avoids a width/visibility feedback cycle.)
                width: parent.width - 12
                clip: true
                model: picker
                boundsBehavior: Flickable.StopAtBounds
                cacheBuffer: 4000
                // The list can take focus with Tab; the search field is the usual
                // key holder, and forwards the navigation keys into the same calls.
                activeFocusOnTab: true
                // One handler, not one per key: Keys has no onPageUpPressed/
                // onPageDownPressed handler, and naming a handler that does not
                // exist makes the whole scene fail to load (silently white).
                Keys.onPressed: (event) => {
                    switch (event.key) {
                    case Qt.Key_Down: picker.step(1); event.accepted = true; break;
                    case Qt.Key_Up: picker.step(-1); event.accepted = true; break;
                    case Qt.Key_PageDown: picker.step(10); event.accepted = true; break;
                    case Qt.Key_PageUp: picker.step(-10); event.accepted = true; break;
                    case Qt.Key_Left: picker.collapseCurrentSection(); event.accepted = true; break;
                    case Qt.Key_Right: picker.expandCurrentSection(); event.accepted = true; break;
                    case Qt.Key_Return:
                    case Qt.Key_Enter: picker.activateCurrent(); event.accepted = true; break;
                    default: break;
                    }
                }

                // Every tree node is a row; one that is not on screen collapses to
                // zero height, so expansion and filtering never renumber anything.
                //
                // Measured on this Qt before the rows became ItemDelegates:
                //   * `hoverEnabled` defaults to TRUE here (unlike Button and CheckBox,
                //     which default false) and `focusPolicy` to 0x0 = NoFocus, so a row
                //     is not a Tab stop and the ListView's activeFocusOnTab +
                //     Keys.onPressed keep owning the keyboard - contract A1's tab order
                //     is untouched. Neither is stated below as a guess: both were read
                //     off a built ItemDelegate in an offscreen spike.
                //   * The control's OWN `hovered` cannot carry this row's highlight, and
                //     the reason is a measured one. An event trace of a 100px
                //     press-drag logged, for every row the pointer crossed, MouseMove
                //     and NO HoverLeave - so with hoverEnabled true, `hovered` latched
                //     true on all three crossed rows and was still true after release
                //     with the pointer on the last of them: exactly the leftover
                //     highlight this design never had. During that same gesture the
                //     row's HoverHandler reports nothing at all (listArea.hoveredRow
                //     stays -1 where the old Rectangle tracked 1,2,3), and with
                //     hoverEnabled false it reports what it always did, because the
                //     colour, the cursor and the tooltip read from the handler, as
                //     they always did. One divergence remains, measured and
                //     deliberate: while a button is held the handler no longer follows
                //     the pointer (the old rows did); the first buttonless move after
                //     release puts it back exactly where the old code left it.
                //   * The Basic style's ItemDelegate sets padding: 12, its own
                //     `background` (visible only when down/highlighted/visualFocus) and
                //     an IconLabel `contentItem`. Supplying our own background removes
                //     those fills - no pressed fill appears that this design does not
                //     have - and our children are anchored to the DELEGATE ROOT, whose
                //     parent is still the control, so the style's padding never moves
                //     them. The explicit width/height stay for the same reason: a
                //     Control sizes its delegates from padding and contentItem, and the
                //     collapsed row's height of 0 is load-bearing (it is how expansion
                //     and filtering avoid renumbering).
                //   * `clicked` and a TapHandler coexist on one row: a tap gives
                //     clicked=1 AND tapped=1, a double tap adds double=1. So select and
                //     toggle live in `onClicked` and the handler keeps only
                //     onDoubleTapped - wiring both would toggle a section twice.
                delegate: ItemDelegate {
                    id: rowItem
                    width: list.width
                    // THREE heights, because there are three kinds of row: the 30 px
                    // destination/section rhythm, the separator's own band, and 0 for a
                    // row that is not on screen (which is how expansion and filtering
                    // avoid renumbering - load-bearing, see the note above).
                    height: model.onScreen ? (model.isSeparator ? rowItem.separatorHeight : 30) : 0
                    visible: model.onScreen
                    // Off on purpose - see the measured note above. Hover is the
                    // HoverHandler's job; the control supplies activation and focus.
                    hoverEnabled: false
                    // A RULE IS NOT A ROW: nothing about it can be clicked, hovered,
                    // focused or announced, so the whole delegate is disabled rather
                    // than left enabled with three handlers that each have to remember
                    // to say no. A disabled Control neither emits `clicked` nor lights
                    // a HoverHandler, so the pointer passes over the rule as if the
                    // row were not there.
                    enabled: !model.isSeparator

                    // The separator's band: 12 px, which is the 1 px rule plus ~5 px of
                    // air on each side. Chosen against the 30 px rhythm it interrupts:
                    // with no margin the rule reads as an underline of the row above it
                    // (a claim about that row rather than a boundary after the block),
                    // and at a full 30 px it reads as one more row in the list. 12 px is
                    // short enough that the two blocks still run as ONE list - which is
                    // what they are - and tall enough to separate them at a glance.
                    readonly property int separatorHeight: 12

                    // C18: ONE statement of where this row sits horizontally, and
                    // every lane below is that statement plus a fixed offset. It is
                    // deliberately NOT `isSection`: a platform with a single
                    // destination, or a group whose rows are not variants of one
                    // platform, is appended with no section above it, so such a
                    // destination is level 0 and lines up with the sections instead
                    // of taking the one-lane-in child offset. `isSection` still says
                    // what the row MEANS (a heading that discloses vs. a row that can
                    // be selected); it no longer decides where anything is drawn.
                    // Lanes: caret 6, badge 24, label 48, then +14 per level.
                    readonly property int indentPx: model.level * 14

                    // The row's own highlight, in this design's terms: the selection
                    // token, or the raised token while hovered - not the style's
                    // background, which draws only for down/highlighted/visualFocus.
                    // The separator draws its own rule and nothing else, so it has no
                    // background at all: no selection token (it can never be current)
                    // and no hover fill (the pointer cannot reach it).
                    background: Rectangle {
                        radius: 4
                        color: {
                            if (model.isSeparator)
                                return "transparent";
                            if (model.isSection)
                                return rowHover.hovered ? theme.raised : "transparent";
                            if (model.selected)
                                return theme.selection;
                            return rowHover.hovered ? theme.raised : "transparent";
                        }
                    }

                    // The disclosure caret: the one affordance that says a platform
                    // opens. Quiet, like the platform row itself.
                    Image {
                        visible: model.isSection
                        x: 6 + rowItem.indentPx
                        anchors.verticalCenter: parent.verticalCenter
                        width: 12
                        height: 12
                        source: theme.iconUrl(model.expanded ? "caret-down" : "caret-right")
                        sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                        sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
                        smooth: true
                        fillMode: Image.PreserveAspectFit
                    }
                    // A platform's badge is its destinations' badge, so the closed
                    // list still says which platform each line is.
                    Image {
                        visible: model.iconName !== ""
                        x: 24 + rowItem.indentPx
                        anchors.verticalCenter: parent.verticalCenter
                        width: 18
                        height: 18
                        source: model.iconName === "" ? "" : theme.iconUrl(model.iconName)
                        sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                        sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
                        smooth: true
                        fillMode: Image.PreserveAspectFit
                    }
                    Text {
                        id: rowLabel
                        // What the E29 truncation test reads (truncated) by.
                        objectName: "rowLabelText"
                        anchors.left: parent.left
                        anchors.leftMargin: 48 + rowItem.indentPx
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: model.label
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        font.bold: model.isSection
                        color: {
                            // The broken-table statement is not a choice: dim it, so
                            // it does not read as a destination.
                            if (model.id === "" && !model.isSection) return theme.textMuted;
                            if (model.isSection) return theme.textMuted;
                            if (model.selected) return theme.selectionText;
                            return theme.text;
                        }
                    }

                    // THE STRUCTURAL RULE ITSELF (model.isSeparator; the `general`
                    // block's `separatorAfter` in the data table): ONE HAIRLINE ACROSS
                    // THE LIST.
                    //
                    // Spanning the full width is the whole point. The rule states
                    // something about the list - "everything above is a file with no
                    // platform of its own; everything below is grouped by platform" -
                    // which is not a fact about any row's depth: it neither belongs to
                    // the child lane (which would read it as part of the section below
                    // it) nor moves the lanes the rows around it draw their caret, badge
                    // and label in (C18: 0/24/48, +14 per level; all untouched).
                    //
                    // theme.hairline is the token every other rule in this design draws
                    // with (AeSectionHeader's ruled heading, the export form's footer
                    // hairline), so the picker's rule agrees with the form's by
                    // construction rather than by a copied literal.
                    Rectangle {
                        // What the scene test and a probe-off-the-scene read it by.
                        objectName: "rowSeparatorRule"
                        visible: model.isSeparator
                        x: 0
                        // Pixel-snapped, not centered with `anchors`: a 1 px rectangle
                        // on a fractional y lands across two device pixels and draws as
                        // a 2 px smear, which is what a hairline must not look like.
                        y: Math.round((rowItem.height - height) / 2)
                        width: list.width
                        height: 1
                        color: theme.hairline
                    }

                    // A rule has no words and nothing to announce, so it stays OUT of
                    // the accessibility tree rather than being exposed as a nameless
                    // list item every screen reader would stop on and have nothing to
                    // say about; the rows around it already say what the two blocks
                    // are.
                    Accessible.ignored: model.isSeparator
                    Accessible.role: model.isSection ? Accessible.Button : Accessible.ListItem
                    Accessible.name: model.label
                    Accessible.onPressAction: {
                        if (model.isSection) picker.toggleSection(index);
                        else picker.select(index);
                    }

                    // The row's hover, still - colour, pointing-hand cursor and tooltip
                    // all read from this handler, exactly as they did when the row was
                    // a Rectangle. It is not redundant with the control: measured, the
                    // two disagree during a press-drag (the control latches and never
                    // leaves, the handler tracks the pointer step by step), and only
                    // the handler behaves the way this design's highlight requires.
                    HoverHandler {
                        id: rowHover
                        // Belt and braces with the delegate's own `enabled: false`
                        // above: the rule is not hoverable at all, so it can never take
                        // the raised fill, the pointing-hand cursor or the tooltip (and
                        // never becomes the row a tooltip would be about).
                        enabled: !model.isSeparator
                        cursorShape: Qt.PointingHandCursor
                        onHoveredChanged: {
                            if (hovered) {
                                listArea.hoveredRow = index;
                                // E29 (decided 2026-09-30): a tooltip only where
                                // the label is ACTUALLY truncated, and then it
                                // carries the label in full (the elide hid it)
                                // ahead of the model's identification (the family
                                // and the id for a destination, the count for a
                                // section). An untruncated row's label already
                                // states the same thing - no tooltip for it.
                                listArea.hoveredTooltip = rowLabel.truncated
                                    ? (model.label + (model.tooltip !== "" ? "\n" + model.tooltip : ""))
                                    : "";
                            } else if (listArea.hoveredRow === index) {
                                listArea.hoveredRow = -1;
                                listArea.hoveredTooltip = "";
                            }
                        }
                    }
                    // One click on a platform opens or closes it; one click on a
                    // destination selects it; a double click on a destination is the
                    // answer. A platform never answers, whatever is clicked - and the
                    // separator never gets here at all (the delegate is disabled, and
                    // select() refuses it anyway).
                    onClicked: {
                        if (model.isSection) picker.toggleSection(index);
                        else picker.select(index);
                    }
                    // The DOUBLE tap only: the control above owns the single click.
                    // This stays a handler because it still sees every tap alongside
                    // `clicked` (measured: tapped and clicked both increment per tap,
                    // and double arrives once), so there is nothing to reconstruct from
                    // click timing. A double tap on a section still nets two toggles -
                    // two clicks, plus a doubleTapped that ignores sections - which is
                    // exactly what it did when the handler owned both.
                    TapHandler {
                        onDoubleTapped: {
                            if (!model.isSection) picker.activate(index);
                        }
                    }
                }

                // The list's scroll bar is a QtQuick.Controls ScrollBar ATTACHED to
                // the ListView, so its position, size, active and its
                // press/drag/paging come from the ListView's own visibleArea (D23)
                // instead of from arithmetic over contentY. Two things are stated
                // rather than inherited, both measured from the Basic style's
                // ScrollBar.qml:
                //
                //  - the style's contentItem is TRANSIENT (opacity 0 with a fade-in
                //    state that times out 450ms after the last interaction). The
                //    design has no fade, so contentItem is replaced outright, which
                //    also removes the state that drove it;
                //  - with the fade gone, the style's `visible: policy !== AlwaysOff`
                //    would draw an empty track when nothing overflows. So visible is
                //    the ratio test the design asks for: show it only when the
                //    content does not fit (D23).
                //
                // It is parented to the list AREA rather than the list: the attached
                // bar would otherwise re-parent itself to the ListView and sit inside
                // its clip. The Qt docs name this as the way to keep an attached bar
                // outside a clipping Flickable, and position/size/active stay
                // automatic.
                ScrollBar.vertical: AeScrollBar {
                    id: vbar
                    parent: listArea
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    orientation: Qt.Vertical
                    width: 12
                }
            }

            // The row tooltip. It is mounted in the window overlay (see
            // AeToolTip's "where it lives"), so neither the ListView's clip nor
            // the dialog's own stacking can cut it off or hide it behind a later
            // sibling; `ownerItem: listArea` names the item its rect is
            // expressed in. Its content is the hovered row's full label plus the
            // model's identification (E29's kept kind), and it sits on the ROW
            // rect passed as ownerX/Y/W/H in the list area's coordinates -
            // AeToolTip does the rest: below-first, flipping above at the bottom
            // edge, and shifted inside the window so it is never clipped
            // (E27/E28).
            //
            // The rect is the delegate's own, in the list area's coordinates -
            // which is why it must be re-read rather than captured once. A
            // ListView scrolls its CONTENT under a clip window that does not
            // move, so the delegate's y is a coordinate in the content, not on
            // screen: without subtracting the view's contentY a wheel under a
            // stationary hovered pointer left the tooltip anchored at the row's
            // PRE-SCROLL position (E27 violated in that one case; E28's clamp
            // still held, because it re-reads every ancestor's contentY on
            // purpose - see AeToolTip's own note). The read is in the binding
            // itself, so the dependency is on contentY and every scroll
            // re-evaluates it.
            AeToolTip {
                ownerItem: listArea
                text: listArea.hoveredTooltip
                shown: listArea.hoveredRow >= 0 && text !== ""
                ownerX: { const item = list.itemAtIndex(listArea.hoveredRow); return item ? list.x + item.x : 0 }
                ownerY: { const item = list.itemAtIndex(listArea.hoveredRow); return item ? list.y + item.y - list.contentY : 0 }
                ownerW: { const item = list.itemAtIndex(listArea.hoveredRow); return item ? item.width : 0 }
                ownerH: { const item = list.itemAtIndex(listArea.hoveredRow); return item ? item.height : 0 }
            }
        }

        // ---- manual mode has no button (decided 2026-10-08) ----
        // "My platform isn't listed" is gone from the footer. It read as a fourth
        // kind of answer standing beside the destinations, and the generic rows at
        // the top of the list are what it was there for. The way in is unchanged
        // underneath it: a query that matches nothing plus "Continue"
        // is the model's own empty-id fallback (activateCurrent() -> activate()
        // with no match -> chosenId cleared), so the dialog still hands the export
        // form no destination, which is what manual mode means.

        // ---- OK / Cancel ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Item { Layout.fillWidth: true }
            AeButton {
                text: picker.cancelText
                onClicked: picker.reject()
            }
            AeButton {
                text: picker.okText
                kind: "primary"
                onClicked: picker.activateCurrent()
            }
        }
    }

    // Keep the selection on screen when it moves by keyboard or by a search.
    Connections {
        target: picker
        function onCurrentRowChanged() {
            if (picker.currentRow >= 0)
                list.positionViewAtIndex(picker.currentRow, ListView.Contain);
        }
    }

    // B10 (decided 2026-09-30): Escape empties the search field FIRST and closes
    // the picker only when the field is already empty - a half-typed query is never
    // a reason to throw the dialog away. The handler is on the scene ROOT because
    // Escape belongs to the picker whatever holds focus: the field keeps Left/Right
    // for the caret and does not touch Escape, the list's handler takes its own
    // keys and leaves the rest, and whatever neither wants bubbles up to here.
    // The clear reports an edit exactly like the clear button does; the close
    // takes the Cancel button's own route (picker.reject, which the shell wires to
    // QDialog::reject), so one press is one action however it arrives. Both
    // branches consume the key (measured: a Keys key handler accepts unless it is
    // told otherwise) so the dialog shell never ALSO reacts to an Escape this
    // scene has answered.
    Keys.onEscapePressed: (event) => {
        if (search.text !== "") {
            search.text = "";
            search.edited("");
        } else {
            picker.reject();
        }
        event.accepted = true;
    }
}
