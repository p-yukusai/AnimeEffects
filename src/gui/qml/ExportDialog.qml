// All qsTr() strings in this module live under one stable, module-scoped
// translation context (AnimeEffects.Dialogs) rather than the default filename
// context, so the entries are rename-safe and never collide with the C++ context
// of the shell class (gui::ExportDialog). The .qm are embedded at :/i18n.
pragma Translator: "AnimeEffects.Dialogs"

import QtQuick
import QtQuick.Layouts
// For ScrollBar only (the scroll host's attached bar, the picker's pattern):
// the controls in this form are the Ae* family; the bar is stock, styled below.
import QtQuick.Controls

// The export parameter form, rendered as a full-bleed Qt Quick scene inside the
// ExportDialog QDialog shell.
//
// THE SHAPE, top to bottom:
//
//   [ brand icon ] Where it is going                 <- one click to change it
//   Output / Frame rate / Format / Audio / ...       <- label + value rows
//   > Details (the destination's own notes)          <- collapsed; a note can be long
//   ---
//   OUTPUT     resolution - frame rate
//   ENCODING   format - codec - quality - capabilities
//   > Advanced  frame ranges - loop - audio - palette - codec's own numbers
//   ---
//   [ Export ]  [ Cancel ]
//
// Everything it decides lives in the "model" context property
// (gui::ExportViewModel); the paint lives here. Two rules from the widget form
// survive verbatim: a fact the destination imposes is STATED and the control that
// would contradict it is HIDDEN (not greyed), and the plan's own answers are never
// second-guessed on this side.
//
// EVERY ROW IS A REAL LAYOUT. The first version of this scene built its rows from
// an inline `Item` whose height came from `childrenRect`, which is not a layout:
// the label and its field overlapped, and the facts rows reserved height without
// ever being given a width. Rows are (AeFieldLabel, field) pairs inside one
// GridLayout per section, so the label column has ONE width, every row has a
// height, and no row computes a position.
Item {
    id: root

    implicitWidth: 560
    implicitHeight: 640

    // The shell owns the QDialog (and its own word for what happened); the scene
    // only reports that a choice was made. Two signals, so "approved" and
    // "cancelled" cannot be confused for a state the scene never checked.
    signal accepted()
    signal rejected()
    // The palette is the platform's own file chooser: a WIDGET dialog. The scene
    // asks the shell for it rather than pretending to own it.
    signal paletteRequested()

    // The frame-range edits, addressed by the scene and performed by the view model.
    // See the delegate's note: inside a Repeater delegate `model` may name the role
    // model rather than the view model, so these are resolved HERE, where it cannot.
    function setRangeFirstAt(index, value) { exportModel.ranges.setRangeFirst(index, value) }
    function setRangeLastAt(index, value) { exportModel.ranges.setRangeLast(index, value) }
    function removeRangeAt(index) { exportModel.removeRange(index) }

    function paletteName() {
        const path = exportModel.palettePath;
        if (path === "")
            return qsTr("Select palette path");
        const parts = path.split("/");
        return parts[parts.length - 1];
    }

    Connections {
        target: exportModel
        function onAcceptRequested() { root.accepted() }
        function onCancelRequested() { root.rejected() }
    }

    Shortcut { sequence: "Ctrl+Return"; onActivated: exportModel.accept() }
    Shortcut { sequence: "Escape"; onActivated: exportModel.cancel() }

    // D25's "keys", the second kind: keyboard travel brings the target INTO VIEW
    // (a focused control scrolled out of sight is unreachable), and
    // PageUp/PageDown/Home/End scroll by page wherever the focus sits - the
    // single-line editors do not consume these four, so they bubble to here.
    // Home/End inside a text field stay the caret's (the editor consumes them).
    Connections {
        target: root.Window ? root.Window : null
        function onActiveFocusItemChanged() {
            const fi = root.Window ? root.Window.activeFocusItem : null;
            if (fi) scroller.ensureVisible(fi);
        }
    }
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_PageUp) { scroller.pageBy(24 - scroller.height); event.accepted = true; }
        else if (event.key === Qt.Key_PageDown) { scroller.pageBy(scroller.height - 24); event.accepted = true; }
        else if (event.key === Qt.Key_Home) { scroller.contentX = 0; scroller.contentY = 0; event.accepted = true; }
        else if (event.key === Qt.Key_End) {
            scroller.contentY = Math.max(0, scroller.contentHeight - scroller.height);
            scroller.contentX = Math.max(0, scroller.contentWidth - scroller.width);
            event.accepted = true;
        }
    }

    Rectangle {
        id: shell
        anchors.fill: parent
        color: theme.base

        // Background click: drop focus from any field being edited. A3 (item 3)
        // and B11 (item 11): a click outside a focused text field removes its
        // focus, and what the field holds is untouched by that click. The blur is
        // by objectName, because that is the one thing both kinds of editor can
        // be named by: `scrubEdit` is ScrubSpinBox's inner TextInput, and
        // `bitrateEdit` is the Advanced section's bitrate AeTextField - a
        // TextField IS its own editor, unlike the scrub box, so it is the field
        // itself that carries the name. The bitrate field was the hole in this
        // handler until 2026-10-09: everything else on the form lost focus to a
        // background click and this one kept the caret.
        MouseArea {
            anchors.fill: parent
            onPressed: {
                const fi = root.Window ? root.Window.activeFocusItem : null;
                if (fi && (fi.objectName === "scrubEdit" || fi.objectName === "bitrateEdit"))
                    fi.focus = false;
            }
        }

        // D25/Q9 (decided 2026-09-30): when the content does not fit - the measured
        // case is minimum size with Advanced open and four frame ranges: 700px of
        // content in 558px of page (the 630px this note carried before 2026-10-03 is
        // stale; re-measured 2026-10-09 after H41 moved the frame ranges in here -
        // 702px on the row's old placement, and 2px less now that it sits in this
        // column's spacing rather than the OUTPUT grid's row spacing), and cut with
        // no recourse before this - the CONTENT scrolls and the action row below
        // stays put and reachable. Wheel and drag are the Flickable's own; the keys
        // are two kinds: focus movement scrolls the target into view (every control
        // stays reachable), and PageUp/PageDown/Home/End scroll by page when the
        // focused editor does not consume them. Horizontal too: below 343px of width
        // - the form's own layout minimum, measured at 552 with the frame ranges on
        // the default surface (a 320-wide range row plus the 104px label column was
        // the widest thing in the form) and 343 now that they are here - the form's
        // minimums overflow (measured, overflow-spike), and the line says never
        // exceeds the window WITHOUT A SCROLL - so it scrolls rather than cuts. H41
        // (2026-10-09) moved the frame-range rows INTO the Advanced section, so the
        // worst case above is essentially UNCHANGED - they were already inside its
        // height while the section was open - and the default surface (Advanced
        // shut) is now shorter by them, which is the point of the move. Both figures
        // are printed and re-measured by tests/form_layout.cpp.
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

            // Scroll a descendant of the page into the visible area (both axes).
            // Called from the window's focus changes: keyboard travel is the
            // "keys" in the line, and a focused control that is scrolled out of
            // sight is unreachable. The target's rect is UNIONED with its
            // ancestors' up to the page (skipping anything larger than the
            // viewport), so focusing a field brings its whole ROW into view -
            // a field with its row half-cut is only half reachable. Call from
            // the window's focus changes.
            function ensureVisible(it) {
                if (contentHeight <= height) contentY = 0;
                if (contentWidth <= width) contentX = 0;
                // QML items walk by `.parent` - `parentItem` reads as undefined
                // here (C++ getter only) and would end every walk after one hop.
                var p = it;
                while (p && p !== page) p = p.parent;
                if (p !== page || contentHeight <= height) return;
                var x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
                for (var a = it; a && a !== page; a = a.parent) {
                    if (a.width > width || a.height > height) continue;
                    var r = a.mapToItem(page, 0, 0);
                    x0 = Math.min(x0, r.x); y0 = Math.min(y0, r.y);
                    x1 = Math.max(x1, r.x + a.width); y1 = Math.max(y1, r.y + a.height);
                }
                var maxY = contentHeight - height;
                if (y0 < contentY) contentY = Math.max(0, y0 - 8);
                else if (y1 > contentY + height)
                    contentY = Math.min(maxY, y1 - height + 8);
                var maxX = contentWidth - width;
                if (maxX > 0) {
                    if (x0 < contentX) contentX = Math.max(0, x0 - 8);
                    else if (x1 > contentX + width)
                        contentX = Math.min(maxX, x1 - width + 8);
                }
            }
            function pageBy(dy) {
                contentY = Math.max(0, Math.min(contentHeight - height, contentY + dy));
            }

        ColumnLayout {
            id: page
            objectName: "formPage"
            width: scroller.contentWidth
            spacing: 0

            // ================================================== where it is going
            // The destination is the first question, not the last: it is the one
            // thing the user knows that the system cannot derive, and everything
            // below it is either a consequence of it or a genuine choice.
            Rectangle {
                id: destinationRow
                Layout.fillWidth: true
                implicitHeight: 46
                radius: 6
                color: destMouse.containsMouse ? theme.raisedHover : theme.raised
                border.width: 1
                border.color: destMouse.containsMouse ? theme.hover : theme.hairline
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Destination")
                Accessible.description: exportModel.destinationLabel
                Accessible.onPressAction: exportModel.changeDestinationRequested()

                Image {
                    id: destIcon
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 22
                    height: 22
                    visible: exportModel.destinationIconName !== ""
                    source: exportModel.destinationIconName === ""
                            ? "" : theme.iconUrl(exportModel.destinationIconName)
                    sourceSize.width: 64
                    sourceSize.height: 64
                    fillMode: Image.PreserveAspectFit
                }
                Text {
                    anchors.left: destIcon.visible ? destIcon.right : parent.left
                    anchors.leftMargin: destIcon.visible ? 10 : 12
                    anchors.right: changeHint.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: exportModel.destinationLabel
                    color: theme.text
                    font.pixelSize: 14
                    font.bold: true
                    elide: Text.ElideRight
                }
                Text {
                    id: changeHint
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Change")
                    color: theme.accent
                    font.pixelSize: 11
                }
                MouseArea {
                    id: destMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: exportModel.changeDestinationRequested()
                }
            }

            Item { Layout.preferredHeight: 12 }

            // ======================================================= the key facts
            // Label/value rows, NOT a paragraph: the widget form printed every fact
            // as one line of the same weight, which is the wall of text being fixed.
            // The destination's own notes are not here - they are behind the
            // disclosure below, because a note can be seven lines of platform rules.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                Repeater {
                    model: exportModel.facts
                    delegate: AeFactRow {
                        required property string factId
                        required property string factLabel
                        required property string factValue
                        required property string factDetail
                        required property string factSeverity

                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                        visible: factId !== "note"
                        label: factLabel
                        value: factValue
                        detail: factDetail
                        severity: factSeverity
                    }
                }
            }

            // ============================================================ the notes
            AeDisclosure {
                id: notesToggle
                Layout.fillWidth: true
                Layout.topMargin: 8
                text: qsTr("Details")
                open: notesOpen
                visible: exportModel.facts.noteCount > 0
                onToggled: notesOpen = !notesOpen
                property bool notesOpen: false
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.topMargin: 2
                visible: notesToggle.notesOpen
                spacing: 4
                Repeater {
                    model: exportModel.facts
                    delegate: Text {
                        required property string factId
                        required property string factValue
                        required property string factSeverity

                        Layout.fillWidth: true
                        Layout.preferredWidth: 0
                        Layout.preferredHeight: implicitHeight
                        visible: factId === "note"
                        text: "• " + factValue
                        color: factSeverity === "warning" ? theme.warning : theme.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        // The notes behind "Details" are the destination's own
                        // rules; a reader must be able to reach them, not only see
                        // them.
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                    }
                }
            }

            Item { Layout.preferredHeight: 16 }

            // =============================================================== OUTPUT
            AeSectionHeader {
                Layout.fillWidth: true
                text: qsTr("Output")
            }
            Item { Layout.preferredHeight: 6 }

            // What the OUTPUT grid holds now: how big, then how fast - the order a
            // person thinks about them. The frame range that used to open this grid
            // (which frames, then how big, then how fast) lives under Advanced now
            // (see the block there), so the default surface stays uncluttered for
            // the common case.
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 8

                // Resolution: width x height as ONE row, with ONE reset for both. The
                // widget form had two rows and two resets, so "make it the project's
                // own size" was two clicks and the pair could be left half-reset.
                AeFieldLabel { text: qsTr("Resolution") }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    AeNumberField {
                        id: widthField
                        Layout.fillWidth: true
                        label: qsTr("Width")
                        from: 0
                        to: exportModel.widthMaximum
                        enabled: exportModel.sizeEditable
                        value: exportModel.width
                        onValueEdited: (value) => { exportModel.width = value }
                        Connections {
                            target: exportModel
                            function onSizeChanged() { widthField.value = exportModel.width }
                        }
                    }
                    Text {
                        text: "×"
                        color: theme.textMuted
                        font.pixelSize: 12
                    }
                    AeNumberField {
                        id: heightField
                        Layout.fillWidth: true
                        label: qsTr("Height")
                        from: 0
                        to: exportModel.heightMaximum
                        enabled: exportModel.sizeEditable
                        value: exportModel.height
                        onValueEdited: (value) => { exportModel.height = value }
                        Connections {
                            target: exportModel
                            function onSizeChanged() { heightField.value = exportModel.height }
                        }
                    }
                    AeIconButton {
                        iconName: "arrow-clockwise"
                        label: qsTr("Reset to native")
                        // E29's kept kind: an Auto/Reset affordance.
                        showTip: true
                        enabled: exportModel.sizeEditable
                        onClicked: exportModel.setSizeToNative()
                    }
                    // The link that makes the pair one number: a compact toggle on
                    // the same row, not a full-width checkbox of its own.
                    AeIconButton {
                        iconName: "link"
                        label: qsTr("Keep aspect ratio")
                        enabled: exportModel.sizeEditable
                        checked: exportModel.keepAspectRatio
                        onClicked: exportModel.keepAspectRatio = !exportModel.keepAspectRatio
                    }
                }

                AeFieldLabel { text: qsTr("Frame rate"); visible: exportModel.fpsVisible }
                AeNumberField {
                    id: fpsField
                    Layout.fillWidth: true
                    Layout.preferredWidth: 160
                    Layout.maximumWidth: 260
                    visible: exportModel.fpsVisible
                    label: qsTr("Frame rate")
                    from: 1
                    to: exportModel.fpsMaximum
                    value: exportModel.fps
                    resetLabel: qsTr("Reset to native")
                    onValueEdited: (value) => { exportModel.fps = value }
                    onResetRequested: exportModel.setFpsToNative()
                    Connections {
                        target: exportModel
                        function onFpsChanged() { fpsField.value = exportModel.fps }
                    }
                }
            }

            Item { Layout.preferredHeight: 16 }

            // ============================================================= ENCODING
            AeSectionHeader {
                Layout.fillWidth: true
                text: qsTr("Encoding")
            }
            Item { Layout.preferredHeight: 6 }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                rowSpacing: 8

                // The escape hatch: the artifact, for a user who has no destination
                // in mind. This is the ONE place a format is genuinely the user's
                // choice.
                AeFieldLabel { text: qsTr("Format"); visible: exportModel.formatVisible }
                AeComboBox {
                    objectName: qsTr("Format")
                    Layout.fillWidth: true
                    Layout.maximumWidth: 260
                    visible: exportModel.formatVisible
                    overlay: root
                    enabled: exportModel.formatVisible
                    options: exportModel.formatOptions
                    index: exportModel.formatIndex
                    onActivated: (index) => exportModel.formatIndex = index
                }

                // MANUAL mode's codec row stays beside Format (the escape
                // hatch's own combo group, contract section G) — pinned by
                // probe_format and probe_axes ("the Codec row is there beside
                // it"). The DESTINATION-mode offered row is a separate instance
                // under Advanced (item 42's strict reading, the user's call
                // 2026-10-01): the two share every binding and are never both
                // visible, so the split is placement-only.
                AeFieldLabel {
                    text: qsTr("Codec")
                    visible: exportModel.codecVisible && exportModel.manualMode
                }
                AeComboBox {
                    objectName: qsTr("Codec")
                    Layout.fillWidth: true
                    Layout.maximumWidth: 260
                    visible: exportModel.codecVisible && exportModel.manualMode
                    overlay: root
                    enabled: exportModel.codecEnabled
                    options: exportModel.codecOptions
                    index: exportModel.codecIndex
                    onActivated: (index) => exportModel.codecIndex = index
                }

                // K46: the Quality row appears exactly where a video encoder writes
                // the file, so it follows the ARTIFACT rather than the mode -
                // qualityVisible is the negation of imageQualityVisible, so a still or
                // a sequence of stills (written by QImage::save, which has no bucket)
                // shows "Image quality" below instead, and never both.
                AeFieldLabel { text: qsTr("Quality"); visible: exportModel.qualityVisible }
                AeComboBox {
                    objectName: qsTr("Quality")
                    Layout.fillWidth: true
                    Layout.maximumWidth: 260
                    visible: exportModel.qualityVisible
                    overlay: root
                    // NOT disabled while the codec's own number is set: naming that
                    // number is a fourth answer to the same question, and a combo
                    // reading "Balanced" beside "CRF 18" is the form contradicting
                    // itself. It reads "Custom" instead (ruled 2026-10-08: "when CRF is
                    // used Quality should change to Custom instead of disabling, this
                    // is established UI convention"), and choosing a bucket from it
                    // hands the decision back to the intent - the model clears the
                    // override, so "Custom" cannot outlive the choice that ended it.
                    options: {
                        const labels = exportModel.qualityLabels;
                        const out = [];
                        for (let i = 0; i < labels.length; ++i)
                            out.push({ label: labels[i], value: i });
                        if (exportModel.qualityCustom)
                            out.push({ label: exportModel.qualityCustomLabel,
                                       value: labels.length });
                        return out;
                    }
                    // The Custom entry exists only while it IS the state, so it can
                    // never be picked as if it were a fifth intent (the model refuses an
                    // index above the buckets anyway).
                    index: exportModel.qualityCustom ? exportModel.qualityLabels.length
                                                     : exportModel.qualityIndex
                    onActivated: (index) => exportModel.qualityIndex = index
                }

                // Image quality: QImage::save()'s own setting, the only quality control
                // a sequence has. Auto is the writer's own default, and it is a STATE
                // (the word, not a -1 sitting at the bottom of the range): the range is
                // the writer's own 0..100, so every number the field offers is a number
                // that reaches the file.
                AeFieldLabel { text: qsTr("Image quality"); visible: exportModel.imageQualityVisible }
                AeNumberField {
                    id: imageQualityField
                    objectName: "imageQualityField"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 160
                    Layout.maximumWidth: 260
                    visible: exportModel.imageQualityVisible
                    label: qsTr("Image quality")
                    from: 0
                    to: 100
                    value: exportModel.imageQuality
                    auto: exportModel.imageQualityAuto
                    placeholder: qsTr("Auto")
                    resetLabel: qsTr("Reset to auto")
                    onValueEdited: (value) => { exportModel.imageQuality = value }
                    onResetRequested: exportModel.setImageQualityToAuto()
                    Connections {
                        target: exportModel
                        function onImageQualityChanged() { imageQualityField.value = exportModel.imageQuality }
                    }
                }
            }

            // Transparency is a REGULAR option, not an Advanced one (ruled
            // 2026-10-03: "why is transparency under Advanced? since it pushes
            // the codec choice onto a suboptimal codec it belongs in the
            // regular options, and off by default"). It is not a
            // control-for-a-default: turning it ON moves the encoder ladder
            // off its own best pick — measured (table in
            // docs/export-dialog-implementation-notes.md, 2026-10-01):
            // libsvtav1 keeps the encode "ok" and silently drops alpha,
            // libx265 refuses to open with yuva420p, and only libvpx-vp9 and
            // ffv1 carry it. That is a quality trade the user has to see next
            // to the encoding controls, so the toggle sits here and starts
            // OFF; a preference saved by an earlier run is read as this dialog's
            // starting point (MainWindow's export_allow_transparency), though the
            // target's own alpha rule still overrides it — see item 42 of
            // docs/export-dialog-behavior.md for what a saved `true` does today.
            // A codec that cannot
            // carry alpha hides the row (ExportViewModel's
            // applyArtifactConstraints).
            AeCheckBox {
                Layout.fillWidth: true
                visible: exportModel.transparency.visible
                text: exportModel.transparency.label
                checked: exportModel.transparency.value
                enabled: exportModel.transparency.enabled
                onToggled: exportModel.transparency.value = checked
            }

            // ============================================================ ADVANCED
            Item { Layout.preferredHeight: 12 }
            AeDisclosure {
                id: advancedToggle
                Layout.fillWidth: true
                visible: exportModel.advancedVisible
                text: qsTr("Advanced")
                open: exportModel.advancedOpen
                onToggled: exportModel.advancedOpen = !exportModel.advancedOpen
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.topMargin: 2
                visible: exportModel.advancedVisible && exportModel.advancedOpen
                spacing: 6

                // WHICH FRAMES - the FIRST row of this section, in the order it
                // always had: which frames, then how big (Resolution, above), then
                // how fast (Frame rate, above). The placement is contract item 41
                // ([R12], unamended): "the frame range lives under the
                // Custom/Advanced section; the default surface stays uncluttered
                // for the common case." It used to be the first row of the OUTPUT
                // grid instead, and `rangeVisible` - true for video, animated image
                // and image sequence, the app's primary use - put it on the default
                // surface for exactly the common case the line names. Still gated by
                // rangeVisible, so manual mode and a Still artifact are unchanged.
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8

                    AeFieldLabel { text: qsTr("Frames"); visible: exportModel.rangeVisible }
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: exportModel.rangeVisible
                        spacing: 4

                        Repeater {
                            model: exportModel.ranges
                            delegate: AeFrameRangeRow {
                                required property int index
                                required property int first
                                required property int last
                                required property string firstName
                                required property string lastName
                                required property bool canRemove

                                Layout.fillWidth: true
                                ordinal: index + 1
                                // Bound WITH verbs: `firstFrame: first` is a binding
                                // from the model role to the row's own property, never an
                                // override of it (see AeFrameRangeRow's note).
                                firstFrame: first
                                lastFrame: last
                                firstLabel: firstName
                                lastLabel: lastName
                                removable: canRemove
                                // Through root's own functions: inside a delegate the name
                                // `model` is ambiguous (the role model vs. the context
                                // property), and a click that removes the wrong row is the
                                // bug this row exists to make impossible.
                                onFirstEdited: (value) => root.setRangeFirstAt(index, value)
                                onLastEdited: (value) => root.setRangeLastAt(index, value)
                                onRemoveRequested: root.removeRangeAt(index)
                            }
                        }
                        AeButton {
                            text: qsTr("Add range")
                            kind: "ghost"
                            iconName: "plus-bold"
                            onClicked: exportModel.addRange()
                        }
                    }
                }

                AeCheckBox {
                    Layout.fillWidth: true
                    visible: exportModel.loop.visible
                    text: exportModel.loop.label
                    checked: exportModel.loop.value
                    enabled: exportModel.loop.enabled
                    onToggled: exportModel.loop.value = checked
                }
                AeCheckBox {
                    Layout.fillWidth: true
                    visible: exportModel.audio.visible
                    text: exportModel.audio.label
                    checked: exportModel.audio.value
                    enabled: exportModel.audio.enabled
                    onToggled: exportModel.audio.value = checked
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: exportModel.palette.visible
                    spacing: 6
                    AeCheckBox {
                        id: paletteBox
                        Layout.fillWidth: true
                        text: exportModel.palette.label
                        checked: exportModel.palette.value
                        enabled: exportModel.palette.enabled
                        onToggled: exportModel.palette.value = checked
                    }
                    AeButton {
                        kind: "secondary"
                        enabled: paletteBox.checked
                        text: root.paletteName()
                        onClicked: root.paletteRequested()
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8

                    // The offered codec row lives UNDER ADVANCED (contract item
                    // 42's strict reading, the user's explicit call 2026-10-01:
                    // "controls the destination merely defaults stay reachable
                    // under Advanced") — this is the DESTINATION-mode instance
                    // (manual mode's stays beside Format above; the two share
                    // every binding and are never both visible). It sits first
                    // in this grid because the archive rule's default codec
                    // follows the transparency toggle — which since 2026-10-03
                    // lives on the REGULAR surface above, not beside this row
                    // (see the toggle's own comment; only placement moved, the
                    // dependency is the same). A destination that DICTATES one
                    // codec keeps the row hidden and states the fact instead.
                    AeFieldLabel {
                        text: qsTr("Codec")
                        visible: exportModel.codecVisible && !exportModel.manualMode
                    }
                    AeComboBox {
                        objectName: qsTr("Codec")
                        Layout.fillWidth: true
                        Layout.maximumWidth: 260
                        visible: exportModel.codecVisible && !exportModel.manualMode
                        overlay: root
                        enabled: exportModel.codecEnabled
                        options: exportModel.codecOptions
                        index: exportModel.codecIndex
                        onActivated: (index) => exportModel.codecIndex = index
                    }

                    // The codec's own value, for a user who knows the number they want
                    // instead of the intent. It starts in the Auto STATE, drawn as the
                    // word rather than as a number, and while it is there the Quality
                    // setting is what decides. Naming a number is what switches the
                    // Quality row to "Custom" (see the combo above).
                    AeFieldLabel {
                        text: exportModel.qualityOverrideLabel
                        visible: exportModel.qualityOverrideVisible
                    }
                    AeNumberField {
                        id: overrideField
                        objectName: "qualityOverrideField"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 160
                        Layout.maximumWidth: 260
                        visible: exportModel.qualityOverrideVisible
                        label: qsTr("Quality override")
                        // The codec's OWN span: the field offers the numbers the codec
                        // accepts, and Auto is not one of them.
                        from: exportModel.qualityOverrideMinimum
                        to: exportModel.qualityOverrideMaximum
                        value: exportModel.qualityOverrideValue
                        auto: exportModel.qualityOverrideAuto
                        placeholder: exportModel.qualityOverrideAutoText
                        resetLabel: qsTr("Reset to auto")
                        onValueEdited: (value) => { exportModel.qualityOverrideValue = value }
                        onResetRequested: exportModel.setQualityOverrideToAuto()
                        Connections {
                            target: exportModel
                            function onQualityOverrideChanged() {
                                overrideField.value = exportModel.qualityOverrideValue;
                            }
                        }
                    }

                    // Bitrate is what is left when there is no quality knob to turn
                    // (ProRes, FFV1, MJPEG). Offering both is offering two answers to
                    // one question, which is why the model shows exactly one.
                    AeFieldLabel { text: qsTr("Bitrate"); visible: exportModel.bitrateVisible }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.maximumWidth: 320
                        visible: exportModel.bitrateVisible
                        spacing: 6
                        AeTextField {
                            id: bitrateField
                            // The name the shell's background click addresses this
                            // editor by (A3/B11): a TextField is its OWN editor, so
                            // unlike the scrub fields - whose name sits on the inner
                            // TextInput, "scrubEdit" - it is the field itself that
                            // carries it.
                            objectName: "bitrateEdit"
                            Layout.fillWidth: true
                            placeholderText: qsTr("Auto")
                            accessibleName: qsTr("Bitrate")
                            onEdited: (text) => exportModel.bitrate = text
                            Connections {
                                target: exportModel
                                function onBitrateChanged() { bitrateField.text = exportModel.bitrate }
                            }
                        }
                        AeIconButton {
                            iconName: "arrow-clockwise"
                            label: qsTr("Reset to auto")
                            // E29's kept kind: an Auto/Reset affordance.
                            showTip: true
                            onClicked: exportModel.bitrate = "Auto"
                        }
                    }

                    // I43 (decided 2026-09-30): the size cap is OPT-IN and settable.
                    // "Auto" is the STATE of no cap at all — the destination's own cap
                    // stays a stated fact in the facts panel and the picker badge — and
                    // the field draws the word for it rather than comparing a number to
                    // a magic one (which is how a 0.25 MB cap used to read as "Auto").
                    // Set a value and the export is fitted under it; when it cannot fit,
                    // the export is REFUSED with a visible reason rather than silently
                    // reduced further.
                    AeFieldLabel { text: qsTr("Size cap (MB)"); visible: exportModel.sizeCapVisible }
                    AeNumberField {
                        id: sizeCapField
                        objectName: "sizeCapField"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 160
                        Layout.maximumWidth: 260
                        visible: exportModel.sizeCapVisible
                        label: qsTr("Size cap (MB)")
                        from: exportModel.sizeCapMinimum
                        to: exportModel.sizeCapMaximum
                        decimals: 2
                        step: 0.1
                        value: exportModel.sizeCapValue
                        auto: exportModel.sizeCapAuto
                        placeholder: qsTr("Auto")
                        resetLabel: qsTr("Reset to auto")
                        onValueEdited: (value) => { exportModel.sizeCapValue = value }
                        onResetRequested: exportModel.setSizeCapToAuto()
                        Connections {
                            target: exportModel
                            function onSizeCapChanged() { sizeCapField.value = exportModel.sizeCapValue }
                        }
                    }
                }
            }

        }

            // The scroll bar is the picker's own pattern (its D23 comment says why
            // each line): attached to the Flickable so position, size and the
            // press/drag/paging come from its visibleArea instead of arithmetic,
            // parented OUTSIDE the clip (the shell) so the clip cannot cut it, the
            // style's transient contentItem replaced outright (the design has no
            // fade), and shown only when the content overflows (D23).
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
                    onClicked: exportModel.cancel()
                }
                AeButton {
                    text: qsTr("Export")
                    kind: "primary"
                    onClicked: exportModel.accept()
                }
            }
        }
    }
}
