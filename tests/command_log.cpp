// The Properties panel's command log (src/gui/CommandLogView.{h,cpp}): badges
// instead of the old "Undone : X" strings, and the cross-entry selection the note
// asks us to keep.
//
// The note asks for two things that pull in opposite directions: a per-entry BADGE
// and "multiline text select and copy works over multiple items". A delegate list
// would give the badge and take the selection (each item owns its text, so a drag
// that starts in one entry and ends in another selects nothing), which is why the
// log stays ONE text document and the badge is the span of characters the verb
// occupies. Test 3 is therefore the load-bearing one: it copies across entries
// through the real clipboard and asserts the exact plain text, so a regression to
// per-entry widgets - or to markup - fails here instead of being noticed by hand.
//
// Test 1 is the other regression this file exists for. The old line was
// tr("Undone : ") + commandName(), and a plain command's name() is EMPTY, so the
// log showed a bare "Undone : ". The badge form has no separator to dangle: the
// verb and the name are separated by a space, and an empty name renders the verb
// alone (test 4). Test 6 pins the reason the colours are per-run formats and not
// HTML: a theme switch re-inks the document IN PLACE, so the text and a live
// selection survive it.
//
// Headless (QT_QPA_PLATFORM=offscreen), no GL, no project.
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/command_log
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextStream>
#include <QVector>

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
    QColor fill;
    bool hasFill;
};

static QVector<Run> runsOf(const QTextBlock& aBlock) {
    QVector<Run> runs;
    for (auto it = aBlock.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (!fragment.isValid()) continue;
        const QTextCharFormat format = fragment.charFormat();
        runs.push_back({fragment.text(), format.foreground().color(),
                        format.background().color(),
                        format.background().style() != Qt::NoBrush});
    }
    return runs;
}

static QColor inkOf(const gui::CommandLogView& aView, int aBlock, int aRun) {
    const QVector<Run> runs = runsOf(aView.document()->findBlockByNumber(aBlock));
    return aRun < runs.size() ? runs[aRun].ink : QColor();
}

