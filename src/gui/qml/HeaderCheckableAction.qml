import QtQuick
import QtQuick.Controls

// A checkable menu command (the Panels toggles).
//
// Deliberately NOT a checkable QML Action. A checkable Action flips its own
// `checked` on trigger and assignment destroys any binding to the application's
// state; the header would then have two answers to "is this dock showing", and the
// one in the menu would go stale as soon as a dock was closed by its own header or a
// drag. The command is the only authority, so this Action stays a plain command and
// exposes the state as two ordinary properties; HeaderMenuItem.qml draws the check
// from them and reports the MenuItem's accessibility as checkable.
Action {
    id: action

    required property string commandId
    objectName: "action_" + commandId
    readonly property var command: mainMenu.command(commandId)

    // Read by HeaderMenuItem.qml through the delegate's `action` property.
    readonly property bool checkableState: true
    readonly property bool checkedState: command.checked

    text: command.text
    enabled: command.enabled
    // See HeaderAction.qml: the menu must close before a command that opens a
    // dialog can run.
    onTriggered: Qt.callLater(function() { command.trigger(); })
}
