#include "gui/CommandLogView.h"

#include "gui/theme/Colors.h"

#include <QTextDocument>

namespace gui {

namespace {

// The badge's fill is the badge's OWN ink at a low alpha, painted as the run's
// background. What a QTextCharFormat can express is exactly that: a background
// brush. The app's chip vocabulary wants a `recessed` floor with a `hairline`
// edge and a 4px radius, and a text run cannot have either - no edge, no radius.
// The alternative (a tiny widget per entry, the usual way to get a rounded chip)
// would give every entry its own text document and take the property the note
// asks us to keep with it: a drag that starts in one entry and ends in another
// would select nothing, and copying the log would copy one badge. So the badge
// keeps the two things a run does carry - its own ink, and its own background -
// and gives up the square corners instead of the cross-entry selection.
//
// The tint has to come from the ink and not from `recessed`: the log floor IS
// `recessed` (standard.ssa), so a recessed fill would be invisible on it. At 18%
// the wash still reads as a tag on both themes while the ink keeps the contrast
// it has on the bare floor.
constexpr float kBadgeTint = 0.18f;

QColor tinted(const QColor& aInk) {
    QColor fill(aInk);
    fill.setAlphaF(kBadgeTint);
    return fill;
}

// One LogEntry is ONE log line. Messages do arrive with newlines (the resource
// updater packs a list of failures into one string) and QTextCursor::insertText
// turns every '\n' and paragraph separator into a BLOCK separator - so a single
// entry would own several blocks. That breaks the two invariants this class is
// built on: the document's cap is a block cap (one entry, one block) and
// refreshTheme() pairs block i with entry i. The line's breaks therefore become
// spaces before the text is stored, so the retained entry and the block text are
// exactly what is on screen.
QString oneLine(const QString& aText) {
    QString text = aText;
    const QChar space(QLatin1Char(' '));
    text.replace(QChar(QLatin1Char('\n')), space);
    text.replace(QChar(QLatin1Char('\r')), space);
    text.replace(QChar(QChar::ParagraphSeparator), space);
    text.replace(QChar(QChar::LineSeparator), space);
    return text;
}

} // namespace

CommandLogView::CommandLogView(QWidget* aParent): QPlainTextEdit(aParent) {
    // The document keeps the last kMaxEntries blocks and mEntries keeps the last
    // kMaxEntries entries. append() enforces the document's cap itself (see
    // trimToLimit); setMaximumBlockCount() is the backstop for Qt's own layout
    // pass, at the same constant, so the two can never disagree about the size.
    setMaximumBlockCount(kMaxEntries);
    setUndoRedoEnabled(false);
    setReadOnly(true);

    // The pane used to be an anonymous QPlainTextEdit: no probe could find it and
    // the accessibility tree had no name to address it by.
    setObjectName(QStringLiteral("commandLog"));
    setAccessibleName(tr("Command log"));
}

void CommandLogView::append(const LogEntry& aEntry) {
    LogEntry entry = aEntry;
    entry.text = oneLine(entry.text);
    mEntries.push_back(entry);
    while (mEntries.size() > kMaxEntries) mEntries.removeFirst();

    QTextCursor cursor(document());
    if (mEntries.size() == 1 && document()->isEmpty()) {
        // The first line goes INTO the document's initial empty block. Inserting
        // a block first would leave that empty block above it, so the log would
        // open with a blank line and toPlainText() would start with a newline.
        cursor.movePosition(QTextCursor::Start);
    } else {
        cursor.movePosition(QTextCursor::End);
        cursor.insertBlock();
    }
    writeEntry(cursor, entry);
    trimToLimit();

    // Follow the newest line. appendPlainText() used to do this for us by moving
    // the widget's own cursor; writing through a private cursor leaves the view
    // where it was, which on a short pane means the new line arrives off-screen.
    // A LIVE SELECTION is left alone on purpose: a line arriving from an autosave
    // or a long job while the user is copying across entries must not eat the
    // selection this pane exists to keep copyable.
    if (!textCursor().hasSelection()) {
        QTextCursor follow = textCursor();
        follow.movePosition(QTextCursor::End);
        setTextCursor(follow);
        ensureCursorVisible();
    }
}

void CommandLogView::trimToLimit() {
    // Qt enforces maximumBlockCount lazily (at its next layout), and a headless
    // view never lays out - so the document is trimmed here, after the new line
    // exists, in lockstep with mEntries. The removal deletes the oldest block's
    // text together with its separator, so the surviving block keeps its own
    // per-character formats.
    while (document()->blockCount() > kMaxEntries) {
        QTextCursor cursor(document());
        cursor.movePosition(QTextCursor::Start);
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
        if (!cursor.hasSelection()) return;
        cursor.removeSelectedText();
    }
}

void CommandLogView::refreshTheme() {
    QTextCursor cursor(document());
    // One edit block: a refresh is then a single document change instead of one
    // per line, so the log lays out once.
    cursor.beginEditBlock();
    QTextBlock block = document()->firstBlock();
    for (qsizetype i = 0; block.isValid() && i < mEntries.size(); ++i, block = block.next()) {
        inkEntry(cursor, block, mEntries[i]);
    }
    cursor.endEditBlock();
}

QString CommandLogView::badgeText(LogKind aKind) {
    switch (aKind) {
    case LogKind::Undo: return tr("Undo");
    case LogKind::Redo: return tr("Redo");
    case LogKind::Warning: return tr("Warning");
    case LogKind::Info: break;
    }
    return QString(); // info is prose: no badge
}

QColor CommandLogView::badgeInk(LogKind aKind) const {
    const theme::Colors& colors = theme::Colors::current();
    switch (aKind) {
    case LogKind::Undo: return colors.textMuted;
    case LogKind::Redo: return colors.accentBright;
    case LogKind::Warning: return colors.warning;
    case LogKind::Info: break;
    }
    return colors.text;
}

QTextCharFormat CommandLogView::badgeFormat(LogKind aKind) const {
    const QColor ink = badgeInk(aKind);
    QTextCharFormat format;
    format.setForeground(ink);
    format.setBackground(tinted(ink));
    return format;
}

QTextCharFormat CommandLogView::messageFormat() const {
    QTextCharFormat format;
    format.setForeground(theme::Colors::current().text);
    // Stated explicitly rather than left unset: setCharFormat() MERGES a format
    // into the existing one, so a message run that did not name its background
    // could keep a badge wash from an earlier ink.
    format.setBackground(Qt::NoBrush);
    return format;
}

void CommandLogView::writeEntry(QTextCursor& aCursor, const LogEntry& aEntry) const {
    const QString badge = badgeText(aEntry.kind);
    if (badge.isEmpty()) {
        aCursor.insertText(aEntry.text, messageFormat());
        return;
    }
    aCursor.insertText(badge, badgeFormat(aEntry.kind));
    if (!aEntry.text.isEmpty()) {
        // The separating space belongs to the message run, not the badge: the
        // badge is a tag, and trailing padding inside its tint would read as part
        // of the tag. No " : " separator anywhere - the badge IS the separator.
        aCursor.insertText(QStringLiteral(" ") + aEntry.text, messageFormat());
    }
}

void CommandLogView::inkEntry(
    QTextCursor& aCursor, const QTextBlock& aBlock, const LogEntry& aEntry
) const {
    const QString badge = badgeText(aEntry.kind);
    const int start = aBlock.position();
    const int end = start + int(aBlock.text().size());
    if (badge.isEmpty()) {
        aCursor.setPosition(start);
        aCursor.setPosition(end, QTextCursor::KeepAnchor);
        aCursor.setCharFormat(messageFormat());
        return;
    }
    aCursor.setPosition(start);
    aCursor.setPosition(start + int(badge.size()), QTextCursor::KeepAnchor);
    aCursor.setCharFormat(badgeFormat(aEntry.kind));
    if (end > start + int(badge.size())) {
        aCursor.setPosition(start + int(badge.size()));
        aCursor.setPosition(end, QTextCursor::KeepAnchor);
        aCursor.setCharFormat(messageFormat());
    }
}

} // namespace gui
