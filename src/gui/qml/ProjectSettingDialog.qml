// All qsTr() strings in this module live under one stable, module-scoped
// translation context (AnimeEffects.Dialogs) rather than the default filename
// context, so the entries are rename-safe and never collide with the C++ context
// of the shell class (gui::ProjectSettingDialog). The .qm are embedded at :/i18n.
pragma Translator: "AnimeEffects.Dialogs"

import QtQuick
import QtQuick.Layouts
// For ScrollBar only (the scroll host's attached bar, the export form's pattern):
// the controls in this form are the Ae* family; the bar is stock, styled below.
import QtQuick.Controls

// The project-settings form, rendered as a full-bleed Qt Quick scene inside the
// ProjectSettingDialog QDialog shell.
//
// THE SHAPE, top to bottom:
//
//   Canvas size       [ 1280 ] x [ 720 ]
//   Frame range       [ 1 ] … [ 600 ]
//   Loop animation    [ ] Loop animation
//   Frames per second [ 24 ]
//   (self-labelled)   [swatch] [ Choose… ] [ ] Use theme background
//   ---
//   [ Cancel ]  [ OK ]
//
// Everything it decides lives in the "projectSettingModel" context property
// (gui::ProjectSettingViewModel); the paint lives here. Two rules the widget form
// implied and this scene states:
//
//   * EVERY ROW IS A REAL LAYOUT. Rows are (AeFieldLabel, field) pairs inside one
//     GridLayout, so the label column has ONE width and no row computes a
//     position - the same rule the new-project and export scenes follow.
//   * The numeric fields are AeNumberField (ScrubSpinBox): a scrub writes the
//     value and DROPS the caller's binding, so each field re-syncs from the model
//     in its own Connections block (exportModel's fields do exactly this).
//
// The model is addressed as `projectSettingModel`, NOT `model`: a Repeater (and a
// ListView) carries a `model` property of its own, and the export form's history
// with that shadowing is why the name is fixed.
Item {
    id: root

    implicitWidth: 460
    // The scene HUGS its rows: the page's own height plus the top margin and the
    // band the pinned footer occupies (the Flickable's bottomMargin). The shell
    // reads this and sizes the window to it, so the form is never taller than what
    // it holds. SizeRootObjectToView sets width/height but not implicitHeight, so
    // this stays readable. The Flickable still scrolls if a screen is shorter.
    implicitHeight: 16 + page.implicitHeight + 66

    // The shell owns the QDialog (and its own word for what happened); the scene
    // only reports that a choice was made. Two signals, so "accepted" and
    // "cancelled" cannot be confused for a state the scene never checked.
    signal accepted()
    signal rejected()

    Connections {
        target: projectSettingModel
        function onAcceptRequested() { root.accepted() }
        function onCancelRequested() { root.rejected() }
    }

    // Escape leaves the dialog. The editors that would otherwise swallow it keep
    // it while they hold focus (ScrubSpinBox's TextInput accepts the shortcut
    // override), exactly as in the export form.
    Shortcut { sequence: "Escape"; onActivated: projectSettingModel.cancel() }

    Rectangle {
        id: shell
        anchors.fill: parent
        color: theme.base

        // Background click on the shell's own margins (outside the scroll host):
        // drop focus from whatever is being edited, leaving what it holds
        // untouched. The in-page background is handled inside the Flickable - see
        // the note there on why the click must be caught THERE and not here.
        MouseArea {
            anchors.fill: parent
            onPressed: {
                const fi = root.Window ? root.Window.activeFocusItem : null;
                if (fi) fi.focus = false;
            }
        }

        // The content scrolls when it does not fit; the action row BELOW stays
        // put and reachable (it is the shell's own child, not part of the page).
        Flickable {
            id: scroller
            // What a test or a probe addresses this scroll host by.
            objectName: "formScroller"
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.topMargin: 16
            anchors.bottomMargin: 66
            clip: true
            contentWidth: Math.max(width, page.implicitWidth)
            contentHeight: page.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            // The official Qt idiom for "a click outside the field clears focus"
            // (Keyboard Focus in Qt Quick; the common MouseArea recipe): an item
            // that clears the active focus on a press, sitting BELOW the controls.
            //
            // It has to live INSIDE the Flickable, not on the shell behind it: a
            // QQuickFlickable accepts a left-button press over its whole area (that
            // is how dragging scrolls), so a background MouseArea underneath it
            // never sees the click. Here childAt() skips the page (a ColumnLayout
            // takes no mouse buttons) and reaches this item for a press that lands
            // on empty space, while a control still gets its own press first.
            //
            // accepted = false is load-bearing: without it this item would grab the
            // press and a drag that starts on empty space would no longer scroll
            // the page.
            MouseArea {
                anchors.fill: parent
                onPressed: (mouse) => {
                    const fi = root.Window ? root.Window.activeFocusItem : null;
                    if (fi) fi.focus = false;
                    mouse.accepted = false;
                }
            }

            ColumnLayout {
                id: page
                objectName: "formPage"
                width: scroller.contentWidth
                spacing: 0

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8

                    // ================================================ the canvas
                    // The pair is a UNION - [width] x [height] on ONE row, the shape
                    // the new-project form's canvas pair uses - rather than two
                    // full-width rows for two numbers that are meaningless apart.
                    // The ceiling is the device's, read from GL by the shell; the
                    // model clamps to the same bound (the widget form's QSpinBox
                    // range was [1, maxBufferSize]).
                    AeFieldLabel { text: qsTr("Canvas size") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        AeNumberField {
                            id: canvasWidthField
                            Layout.fillWidth: true
                            Layout.preferredWidth: 96
                            Layout.maximumWidth: 160
                            label: qsTr("Canvas width")
                            from: 1
                            to: projectSettingModel.maxCanvasSize
                            step: 1
                            value: projectSettingModel.canvasWidth
                            onValueEdited: (value) => { projectSettingModel.canvasWidth = value }
                            Connections {
                                target: projectSettingModel
                                function onCanvasWidthChanged() { canvasWidthField.value = projectSettingModel.canvasWidth }
                            }
                        }
                        Text {
                            text: "×"
                            color: theme.textMuted
                            font.pixelSize: 12
                            // "1280 × 720" is a size, not two numbers beside a glyph;
                            // the multiplication sign means "by" and is the only thing
                            // tying the pair together, so a screen reader hears the
                            // relation rather than silence.
                            Accessible.role: Accessible.StaticText
                            Accessible.name: qsTr("by")
                        }
                        AeNumberField {
                            id: canvasHeightField
                            Layout.fillWidth: true
                            Layout.preferredWidth: 96
                            Layout.maximumWidth: 160
                            label: qsTr("Canvas height")
                            from: 1
                            to: projectSettingModel.maxCanvasSize
                            step: 1
                            value: projectSettingModel.canvasHeight
                            onValueEdited: (value) => { projectSettingModel.canvasHeight = value }
                            Connections {
                                target: projectSettingModel
                                function onCanvasHeightChanged() { canvasHeightField.value = projectSettingModel.canvasHeight }
                            }
                        }
                    }

                    // ============================================ the frame range
                    // Two linked fields (start, end) on one row, the same union
                    // shape; either bound may be negative (pre-roll frames before 0
                    // are a valid project extent). The LINK the widget form made with
                    // setMaximum/setMinimum is stated twice on purpose: each field
                    // stops at its partner so the edit that would invert is never
                    // OFFERED, and the model refuses it as well, so the pair stays
                    // consistent whatever wrote the other end.
                    AeFieldLabel { text: qsTr("Frame range") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        AeNumberField {
                            id: frameStartField
                            Layout.fillWidth: true
                            Layout.preferredWidth: 120
                            Layout.maximumWidth: 180
                            label: qsTr("Frame start")
                            from: -2147483648
                            to: projectSettingModel.frameEnd
                            step: 1
                            value: projectSettingModel.frameStart
                            onValueEdited: (value) => { projectSettingModel.frameStart = value }
                            Connections {
                                target: projectSettingModel
                                function onFrameStartChanged() { frameStartField.value = projectSettingModel.frameStart }
                            }
                        }
                        Text {
                            text: "…"
                            color: theme.textMuted
                            font.pixelSize: 12
                            // The ellipsis is the range's own word ("from … to"); it
                            // is the only thing separating the two numbers, so it has
                            // to exist in the accessibility tree, not only in pixels.
                            Accessible.role: Accessible.StaticText
                            Accessible.name: qsTr("to")
                        }
                        AeNumberField {
                            id: frameEndField
                            Layout.fillWidth: true
                            Layout.preferredWidth: 120
                            Layout.maximumWidth: 180
                            label: qsTr("Frame end")
                            from: projectSettingModel.frameStart
                            to: 2147483647
                            step: 1
                            value: projectSettingModel.frameEnd
                            onValueEdited: (value) => { projectSettingModel.frameEnd = value }
                            Connections {
                                target: projectSettingModel
                                function onFrameEndChanged() { frameEndField.value = projectSettingModel.frameEnd }
                            }
                        }
                    }

                    // ============================================ the loop flag
                    // The CHECKBOX carries the words, so the label column is
                    // deliberately empty - a label saying "Loop animation" beside a
                    // box that already says "Loop animation" is the same words twice
                    // (the new-project form's live-link row states the same rule).
                    AeFieldLabel { }
                    AeCheckBox {
                        text: qsTr("Loop animation")
                        checked: projectSettingModel.loop
                        onToggled: projectSettingModel.loop = checked
                    }

                    // ================================================= the rate
                    AeFieldLabel { text: qsTr("Frames per second") }
                    AeNumberField {
                        id: fpsField
                        Layout.fillWidth: true
                        Layout.preferredWidth: 160
                        Layout.maximumWidth: 260
                        label: qsTr("Frames per second")
                        from: 1
                        to: 2147483647
                        step: 1
                        value: projectSettingModel.fps
                        onValueEdited: (value) => { projectSettingModel.fps = value }
                        Connections {
                            target: projectSettingModel
                            function onFpsChanged() { fpsField.value = projectSettingModel.fps }
                        }
                    }

                    // ==================================== the viewport background
                    // THE ROW IS SELF-LABELLED: the box carries its words ("use the
                    // theme background"), so the label column stays empty, exactly as
                    // the loop row above states the rule. The swatch is the colour in
                    // force - the project's override when it has one, and the theme's
                    // recessed canvas token otherwise (theme.recessed is
                    // theme::Colors::current().recessed, what the app painted before
                    // the override existed). Choose… hands the pick to the shell: the
                    // scene has no colour picker of its own, and the shell owns the
                    // stock QColorDialog the same way it owns the export form's file
                    // chooser.
                    //
                    // This row IS labelled, unlike the loop row below it: the checkbox
                    // there repeats its own row's words, while "Use theme background"
                    // alone does not say whose background, and "Choose…" says nothing
                    // about what is being chosen.
                    AeFieldLabel { text: qsTr("Viewport background") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Rectangle {
                            id: viewportBackgroundSwatch
                            // What a probe addresses the swatch by.
                            objectName: "viewportBackgroundSwatch"
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 20
                            radius: 3
                            border.width: 1
                            border.color: theme.outline
                            // Qt.color() takes the override's hex string when set and
                            // the theme's QColor when not, so the swatch always paints
                            // the colour the canvas would.
                            color: Qt.color(projectSettingModel.viewportBackgroundHex || theme.recessed)
                            // A plain Rectangle is not in the accessibility tree, and
                            // when an override IS set this swatch is the only place its
                            // value is shown. The name carries the hex itself (no new
                            // string to translate) and is empty for the theme default,
                            // where the checkbox already says which colour is in force.
                            Accessible.role: Accessible.Indicator
                            Accessible.name: projectSettingModel.viewportBackgroundHex
                        }
                        AeButton {
                            text: qsTr("Choose…")
                            kind: "secondary"
                            onClicked: projectSettingModel.requestViewportBackgroundPick()
                        }
                        AeCheckBox {
                            text: qsTr("Use theme background")
                            checked: projectSettingModel.viewportBackgroundIsThemeDefault
                            // Ticking drops the override (the empty hex is the theme
                            // default); unticking asks for a colour. Either way the box
                            // re-reads the model, so a cancelled chooser snaps it back
                            // to ticked rather than leaving a state the project is not in.
                            onToggled: {
                                if (checked) projectSettingModel.viewportBackgroundHex = ""
                                else projectSettingModel.requestViewportBackgroundPick()
                            }
                        }
                    }
                }
            }

            // The scroll bar is the export form's own pattern (its note says why
            // each line): attached to the Flickable so position, size and the
            // press/drag/paging come from its visibleArea instead of arithmetic,
            // parented OUTSIDE the clip (the shell) so the clip cannot cut it, the
            // style's transient contentItem replaced outright (the design has no
            // fade), and shown only when the content overflows.
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
        // Pinned to the shell (NOT the page), so the action row does not scroll
        // with the form - the export form's own geometry (the Flickable's
        // bottomMargin of 66 leaves it clear). Cancel is the secondary action, OK
        // is the ONE primary on this surface.
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
                    onClicked: projectSettingModel.cancel()
                }
                AeButton {
                    text: qsTr("OK")
                    kind: "primary"
                    onClicked: projectSettingModel.accept()
                }
            }
        }
    }
}
