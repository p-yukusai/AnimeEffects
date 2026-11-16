#ifndef GUI_LOGENTRY_H
#define GUI_LOGENTRY_H

#include <QString>

namespace gui {

// One line of the Properties panel's command log, in the terms the log needs to
// render it - not in the terms of the old HTML string that carried both the ink
// and the message.
//
// The log used to be fed raw markup: MainWindow built "Undone : " + command name
// and ViaPoint wrapped that string in <font color=...>. Three problems came with
// that shape, and all three are why the entry is structured instead:
//   * the command's name was interpolated into markup UNESCAPED, so a command
//     whose name contains '<' or '&' corrupted the line;
//   * the colour was captured when the line was pushed, so a theme switch left
//     every older line in the previous theme's ink;
//   * a plain command's name() is empty, so the line rendered as a bare
//     "Undone : " with nothing after the separator.
// A kind plus a message lets the view decide the ink (it can re-read the theme
// whenever it changes) and decide the punctuation (no separator is written when
// there is no name).
enum class LogKind {
    Info,    // a plain editor message; renders as prose, with no badge
    Warning, // a message the user has to act on; carries a "Warning" badge
    Undo,    // carries an "Undo" badge and the undone command's name
    Redo     // carries a "Redo" badge and the redone command's name
};

// `text` is the message for Info/Warning and the command's NAME for Undo/Redo.
// It may be empty: a command that returns no name (the plain ones do) must still
// log the undo, so the view renders the badge alone rather than a dangling
// separator. `text` is never parsed as markup, which is what makes the escaping
// question moot.
struct LogEntry {
    LogKind kind = LogKind::Info;
    QString text;
};

} // namespace gui

#endif // GUI_LOGENTRY_H
