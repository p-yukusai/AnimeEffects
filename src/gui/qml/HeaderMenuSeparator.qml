import QtQuick
import QtQuick.Controls

// The one separator the menus use. `MenuSeparator` owns the spacing and the
// separatorsCollapsible behaviour; the content item is the hairline.
MenuSeparator {
    padding: 4

    contentItem: Rectangle {
        implicitWidth: 188
        implicitHeight: 1
        // The widget menu's separator ran the full inner width of the panel; the
        // row's content is inset by the menu padding, so the rule reaches back out.
        x: -4
        width: parent.width + 8
        color: theme.floaterEdge
    }
}
