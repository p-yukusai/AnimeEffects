#ifndef GUI_COMMANDLOGVIEW_H
#define GUI_COMMANDLOGVIEW_H

#include <QColor>
#include <QRectF>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QVector>

#include "gui/LogEntry.h"

class QPainter;
class QTextLine;

namespace gui {

// The Properties panel's bottom pane: the command log the editors report into
// (ViaPoint::pushLog) and the one place undo/redo says which command it moved.
//
// It is ONE text document, deliberately. The note asks for structured entries
// instead of the old "Undone : X" strings and, in the same breath, that selecting
// and copying several lines keeps working; a QML delegate list would give the
// first and take the second (each delegate owns its own text, so a drag that
// starts in one entry and ends in another selects nothing). Every entry therefore
// stays text, and the tag is the span of characters the entry's verb occupies.
//
// THE ENTRY IS A QUIET PILL AND A MESSAGE:
//
//     (Undo) Add filter
//     (Redo) Add filter
//     (Warning) Failed to save project
//
// The pill is a SOFT ROUNDED FILL and nothing else: no border, no icon, no
// gradient. It is painted by this class in paintEvent, UNDER the text - a
// QTextCharFormat can only fill a run's rectangle (no radius), and the first
// version of this pane was exactly that, a square wash of ink. Painting under the
// text is safe because the view's own paint does not erase the viewport (the
// viewport fills its background before the event, so a fill issued here survives
// and the text is laid over it; measured with a red rect in both QPlainTextEdit
// and QTextEdit).
//
// A bordered chip around every verb was tried and rejected by eye at the dock's
// 320px: four outlined boxes stacked in a 90px pane read as a row of controls, and
// the accent outline doubled the hue the application reserves for the DOCUMENT's
// own marks. The border is what made it look like a form; the fill alone reads as
// a tag. A warning is the only kind with a stronger fill, because it is the only
// kind that has to catch the eye.
//
// THE BASE IS A QTextEdit, and only for the gutter. The pill has to extend to the
// LEFT of the verb (that is what makes it a tag rather than a highlighted word),
// which needs a per-block left margin; QPlainTextDocumentLayout accepts a block
// format's leftMargin and never applies it, while QTextEdit's document layout
// does. The pane's own look comes from `QTextEdit#commandLog` in standard.ssa
// rather than from the QTextEdit rule above it (which styles editable fields as
// `raised`).
//
// Entries are written with QTextCursor + QTextCharFormat rather than as HTML:
// the old HTML append interpolated the message into markup, so a command name
// containing '<' or '&' corrupted the line, and the colour was fixed when the line
// was pushed, so a theme switch left the older lines in the previous theme's ink.
// The formats are re-applied in place from refreshTheme() instead, which is why
// the entries are also kept in mEntries and both the document and that vector are
// capped by the same constant.
class CommandLogView: public QTextEdit {
    Q_OBJECT
public:
    // One constant caps the document's blocks AND the retained entries: the
    // theme refresh walks the two in lockstep, so a second limit could let them
    // drift apart and leave a block holding the ink of the theme it was written
    // in (the exact bug this class exists to remove).
    static constexpr int kMaxEntries = 32;

    explicit CommandLogView(QWidget* aParent = nullptr);

    // One line: the entry's verb, a separator space widened to the gap after the
    // pill, then its message. The verb is skipped for Info, whose prose is the
    // whole line; the message is skipped when it is empty (a plain command returns
    // no name), so a line never ends in a dangling separator. The separator stays a
    // real space: a copy of the log must read exactly as the pane does.
    void append(const LogEntry& aEntry);

    // Re-ink every entry from theme::Colors::current() IN PLACE: the document's
    // text and every live selection stay where they are, which setPlainText() (or
    // rebuilding the lines) would not preserve. Call it on a theme switch. The
    // pills need no re-ink of their own - they read the palette when they are
    // painted - but the formats do, because the separator's width is measured from
    // the font and pills hugging the verbs un-padded would move.
    void refreshTheme();

    // How many entries the log still holds (== document()->blockCount() unless
    // the document was written from outside, which this class never does).
    int entryCount() const { return int(mEntries.size()); }

    // Where the last paint drew entry aEntry's pill, in VIEWPORT coordinates, or
    // an empty rect for an entry with no badge (Info) and for one that was not on
    // screen. The gate asks this rather than re-deriving the geometry, so it can
    // measure what was inked against the text it was inked around.
    QRectF badgePillRect(int aEntry) const { return mPillRects.value(aEntry); }

protected:
    void paintEvent(QPaintEvent* aEvent) override;

private:
    // The word painted in the pill ("Undo", "Redo", "Warning"), or an empty string
    // for Info, which is prose and gets no pill.
    static QString badgeText(LogKind aKind);
    // The verb's own ink: undo is quiet (textMuted), redo is the accent's bright
    // edge, and a warning takes the semantic warning token - never the accent,
    // because the accent is the user's hue and a warning has to read as one
    // whatever the user picked.
    QColor badgeInk(LogKind aKind) const;
    // The pill's fill: the pane's own text ink at a low alpha, or the warning
    // token's, painted OVER the pane's floor so it reads on the log's `recessed`
    // surface in either theme without the pill having to know that colour.
    QColor pillFill(LogKind aKind) const;

    QTextCharFormat badgeFormat(LogKind aKind) const;
    // The separator space, widened so the message keeps its distance from the
    // pill's right edge. Its TEXT is a single ' ' - only its advance moves.
    QTextCharFormat gapFormat() const;
    QTextCharFormat messageFormat() const;
    // The block's geometry: the gutter the pill hangs into, and the gap between two
    // pills (the pane is dense - one line per entry).
    QTextBlockFormat lineFormat() const;

    void writeEntry(QTextCursor& aCursor, const LogEntry& aEntry) const;
    // Applies the entry's formats to an existing block (the refresh path).
    void inkEntry(QTextCursor& aCursor, const QTextBlock& aBlock, const LogEntry& aEntry) const;
    // Drops the oldest blocks until the document holds kMaxEntries, so the
    // document's cap is enforced HERE and now rather than by Qt at its next
    // layout. BOTH caps are kMaxEntries: the theme refresh pairs block i with
    // entry i, so a document that trimmed later (or not at all, headless) would
    // re-ink the wrong line.
    void trimToLimit();

    // Paints every entry's pill and refills mPillRects. Under the text, so it runs
    // before the base class's paint.
    void paintPills(QPainter& aPainter, const QRect& aClip);
    // The pill of ONE line of a verb run, in viewport coordinates.
    QRectF pillRectForLine(
        QTextCursor& aProbe, const QTextBlock& aBlock, const QTextLine& aLine, int aVerbLength
    ) const;

    QVector<LogEntry> mEntries;
    // Parallel to mEntries: the pill each entry was last painted with. Filled by
    // paintPills (which runs on every paint), empty where nothing was drawn.
    QVector<QRectF> mPillRects;
};

} // namespace gui

#endif // GUI_COMMANDLOGVIEW_H
