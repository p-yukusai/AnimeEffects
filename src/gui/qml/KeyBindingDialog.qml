// All qsTr() strings in this module live under one stable, module-scoped translation
// context (AnimeEffects.Dialogs) rather than the default filename context, so the
// entries are rename-safe and never collide with the C++ context of the shell class
// (gui::KeyBindingDialog). The .qm are embedded at :/i18n.
pragma Translator: "AnimeEffects.Dialogs"

import QtQuick
import QtQuick.Layouts
// For ScrollBar only (the list's attached bar, the export form's pattern): the
// controls in this form are the Ae* family; the bar is stock, styled below.
import QtQuick.Controls

// The key-bindings form, rendered as a full-bleed Qt Quick scene inside the
// KeyBindingDialog QDialog shell.
//
// THE SHAPE, top to bottom:
//
//   [ General ] [ Timeline ] [ View ] [ Tools ]          [ Reset keybinds ]
//   -------------------------------------------------------------------------
//   Undo last action        [ Ctrl + Z   ]   Conflicts with Timeline/Copy ...
//   Redo last action        [ Ctrl + Shift + Z ]
//   ...                                                        (scrolls)
//   -------------------------------------------------------------------------
//   [ Cancel ]  [ Ok ]
//
// Everything it decides lives in the "keyBindingModel" context property
// (gui::KeyBindingViewModel); the paint lives here. Three rules the widget form
// implied and this scene states:
//
//   * ONE LIST, TABS BY GROUP. The widget form built a QTabWidget with a QFormLayout
//     per group. Here there is a single ListView over the whole model and a delegate
//     whose height collapses to 0 when its group is not the current tab, so the row
//     index the field reports to the model IS the model row - no per-tab offset
//     arithmetic, and switching tabs is one string comparison per delegate.
//   * THE FIELD IS NOT A TEXT EDITOR. A chord field must CAPTURE every key, and a
//     TextInput would consume the characters, arrows and Backspace it is supposed to
//     record (AeTextField's own Keys filter deliberately forwards cursor keys to the
//     caret). So the field is a focusable Rectangle with its own Keys.onPressed and
//     the same raised/rounded/focus-border chrome as AeTextField - the design's
//     visual language, without the editor underneath it.
//   * THE CONFLICT IS A SIBLING SENTENCE, not a suffix on the chord. The widget form
//     appended " (conflicted with X/Y)" to the QLineEdit's text; showing it as its
//     own muted line keeps the chord the field shows the chord the model holds.
//
// The model is addressed as `keyBindingModel`, NOT `model`: a ListView carries a
// `model` property of its own, and in THIS scene the delegate's `model.group` reading
// the tab's model is exactly the case the name is fixed for.
Item {
    id: root

    implicitWidth: 560
    // The scene HUGS its content: the top margin + the tab row + the gap + the
    // tallest group's rows + the footer. The shell reads this and sizes the window to
    // it. SizeRootObjectToView sets width/height but not implicitHeight, so this stays
    // readable; the ListView still scrolls for a shorter screen or a shorter tab.
    readonly property int rowHeight: 34
    implicitHeight: 16 + 28 + 12 + keyBindingModel.maxGroupRows * rowHeight + 8 + 52

    // The tab the list shows. The first group's title is the opening tab; the binding
    // keeps it live if the model's groups are ever re-seeded.
    property string currentGroup: keyBindingModel.groups.length > 0 ? keyBindingModel.groups[0] : ""

    // Escape leaves the dialog, exactly as it did in the widget form. The chord field
    // consumes Escape while it holds focus (a bare Escape is not a valid chord, so it
    // is ignored there and must not silently close the dialog mid-entry) - which is
    // why this is a Shortcut and not the field's business.
    Shortcut { sequence: "Escape"; onActivated: keyBindingModel.cancel() }

    Rectangle {
        id: shell
        anchors.fill: parent
        color: theme.base

        // A click on the shell's own margins (outside the list and the tab row) drops
        // focus from whatever chord field is being edited, which also ends its sub-key
        // window (the focusOut() the field reports below).
        MouseArea {
            anchors.fill: parent
            onPressed: {
                const fi = root.Window ? root.Window.activeFocusItem : null;
                if (fi) fi.focus = false;
            }
        }

        // ============================================================== the tabs
        // The group selector. AeButton is the design's button, and "selected" is the
        // raised secondary fill against the ghost of the others - the tab is a choice
        // among peers, so neither is the surface's one primary (that is Ok, below).
        RowLayout {
            id: tabRow
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: 16
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 6

            Repeater {
                model: keyBindingModel.groups
                AeButton {
                    text: modelData
                    kind: root.currentGroup === modelData ? "secondary" : "ghost"
                    onClicked: root.currentGroup = modelData
                }
            }

            Item { Layout.fillWidth: true }

            AeButton {
                text: qsTr("Reset keybinds")
                kind: "ghost"
                iconName: "arrow-clockwise"
                // Asks, does not act: the confirmation is a QMessageBox the shell
                // owns, and the shell calls resetAll() only on "Yes".
                onClicked: keyBindingModel.requestReset()
            }
        }

        // ============================================================== the rows
        ListView {
            id: list
            objectName: "keyBindingList"
            anchors.top: tabRow.bottom
            anchors.topMargin: 12
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: footer.top
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: keyBindingModel

            delegate: RowLayout {
                id: row
                width: list.width
                // A row of another tab takes no space AND is not drawn. The height
                // alone is not enough: a zero-height RowLayout still paints its
                // children (they overflow their parent), so every other group's labels
                // and fields were drawn on top of the visible tab's rows - measured on
                // the running dialog, where the General tab showed two rows of
                // overlapping text at the positions the Timeline and Tools rows
                // collapsed to. `visible` is what removes them from the paint; the
                // height keeps them out of the layout, and `index` still IS the model
                // row that press()/focusOut() take.
                visible: model.group === root.currentGroup
                height: visible ? root.rowHeight : 0
                spacing: 8

                Text {
                    id: rowLabel
                    Layout.preferredWidth: 200
                    Layout.alignment: Qt.AlignVCenter
                    text: model.label
                    color: theme.text
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    // The command's words are the row's identity, so they are real
                    // text in the accessibility tree (AeFieldLabel's fixed 104px lane
                    // is for short form labels; command labels run to ~34 chars).
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                Rectangle {
                    id: keyField
                    objectName: "keyBindingField"
                    // Readable by a probe (the accessibility name is an ATTACHED
                    // property and is not a plain property a test can read back).
                    property string accessibleName: model.label

                    Layout.preferredWidth: 180
                    Layout.preferredHeight: 28
                    Layout.alignment: Qt.AlignVCenter
                    radius: 4
                    color: !enabled ? theme.recessed
                         : fieldHover.hovered ? theme.raisedHover : theme.raised
                    border.width: 1
                    border.color: activeFocus ? theme.focus
                                : fieldHover.hovered ? theme.hover : theme.raised

                    // A focusable field the keyboard can Tab to and that a screen
                    // reader can address by the command it edits.
                    activeFocusOnTab: true
                    Accessible.role: Accessible.EditableText
                    Accessible.name: keyField.accessibleName
                    Accessible.description: model.text !== "" ? model.text : qsTr("Not set")
                    Accessible.onPressAction: keyField.forceActiveFocus()

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 9
                        anchors.rightMargin: 9
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        text: model.text !== "" ? model.text : qsTr("Not set")
                        color: model.text !== "" ? theme.text : theme.textMutedDim
                        font.pixelSize: 12
                    }

                    HoverHandler { id: fieldHover; enabled: keyField.enabled; cursorShape: Qt.IBeamCursor }
                    TapHandler { onTapped: keyField.forceActiveFocus() }

                    // The capture. Every key is taken from the editor (there is none)
                    // and handed to the model, which owns the rules; nothing here
                    // decides what a key means. auto-repeat is PASSED, not filtered
                    // here: the model is where "holding a key does nothing" is stated.
                    //
                    // Escape is the ONE key not consumed, so the Shortcut above still
                    // closes the dialog while a chord field has focus - the widget
                    // dialog did exactly that (QDialog rejects on Escape, and a
                    // QLineEdit never took it), and a chord cannot be Escape anyway.
                    Keys.onPressed: (event) => {
                        keyBindingModel.press(index, event.key, event.modifiers, event.isAutoRepeat);
                        event.accepted = event.key !== Qt.Key_Escape;
                    }
                    // Leaving the row ends its sub-key window, so the next key pressed
                    // after coming back replaces the chord instead of extending it.
                    onActiveFocusChanged: if (!activeFocus) keyBindingModel.focusOut(index)
                }

                Text {
                    id: conflictText
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    visible: model.conflict !== ""
                    text: qsTr("Conflicts with %1").arg(model.conflict)
                    color: theme.danger
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    // Meaning-bearing text (which other command owns the same chord)
                    // exists in the tree rather than only in pixels.
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
            }

            // The scroll bar is the export form's own pattern: attached to the
            // ListView so position, size and the press/drag/paging come from its
            // visibleArea instead of arithmetic, parented OUTSIDE the clip (the
            // shell) so the clip cannot cut it, and shown only when the content
            // overflows the tab's rows.
            ScrollBar.vertical: AeScrollBar {
                id: vbar
                parent: shell
                anchors.right: list.right
                anchors.top: list.top
                anchors.bottom: list.bottom
                orientation: Qt.Vertical
                width: 12
            }
        }

        // ============================================================== FOOTER
        // Pinned to the shell (NOT the list), so the action row does not scroll with
        // the form. Cancel is the secondary action, Ok is the ONE primary here; Ok
        // flushes the rows into the map and persists, Cancel discards.
        Rectangle {
            id: footer
            objectName: "formFooter"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 52
            color: theme.base
            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: theme.hairline
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                Item { Layout.fillWidth: true }
                AeButton {
                    text: qsTr("Cancel")
                    kind: "secondary"
                    onClicked: keyBindingModel.cancel()
                }
                AeButton {
                    text: qsTr("Ok")
                    kind: "primary"
                    onClicked: keyBindingModel.accept()
                }
            }
        }
    }
}
