pragma Translator: "AnimeEffects.Dialogs"

import QtQuick
import QtQuick.Controls

// A single-line text field matching the app's raised-input look.
//
// Qt Quick Controls' TextField with the app's own background, rather than a
// hand-rolled TextInput (see docs/export-dialog-implementation-notes.md, "Control
// inventory"). The chrome is ours; everything the editor does is Qt's. A hand-rolled
// TextInput had to re-earn caret motion, keyboard selection, drag-select, word-jumps,
// the clipboard and undo (the Basic style even supplies the editing context menu),
// input methods, and the horizontal scrolling that keeps a long line's caret in view
// - and it did not earn all of them: the MouseArea that focused the field also painted
// an arrow cursor over its text.
//
// `text` is the TextField's own property, so a caller can both read the committed
// value and reset it from outside (the destination picker clears its query on a
// refresh).
TextField {
    id: root

    // The name a screen reader announces and the accessibility tree answers to.
    // Defaults to the placeholder - which is what it always was - but the two are
    // different jobs: a search field's placeholder is the sentence that explains
    // what to type, its accessible name is what a probe (scripts/gui/ae_ui.py) can
    // address it by. An EMPTY placeholder is announced as "text" and cannot be
    // addressed by name at all. An explicit name is for a field whose placeholder
    // is not its name (the export dialog's Bitrate field shows "Auto").
    property string accessibleName: ""
    // Optional leading glyph, e.g. "magnifying-glass" for a search field.
    property string leadingIcon: ""
    // Show an x button that empties the field (the widget picker had
    // QLineEdit::setClearButtonEnabled).
    property bool clearButton: false
    // Dialog controls keep 12px; the inspector passes the app font.
    property font textFont: Qt.font({ pixelSize: 12 })
    // Forward the VERTICAL tree-navigation keys instead of letting the editor keep
    // them, and announce each as navigate(key). A chooser you have to Tab out of is
    // a chooser you fight: the search field keeps the text cursor while Up/Down move
    // the list's selection. They live in a Keys filter, which runs before the
    // editor's own handling, rather than in a keyPressEvent we do not own.
    //
    // Left/Right are deliberately NOT forwarded (design decision, contract B9): they
    // belong to the caret. Forwarding them made a stray arrow while typing collapse
    // or expand the highlighted platform - a tree action nobody asked for from inside
    // a text field, and one of the reported defects.
    property bool navigationKeys: false
    // NO `property bool enabled`: QQuickItem already has one and redeclaring it
    // shadows the base property (qmllint: [property-override]), so a disabled
    // field never reports disabled to the accessibility tree. See AeButton.
    //
    // NO `signal accepted` either, for the same reason: TextField inherits
    // accepted() from TextInput, and redeclaring it with a different signature is a
    // duplicate name. Return already reaches the caller, which is what it did.
    signal edited(string text)
    signal navigate(int key)

    implicitWidth: 160
    implicitHeight: 28
    Accessible.role: Accessible.EditableText
    Accessible.name: root.accessibleName !== "" ? root.accessibleName : root.placeholderText
    Accessible.onPressAction: root.forceActiveFocus()

    color: root.enabled ? theme.text : theme.textDisabled
    font: root.textFont
    placeholderTextColor: theme.textMutedDim
    selectionColor: theme.selection
    selectedTextColor: theme.selectionText
    selectByMouse: true
    clip: true
    // `hovered` is only kept up to date when hover is enabled (the TextField
    // default is false), and the raised-hover background is drawn from it.
    hoverEnabled: true
    // Room for the leading glyph and the clear button, so the text AND the
    // placeholder both start and stop clear of them. Keyed off the static
    // properties, not off `clearBtn.visible`: reflowing the line every time the x
    // button appears would shuffle the text the user is reading.
    leftPadding: root.leadingIcon !== "" ? 30 : 9
    rightPadding: root.clearButton ? 26 : 9
    topPadding: 0
    bottomPadding: 0

    onTextEdited: root.edited(text)

    // Focus the editable text. The field IS the editor here, so this is a spelling
    // of forceActiveFocus() that says why it exists: a caller that wants the field
    // focused when its scene opens calls this from Component.onCompleted.
    function forceInputFocus() { root.forceActiveFocus() }

    // The active-focus border, the hover border and the disabled surface are the
    // design's; the Basic default background (palette colours, a 2px focus border,
    // 200x40) is not. The control sizes this item for us.
    background: Rectangle {
        radius: 4
        color: !root.enabled ? theme.recessed
              : root.hovered ? theme.raisedHover : theme.raised
        border.width: 1
        border.color: root.activeFocus ? theme.focus
                      : root.hovered ? theme.hover : theme.raised
    }
    Image {
        id: lead
        visible: root.leadingIcon !== ""
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        width: 14
        height: 14
        source: root.leadingIcon === "" ? "" : theme.iconUrl(root.leadingIcon)
        sourceSize.width: 64
        sourceSize.height: 64
        fillMode: Image.PreserveAspectFit
        opacity: 0.8
    }
    AeIconButton {
        id: clearBtn
        visible: root.clearButton && root.text.length > 0 && root.enabled
        anchors.right: parent.right
        anchors.rightMargin: 3
        anchors.verticalCenter: parent.verticalCenter
        size: 22
        iconSize: 12
        iconName: "x"
        label: qsTr("Clear")
        // Clearing is a keystroke the user made, so it reports an edit: the
        // TextField only emits textEdited() for typing, and the export dialog's
        // bitrate field follows that signal.
        onClicked: { root.text = ""; root.edited(""); root.forceActiveFocus(); }
    }
    // Runs before the editor's own key handling, so Up/Down can move the list's
    // selection while everything else - including Left/Right and every character -
    // stays with the caret.
    Keys.onPressed: (event) => {
        if (!root.navigationKeys) return;
        switch (event.key) {
        case Qt.Key_Down:
        case Qt.Key_Up:
        case Qt.Key_PageDown:
        case Qt.Key_PageUp:
            root.navigate(event.key);
            event.accepted = true;
            break;
        default:
            break;
        }
    }
}
