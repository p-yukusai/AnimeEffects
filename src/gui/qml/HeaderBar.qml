pragma Translator: "AnimeEffects.Header"

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models

// THE WINDOW HEADER.
//
// Qt Quick Controls' MenuBar is the bar; its own MenuBarItem delegate draws the
// titles, its Menus own the popup windows and the hover/keyboard behaviour, and its
// MenuItem delegate draws the rows. There is no QMenuBar, QMenu, QAction or
// QToolButton behind this scene: the menu structure lives here (where a menu
// belongs), and gui::MainMenuBar supplies the translated words, enabled/checked
// state and callbacks through MenuCommand. See HeaderAction.qml, HeaderMenu.qml,
// HeaderMenuItem.qml and HeaderMenuBarItem.qml.
//
// THE ORDER IS THE WINDOW'S. Menu titles on the left, where a desktop menu bar keeps
// them; the Donate action beside Help, as it was; a flexible gap; then the view
// settings and the slot toggles on the right, the corner those controls have
// occupied since the docking rewrite. The view settings used to be a strip inside
// the Tools dock, above the tool panel. They are window state, not tool state: Show
// mesh, cut-outside-frame, the canvas rotations and Flip canvas all describe what
// the viewport draws, and they must not move when the user switches tools.
Rectangle {
    id: root

    color: theme.base

    MenuBar {
        id: bar
        anchors.fill: parent
        padding: 0
        spacing: 0
        implicitHeight: 30
        delegate: HeaderMenuBarItem { }
        background: Rectangle { color: theme.base }

        // The bar's content is the menu titles followed by the window controls. The
        // Repeater is the documented MenuBar customization (Basic/MenuBar.qml uses
        // the same shape); MenuBar supplies the delegate and the menu opening.
        contentItem: Item {
            implicitHeight: 30

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 1

                Row {
                    id: menuRow
                    // The QMenuBar spaced its items by PM_MenuBarItemSpacing; 9px is
                    // measured against the widget bar this replaces (File at x=17,
                    // Edit at x=61, ... Donate at x=304).
                    spacing: 9

                    Repeater {
                        model: bar.contentModel
                    }

                    // Donate is a top-level action, not a menu, so it is a flat text
                    // button in the same row. A MenuBarItem requires a Menu and would
                    // add a dropdown to a command that only opens a page.
                    HeaderTextButton {
                        id: donateButton
                        visible: mainMenu.hasCommand("donate")
                        text: visible ? mainMenu.command("donate").text : ""
                        onClicked: mainMenu.command("donate").trigger()
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                }

                // ---- the viewport settings ----------------------------------------
                // Six controls, in the order the Tools strip had: the two drawing
                // toggles, the three rotation verbs (counter-clockwise / reset /
                // clockwise), then the canvas flip. They are WINDOW state: reachable
                // without opening the Tools dock, and they cannot move when the tool
                // changes. Their glyphs take the -quiet tint: below the dock's
                // content, above the slot toggles, so the header has three readable
                // levels (view controls, then panel state).
                Row {
                    id: viewRow
                    spacing: 2
                    Layout.alignment: Qt.AlignVCenter

                    ToolIconButton {
                        iconName: "cube-quiet"
                        label: viewSettings.showMeshTip
                        checkable: true
                        checked: viewSettings.showMesh
                        onClicked: viewSettings.showMesh = !viewSettings.showMesh
                    }
                    ToolIconButton {
                        iconName: "image-quiet"
                        label: viewSettings.cutOutsideFrameTip
                        checkable: true
                        checked: viewSettings.cutOutsideFrame
                        onClicked: viewSettings.cutOutsideFrame = !viewSettings.cutOutsideFrame
                    }
                    ToolIconButton {
                        iconName: "arrow-counter-clockwise-quiet"
                        label: viewSettings.rotateCounterClockwiseTip
                        onClicked: viewSettings.rotateCounterClockwise()
                    }
                    ToolIconButton {
                        iconName: "dot-outline-quiet"
                        label: viewSettings.resetRotationTip
                        onClicked: viewSettings.resetRotation()
                    }
                    ToolIconButton {
                        iconName: "arrow-clockwise-quiet"
                        label: viewSettings.rotateClockwiseTip
                        onClicked: viewSettings.rotateClockwise()
                    }
                    ToolIconButton {
                        iconName: "flip-horizontal-quiet"
                        label: viewSettings.flipCanvasTip
                        checkable: true
                        checked: viewSettings.flipCanvas
                        onClicked: viewSettings.flipCanvas = !viewSettings.flipCanvas
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 14
                    Layout.alignment: Qt.AlignVCenter
                    Layout.leftMargin: 4
                    Layout.rightMargin: 4
                    color: theme.hairline
                }

                // ---- the layout toggles -------------------------------------------
                Row {
                    id: slotRow
                    spacing: 0
                    Layout.alignment: Qt.AlignVCenter

                    Repeater {
                        model: dockSlots.toggles
                        delegate: HeaderSlotButton {
                            required property int index
                            required property var modelData

                            iconName: modelData.glyph
                            label: modelData.accessibleName
                            tipText: modelData.tip
                            checked: modelData.checked
                            enabled: modelData.available
                            onClicked: dockSlots.toggle(index)
                        }
                    }
                }
            }
        }

        // ---- File ---------------------------------------------------------------
        HeaderMenu {
            objectName: "fileMenu"
            title: mainMenu.fileTitle

            HeaderAction { commandId: "newProject" }
            HeaderAction { commandId: "openProject" }

            HeaderMenu {
                id: recentMenu
                objectName: "recentMenu"
                title: mainMenu.openRecentTitle

                // The dynamic list is the documented pattern (Menu docs,
                // "Dynamically Generating Menu Items"): the Instantiator owns the
                // Actions and adds them to this Menu. The model is the registry's
                // recentFiles list.
                Instantiator {
                    model: mainMenu.recentFiles
                    delegate: Action {
                        required property var modelData
                        text: modelData.text
                        enabled: modelData.enabled
                        onTriggered: Qt.callLater(function() { mainMenu.openRecentPath(modelData.path); })
                    }
                    onObjectAdded: (index, object) => recentMenu.addAction(object)
                    onObjectRemoved: (index, object) => recentMenu.removeAction(object)
                }
            }

            HeaderMenuSeparator { }
            HeaderAction { commandId: "saveProject" }
            HeaderAction { commandId: "saveProjectAs" }
            HeaderAction { commandId: "exportProject" }
            HeaderAction { commandId: "quickExport" }
            HeaderAction { commandId: "exportAgain" }
            HeaderMenuSeparator { }
            HeaderAction { commandId: "closeProject" }
        }

        // ---- Edit ---------------------------------------------------------------
        HeaderMenu {
            objectName: "editMenu"
            title: mainMenu.editTitle

            HeaderAction { commandId: "undo" }
            HeaderAction { commandId: "redo" }
            HeaderMenuSeparator { }
            HeaderAction { commandId: "projectSettings" }
            HeaderAction { commandId: "assets" }
        }

        // ---- Panels -------------------------------------------------------------
        HeaderMenu {
            objectName: "panelsMenu"
            title: mainMenu.panelsTitle

            HeaderCheckableAction { commandId: "panelTools" }
            HeaderCheckableAction { commandId: "panelProperties" }
            HeaderCheckableAction { commandId: "panelAnimation" }
        }

        // ---- Preferences --------------------------------------------------------
        HeaderMenu {
            objectName: "preferencesMenu"
            title: mainMenu.preferencesTitle

            HeaderAction { commandId: "settings" }
            HeaderAction { commandId: "keybindings" }
        }

        // ---- Help ---------------------------------------------------------------
        HeaderMenu {
            objectName: "helpMenu"
            title: mainMenu.helpTitle

            HeaderAction { commandId: "about" }
            HeaderAction { commandId: "checkUpdates" }
            HeaderAction { commandId: "telemetry" }
        }
    }
}
