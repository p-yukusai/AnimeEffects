pragma Translator: "AnimeEffects.Tools"

import QtQuick
import QtQuick.Controls

// The dock's square icon button: 26x26 with an 18px glyph.
//
// IT IS NOT A Controls `Button`, and the reason is a measured trap. `checked` here is
// a BINDING - the model says which tool, mode or toggle is up, and every button in the
// row must follow it. A checkable Button writes `checked` itself on every click, which
// DESTROYS the binding on that button only, so the next click elsewhere leaves two
// buttons lit at once (the clicked one from its own write, the previous one because
// its binding is still live and now evaluates true). AeIconButton, the export dialogs'
// square icon button, is hand-rolled for the same class of reason; this is the app's
// own idiom, not a new one.
//
// Three checked fills, and the difference is meaning rather than taste:
//   * `accent` - the toolbox's current TOOL. It is the application's mode, the one
//     state in this dock that earns the accent, and the deleted stylesheet painted it
//     with @accent@ under `#modePanel QPushButton:checked`;
//   * plain checked - a MODE of the current tool, or a VIEW toggle: the neutral
//     `active` fill, because neither is the document's mode.
//
// THE GLYPH IS ALWAYS THE PLAIN ONE, and that is a MEASURED fix rather than a
// simplification. The widget toolbox swapped to the <name>-active pair (tinted with
// theme.accentText) when its button was checked, which is right on the dark theme's
// saturated accent (#6d28d9 with #e5e5e5 ink, 5.4:1) and nearly invisible on the light
// theme's pale one: MEASURED #f5f5f5 ink on #ddd6fe fill, 1.35:1 - the active tool's
// glyph was, in the light theme, the one control a user could not read. The plain glyph
// is tinted with the theme's `text`, which contrasts with BOTH fills (dark: near-white on
// the deep accent; light: near-black on the pale one), because `text` is the ink of a
// surface and the accent fill is a surface.
Item {
    id: root

    property string iconName: ""
    // The name a screen reader announces and (unless tipText says otherwise) the
    // tooltip shows. An icon with no name is a control nobody can identify - and the
    // widget toolbox's icon-only buttons answered to the GROUP BOX's name for exactly
    // that omission.
    property string label: ""
    // The hover text, when it has more to say than the accessible name. The header's
    // slot toggles are the case: a screen reader should hear "Show or hide the left
    // side", while an empty slot's tooltip explains "Nothing is docked on the left
    // side". Defaulting to label keeps every existing call site unchanged.
    property string tipText: ""
    property bool checkable: false
    property bool checked: false
    property bool accent: false
    property int iconSize: 18
    signal clicked()

    implicitWidth: 26
    implicitHeight: 26

    Accessible.role: Accessible.Button
    Accessible.name: root.label
    // The attached type has no `checked` state, so the on/off word rides the
    // description (the same choice AeIconButton made).
    Accessible.description: root.checkable ? (root.checked ? qsTr("On") : qsTr("Off")) : ""
    Accessible.onPressAction: if (root.enabled) root.clicked()

    Rectangle {
        anchors.fill: parent
        radius: 4
        antialiasing: true
        // Keyboard focus is visible: the same 1px `theme.focus` ring AeCheckBox draws,
        // and the ONLY border this button ever has (contract line A1, and a Tab user has
        // no other way to know which of seven identical squares is current).
        border.width: root.activeFocus ? 1 : 0
        border.color: theme.focus
        // HOVER KEEPS THE CHECKED FILL'S HUE. The old cascade used `activeHover`
        // for every checked button, so the ONE accent tool lost its accent and
        // turned neutral gray the moment the pointer entered it. The state is
        // still visible under the pointer - it just stays an accent for the
        // accent button and neutral for the rest.
        color: {
            if (!root.enabled) return "transparent";
            if (root.checked) {
                if (root.accent) {
                    if (mouse.pressed || mouse.containsMouse) return theme.accentHover;
                    return theme.accent;
                }
                if (mouse.pressed || mouse.containsMouse) return theme.activeHover;
                return theme.active;
            }
            if (mouse.pressed) return theme.active;
            if (mouse.containsMouse) return theme.raisedHover;
            return "transparent";
        }
    }
    Image {
        anchors.centerIn: parent
        width: root.iconSize
        height: root.iconSize
        // THE REVISION IS READ HERE, and that comma is load-bearing: `theme.iconUrl()` is
        // a Q_INVOKABLE, and a METHOD CALL in a binding registers NO dependency - the
        // binding would evaluate once and keep the theme it was built with. MEASURED: the
        // dock kept the dark theme's near-white glyphs on the light background (1.1:1,
        // i.e. the icons vanished) even though the files had been re-tinted and the URL
        // carried a new generation. Reading `theme.iconRevision` (a Q_PROPERTY) is what
        // makes this binding re-evaluate on a theme switch; AGENTS.md records the same trap
        // for `exportModel.manualMode()`.
        source: root.iconName === ""
                ? ""
                : (theme.iconRevision, theme.iconUrl(root.iconName))
        // THE RASTER IS SIZED TO THE GLYPH'S PHYSICAL FOOTPRINT. A fixed 64px source
        // (what this file did first) is four times the 18px it is drawn at, so Qt
        // downsampled the SVG and the edges came out soft - the reported "poor
        // anti-aliasing". ScrubSpinBox carries the same fix and the measurement for it
        // (a 64px source was a four-times-downsample blur); devicePixelRatio is what
        // keeps it exact on a scaled screen instead of stretching a 1x bitmap.
        sourceSize.width: Math.ceil(root.iconSize * Screen.devicePixelRatio)
        sourceSize.height: Math.ceil(root.iconSize * Screen.devicePixelRatio)
        smooth: true
        fillMode: Image.PreserveAspectFit
        opacity: root.enabled ? 1.0 : 0.45
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
        onContainsMouseChanged: {
            if (mouse.containsMouse && root.nativeToolTip) {
                nativeTipTimer.restart();
            } else {
                nativeTipTimer.stop();
                if (root.nativeToolTip) headerHost.hideHeaderToolTip();
            }
        }
    }
    Component.onDestruction: if (root.nativeToolTip) headerHost.hideHeaderToolTip()
    // The header is a 30px QQuickWidget, so its scene cannot draw a tooltip below
    // itself. A header host exposes showHeaderToolTip; the Tools dock has no such
    // context property and keeps AeToolTip.
    readonly property bool nativeToolTip: typeof headerHost !== "undefined" && headerHost !== null
                                          && typeof headerHost.showHeaderToolTip === "function"
    readonly property string tip: root.tipText !== "" ? root.tipText : root.label
    Timer {
        id: nativeTipTimer
        interval: theme.toolTipDelay
        repeat: false
        onTriggered: if (root.nativeToolTip && mouse.containsMouse && root.enabled && root.tip !== "")
                         headerHost.showHeaderToolTip(root, root.tip)
    }
    AeToolTip {
        ownerItem: root
        text: root.tip
        shown: !root.nativeToolTip && mouse.containsMouse && root.enabled && text !== ""
    }

    // Tab reaches the dock's buttons; Space and Return act, as AeIconButton's do.
    activeFocusOnTab: true
    Keys.onSpacePressed: if (root.enabled) { root.clicked(); event.accepted = true }
    Keys.onReturnPressed: if (root.enabled) { root.clicked(); event.accepted = true }
}
