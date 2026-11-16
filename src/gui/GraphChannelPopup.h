#ifndef GUI_GRAPHCHANNELPOPUP_H
#define GUI_GRAPHCHANNELPOPUP_H

#include <QWidget>
#include <QVector>
#include <QRect>
#include <QString>
#include <functional>
#include "gui/GraphPanelModel.h"

class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QPainter;

namespace gui {

// THE CHANNEL LIST, as the dock header's channels control shows it.
//
// The panel used to own this list as a QMenu (gui::MainDisplayWidget::
// showGraphChannelMenu). The design moved the CONTROL into the dock's header - it is
// a view action, so it belongs with the view controls - and the design's list is not
// a menu: a LAYER section heading (10px muted), then self-describing 22px rows with a
// 12x12 rounded swatch at the heading's left edge and the name 8px after it
// ("Move X", "Move Y", "Rotate", "Opacity"); the swatch is the row's indent.
// There is NO group heading (the rows already carry the property and component), no
// "layer · group" separator and no bold for the sticky focus. gui::AppStyle sizes every QMenu row to 24px
// and QSS cannot reach the rest (see AppStyle::sizeFromContents), so the list is its
// own painted widget rather than a QMenu with a stylesheet.
//
// THE BODY IS THE APPLICATION'S SUNKEN ONE. The user ruled that the app's menu body
// reads better than the mockup's own surface: the body is theme::Colors::floaterBody
// edged with floaterEdge - the same pair gui::AppStyle paints for PE_PanelMenu - with
// the design's 6px radius, 6px/0 padding, 150px floor and its drop shadow.
//
// ONE ACTION PER ROW, the legend's own rule: the row IS the toggle (the swatch is its
// colour key - filled with the hue while the channel is shown, the hue as a 1px
// outline while it is hidden), and hovering a row FOCUSES that channel (the panel
// then draws its tangent handles). Keyboard: the list takes focus, Up/Down/Home/End
// move the current row, Space/Return toggles it and Escape (or a click outside) closes.
class GraphChannelPopup: public QWidget {
    Q_OBJECT
public:
    explicit GraphChannelPopup(QWidget* aParent = nullptr);
    ~GraphChannelPopup() override;

    // The rows to list, in the model's own order (contiguous by group): a heading is
    // drawn wherever the (layer, group) text changes, exactly as the model's
    // PanelChannelRow::group promises.
    void setRows(const QVector<PanelChannelRow>& aRows);
    // Opens under aGlobalBottomRight: the BODY's right edge is aligned with that
    // point's x and its top sits just under it (the mock's own anchoring of the
    // header's button), clamped to the screen. The size is the rows' own layout.
    void openAt(const QPoint& aGlobalBottomRight);

    // The row's toggle: called with the row's NEW visibility, AFTER this list flipped
    // its own copy (the swatch and the name follow). False CLOSES the list - the row
    // names a node the document no longer holds, so there is nothing left to toggle.
    std::function<bool(const PanelChannelRow&)> onRowToggled;
    // The row under the pointer CHANGED, and it is a channel row: focus that channel.
    // Not called while the pointer stays within one row.
    std::function<void(const PanelChannelRow&)> onRowHovered;

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    // One painted line: a LAYER section heading or a channel row (the index into
    // mRows), with the box the layout gave it (this widget's pixels). There is no
    // group heading: "Move X"/"Move Y" already name themselves, and a group title
    // above them only obscured where the group ended.
    enum class EntryKind { Layer, Row };
    struct Entry {
        EntryKind kind = EntryKind::Row;
        int row = -1;
        QString text;
        QRect rect;
    };

    void rebuildLayout();
    void paintBody(QPainter& aPainter) const;
    void paintEntry(QPainter& aPainter, int aIndex) const;
    // The channel row under aPos, or -1 (a heading or the padding is no row).
    int rowAt(const QPoint& aPos) const;
    // Moves the keyboard's current row by aStep rows, skipping headings.
    void moveCurrent(int aStep);
    // Toggles the keyboard's current row, through the same path a click takes.
    void activateCurrent();
    void toggleRow(int aIndex);
    void setHover(int aIndex);

    QVector<PanelChannelRow> mRows;
    QVector<Entry> mEntries;
    int mHover = -1;    // the row under the pointer, -1 when none
    int mCurrent = -1;  // the row the keyboard names, -1 when none
    // Whether the keyboard has actually shown mCurrent. A list opened with the pointer
    // starts with row 0 current so Space/Return have a target, but nothing is marked
    // until a key moves or activates the cursor: the pointer's own mark is the hover
    // fill, and a keyboard cursor that appeared on open would read as a second, phantom
    // highlight. Cleared when the pointer takes over a row.
    bool mKeyboardCursor = false;
    QRect mBody;        // the body (shadow inset included) in this widget's pixels
};

} // namespace gui

#endif // GUI_GRAPHCHANNELPOPUP_H
