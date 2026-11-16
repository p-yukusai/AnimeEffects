#ifndef GUI_DOCKPANEL_H
#define GUI_DOCKPANEL_H

#include "gui/DockMetrics.h"

#include <QMainWindow>
#include <QPoint>
#include <QDockWidget>
#include <QPointer>
#include <QVector>

namespace gui {

/// The panel's right-click menu (undock/dock, hide). Shared, because which
/// widget receives the click depends on the state: a docked panel's header is
/// opaque to the mouse and handles it, a floating panel's header is transparent
/// so the QDockWidget underneath receives it.
void showDockPanelMenu(QDockWidget* aDock, const QPoint& aGlobalPos);

/**
 * A QDockWidget that owns its own chrome.
 *
 * QMainWindow keeps doing all the docking work - areas, splitters, tabify,
 * resizeDocks, saveState - so this is a dock, not a docking engine. What it
 * takes over is the part Qt gets wrong on Linux:
 *
 *  - the header, through DockTabBar (which also suppresses Qt's float and
 *    close buttons and Qt's own drag),
 *  - the floating window's frame. Qt paints neither frame nor title once a
 *    title bar widget exists, and on Wayland the panel is a frameless tool
 *    window that nothing else decorates, so the edge and its rounding are
 *    drawn here.
 *
 * The floating window carries a ring of kRing on every side, inside a
 * kGrabMargin halo. The ring is what the border is painted in; the halo plus
 * the ring is the resize band. The inset itself is created by the transparent
 * QDockWidget border in the theme, because
 * QStyleSheetStyle::pixelMetric(PM_DockWidgetFrameWidth) follows the QSS border
 * width and QDockWidgetLayout insets the header and the content by exactly that
 * much (kGrabMargin + kRing). Without it the content widget is flush with the
 * window edge and any border painted by the dock ends up underneath it.
 */
class DockPanel: public QDockWidget {
    Q_OBJECT

public:
    explicit DockPanel(QWidget* aParent);

    /// Undock/dock the panel. The translucent attributes have to be in place
    /// before the floating window is created, and must not outlive it, so every
    /// float goes through here rather than calling setFloating() directly.
    void setPanelFloating(bool aFloating);

    /// True while this panel shares a dock area with another one, i.e. Qt has
    /// moved it into a tab group. The group's tab strip is the panel's title UI
    /// in that state, so the panel's own header steps aside instead of drawing
    /// a second title row under the tabs.
    bool isTabGrouped();
    /// Shows the header only while this panel is the only one in its area.
    /// Public: the main window re-syncs after layout changes, since a dock's own
    /// events do not cover every way its group membership can change.
    void updateTitleChrome();

    /// The window this panel docks into, remembered at creation. It cannot be
    /// found later: a floating panel is its own toplevel, so walking up from the
    /// panel's window reaches the panel, not the window it belongs to - which is
    /// exactly the walk that has to work when a drag starts on a floating panel.
    void setMainWindow(QMainWindow* aWindow) { mMainWindow = aWindow; }
    QMainWindow* mainWindow() const { return mMainWindow; }

protected:
    void paintEvent(QPaintEvent* aEvent) override;
    void resizeEvent(QResizeEvent* aEvent) override;
    void contextMenuEvent(QContextMenuEvent* aEvent) override;
    bool event(QEvent* aEvent) override;
    bool eventFilter(QObject* aObject, QEvent* aEvent) override;

private:
    /// Invisible margin between the window's outer edge and the painted frame:
    /// Wayland gives a frameless toplevel no server-side border at all, so
    /// without it the frame line sits exactly on the window edge and there is
    /// nowhere to grab except inward, the opposite of every other window on the
    /// desktop. See dockMetrics::kFrameMargin - the drop preview measures
    /// against the same numbers, because a floating panel's sizeHint includes
    /// the frame.
    static constexpr int kGrabMargin = dockMetrics::kFrameMargin;
    /// Width of the border ring painted inside the frame.
    static constexpr int kRing = dockMetrics::kFrameRing;
    /// The resize band, measured from the window's outer edge: kGrabMargin
    /// outside the frame line plus kGrabMargin again inside it, so the grab
    /// straddles the edge instead of sitting on one side of it.
    static constexpr int kBand = kGrabMargin * 2;
    /// Corner radius of the floating frame; kRing keeps the content's square
    /// corners inside the rounded body, so they are never visible.
    static constexpr int kRadius = kRing;

    /// Input-only strip lying in one band of the floating panel's edge ring.
    ///
    /// These exist because owning the edge gesture through an event filter is a
    /// race that was lost: Qt emits QDockWidget::topLevelChanged *before* it
    /// installs its own QWidgetResizeHandler (qdockwidget.cpp, setWindowState),
    /// and event filters run newest-first, so Qt's handler always ends up in
    /// front of any filter re-installed from that signal - and its resize
    /// recomputes the widget geometry, whose position half Wayland throws away,
    /// which is why the top and left edges resized from the opposite corner.
    /// A child widget is hit before its parent, so a press in the ring never
    /// reaches the dock and Qt's handler never sees it.
    class RingBand;

    void layoutRing();
    /// Diagnostic: logs what the window's own edge hit-tests as, once the bands
    /// are placed (childAt() is the same hit-test input dispatch uses).
    void logRingProbe();
    /// Relayouts the bands if the panel changed size since they were placed.
    /// A band left at a stale size is not just cosmetic: the press it should
    /// have caught falls through to the dock, where Qt's own resize handler
    /// takes it and resizes from the opposite anchor - and the cursor it
    /// derives comes from the same stale rectangle.
    void ensureRing();
    void onTopLevelChanged(bool aFloating);

    QVector<RingBand*> mRing;
    QSize mRingSize;
    QPointer<QMainWindow> mMainWindow;
};

} // namespace gui

#endif
