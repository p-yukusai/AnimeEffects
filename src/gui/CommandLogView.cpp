#include "gui/CommandLogView.h"

#include "gui/theme/Colors.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPalette>
#include <QPaintEvent>
#include <QTextDocument>
#include <QTextLayout>

namespace gui {

namespace {

// --- the pill's geometry, in LOGICAL pixels --------------------------------
//
// The pill is a soft fill around the verb: room on both sides of the word (more on
// the left, because the tag hangs into the gutter) and a radius small enough that a
// 13px-tall tag still reads as a rounded label rather than a capsule.
constexpr qreal kPillPadLeft = 6.0;
constexpr qreal kPillPadRight = 5.0;
constexpr qreal kPillRadius = 3.0;
// The pill is a hair shorter than the line's own text band, and the block's top
// margin is what keeps two pills from touching: the pane is dense, one line per
// entry.
constexpr qreal kPillVerticalShrink = 1.0;
constexpr qreal kBlockTopMargin = 3.0;
// The distance between the pill's right edge and the message. The separator space
// is widened to carry it (see gapFormat) - one space at 8pt Geist is 2.7px.
constexpr qreal kPillToMessage = 4.0;

// The fills, as a share of the ink they are made from. A neutral tag is the pane's
// own text ink at 7%: on the log's near-black floor that is a step up from it, and
// on the light theme's near-white floor a step down - one rule for both. A warning
// takes its own token at 16%, because being the only loud row in the pane is the
// job.
constexpr float kPillFillAlpha = 0.07f;
constexpr float kWarningFillAlpha = 0.16f;

QColor tinted(const QColor& aInk, float aAlpha) {
    QColor fill(aInk);
    fill.setAlphaF(aAlpha);
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

CommandLogView::CommandLogView(QWidget* aParent): QTextEdit(aParent) {
    // The command log is a QTextEdit rather than a QPlainTextEdit for ONE reason:
    // the pill hangs to the LEFT of its verb, which needs a per-block left margin,
    // and QPlainTextDocumentLayout does not implement block margins (measured: a
    // block format's leftMargin is accepted and never applied). QTextEdit's
    // document layout does, which is what puts every verb on one content column
    // with no top or bottom padding cost.
    setUndoRedoEnabled(false);
    setReadOnly(true);
    setAcceptRichText(false);

    // An empty pane says what it is for. It is a placeholder, not a document
    // entry: it never enters the text, so it cannot be selected or copied.
    setPlaceholderText(tr("Undo, redo and warnings appear here."));

    // The pane used to be an anonymous text box: no probe could find it and the
    // accessibility tree had no name to address it by.
    setObjectName(QStringLiteral("commandLog"));
    setAccessibleName(tr("Command log"));
    setAccessibleDescription(tr("Newest entries at the bottom. Select and copy across entries."));

    refreshTheme();
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

    // Follow the newest line. A live selection is left alone on purpose: a line
    // arriving from an autosave or a long job while the user is copying across
    // entries must not eat the selection this pane exists to keep copyable.
    if (!textCursor().hasSelection()) {
        QTextCursor follow = textCursor();
        follow.movePosition(QTextCursor::End);
        setTextCursor(follow);
        ensureCursorVisible();
    }
}

void CommandLogView::trimToLimit() {
    // The document's cap is enforced HERE and now rather than by Qt at its next
    // layout (a headless view never lays out), in lockstep with mEntries: the
    // theme refresh pairs block i with entry i, so a document that trimmed later
    // would re-ink the wrong line. The removal deletes the oldest block's text
    // together with its separator, so the surviving block keeps its own
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
    // The placeholder is drawn by Qt with the palette's own role, which is what
    // keeps it out of the document and out of the clipboard.
    QPalette pal = palette();
    pal.setColor(QPalette::PlaceholderText, theme::Colors::current().textDisabled);
    setPalette(pal);
    viewport()->setPalette(pal);

    QTextCursor cursor(document());
    // One edit block: a refresh is then a single document change instead of one
    // per line, so the log lays out once.
    cursor.beginEditBlock();
    QTextBlock block = document()->firstBlock();
    for (qsizetype i = 0; block.isValid() && i < mEntries.size(); ++i, block = block.next()) {
        inkEntry(cursor, block, mEntries[i]);
    }
    cursor.endEditBlock();
    viewport()->update(); // the pills read the palette when painted
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

QColor CommandLogView::pillFill(LogKind aKind) const {
    const theme::Colors& colors = theme::Colors::current();
    if (aKind == LogKind::Warning) return tinted(colors.warning, kWarningFillAlpha);
    // Neutral for both verbs: the tag's identity is its WORD and its ink, and two
    // differently tinted pills side by side made the log look like a legend.
    return tinted(colors.text, kPillFillAlpha);
}

QTextCharFormat CommandLogView::badgeFormat(LogKind aKind) const {
    QTextCharFormat format;
    format.setForeground(badgeInk(aKind));
    // No background and no border: the pill is PAINTED behind this run
    // (paintPills). A run background would be a square of fill inside the rounded
    // pill, and an outline is what made an earlier version read as a form.
    format.setBackground(Qt::NoBrush);
    format.setFont(font());
    return format;
}

QTextCharFormat CommandLogView::gapFormat() const {
    // A real space - copying the log must yield "Undo Add filter" - with a widened
    // ADVANCE, so the message keeps its distance from the pill's right edge. Qt's
    // absolute spacing ADDS to the glyph's own advance (measured: a 5.0 spacing
    // took a 2.73px space to 7.73px), so the space's own advance is subtracted.
    QTextCharFormat format = messageFormat();
    const QFontMetricsF metrics(font());
    const qreal natural = metrics.horizontalAdvance(QStringLiteral(" "));
    QFont wide = font();
    wide.setLetterSpacing(
        QFont::AbsoluteSpacing, qMax(0.0, kPillPadRight + kPillToMessage - natural)
    );
    format.setFont(wide);
    return format;
}

QTextCharFormat CommandLogView::messageFormat() const {
    QTextCharFormat format;
    format.setForeground(theme::Colors::current().text);
    // Stated explicitly rather than left unset: setCharFormat() MERGES a format
    // into the existing one, so a message run that did not name its font could
    // keep the separator's letter spacing.
    QFont base = font();
    base.setLetterSpacing(QFont::PercentageSpacing, 100.0);
    format.setFont(base);
    format.setBackground(Qt::NoBrush);
    return format;
}

QTextBlockFormat CommandLogView::lineFormat() const {
    QTextBlockFormat format;
    // The gutter the pill's left padding occupies: the pill starts one pad to the
    // left of the verb, and the pane's own document margin keeps it off the edge.
    // An Info line's prose starts on the same content column, so the pane has ONE
    // text column and the pills hang into the gutter.
    format.setLeftMargin(kPillPadLeft);
    format.setRightMargin(0.0);
    format.setTopMargin(kBlockTopMargin);
    return format;
}

void CommandLogView::writeEntry(QTextCursor& aCursor, const LogEntry& aEntry) const {
    // The block's own geometry is part of the entry's ink, so it is written with it
    // (and re-written by inkEntry on a theme switch, where the font may have
    // changed size).
    aCursor.setBlockFormat(lineFormat());

    const QString badge = badgeText(aEntry.kind);
    if (badge.isEmpty()) {
        aCursor.insertText(aEntry.text, messageFormat());
        return;
    }
    aCursor.insertText(badge, badgeFormat(aEntry.kind));
    if (!aEntry.text.isEmpty()) {
        // The separator is its own run so its widened advance cannot leak into
        // either neighbour, and it is a plain ' ' so the clipboard stays exact.
        aCursor.insertText(QStringLiteral(" "), gapFormat());
        aCursor.insertText(aEntry.text, messageFormat());
    }
}

void CommandLogView::inkEntry(
    QTextCursor& aCursor, const QTextBlock& aBlock, const LogEntry& aEntry
) const {
    const QString badge = badgeText(aEntry.kind);
    const int start = aBlock.position();
    const int end = start + int(aBlock.text().size());
    // The pairing is block i <-> entry i (both are capped at kMaxEntries, and
    // append() writes the verb first). Stated as a guard rather than assumed: a
    // block that does not start with its verb is not this entry's line, and
    // re-inking it would paint one entry's ink over another's words.
    if (!badge.isEmpty() && !aBlock.text().startsWith(badge)) return;
    aCursor.setBlockFormat(lineFormat());
    if (badge.isEmpty()) {
        aCursor.setPosition(start);
        aCursor.setPosition(end, QTextCursor::KeepAnchor);
        aCursor.setCharFormat(messageFormat());
        return;
    }
    const int badgeEnd = start + int(badge.size());
    aCursor.setPosition(start);
    aCursor.setPosition(badgeEnd, QTextCursor::KeepAnchor);
    aCursor.setCharFormat(badgeFormat(aEntry.kind));
    if (badgeEnd >= end) return;
    // The separator is exactly one character (see writeEntry).
    aCursor.setPosition(badgeEnd);
    aCursor.setPosition(badgeEnd + 1, QTextCursor::KeepAnchor);
    aCursor.setCharFormat(gapFormat());
    if (badgeEnd + 1 < end) {
        aCursor.setPosition(badgeEnd + 1);
        aCursor.setPosition(end, QTextCursor::KeepAnchor);
        aCursor.setCharFormat(messageFormat());
    }
}

void CommandLogView::paintEvent(QPaintEvent* aEvent) {
    // Scoped: QTextEdit's own paint creates a painter on the viewport, and a paint
    // device cannot carry two at once.
    {
        QPainter painter(viewport());
        paintPills(painter, aEvent->rect());
    }
    QTextEdit::paintEvent(aEvent);
}

void CommandLogView::paintPills(QPainter& aPainter, const QRect& aClip) {
    mPillRects.clear();
    mPillRects.resize(mEntries.size());

    aPainter.save();
    aPainter.setClipRect(aClip);
    aPainter.setRenderHint(QPainter::Antialiasing, true);

    QTextCursor probe(document());
    QTextBlock block = document()->firstBlock();
    for (qsizetype i = 0; block.isValid() && i < mEntries.size(); ++i, block = block.next()) {
        const QString badge = badgeText(mEntries[i].kind);
        if (badge.isEmpty()) continue;
        QTextLayout* layout = block.layout();
        if (!layout || layout->lineCount() == 0) continue;
        if (!block.text().startsWith(badge)) continue; // see inkEntry

        const QTextLine line = layout->lineAt(0);
        const QRectF pill = pillRectForLine(probe, block, line, int(badge.size()));
        if (pill.isEmpty()) continue;
        if (pill.bottom() < aClip.top() || pill.top() > aClip.bottom()) continue;

        // Fill only. No pen: a 1px edge is what turned this pane's tags into a row
        // of bordered boxes (and, in the accent's hue, into chrome that competed
        // with the document's own marks).
        aPainter.setPen(Qt::NoPen);
        aPainter.setBrush(pillFill(mEntries[i].kind));
        aPainter.drawRoundedRect(pill, kPillRadius, kPillRadius);
        mPillRects[i] = pill;
    }

    aPainter.restore();
}

QRectF CommandLogView::pillRectForLine(
    QTextCursor& aProbe, const QTextBlock& aBlock, const QTextLine& aLine, int aVerbLength
) const {
    if (aVerbLength <= 0 || aLine.textLength() < aVerbLength) return QRectF();

    // The LINE's viewport position comes from Qt: cursorRect answers in viewport
    // coordinates, with the document's margins and the scroll position already in
    // it. The x INSIDE the line comes from the layout, and taking its origin as
    // the difference between the two makes the result independent of how Qt
    // composes them - re-deriving contentOffset() plus the block's position would
    // be re-deriving Qt's own margin arithmetic.
    const int lineStart = aLine.textStart();
    aProbe.setPosition(aBlock.position() + lineStart);
    const QRect lineRect = cursorRect(aProbe);
    const qreal dx = lineRect.left() - aLine.cursorToX(lineStart);

    // Leading at `to` is the end of the run's last character: with Trailing it
    // would include the NEXT character - the widened separator space - and the pill
    // came out 7.7px too wide (measured).
    const qreal x1 = dx + aLine.cursorToX(lineStart);
    const qreal x2 = dx + aLine.cursorToX(lineStart + aVerbLength);
    const qreal height = QFontMetricsF(font()).height() - kPillVerticalShrink;

    return QRectF(
        x1 - kPillPadLeft,
        lineRect.top() + (aLine.height() - height) / 2.0,
        (x2 - x1) + kPillPadLeft + kPillPadRight,
        height
    );
}

} // namespace gui
