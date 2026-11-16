// The Properties panel's command log (src/gui/CommandLogView.{h,cpp}): a quiet pill
// around the entry's verb, its message after it, and the cross-entry selection the
// note asks us to keep.
//
// The note asks for two things that pull in opposite directions: structured entries
// instead of the old "Undone : X" strings, and "multiline text select and copy works
// over multiple items". A delegate list would give the first and take the second
// (each item owns its text, so a drag that starts in one entry and ends in another
// selects nothing), which is why the log stays ONE text document. Tests 1-3 are
// therefore the load-bearing ones: the text is exact, a selection that CROSSES two
// entries copies as one plain run, and the tag is real ink (a painted FILL, not a
// character) that cannot pollute the clipboard.
//
// Test 4 measures the PILL on a render of the real view, at the rect the view
// itself reports it painted (badgePillRect): a soft rounded fill with NO border,
// padded on both sides of the verb and hanging into the gutter, in the neutral or
// the warning wash, and absent on an Info line. The outlined chip this pane had
// first fails that gate on its edges; the glyph version failed on the noise.
//
// Tests 5-7 keep the rest of the contract: no dangling separator when a command has
// no name, one cap for the document and the retained entries, and a theme switch
// that re-inks IN PLACE (a setPlainText() rebuild would drop the user's selection).
//
// Headless (QT_QPA_PLATFORM=offscreen), no GL, no project.
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/command_log
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QFontMetricsF>
#include <QImage>
#include <QPalette>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QTextStream>
#include <QVector>

#include <cmath>
#include <cstdio>

#include "gui/CommandLogView.h"
#include "gui/LogEntry.h"
#include "gui/theme/Colors.h"

static int failures = 0;