static QString quoted(const QString& aText) {
    const QString escaped = QString(aText).replace(QChar(QLatin1Char('\n')), QStringLiteral("\\n"));
    return QStringLiteral("\"") + escaped + QStringLiteral("\"");
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // --- 1/2/3: the badge, its two inks, and the copy ACROSS entries ---
    {
        gui::CommandLogView log;
        log.append({gui::LogKind::Undo, QStringLiteral("Move keys")});
        log.append({gui::LogKind::Redo, QStringLiteral("Move keys")});

        const QString plain = log.toPlainText();
        check(plain == QStringLiteral("Undo Move keys\nRedo Move keys"),
              QStringLiteral("test 1: plain text is %1 (want \"Undo Move keys\\nRedo Move keys\")")
                  .arg(quoted(plain)));
        check(!plain.contains(QStringLiteral("Undone")) && !plain.contains(QStringLiteral(" : ")),
              QStringLiteral("test 1: no \"Undone : \" punctuation survives (%1)").arg(quoted(plain)));

        // The badge is a TAG: its own ink AND its own background, and the
        // background has to differ from the log floor (`recessed` in standard.ssa)
        // or the tag would be invisible on it.
        const theme::Colors& colors = theme::Colors::current();
        const QVector<Run> undoRuns = runsOf(log.document()->findBlockByNumber(0));
        const QVector<Run> redoRuns = runsOf(log.document()->findBlockByNumber(1));
        check(undoRuns.size() == 2 && undoRuns[0].text == QStringLiteral("Undo") &&
                  undoRuns[1].text == QStringLiteral(" Move keys"),
              QStringLiteral("test 2: block 0 is two runs, badge %1 + message %2")
                  .arg(undoRuns.isEmpty() ? QStringLiteral("<none>") : quoted(undoRuns[0].text),
                       undoRuns.size() < 2 ? QStringLiteral("<none>") : quoted(undoRuns[1].text)));
        if (undoRuns.size() == 2) {
            check(undoRuns[0].ink == colors.textMuted,
                  QStringLiteral("test 2: undo badge ink is textMuted (%1, got %2)")
                      .arg(colors.textMuted.name(), undoRuns[0].ink.name()));
            check(undoRuns[0].hasFill && undoRuns[0].fill != colors.recessed,
                  QStringLiteral("test 2: undo badge carries a background distinct from the log "
                                 "floor (%1 vs recessed %2, fill %3)")
                      .arg(undoRuns[0].hasFill ? QStringLiteral("set") : QStringLiteral("none"),
                           colors.recessed.name(), undoRuns[0].fill.name()));
            check(undoRuns[1].ink == colors.text && !undoRuns[1].hasFill,
                  QStringLiteral("test 2: message ink is text (%1, got %2), no badge background (%3)")
                      .arg(colors.text.name(), undoRuns[1].ink.name(),
                           undoRuns[1].hasFill ? QStringLiteral("set") : QStringLiteral("none")));
        }
        if (redoRuns.size() == 2) {
            check(redoRuns[0].ink == colors.accentBright,
                  QStringLiteral("test 2: redo badge ink is accentBright (%1, got %2)")
                      .arg(colors.accentBright.name(), redoRuns[0].ink.name()));
            check(redoRuns[0].hasFill && redoRuns[0].fill != colors.recessed,
                  QStringLiteral("test 2: redo badge carries a background distinct from the log "
                                 "floor (fill %1, recessed %2)")
                      .arg(redoRuns[0].fill.name(), colors.recessed.name()));
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
    }

    // --- 4: an empty command name renders the verb alone ---
    {
        gui::CommandLogView log;
        log.append({gui::LogKind::Undo, QString()});
        const QString plain = log.toPlainText();
        check(plain == QStringLiteral("Undo"),
              QStringLiteral("test 4: empty name renders %1 (want \"Undo\")").arg(quoted(plain)));
        check(!plain.endsWith(QLatin1Char(' ')) && !plain.endsWith(QStringLiteral(" :")) &&
                  !plain.endsWith(QLatin1Char(':')),
              QStringLiteral("test 4: no dangling separator (%1)").arg(quoted(plain)));
        const QVector<Run> runs = runsOf(log.document()->findBlockByNumber(0));
        check(runs.size() == 1 && runs[0].text == QStringLiteral("Undo") && runs[0].hasFill,
              QStringLiteral("test 4: the badge alone is one tinted run, %1")
                  .arg(runs.size() == 1 ? quoted(runs[0].text) : QStringLiteral("<not one run>")));
    }

    // --- 5: one constant caps BOTH the blocks and the retained entries ---
    {
        gui::CommandLogView log;
        for (int i = 0; i < 40; ++i) {
            log.append({i % 2 ? gui::LogKind::Redo : gui::LogKind::Undo,
                        QStringLiteral("step %1").arg(i)});
        }
        check(log.document()->blockCount() == gui::CommandLogView::kMaxEntries,
              QStringLiteral("test 5: 40 appends leave %1 blocks (constant %2)")
                  .arg(log.document()->blockCount())
                  .arg(gui::CommandLogView::kMaxEntries));
        check(log.entryCount() == gui::CommandLogView::kMaxEntries,
              QStringLiteral("test 5: 40 appends leave %1 retained entries (constant %2)")
                  .arg(log.entryCount())
                  .arg(gui::CommandLogView::kMaxEntries));
        check(log.document()->blockCount() == log.entryCount(),
              QStringLiteral("test 5: blocks and entries stay in lockstep (%1 vs %2)")
                  .arg(log.document()->blockCount())
                  .arg(log.entryCount()));
        check(log.toPlainText().startsWith(QStringLiteral("Undo step 8")),
              QStringLiteral("test 5: the newest 32 survive, the first block is %1")
                  .arg(quoted(log.document()->firstBlock().text())));
    }

    // --- 6: a theme switch re-inks IN PLACE, keeping text and selection ---
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
              QStringLiteral("test 6: after the first refresh the badge ink is the dark "
                             "textMuted (%1, got %2)")
                  .arg(theme::Colors::current().textMuted.name(), beforeInk.name()));

        log.selectAll();
        const QTextCursor beforeSelection = log.textCursor();
        const int beforeStart = beforeSelection.selectionStart();
        const int beforeEnd = beforeSelection.selectionEnd();
        const QString beforeSelectionText = beforeSelection.selectedText();

        // A different theme AND a different accent hue: accentBright is part of the
        // accent row, so the redo badge must move with the hue as well.
        theme::Colors::activate(false, theme::kAccentRed);
        log.refreshTheme();

        const QColor afterInk = inkOf(log, 0, 0);
        check(afterInk == theme::Colors::current().textMuted && afterInk != beforeInk,
              QStringLiteral("test 6: after the switch the badge ink is the NEW textMuted (%1, "
                             "was %2)")
                  .arg(theme::Colors::current().textMuted.name(), beforeInk.name()));
        check(inkOf(log, 1, 0) == theme::Colors::current().accentBright,
              QStringLiteral("test 6: the redo badge follows the new accent (%1, got %2)")
                  .arg(theme::Colors::current().accentBright.name(),
                       inkOf(log, 1, 0).name()));
        check(log.toPlainText() == beforeText,
              QStringLiteral("test 6: the plain text is byte-identical across the refresh (%1)")
                  .arg(quoted(log.toPlainText())));

        const QTextCursor afterSelection = log.textCursor();
        check(afterSelection.selectionStart() == beforeStart &&
                  afterSelection.selectionEnd() == beforeEnd,
              QStringLiteral("test 6: the live selection still spans %1..%2 (was %3..%4)")
                  .arg(afterSelection.selectionStart())
                  .arg(afterSelection.selectionEnd())
                  .arg(beforeStart)
                  .arg(beforeEnd));
        check(afterSelection.selectedText() == beforeSelectionText,
              QStringLiteral("test 6: the selected text is unchanged (%1)")
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
        check(infoRuns.size() == 1 && infoRuns[0].ink == colors.text && !infoRuns[0].hasFill,
              QStringLiteral("warn-vs-info: info is one plain run (%1) in text (%2)")
                  .arg(infoRuns.size() == 1 ? quoted(infoRuns[0].text)
                                            : QStringLiteral("<not one run>"),
                       infoInk));
        check(warnRuns.size() == 2 && warnRuns[0].text == QStringLiteral("Warning") &&
                  warnRuns[0].ink == colors.warning && warnRuns[0].hasFill,
              QStringLiteral("warn-vs-info: the warning row carries a tinted \"Warning\" badge in "
                             "the warning token (%1, fill %2)")
                  .arg(warnRuns.isEmpty() ? QStringLiteral("<none>") : warnRuns[0].ink.name(),
                       colors.warning.name()));
        if (warnRuns.size() == 2) {
            check(warnRuns[1].ink == colors.text,
                  QStringLiteral("warn-vs-info: the warning's message keeps the primary text ink "
                                 "(%1, got %2)")
                      .arg(colors.text.name(), warnRuns[1].ink.name()));
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
