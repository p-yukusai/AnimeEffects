pragma Translator: "AnimeEffects.Dialogs"

import QtQuick
import QtQuick.Controls

// A flat button, the QML counterpart of the app's borderless fields.
//
// Qt Quick Controls' Button with the app's OWN background and content. The control's
// chrome is a delegate, so the design keeps exactly what it specifies - the raised
// fill, the accent fill for the one primary action, no fill for a ghost, and a focus
// ring as the only border ever drawn - while Qt supplies hover, pressed, the focus
// policy, Space, the button role in the accessibility tree, and `checked`.
//
// The old header justified itself with "a QC2 Button draws its own control-style
// chrome, and the app has no such chrome anywhere". That reads as a limit and is not
// one: that chrome is precisely the `background` replaced below.
//
// Measured on this Qt before converting (see docs/export-dialog-implementation-notes):
//   * Space fires clicked, and on RELEASE - so the hand-written Keys.onSpacePressed
//     would have fired it twice. Dropped, replaced by nothing.
//   * Return and Enter do NOT fire it: Qt's Button has no default-button concept in
//     QML, so contract line A2 still needs the Keys.onReturnPressed at the bottom.
//   * `visualFocus`, `hovered`, `pressed`, `down`, `focusPolicy`, `checked`,
//     `palette`, `click()` all exist; `clicked()` takes NO argument.
//   * Basic's padding is symmetric (8 left, 8 right), so centred content stays
//     centred inside the content area the control assigns.
//   * `HoverHandler` has `cursorShape`, which is how the pointing-hand cursor
//     survives - a MouseArea would have double-handled the click.
//
// "kind" is one of:
//   secondary - the default action button: raised fill, no border
//   primary   - the accent fill, exactly one per surface (Export)
//   ghost     - no fill at rest; for low-priority actions (Add range)
Button {
    id: root

    property string kind: "secondary"
    // A tinted glyph name from theme.iconUrl() ("plus-bold", "trash"); empty
    // for a text-only button. See QmlTheme::iconUrl.
    property string iconName: ""
    // NO `property bool enabled`, NO `property string text`, NO `signal clicked`:
    // Button has all three. Redeclaring `enabled` shadows the base property
    // (qmllint: [property-override]), which leaves the base state "enabled" and a
    // disabled button never reports itself disabled to the accessibility tree - the
    // one thing a GUI probe uses to tell a frozen control from a live one. `kind`
    // and `iconName` stay ours: they are this design's vocabulary, not Qt's.

    implicitWidth: Math.max(72, content.implicitWidth + 24)
    implicitHeight: 28
    hoverEnabled: true
    // Explicit because contract line A1 requires Tab traversal of these controls and
    // depending on a style's default is how that quietly stops being true.
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.Button
    Accessible.name: root.text
    Accessible.onPressAction: if (root.enabled) root.click()

    // The focus ring is the ONLY border this button ever draws. `visualFocus` is
    // Qt's own, and it means keyboard focus - where the previous `activeFocus` also
    // lit the ring after an ordinary mouse click.
    background: Rectangle {
        radius: 4
        color: {
            if (!root.enabled) return theme.recessed;
            if (root.down) {
                if (root.kind === "primary") return theme.accentBright;
                return theme.active;
            }
            if (root.hovered) {
                if (root.kind === "primary") return theme.accentHover;
                if (root.kind === "ghost") return theme.raised;
                return theme.raisedHover;
            }
            if (root.kind === "primary") return theme.accent;
            // C21: the ghost's rest fill is INVISIBLE but NOT "transparent".
            // `Behavior on color` below lerps straight RGBA, and rgba(0,0,0,0)
            // carries BLACK in its RGB: the 80 ms fade to theme.raised passed
            // through dark semi-transparent frames - the "Add range hover
            // flashes black" defect (measured: the fill's own colour climbed
            // #000000 -> #020202 -> #0a0a0a -> #171717 -> #292929 -> #404040
            // over 86 ms, and the leave fade dipped below the base on screen).
            // A zero-alpha fill in the HOVER colour's own RGB fades as a clean
            // alpha ramp and paints nothing at rest - identical settled look,
            // no dark frame in either direction.
            if (root.kind === "ghost") return Qt.rgba(theme.raised.r, theme.raised.g, theme.raised.b, 0);
            return theme.raised;
        }
        border.width: root.visualFocus ? 1 : 0
        border.color: theme.focus
        Behavior on color { ColorAnimation { duration: 80 } }
    }

    // Our content rather than Qt's IconLabel: the design fixes a 14px glyph and a
    // 12px label, and Qt's icon.* path colourises images instead of drawing the
    // pre-tinted SVGs this theme hands out. The outer Item is what the control
    // sizes, so the row keeps centring itself inside the padding.
    contentItem: Item {
        Row {
            id: content
            anchors.centerIn: parent
            spacing: 7
            Image {
                visible: root.iconName !== ""
                width: 14
                height: 14
                anchors.verticalCenter: parent.verticalCenter
                source: root.iconName === "" ? "" : theme.iconUrl(root.iconName)
                sourceSize.width: 64
                sourceSize.height: 64
                fillMode: Image.PreserveAspectFit
                opacity: root.enabled ? 1.0 : 0.5
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.text
                color: {
                    if (!root.enabled) return theme.textDisabled;
                    return root.kind === "primary" ? theme.accentText : theme.text;
                }
                font.pixelSize: 12
            }
        }
    }

    HoverHandler {
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
    }

    // Return/Enter: NOT handled by Qt (measured), and contract line A2 says Enter
    // activates the default action. Space is handled by Qt, on release - so there is
    // deliberately no Keys.onSpacePressed here, which would have clicked twice.
    Keys.onReturnPressed: (event) => {
        if (!root.enabled) return;
        root.click();
        event.accepted = true;
    }
}
