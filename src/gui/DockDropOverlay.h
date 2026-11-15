#ifndef GUI_DOCKDROPOVERLAY_H
#define GUI_DOCKDROPOVERLAY_H

#include <QDockWidget>
#include <QPixmap>
#include <QRect>
#include <functional>

class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;
class QMainWindow;
class QMimeData;

namespace gui {

/// The image a drag carries and where inside it the pointer grabbed. For a tab
/// drag that is the tab itself, so the ghost next to the cursor is the thing the
/// user picked up rather than an abstract rectangle.
struct DockDragImage {
    QPixmap pixmap;
    QPoint hotSpot;
};

/// Starts the dock drag for a panel: the drag image, the MIME payload and the
/// overlay that resolves where it lands. Shared by every title row this app has,
/// since a drag has to mean the same thing from all of them.
void beginDockPanelDrag(QDockWidget* aPanel, QWidget* aSource,
                        const DockDragImage& aImage = DockDragImage());

/**
 * The drop side of the dock gesture: what a dragged panel lands on, and the
 * preview that says so.
 *
 * Qt's own drop handling is not used. It exists - QMainWindow::event handles
 * DragEnter/DragMove/Drop for a dock drag - but its feedback is a QRubberBand
 * drawn at the rect the layout would give the panel, which is truthful and
 * useless: at this app's sizes it is a sliver a few pixels wide, it appears
 * only once the cursor is already over it, and its leave path can miss, leaving
 * it stuck on screen. What is here instead is one resolver that decides the
 * target and a rectangle for the preview derived from the SAME slot model and
 * placement rule the drop follows (MainWindow::placeDock), drawn large enough to
 * aim at, shown for the whole drag rather than only once the cursor is inside,
 * and cleared the moment the drag leaves.
 *
 * The overlay is a child of the main window covering it, shown only while a
 * drag is in flight. That is also what makes it the drop target: a drag is
 * delivered to the widget under the cursor, so covering the window means the
 * decision is ours and Qt's own handler never sees the drop.
 *
 * Hovering is keyed to the pointer, which during a drag *is* the panel: the
 * drag image is the panel's own snapshot, so "where the window is" and "where
 * the cursor is" are the same place. There is nothing else it could be keyed
 * to: a drag-and-drop is the only gesture in which a Wayland client is told
 * where the pointer is at all, and the price of that is that the window itself
 * cannot follow the cursor (that needs the compositor's move, which reports no
 * positions).
 */
/// What a drop would do.
enum class DockDropKind {
    None,   ///< nothing here: the panel stays floating where it is
    Area,   ///< take a side of the central widget (addDockWidget)
    Split,  ///< divide the target group (splitDockWidget)
    Tabify, ///< become a tab of the target group (tabifyDockWidget)
};

/// A resolved drop: at namespace scope rather than nested, so the main window
/// can declare it and take it by reference without including this header.
struct DockDropTarget {
    DockDropKind kind = DockDropKind::None;
    Qt::DockWidgetArea area = Qt::NoDockWidgetArea;
    /// The group's title-bearing dock, for Split and Tabify.
    QDockWidget* group = nullptr;
    /// The rectangle the drop would occupy, in main-window coordinates.
    QRect rect;
};

class DockDropOverlay: public QWidget {
    Q_OBJECT

public:
    explicit DockDropOverlay(QMainWindow* aWindow);

    /// The overlay of the main window a widget lives in, if there is one. This
    /// only works for a widget that is docked: a floating panel is a toplevel,
    /// so its window chain ends at itself - use forPanel for a dock widget.
    static DockDropOverlay* forWidget(QWidget* aWidget);
    /// The overlay a drag of this panel has to land on.
    static DockDropOverlay* forPanel(QDockWidget* aPanel);

    /// The MIME type carrying the dragged panel's object name.
    static const char* mimeType();

    /// Show the overlay for a drag of aPanel, carrying aImage as the thing the
    /// gesture picked up. Call before QDrag::exec: the overlay has to be up to
    /// receive the drag's enter event at all.
    void beginDrag(QDockWidget* aPanel, const DockDragImage& aImage = DockDragImage());
    /// Hide it again, whatever the outcome.
    void endDrag();

    /// Resolve a position and repaint the preview. Public because the target
    /// rule is the thing worth testing, and a mouse is not the only way in.
    DockDropTarget hover(const QPoint& aPosInWindow);
    /// The target at a position, without showing anything.
    DockDropTarget resolve(const QPoint& aPosInWindow) const;

    /// Called with the resolved target when a drag is dropped on the overlay.
    std::function<void(QDockWidget*, const DockDropTarget&)> onDropped;

protected:
    void paintEvent(QPaintEvent* aEvent) override;
    void resizeEvent(QResizeEvent* aEvent) override;
    void dragEnterEvent(QDragEnterEvent* aEvent) override;
    void dragMoveEvent(QDragMoveEvent* aEvent) override;
    void dragLeaveEvent(QDragLeaveEvent* aEvent) override;
    void dropEvent(QDropEvent* aEvent) override;

private:
    /// The region an area drop takes, following the placement rule: the slice
    /// beside the area's innermost dock (the split anchor), or the area's own
    /// place when it is empty.
    QRect targetRect(const DockDropTarget& aTarget) const;

    QDockWidget* panelFromMime(const QMimeData* aData) const;

    QMainWindow* mWindow;
    QDockWidget* mPanel = nullptr;
    DockDropTarget mTarget;
    /// The tab being carried, and where the pointer last was. The ghost is drawn
    /// here rather than left to the platform's drag icon: a drag pixmap is a
    /// surface the compositor has to place and paint, and on Wayland it does not
    /// appear at all - measured, the drag runs (the preview region updates with the
    /// pointer) with the tab image nowhere on screen. The overlay is a widget in
    /// this window that is already following the drag, so it is the one place that
    /// can always show the image, on every platform.
    DockDragImage mDragImage;
    QPoint mPointer;
    bool mHavePointer = false;
};

} // namespace gui

#endif // GUI_DOCKDROPOVERLAY_H
