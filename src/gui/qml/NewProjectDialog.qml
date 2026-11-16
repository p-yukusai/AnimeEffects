// All qsTr() strings in this module live under one stable, module-scoped
// translation context (AnimeEffects.Dialogs) rather than the default filename
// context, so the entries are rename-safe and never collide with the C++ context
// of the shell class (gui::NewProjectDialog). The .qm are embedded at :/i18n.
pragma Translator: "AnimeEffects.Dialogs"

import QtQuick
import QtQuick.Layouts
// For ScrollBar only (the scroll host's attached bar, the export form's pattern):
// the controls in this form are the Ae* family; the bar is stock, styled below.
import QtQuick.Controls

// The new-project form, rendered as a full-bleed Qt Quick scene inside the
// NewProjectDialog QDialog shell.
//
// THE SHAPE, top to bottom:
//
//   Initial asset     [ path ................ ] [ Browse… ]
//   Live link         [ ] Live link
//                         Reload when the asset file changes on disk
//   Frame count       [ 600 ]
//   Frames per second [ 24 ]
//   Canvas size       [ ] Specify canvas size
//   Canvas width      [ 512 ]   (enabled only while the box is ticked)
//   Canvas height     [ 512 ]
//   ---
//   [ Cancel ]  [ Create ]
//
// Everything it decides lives in the "newProjectModel" context property
// (gui::NewProjectViewModel); the paint lives here. Two rules the widget form
// implied and this scene states:
//
//   * EVERY ROW IS A REAL LAYOUT. Rows are (AeFieldLabel, field) pairs inside one
//     GridLayout, so the label column has ONE width and no row computes a
//     position - the same rule the export scene follows.
//   * The numeric fields are AeNumberField (ScrubSpinBox): a scrub writes the
//     value and DROPS the caller's binding, so each field re-syncs from the model
//     in its own Connections block (exportModel's fields do exactly this).
//
// The model is addressed as `newProjectModel`, NOT `model`: a Repeater (and a
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
    // only reports that a choice was made. Two signals, so "created" and
    // "cancelled" cannot be confused for a state the scene never checked.
    signal accepted()
    signal rejected()

    Connections {
        target: newProjectModel
        function onAcceptRequested() { root.accepted() }
        function onCancelRequested() { root.rejected() }
    }

    // Escape leaves the dialog. The editors that would otherwise swallow it keep
    // it while they hold focus (ScrubSpinBox's TextInput accepts the shortcut
    // override), exactly as in the export form.
    Shortcut { sequence: "Escape"; onActivated: newProjectModel.cancel() }

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
            // never sees the click - which is exactly the reported defect, the
            // field keeping focus after a click on the page. Here childAt() skips
            // the page (a ColumnLayout takes no mouse buttons) and reaches this
            // item for a press that lands on empty space, while a control still
            // gets its own press first.
            //
            // accepted = false is load-bearing: without it this item would grab the
            // press and a drag that starts on empty space would no longer scroll
            // the page (D25's "drag").
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

                    // =============================================== the asset
                    // The file chooser is a WIDGET dialog the shell owns; the field
                    // is read-only and only shows what the shell's browse() wrote
                    // back. The name the leaf carries is exposed as the accessible
                    // name, so a screen reader can address the field.
                    AeFieldLabel { text: qsTr("Initial asset") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        AeTextField {
                            id: assetField
                            objectName: "assetPathEdit"
                            Layout.fillWidth: true
                            readOnly: true
                            text: newProjectModel.fileName
                            placeholderText: qsTr("Select an asset file")
                            accessibleName: qsTr("Initial asset")
                        }
                        AeButton {
                            text: qsTr("Browse…")
                            kind: "secondary"
                            iconName: "folder"
                            onClicked: newProjectModel.browse()
                        }
                    }

                    // ============================================ the live link
                    // It sits directly under the asset it is about: the option
                    // governs whether THAT file is watched, so a row anywhere else
                    // would read as a project-wide switch. The CHECKBOX carries the
                    // words, so the label column is deliberately empty - a label
                    // saying "Live link" beside a box that already says "Live link"
                    // is the same words twice. A muted sentence sits under the box
                    // (one grid CELL, a ColumnLayout, so the sentence clings to what
                    // it explains rather than a row-gap away) and wraps at the
                    // cell's width.
                    AeFieldLabel { }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        AeCheckBox {
                            text: qsTr("Live link")
                            checked: newProjectModel.liveLink
                            onToggled: newProjectModel.liveLink = checked
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Reload when the asset file changes on disk")
                            color: theme.textMuted
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            // The sentence is a fact about the box above it; make it
                            // real text in the tree rather than pixels only.
                            Accessible.role: Accessible.StaticText
                            Accessible.name: text
                        }
                    }

                    // =============================================== the frames
                    // 1-indexed: the frame-count value N is frames 1..N, so the
                    // floor is 1 and the ceiling is the same INT_MAX the widget
                    // QSpinBox carried.
                    AeFieldLabel { text: qsTr("Frame count") }
                    AeNumberField {
                        id: frameCountField
                        Layout.fillWidth: true
                        Layout.preferredWidth: 160
                        Layout.maximumWidth: 260
                        label: qsTr("Frame count")
                        from: 1
                        to: 2147483647
                        step: 1
                        value: newProjectModel.frameCount
                        onValueEdited: (value) => { newProjectModel.frameCount = value }
                        Connections {
                            target: newProjectModel
                            function onFrameCountChanged() { frameCountField.value = newProjectModel.frameCount }
                        }
                    }

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
                        value: newProjectModel.fps
                        onValueEdited: (value) => { newProjectModel.fps = value }
                        Connections {
                            target: newProjectModel
                            function onFpsChanged() { fpsField.value = newProjectModel.fps }
                        }
                    }

                    // ============================================== the canvas
                    // The override is OPT-IN and settable: the checkbox turns it on
                    // and the pair below is the size it overrides to. The pair is a
                    // union - [width] x [height] on ONE row, the shape the export
                    // form's resolution row uses - rather than two full-width rows
                    // for two numbers that are meaningless apart. The fields stay
                    // DISABLED (not hidden) until the box is ticked, so the row's
                    // shape does not change under the pointer.
                    AeFieldLabel { text: qsTr("Canvas size") }
                    AeCheckBox {
                        text: qsTr("Specify canvas size")
                        checked: newProjectModel.specifiesCanvasSize
                        onToggled: newProjectModel.specifiesCanvasSize = checked
                    }

                    // The pair carries no label of its own: it reads as the value of
                    // the row above, so it sits in the field column beside it. (A
                    // single row holding the checkbox AND the pair was measured to
                    // overflow the dialog - the checkbox label is locale-width, and
                    // in Spanish it pushed the pair past the edge, which clipped the
                    // Initial-asset Browse button on the row above.)
                    AeFieldLabel { }
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
                            to: 2147483647
                            step: 1
                            enabled: newProjectModel.specifiesCanvasSize
                            value: newProjectModel.canvasWidth
                            onValueEdited: (value) => { newProjectModel.canvasWidth = value }
                            Connections {
                                target: newProjectModel
                                function onCanvasWidthChanged() { canvasWidthField.value = newProjectModel.canvasWidth }
                            }
                        }
                        Text {
                            text: "×"
                            color: theme.textMuted
                            font.pixelSize: 12
                        }
                        AeNumberField {
                            id: canvasHeightField
                            Layout.fillWidth: true
                            Layout.preferredWidth: 96
                            Layout.maximumWidth: 160
                            label: qsTr("Canvas height")
                            from: 1
                            to: 2147483647
                            step: 1
                            enabled: newProjectModel.specifiesCanvasSize
                            value: newProjectModel.canvasHeight
                            onValueEdited: (value) => { newProjectModel.canvasHeight = value }
                            Connections {
                                target: newProjectModel
                                function onCanvasHeightChanged() { canvasHeightField.value = newProjectModel.canvasHeight }
                            }
                        }
                    }
                }
            }

            // The scroll bar is the export form's own pattern (its D23 note says
            // why each line): attached to the Flickable so position, size and the
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
        // bottomMargin of 66 leaves it clear). Cancel is the secondary action,
        // Create is the ONE primary on this surface.
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
                    onClicked: newProjectModel.cancel()
                }
                AeButton {
                    text: qsTr("Create")
                    kind: "primary"
                    onClicked: newProjectModel.accept()
                }
            }
        }
    }
}
