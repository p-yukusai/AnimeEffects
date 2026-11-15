// All qsTr() strings in this module live under one stable, module-scoped
// translation context (AnimeEffects.Dialogs) rather than the default filename
// context, so the entries are rename-safe and never collide with the C++ context
// of the shell class (gui::GeneralSettingDialog). The .qm are embedded at :/i18n.
pragma Translator: "AnimeEffects.Dialogs"

import QtQuick
import QtQuick.Layouts
// For ScrollBar and the accent swatches' Button only (the scroll host's attached
// bar, the export form's pattern): the row controls are the Ae* family; the bar is
// stock, styled below.
import QtQuick.Controls

// The settings form, rendered as a full-bleed Qt Quick scene inside the
// GeneralSettingDialog QDialog shell.
//
// THE SHAPE, top to bottom:
//
//   [ General | QoL | Keyboard | Mouse ]        <- one tab strip, four pages
//   General     Language / easing / range / theme / accent / donation / reset
//   QoL         autosave + delay / mesh / copy / id check / warnings / load / format
//   Keyboard    keybind delay
//   Mouse       three scrolling checkboxes
//   ---
//   [ Cancel ]  [ OK ]
//
// Everything it decides lives in the "generalSettingModel" context property
// (gui::GeneralSettingViewModel); the paint lives here. The four tabs of the widget
// QTabWidget are the same four, in the same order (General 0, QoL 1, Keyboard 2,
// Mouse 3 - the widget's comment named an FFmpeg tab that was never built).
//
// Two rules the widget form implied and this scene states:
//
//   * EVERY ROW IS A REAL LAYOUT. Rows are (AeFieldLabel, field) pairs inside one
//     GridLayout per page, so the label column has ONE width on each page and no row
//     computes a position - the same rule the export/new-project/project-setting
//     scenes follow. The PAGE host's height is a binding over the four GridLayouts'
//     own implicitHeight (never childrenRect), because the window follows it.
//   * The numeric fields are AeNumberField (ScrubSpinBox): a scrub writes the value
//     and DROPS the caller's binding, so each field re-syncs from the model in its
//     own Connections block.
//
// The checkboxes carry their own words rather than sitting beside a second copy of
// them in the label column: that is the new-project form's "Live link" row, and it
// is what gives every box an Accessible.name (requirement shared by the AT-SPI
// probes). A box whose words lived only in the label column would announce nothing.
//
// The model is addressed as `generalSettingModel`, NOT `model`: a Repeater (and a
// ListView) carries a `model` property of its own, and the export form's history
// with that shadowing is why the name is fixed.
Item {
    id: root

    implicitWidth: 520
    // The scene HUGS its current page: the tab strip, the page's own rows and the
    // band the pinned footer occupies (the Flickable's bottomMargin). The shell reads
    // this and sizes the window to it; it re-reads after a tab switch, because each
    // tab is a different set of rows.
    implicitHeight: tabBar.height + 16 + page.implicitHeight + 66

    // The shell owns the QDialog (and its own word for what happened); the scene only
    // reports that a choice was made. Two signals, so "accepted" and "cancelled"
    // cannot be confused for a state the scene never checked.
    signal accepted()
    signal rejected()

    Connections {
        target: generalSettingModel
        function onAcceptRequested() { root.accepted() }
        function onCancelRequested() { root.rejected() }
    }

    // Escape leaves the dialog. The editors that would otherwise swallow it keep it
    // while they hold focus (ScrubSpinBox's TextInput accepts the shortcut override),
    // exactly as in the export form.
    Shortcut { sequence: "Escape"; onActivated: generalSettingModel.cancel() }

    Rectangle {
        id: shell
        anchors.fill: parent
        color: theme.base

        // Background click on the shell's own margins (outside the scroll host): drop
        // focus from whatever is being edited, leaving what it holds untouched.
        MouseArea {
            anchors.fill: parent
            onPressed: {
                const fi = root.Window ? root.Window.activeFocusItem : null;
                if (fi) fi.focus = false;
            }
        }

        // ================================================================== TABS
        // A segmented control on a recessed track, the shape the widget's THEME row
        // used (an accent-filled pill for the active choice, no per-segment borders):
        // the widget's own QTabWidget chrome has no Qt Quick counterpart, and this is
        // the same "one of N" affordance the form already speaks.
        Rectangle {
            id: tabBar
            objectName: "formTabBar"
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 48
            color: theme.base

            Rectangle {
                id: track
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                height: 32
                width: tabsRow.implicitWidth + 4
                radius: 6
                color: theme.recessed

                Row {
                    id: tabsRow
                    anchors.centerIn: parent
                    spacing: 0
                    // The four tabs of the widget's QTabWidget, in its order. Modelled
                    // in the scene because it is pure structure; `currentTab` is what
                    // the model (and selectTab) drive.
                    Repeater {
                        model: [
                            { label: qsTr("General") },
                            { label: qsTr("QoL") },
                            { label: qsTr("Keyboard") },
                            { label: qsTr("Mouse") }
                        ]
                        delegate: AeButton {
                            required property var modelData
                            required property int index
                            text: modelData.label
                            kind: generalSettingModel.currentTab === index ? "primary" : "ghost"
                            onClicked: generalSettingModel.currentTab = index
                        }
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: theme.hairline
            }
        }

        // The content scrolls when it does not fit; the action row BELOW stays put and
        // reachable (it is the shell's own child, not part of the page).
        Flickable {
            id: scroller
            // What a test or a probe addresses this scroll host by.
            objectName: "formScroller"
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.topMargin: tabBar.height + 16
            anchors.bottomMargin: 66
            clip: true
            contentWidth: width
            contentHeight: page.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            // The official Qt idiom for "a click outside the field clears focus"
            // (Keyboard Focus in Qt Quick; the common MouseArea recipe): an item that
            // clears the active focus on a press, sitting BELOW the controls.
            //
            // It has to live INSIDE the Flickable, not on the shell behind it: a
            // QQuickFlickable accepts a left-button press over its whole area (that is
            // how dragging scrolls), so a background MouseArea underneath it never sees
            // the click. Here childAt() skips the page (a GridLayout takes no mouse
            // buttons) and reaches this item for a press that lands on empty space,
            // while a control still gets its own press first.
            //
            // accepted = false is load-bearing: without it this item would grab the
            // press and a drag that starts on empty space would no longer scroll the
            // page.
            MouseArea {
                anchors.fill: parent
                onPressed: (mouse) => {
                    const fi = root.Window ? root.Window.activeFocusItem : null;
                    if (fi) fi.focus = false;
                    mouse.accepted = false;
                }
            }

            // The four pages are siblings, all laid out by real layouts, with only the
            // current one visible. The host's height is the CURRENT page's, read from
            // the GridLayouts' own implicitHeight - so the window can hug each tab -
            // rather than a StackLayout, whose implicitHeight is the tallest page.
            Item {
                id: page
                objectName: "formPage"
                width: scroller.contentWidth
                height: implicitHeight
                implicitHeight: {
                    if (generalSettingModel.currentTab === 0) return pageGeneral.implicitHeight;
                    if (generalSettingModel.currentTab === 1) return pageQol.implicitHeight;
                    if (generalSettingModel.currentTab === 2) return pageKeyboard.implicitHeight;
                    return pageMouse.implicitHeight;
                }

                // ============================================================ GENERAL
                GridLayout {
                    id: pageGeneral
                    objectName: "pageGeneral"
                    width: page.width
                    visible: generalSettingModel.currentTab === 0
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8

                    // The labels live in the MODEL, not here: they are the widget's
                    // existing C++ translation entries ("Auto"/"English"/..., the
                    // util::Easing names, the timeline formats), and re-spelling them
                    // as qsTr() in this file would put them under the scene's context
                    // and drop every translation the catalogs already carry.
                    AeFieldLabel { text: qsTr("Language (needs restart)") }
                    AeComboBox {
                        objectName: qsTr("Language (needs restart)")
                        Layout.fillWidth: true
                        Layout.maximumWidth: 260
                        overlay: root
                        options: generalSettingModel.languageOptions
                        index: generalSettingModel.languageIndex
                        onActivated: (index) => generalSettingModel.languageIndex = index
                    }

                    AeFieldLabel { text: qsTr("Default keyframe easing") }
                    AeComboBox {
                        objectName: qsTr("Default keyframe easing")
                        Layout.fillWidth: true
                        Layout.maximumWidth: 260
                        overlay: root
                        options: generalSettingModel.easingOptions
                        index: generalSettingModel.easingIndex
                        onActivated: (index) => generalSettingModel.easingIndex = index
                    }

                    AeFieldLabel { text: qsTr("Default keyframe range") }
                    AeComboBox {
                        objectName: qsTr("Default keyframe range")
                        Layout.fillWidth: true
                        Layout.maximumWidth: 260
                        overlay: root
                        options: generalSettingModel.rangeOptions
                        index: generalSettingModel.rangeIndex
                        onActivated: (index) => generalSettingModel.rangeIndex = index
                    }

                    // Theme: the widget's segmented control, kept as a segmented
                    // control - the active choice is an accent pill on a recessed
                    // track. The entries come from the model (the shell seeds them
                    // from GUIResources::themeList(), as the widget did); `value` is
                    // the settings id, not a row index.
                    AeFieldLabel { text: qsTr("Theme") }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.maximumWidth: 260
                        implicitHeight: 32
                        radius: 6
                        color: theme.recessed
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 2
                            spacing: 0
                            Repeater {
                                model: generalSettingModel.themeOptions
                                delegate: AeButton {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    kind: generalSettingModel.themeKey === modelData.value ? "primary"
                                                                                            : "ghost"
                                    onClicked: generalSettingModel.themeKey = modelData.value
                                }
                            }
                        }
                    }

                    // Accent color: a row of square swatches. A Button with the
                    // design's own delegates, because the Ae* set has no swatch
                    // control and the widget hand-rolled this one too; keeping Button
                    // keeps focus, Space and the accessibility role. The swatch is the
                    // hue at a mid lightness (accentSwatch) - the fill token itself is
                    // theme-tuned and reads poorly as a bare box on this background.
                    AeFieldLabel { text: qsTr("Accent color") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: generalSettingModel.accentOptions
                            delegate: Button {
                                id: swatch
                                required property var modelData
                                required property int index
                                Layout.preferredWidth: 22
                                Layout.preferredHeight: 22
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                hoverEnabled: true
                                focusPolicy: Qt.StrongFocus
                                Accessible.role: Accessible.RadioButton
                                Accessible.name: qsTr("Accent color %1").arg(index + 1)
                                Accessible.onPressAction: if (swatch.enabled) swatch.click()
                                background: Rectangle {
                                    radius: 4
                                    color: swatch.modelData.swatch
                                    // Border on hover, a contrast ring on the active
                                    // choice (the widget's own cascade).
                                    border.width: 2
                                    border.color: generalSettingModel.accentIndex === swatch.modelData.value
                                                  ? theme.text
                                                  : swatch.hovered ? theme.outline : "transparent"
                                }
                                contentItem: Item { }
                                onClicked: generalSettingModel.accentIndex = swatch.modelData.value
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    AeFieldLabel { }
                    AeCheckBox {
                        text: qsTr("Allow donation menu")
                        checked: generalSettingModel.donationAllowed
                        onToggled: generalSettingModel.donationAllowed = checked
                    }

                    AeFieldLabel { }
                    Item {
                        Layout.alignment: Qt.AlignLeft
                        implicitWidth: resetButton.implicitWidth
                        implicitHeight: resetButton.implicitHeight
                        AeButton {
                            id: resetButton
                            text: qsTr("Reset recent files list")
                            kind: "secondary"
                            onClicked: generalSettingModel.requestResetRecentFiles()
                        }
                        AeToolTip {
                            ownerItem: resetButton
                            text: qsTr("Deletes all project entries from your recents")
                            shown: resetButton.hovered
                        }
                    }
                }

                // ================================================================ QoL
                GridLayout {
                    id: pageQol
                    objectName: "pageQol"
                    width: page.width
                    visible: generalSettingModel.currentTab === 1
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8

                    AeFieldLabel { }
                    AeCheckBox {
                        text: qsTr("Automatically save your project")
                        checked: generalSettingModel.autoSave
                        onToggled: generalSettingModel.autoSave = checked
                    }

                    AeFieldLabel { text: qsTr("Time (in minutes) between autosaves") }
                    AeNumberField {
                        id: autoSaveDelayField
                        Layout.fillWidth: true
                        Layout.preferredWidth: 160
                        Layout.maximumWidth: 260
                        label: qsTr("Time (in minutes) between autosaves")
                        // 0 would make the timer fire continuously; the model holds the
                        // same floor for every writer.
                        from: 1
                        to: 999
                        step: 1
                        value: generalSettingModel.autoSaveDelayMinutes
                        onValueEdited: (value) => { generalSettingModel.autoSaveDelayMinutes = value }
                        Connections {
                            target: generalSettingModel
                            function onAutoSaveDelayMinutesChanged() {
                                autoSaveDelayField.value = generalSettingModel.autoSaveDelayMinutes;
                            }
                        }
                    }

                    // This one is saved the moment it is clicked (the shell wires the
                    // model's signal straight to the key), which is what the widget's
                    // own connect did - it is not part of the OK batch.
                    AeFieldLabel { }
                    AeCheckBox {
                        text: qsTr("Show mesh when selecting FFD")
                        checked: generalSettingModel.autoShowMesh
                        onToggled: generalSettingModel.autoShowMesh = checked
                    }

                    AeFieldLabel { }
                    AeCheckBox {
                        text: qsTr("On copy send keys to the clipboard")
                        checked: generalSettingModel.autoCbCopy
                        onToggled: generalSettingModel.autoCbCopy = checked
                    }

                    AeFieldLabel { }
                    AeCheckBox {
                        text: qsTr("Enforce ID check on asset download")
                        checked: generalSettingModel.resIDCheck
                        onToggled: generalSettingModel.resIDCheck = checked
                    }

                    AeFieldLabel { }
                    AeCheckBox {
                        text: qsTr("Ignore export warnings")
                        checked: generalSettingModel.ignoreWarnings
                        onToggled: generalSettingModel.ignoreWarnings = checked
                    }

                    AeFieldLabel { }
                    AeCheckBox {
                        text: qsTr("Force project to load")
                        checked: generalSettingModel.forceSolverLoad
                        onToggled: generalSettingModel.forceSolverLoad = checked
                    }

                    AeFieldLabel { text: qsTr("Timeline format") }
                    AeComboBox {
                        objectName: qsTr("Timeline format")
                        Layout.fillWidth: true
                        Layout.maximumWidth: 260
                        overlay: root
                        options: generalSettingModel.timeFormatOptions
                        index: generalSettingModel.timeFormatIndex
                        onActivated: (index) => generalSettingModel.timeFormatIndex = index
                    }
                }

                // =========================================================== KEYBOARD
                GridLayout {
                    id: pageKeyboard
                    objectName: "pageKeyboard"
                    width: page.width
                    visible: generalSettingModel.currentTab === 2
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8

                    AeFieldLabel { text: qsTr("Global keybind delay (ms)") }
                    AeNumberField {
                        id: keyDelayField
                        Layout.fillWidth: true
                        Layout.preferredWidth: 160
                        Layout.maximumWidth: 260
                        label: qsTr("Global keybind delay (ms)")
                        from: 0
                        to: 10000
                        step: 1
                        value: generalSettingModel.keyDelayMs
                        onValueEdited: (value) => { generalSettingModel.keyDelayMs = value }
                        Connections {
                            target: generalSettingModel
                            function onKeyDelayMsChanged() {
                                keyDelayField.value = generalSettingModel.keyDelayMs;
                            }
                        }
                    }
                }

                // ============================================================== MOUSE
                GridLayout {
                    id: pageMouse
                    objectName: "pageMouse"
                    width: page.width
                    visible: generalSettingModel.currentTab === 3
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8

                    AeFieldLabel { }
                    // ONE switch for the viewport, the timeline and the graph page: a wheel is one
                    // habit, and three surfaces that disagree is a trap rather than a setting. The
                    // default (unchecked) is the convention - wheel up zooms in.
                    AeCheckBox {
                        text: qsTr("Invert zoom direction")
                        checked: generalSettingModel.invertZoom
                        onToggled: generalSettingModel.invertZoom = checked
                    }

                    AeFieldLabel { }
                    AeCheckBox {
                        text: qsTr("Middle mouse moves canvas")
                        checked: generalSettingModel.middleMouseMoveCanvas
                        onToggled: generalSettingModel.middleMouseMoveCanvas = checked
                    }
                }
            }

            // The scroll bar is the export form's own pattern (its note says why each
            // line): attached to the Flickable so position, size and the press/drag/
            // paging come from its visibleArea instead of arithmetic, parented OUTSIDE
            // the clip (the shell) so the clip cannot cut it, the style's transient
            // contentItem replaced outright (the design has no fade), and shown only
            // when the content overflows.
            ScrollBar.vertical: AeScrollBar {
                id: vbar
                objectName: "formVBar"
                parent: shell
                anchors.right: scroller.right
                anchors.top: scroller.top
                anchors.bottom: scroller.bottom
                orientation: Qt.Vertical
                width: 12
            }
        }

        // ================================================================ FOOTER
        // Pinned to the shell (NOT the page), so the action row does not scroll with
        // the form - the export form's own geometry (the Flickable's bottomMargin of
        // 66 leaves it clear). Cancel is the secondary action, OK is the ONE primary
        // on this surface.
        Rectangle {
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
                    onClicked: generalSettingModel.cancel()
                }
                AeButton {
                    text: qsTr("OK")
                    kind: "primary"
                    onClicked: generalSettingModel.accept()
                }
            }
        }
    }
}
