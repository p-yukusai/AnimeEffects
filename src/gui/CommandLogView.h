#ifndef GUI_COMMANDLOGVIEW_H
#define GUI_COMMANDLOGVIEW_H

#include <QColor>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QVector>

#include "gui/LogEntry.h"

namespace gui {

// The Properties panel's bottom pane: the command log the editors report into
// (ViaPoint::pushLog) and the one place undo/redo says which command it moved.
//
// It is ONE text document, deliberately. The note asks for badges instead of the
// old "Undone : X" strings and, in the same breath, that selecting and copying
// several lines keeps working; a QML delegate list would give the first and take
// the second (each delegate owns its own text, so a drag that starts in one entry
// and ends in another selects nothing). Every entry therefore stays text, and the
// badge is the span of characters the entry's verb occupies.
//
// Entries are written with QTextCursor + QTextCharFormat rather than as HTML:
// the old HTML append interpolated the message into markup, so a command name
// containing '<' or '&' corrupted the line, and the colour was fixed when the line
// was pushed, so a theme switch left the older lines in the previous theme's ink.
// The formats are re-applied in place from refreshTheme() instead, which is why the
// entries are also kept in mEntries and both the document and that vector are
// capped by the same constant.
class CommandLogView: public QPlainTextEdit {
    Q_OBJECT
public:
    // One constant caps the document's blocks AND the retained entries: the
    // theme refresh walks the two in lockstep, so a second limit could let them
    // drift apart and leave a block holding the ink of the theme it was written
    // in (the exact bug this class exists to remove).
    static constexpr int kMaxEntries = 32;

    explicit CommandLogView(QWidget* aParent = nullptr);

    // One line: the entry's badge run, then its message run. The badge is skipped
    // for Info, and the message is skipped when it is empty (a plain command
    // returns no name), so a line never ends in a dangling separator.
    void append(const LogEntry& aEntry);

    // Re-ink every entry from theme::Colors::current() IN PLACE: the document's
    // text and every live selection stay where they are, which setPlainText() (or
    // rebuilding the lines) would not preserve. Call it on a theme switch.
    void refreshTheme();

    // How many entries the log still holds (== document()->blockCount() unless
    // the document was written from outside, which this class never does).
    int entryCount() const { return int(mEntries.size()); }

private:
    // The word painted in the badge ("Undo", "Redo", "Warning"), or an empty
    // string for Info, which is prose and gets no tag.
    static QString badgeText(LogKind aKind);
    // The badge's own ink: undo is quiet (textMuted), redo is the accent's bright
    // edge, and a warning takes the semantic warning token - never the accent,
    // because the accent is the user's hue and a warning has to read as one
    // whatever the user picked.
    QColor badgeInk(LogKind aKind) const;

    QTextCharFormat badgeFormat(LogKind aKind) const;
    QTextCharFormat messageFormat() const;

    void writeEntry(QTextCursor& aCursor, const LogEntry& aEntry) const;
    // Applies the entry's formats to an existing block (the refresh path).
    void inkEntry(QTextCursor& aCursor, const QTextBlock& aBlock, const LogEntry& aEntry) const;
    // Drops the oldest blocks until the document holds kMaxEntries, so the
    // document's cap is enforced HERE and now rather than by Qt at its next
    // layout. BOTH caps are kMaxEntries: the theme refresh pairs block i with
    // entry i, so a document that trimmed later (or not at all, headless) would
    // re-ink the wrong line.
    void trimToLimit();

    QVector<LogEntry> mEntries;
};

} // namespace gui

#endif // GUI_COMMANDLOGVIEW_H
