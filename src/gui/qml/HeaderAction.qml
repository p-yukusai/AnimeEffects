import QtQuick
import QtQuick.Controls

// A menu command, bound to gui::MenuCommand. The QML Action is the menu's own
// action object (the Menu delegate receives it); the command is the window side
// that owns the words and the callback.
Action {
    id: action

    required property string commandId
    objectName: "action_" + commandId

    text: mainMenu.command(commandId).text
    enabled: mainMenu.command(commandId).enabled
    // Defer the command one event-loop turn. A menu closes as part of processing
    // the triggered row; a command that opens a modal dialog synchronously (New
    // project, Export, Settings) would otherwise leave the popup underneath the
    // dialog, because the menu never gets its closing pass.
    onTriggered: Qt.callLater(function() { mainMenu.command(commandId).trigger(); })
}