static void check(bool ok, const QString& what) {
    QTextStream(stdout) << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

// One formatted run of a block: setCharFormat() splits the line into these, and
// they are the only place the two inks are observable.
struct Run {
    QString text;
    QColor ink;
    bool letterSpaced; // the separator run widens its advance (see gapFormat)
};

static QVector<Run> runsOf(const QTextBlock& aBlock) {
    QVector<Run> runs;
    for (auto it = aBlock.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (!fragment.isValid()) continue;
        const QTextCharFormat format = fragment.charFormat();
        runs.push_back(
            {fragment.text(), format.foreground().color(),
             format.font().letterSpacingType() == QFont::AbsoluteSpacing &&
                 format.font().letterSpacing() > 0.0}
        );
    }
    return runs;
}

static QColor inkOf(const gui::CommandLogView& aView, int aBlock, int aRun) {
    const QVector<Run> runs = runsOf(aView.document()->findBlockByNumber(aBlock));
    return aRun < runs.size() ? runs[aRun].ink : QColor();
}

static QString quoted(const QString& aText) {
    const QString escaped = QString::fromUtf8(
        QString(aText).replace(QChar(QLatin1Char('\n')), QStringLiteral("\\n")).toUtf8());
    return QStringLiteral("\"") + escaped + QStringLiteral("\"");
}

// The pane's own floor is what the pills are filled over, so give the view one
// explicitly: the assertions are then about this pane rather than about the host's
// palette.
static void dressFloor(gui::CommandLogView& aLog) {
    const theme::Colors& colors = theme::Colors::current();
    QPalette pal = aLog.palette();
    pal.setColor(QPalette::Base, colors.recessed);
    pal.setColor(QPalette::Text, colors.text);
    aLog.setPalette(pal);
    aLog.viewport()->setPalette(pal);
}

static QImage renderViewport(gui::CommandLogView& aLog) {
    QImage image(aLog.viewport()->size(), QImage::Format_ARGB32);
    // A colour no theme token can produce: a pixel the view did not paint is then
    // obviously unpainted rather than coincidentally floor-coloured.
    image.fill(QColor(255, 0, 255));
    aLog.viewport()->render(&image);
    return image;
}

// What a pill's fill composites to: an ink at aAlpha over the floor. The alphas are
// the design's own - 7% for the neutral tag, 16% for the warning wash.
static QColor pillFill(const QColor& aInk, const QColor& aFloor, qreal aAlpha) {
    return QColor(
        qRound(aInk.red() * aAlpha + aFloor.red() * (1.0 - aAlpha)),
        qRound(aInk.green() * aAlpha + aFloor.green() * (1.0 - aAlpha)),
        qRound(aInk.blue() * aAlpha + aFloor.blue() * (1.0 - aAlpha))
    );
}

static bool near(const QColor& aA, const QColor& aB, int aTolerance) {
    return std::abs(aA.red() - aB.red()) <= aTolerance &&
           std::abs(aA.green() - aB.green()) <= aTolerance &&
           std::abs(aA.blue() - aB.blue()) <= aTolerance;
}

static int countNear(const QImage& aImage, const QRect& aRect, const QColor& aWant, int aTolerance) {
    int count = 0;
    const QRect box = aRect.intersected(aImage.rect());
    for (int y = box.top(); y <= box.bottom(); ++y) {
        for (int x = box.left(); x <= box.right(); ++x) {
            if (near(aImage.pixelColor(x, y), aWant, aTolerance)) ++count;
        }
    }
    return count;
}

// Anything inked (neither the floor nor the message ink) inside a box: how the
// glyph's presence is counted without knowing its exact antialiasing.
// The longest horizontal run of pixels matching aWant: a pill's BODY is one long
// run, while text antialiasing only ever produces scattered matches. This is how a
// row with no pill is told apart from a row whose words happen to pass through the
// fill's grey.
static int longestRunNear(
    const QImage& aImage, const QRect& aRect, const QColor& aWant, int aTolerance
) {
    int longest = 0;
    const QRect box = aRect.intersected(aImage.rect());
    for (int y = box.top(); y <= box.bottom(); ++y) {
        int run = 0;
        for (int x = box.left(); x <= box.right(); ++x) {
            if (near(aImage.pixelColor(x, y), aWant, aTolerance)) {
                longest = qMax(longest, ++run);
            } else {
                run = 0;
            }
        }
    }
    return longest;
}

static int countInked(const QImage& aImage, const QRect& aRect, const QColor& aFloor) {
    int count = 0;
    const QRect box = aRect.intersected(aImage.rect());
    for (int y = box.top(); y <= box.bottom(); ++y) {
        for (int x = box.left(); x <= box.right(); ++x) {
            if (!near(aImage.pixelColor(x, y), aFloor, 6)) ++count;
        }
    }
    return count;
}

// Where a block's character aIndex sits horizontally, in the layout's own
// coordinates: two entries whose message lands on the same column report the same
// number here.
static qreal layoutX(const QTextBlock& aBlock, int aIndex) {
    QTextLayout* layout = aBlock.layout();
    if (!layout || layout->lineCount() == 0) return -1.0;
    return layout->lineAt(0).cursorToX(aIndex);
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // --- 1/2/3: the entry's three runs, the one column, and the copy ACROSS entries
    {
        theme::Colors::activate(true, theme::kDefaultAccent);
        gui::CommandLogView log;
        log.resize(320, 160);
        dressFloor(log);
        log.append({gui::LogKind::Undo, QStringLiteral("Move keys")});
        log.append({gui::LogKind::Redo, QStringLiteral("Move keys")});

        const QString plain = log.toPlainText();
        check(plain == QStringLiteral("Undo Move keys\nRedo Move keys"),
              QStringLiteral("test 1: plain text is %1 (want \"Undo Move keys\\nRedo Move keys\")")
                  .arg(quoted(plain)));
        check(!plain.contains(QStringLiteral("Undone")) && !plain.contains(QStringLiteral(" : ")),
              QStringLiteral("test 1: no \"Undone : \" punctuation survives (%1)").arg(quoted(plain)));

        const theme::Colors& colors = theme::Colors::current();
        const QVector<Run> undoRuns = runsOf(log.document()->findBlockByNumber(0));
        const QVector<Run> redoRuns = runsOf(log.document()->findBlockByNumber(1));
        const QString undoShape = [&] {
            QStringList parts;
            for (const Run& run : undoRuns) parts << quoted(run.text);
            return parts.join(QStringLiteral(" + "));
        }();
        check(undoRuns.size() == 3 && undoRuns[0].text == QStringLiteral("Undo") &&
                  undoRuns[1].text == QStringLiteral(" ") &&
                  undoRuns[2].text == QStringLiteral("Move keys"),
              QStringLiteral("test 2: block 0 is verb + separator + message (%1)").arg(undoShape));
        if (undoRuns.size() == 3) {
            check(undoRuns[0].ink == colors.textMuted,
                  QStringLiteral("test 2: undo verb ink is textMuted (%1, got %2)")
                      .arg(colors.textMuted.name(), undoRuns[0].ink.name()));
            check(undoRuns[2].ink == colors.text,
                  QStringLiteral("test 2: message ink is text (%1, got %2)")
                      .arg(colors.text.name(), undoRuns[2].ink.name()));
            // The separator's TEXT is a single space (test 1 asserts the copy); its
            // ADVANCE is widened so the message lands on the verb column.
            check(undoRuns[1].letterSpaced,
                  QStringLiteral("test 2: the separator's advance is widened (letterSpaced=%1)")
                      .arg(undoRuns[1].letterSpaced ? QStringLiteral("yes")
                                                    : QStringLiteral("no")));
            check(!undoRuns[0].letterSpaced && !undoRuns[2].letterSpaced,
                  QStringLiteral("test 2: the widening stays out of the verb and the message "
                                 "(verb %1, message %2)")
                      .arg(undoRuns[0].letterSpaced ? QStringLiteral("spaced")
                                                    : QStringLiteral("plain"),
                           undoRuns[2].letterSpaced ? QStringLiteral("spaced")
                                                    : QStringLiteral("plain")));
        }
        if (redoRuns.size() == 3) {
            check(redoRuns[0].ink == colors.accentBright,
                  QStringLiteral("test 2: redo verb ink is accentBright (%1, got %2)")
                      .arg(colors.accentBright.name(), redoRuns[0].ink.name()));
        }

        // THE NOTE'S REQUIREMENT: a selection that spans entries copies as one
        // plain text run - no markup, no per-entry fragments, one newline per
        // entry boundary.
        QApplication::clipboard()->clear();
        log.selectAll();
        log.copy();
        const QString copied = QApplication::clipboard()->text();
        check(copied == plain,
              QStringLiteral("test 3: selectAll+copy across BOTH entries puts %1 on the clipboard")
                  .arg(quoted(copied)));
        check(!copied.contains(QLatin1Char('<')),
              QStringLiteral("test 3: the clipboard carries no markup (%1)").arg(quoted(copied)));

        // The same, for a PARTIAL selection that starts inside one entry's message
        // and ends inside the next one's: this is the drag a user actually makes,
        // and the tag must not be sitting between the runs in the character stream
        // (per-entry widgets, or an inline image for the glyph, break here by
        // dropping the verb or by copying a placeholder character).
        QTextCursor drag(log.document());
        const int firstBlock = log.document()->firstBlock().position();
        const int secondBlock = log.document()->findBlockByNumber(1).position();
        drag.setPosition(firstBlock + 8);  // "Undo Mov|e keys": just before "e keys"
        drag.setPosition(secondBlock + 5, QTextCursor::KeepAnchor); // ... to before "Move"
        log.setTextCursor(drag);
        QApplication::clipboard()->clear();
        log.copy();
        const QString partial = QApplication::clipboard()->text();
        check(partial == QStringLiteral("e keys\nRedo "),
              QStringLiteral("test 3: a selection from mid-entry to mid-entry copies %1 (want "
                             "\"e keys\\nRedo \")")
                  .arg(quoted(partial)));
    }

    // --- 4: the PILL, measured on a render of the real view ---
    {
        theme::Colors::activate(true, theme::kDefaultAccent);
        const theme::Colors& colors = theme::Colors::current();
        gui::CommandLogView log;
        log.resize(320, 170);
        dressFloor(log);
        log.append({gui::LogKind::Undo, QStringLiteral("Add filter")});
        log.append({gui::LogKind::Warning, QStringLiteral("Failed to save project")});
        log.append({gui::LogKind::Info, QStringLiteral("A plain editor message")});
        log.append({gui::LogKind::Redo, QStringLiteral("Update opacity")});

        const QImage image = renderViewport(log);
        const QFontMetricsF metrics(log.font());

        // ONE CONTENT COLUMN: every line's own text starts at the same x, whatever
        // kind it is, so the pills hang into a single gutter and an Info line's
        // prose lines up with the verbs.
        const QTextBlock undoBlock = log.document()->findBlockByNumber(0);
        const QTextBlock warnBlock = log.document()->findBlockByNumber(1);
        const QTextBlock infoBlock = log.document()->findBlockByNumber(2);
        const QTextBlock redoBlock = log.document()->findBlockByNumber(3);
        const qreal verbUndo = layoutX(undoBlock, 0);
        const qreal verbWarn = layoutX(warnBlock, 0);
        const qreal verbRedo = layoutX(redoBlock, 0);
        const qreal infoStart = layoutX(infoBlock, 0);
        check(std::abs(verbUndo - verbWarn) < 0.01 && std::abs(verbUndo - verbRedo) < 0.01 &&
                  std::abs(verbUndo - infoStart) < 0.01,
              QStringLiteral("test 4: one content column (undo %1, warning %2, redo %3, info %4)")
                  .arg(verbUndo).arg(verbWarn).arg(verbRedo).arg(infoStart));

        // THE PILL: reported by the view for what it actually painted.
        const QRectF undoPill = log.badgePillRect(0);
        const QRectF warnPill = log.badgePillRect(1);
        const QRectF infoPill = log.badgePillRect(2);
        check(undoPill.isValid() && warnPill.isValid(),
              QStringLiteral("test 4: both badges report a pill (undo %1x%2, warning %3x%4)")
                  .arg(undoPill.width()).arg(undoPill.height())
                  .arg(warnPill.width()).arg(warnPill.height()));
        check(!infoPill.isValid(),
              QStringLiteral("test 4: an Info entry has NO pill (rect %1x%2)")
                  .arg(infoPill.width()).arg(infoPill.height()));

        // PADDED around the word on BOTH sides, and left of it in the gutter: a
        // pill that hugged only its right side would read as a highlighted word.
        const qreal undoText = metrics.horizontalAdvance(QStringLiteral("Undo"));
        check(undoPill.width() > undoText + 8.0,
              QStringLiteral("test 4: the pill is padded around the verb (%1 vs text %2)")
                  .arg(undoPill.width()).arg(undoText));
        check(undoPill.left() < verbUndo && undoPill.right() > verbUndo + undoText,
              QStringLiteral("test 4: the pill hangs into the gutter on the left (%1 < %2) and "
                             "past the word on the right (%3 > %4)")
                  .arg(undoPill.left()).arg(verbUndo)
                  .arg(undoPill.right()).arg(verbUndo + undoText));
        check(undoPill.height() >= metrics.height() - 3.0 &&
                  undoPill.height() <= metrics.height() + 1.0,
              QStringLiteral("test 4: the pill's height tracks the line (%1 vs font %2)")
                  .arg(undoPill.height()).arg(metrics.height()));

        // ITS BODY IS A FILL: the neutral tag is the pane's text ink at 7% over the
        // floor - the whole reason the pane is not "text only" any more.
        const QColor floor = colors.recessed;
        const QColor wantFill = pillFill(colors.text, floor, 0.07);
        const QRect undoBox = undoPill.toAlignedRect();
        const int fillPixels = countNear(image, undoBox, wantFill, 3);
        const int area = undoBox.width() * undoBox.height();
        check(fillPixels > area / 2,
              QStringLiteral("test 4: the neutral pill is the soft fill (%1 of %2 px, want %3)")
                  .arg(fillPixels).arg(area).arg(wantFill.name()));

        // ROUNDED and UNBORDERED. Both are asserted from the same place: the box's
        // four corners are the floor (a radius rounds them away) and so are the
        // columns just outside each edge (a 1px border would ink them). An outlined
        // chip - this pane's first version - fails here.
        int cornerInk = 0;
        for (const QPoint& corner : {undoBox.topLeft(), undoBox.topRight(),
                                     undoBox.bottomLeft(), undoBox.bottomRight()}) {
            if (!near(image.pixelColor(corner), floor, 3)) ++cornerInk;
        }
        check(cornerInk == 0,
              QStringLiteral("test 4: the pill's corners are rounded, not cut (%1 of 4 inked)")
                  .arg(cornerInk));
        const QRect leftEdge(undoBox.left() - 2, undoBox.top() + 2, 2, undoBox.height() - 4);
        const QRect topEdge(undoBox.left() + undoBox.width() / 2 - 5, undoBox.top() - 2, 10, 2);
        const int edgeInk = countInked(image, leftEdge, floor) + countInked(image, topEdge, floor);
        check(edgeInk == 0,
              QStringLiteral("test 4: the pill has NO border (%1 px outside its edges)").arg(edgeInk));

        // A WARNING is the one loud row: its pill is the warning token's wash, not
        // the neutral one.
        const QRect warnBox = warnPill.toAlignedRect();
        const int warnPixels = countNear(image, warnBox, pillFill(colors.warning, floor, 0.16), 3);
        const int neutralThere = countNear(image, warnBox, wantFill, 3);
        check(warnPixels > 100 && warnPixels > neutralThere * 3,
              QStringLiteral("test 4: the warning pill is the warning wash (%1 px, against %2 "
                             "neutral)")
                  .arg(warnPixels).arg(neutralThere));

        // The pill STOPS before the message: the columns between its right edge and
        // the first message ink are floor, so the two never run together.
        int firstMessageInk = -1;
        for (int x = undoBox.right() + 1; x < undoBox.right() + 20 && firstMessageInk < 0; ++x) {
            for (int y = undoBox.top(); y <= undoBox.bottom(); ++y) {
                if (!near(image.pixelColor(x, y), floor, 6)) { firstMessageInk = x; break; }
            }
        }
        check(firstMessageInk > undoBox.right() + 1,
              QStringLiteral("test 4: the message starts after the pill (%1 > %2), with floor "
                             "between them")
                  .arg(firstMessageInk).arg(undoBox.right()));

        // An Info line has no pill at all. Measured as the LONGEST RUN of fill colour
        // on its row - a pill's body is one unbroken run, while the prose's own
        // antialiasing only produces scattered pixels that pass through the wash's
        // grey. The band comes from the row's own layout, not from a guess about the
        // rows below it.
        QTextCursor infoCursor(log.document());
        infoCursor.setPosition(infoBlock.position());
        const QRect infoRow = log.cursorRect(infoCursor);
        const QRect infoBand(0, infoRow.top(), 240, infoRow.height());
        const int pillRun = longestRunNear(image, undoBox, wantFill, 3);
        const int infoRun = longestRunNear(image, infoBand, wantFill, 3);
        check(infoRun < 6 && pillRun >= 20,
              QStringLiteral("test 4: no pill is painted on an Info line (longest fill run %1 px "
                             "at y %2, against %3 px on the Undo row)")
                  .arg(infoRun).arg(infoRow.top()).arg(pillRun));
    }

    // --- 5: an empty command name renders the verb alone ---
    {
        gui::CommandLogView log;
        log.append({gui::LogKind::Undo, QString()});
        const QString plain = log.toPlainText();
        check(plain == QStringLiteral("Undo"),
              QStringLiteral("test 5: empty name renders %1 (want \"Undo\")").arg(quoted(plain)));
        check(!plain.endsWith(QLatin1Char(' ')) && !plain.endsWith(QStringLiteral(" :")) &&
                  !plain.endsWith(QLatin1Char(':')),
              QStringLiteral("test 5: no dangling separator (%1)").arg(quoted(plain)));
        const QVector<Run> runs = runsOf(log.document()->findBlockByNumber(0));
        check(runs.size() == 1 && runs[0].text == QStringLiteral("Undo"),
              QStringLiteral("test 5: the verb is the whole line (%1)")
                  .arg(runs.size() == 1 ? quoted(runs[0].text) : QStringLiteral("<not one run>")));
    }

    // --- 6: one constant caps BOTH the blocks and the retained entries ---
    {
        gui::CommandLogView log;
        for (int i = 0; i < 40; ++i) {
            log.append({i % 2 ? gui::LogKind::Redo : gui::LogKind::Undo,
                        QStringLiteral("step %1").arg(i)});
        }
        check(log.document()->blockCount() == gui::CommandLogView::kMaxEntries,
              QStringLiteral("test 6: 40 appends leave %1 blocks (constant %2)")
                  .arg(log.document()->blockCount())
                  .arg(gui::CommandLogView::kMaxEntries));
        check(log.entryCount() == gui::CommandLogView::kMaxEntries,
              QStringLiteral("test 6: 40 appends leave %1 retained entries (constant %2)")
                  .arg(log.entryCount())
                  .arg(gui::CommandLogView::kMaxEntries));
        check(log.document()->blockCount() == log.entryCount(),
              QStringLiteral("test 6: blocks and entries stay in lockstep (%1 vs %2)")
                  .arg(log.document()->blockCount())
                  .arg(log.entryCount()));
        check(log.toPlainText().startsWith(QStringLiteral("Undo step 8")),
              QStringLiteral("test 6: the newest 32 survive, the first block is %1")
                  .arg(quoted(log.document()->firstBlock().text())));
    }

    // --- 7: a theme switch re-inks IN PLACE, keeping text and selection ---
    {
        gui::CommandLogView log;
        log.append({gui::LogKind::Undo, QStringLiteral("Move keys")});
        log.append({gui::LogKind::Redo, QStringLiteral("Move keys")});

        // Baseline the palette explicitly (dark, default accent) so the "before"
        // ink does not depend on any earlier scenario.
        theme::Colors::activate(true, theme::kDefaultAccent);
        log.refreshTheme();
        const QColor beforeInk = inkOf(log, 0, 0);
        const QString beforeText = log.toPlainText();
        check(beforeInk == theme::Colors::current().textMuted,
              QStringLiteral("test 7: after the first refresh the verb ink is the dark "
                             "textMuted (%1, got %2)")
                  .arg(theme::Colors::current().textMuted.name(), beforeInk.name()));

        log.selectAll();
        const QTextCursor beforeSelection = log.textCursor();
        const int beforeStart = beforeSelection.selectionStart();
        const int beforeEnd = beforeSelection.selectionEnd();
        const QString beforeSelectionText = beforeSelection.selectedText();

        // A different theme AND a different accent hue: accentBright is part of the
        // accent row, so the redo verb must move with the hue as well.
        theme::Colors::activate(false, theme::kAccentRed);
        log.refreshTheme();

        const QColor afterInk = inkOf(log, 0, 0);
        check(afterInk == theme::Colors::current().textMuted && afterInk != beforeInk,
              QStringLiteral("test 7: after the switch the verb ink is the NEW textMuted (%1, "
                             "was %2)")
                  .arg(theme::Colors::current().textMuted.name(), beforeInk.name()));
        check(inkOf(log, 1, 0) == theme::Colors::current().accentBright,
              QStringLiteral("test 7: the redo verb follows the new accent (%1, got %2)")
                  .arg(theme::Colors::current().accentBright.name(),
                       inkOf(log, 1, 0).name()));
        check(log.toPlainText() == beforeText,
              QStringLiteral("test 7: the plain text is byte-identical across the refresh (%1)")
                  .arg(quoted(log.toPlainText())));

        const QTextCursor afterSelection = log.textCursor();
        check(afterSelection.selectionStart() == beforeStart &&
                  afterSelection.selectionEnd() == beforeEnd,
              QStringLiteral("test 7: the live selection still spans %1..%2 (was %3..%4)")
                  .arg(afterSelection.selectionStart())
                  .arg(afterSelection.selectionEnd())
                  .arg(beforeStart)
                  .arg(beforeEnd));
        check(afterSelection.selectedText() == beforeSelectionText,
              QStringLiteral("test 7: the selected text is unchanged (%1)")
                  .arg(quoted(afterSelection.selectedText())));
    }

    // --- the acceptance line beyond the numbered gates: a warning is not info ---
    {
        theme::Colors::activate(true, theme::kDefaultAccent);
        gui::CommandLogView log;
        log.append({gui::LogKind::Info, QStringLiteral("Saved project")});
        log.append({gui::LogKind::Warning, QStringLiteral("Failed to save project")});

        const QVector<Run> infoRuns = runsOf(log.document()->findBlockByNumber(0));
        const QVector<Run> warnRuns = runsOf(log.document()->findBlockByNumber(1));
        const theme::Colors& colors = theme::Colors::current();
        const QString infoInk = infoRuns.isEmpty() ? QStringLiteral("<none>") : infoRuns[0].ink.name();
        check(infoRuns.size() == 1 && infoRuns[0].ink == colors.text,
              QStringLiteral("warn-vs-info: info is one plain run (%1) in text (%2)")
                  .arg(infoRuns.size() == 1 ? quoted(infoRuns[0].text)
                                            : QStringLiteral("<not one run>"),
                       infoInk));
        check(warnRuns.size() == 3 && warnRuns[0].text == QStringLiteral("Warning") &&
                  warnRuns[0].ink == colors.warning,
              QStringLiteral("warn-vs-info: the warning row carries a \"Warning\" verb in the "
                             "warning token (%1)")
                  .arg(warnRuns.isEmpty() ? QStringLiteral("<none>") : warnRuns[0].ink.name()));
        if (warnRuns.size() == 3) {
            check(warnRuns[2].ink == colors.text,
                  QStringLiteral("warn-vs-info: the warning's message keeps the primary text ink "
                                 "(%1, got %2)")
                      .arg(colors.text.name(), warnRuns[2].ink.name()));
            check(warnRuns[0].ink != infoRuns[0].ink,
                  QStringLiteral("warn-vs-info: warning ink %1 differs from info ink %2")
                      .arg(warnRuns[0].ink.name(), infoInk));
        }
    }

    QTextStream(stdout)
        << (failures == 0 ? QStringLiteral("=== command_log: OK ===\n")
                          : QStringLiteral("=== command_log: %1 FAILED ===\n").arg(failures));
    return failures == 0 ? 0 : 1;
}
