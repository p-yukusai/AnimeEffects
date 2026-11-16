#ifndef GUI_EXPORTFACTS_H
#define GUI_EXPORTFACTS_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "ctrl/ExportTarget.h"

namespace gui {

// ONE fact the form states about the export, as a STRUCTURED row rather than as a
// line of prose.
//
// The widget dialog stated these as a QStringList joined with newlines: one
// paragraph of same-typography text whose only structure was the order it came
// in. The QML form has to show hierarchy (a size reads differently from a
// warning), so the list carries the pieces: what the row is ABOUT (id), the
// words a person reads (label/value), the muted explanation behind them
// (detail), and whether it is ordinary, a caveat, or a refusal.
//
// `text()` is glue(code) == "Label value" for the idempotent "Label: value"
// rows, and the bare sentence for every row that is a sentence. That is what
// keeps the pre-existing sentence-level assertions - and the translator
// contexts the strings already have entries under - working unchanged.
struct ExportFact {
    enum class Severity {
        Normal,
        Warning,
        Error,
    };

    // Stable machine names, so a test or the scene can ask for "the size row"
    // without matching on translated words. One of:
    //   "size" "fps" "format" "transparency" "audio" "limits" "note" "error"
    QString id;
    QString label;
    QString value;
    // The muted explanation under the value. For the audio row it is WHY there is
    // no sound (the destination refuses it / the project has none / the user turned
    // it off); for the size row it repeats the frame rate as a number a chip can
    // state on its own row. Never part of text().
    QString detail;
    Severity severity = Severity::Normal;

    QString text() const {
        if (label.isEmpty()) return value;
        if (value.isEmpty()) return label;
        return label + QStringLiteral(" ") + value;
    }

    // The token QML compares against: "normal" / "warning" / "error".
    static QString severityName(Severity aSeverity);
    QString severityName() const { return severityName(severity); }
};

// What the export will produce and what the destination requires, as rows.
//
// This is the authoritative shape; exportFactsText() below is the same rows
// rendered as the lines the widget form used to print.
QList<ExportFact> exportFacts(const ctrl::ExportPlan& aPlan, const ctrl::ExportTarget& aTarget,
                              const ctrl::ExportRequest& aRequest);

// The rows as the plain "Label value" lines they were before this file grew a
// structure: one string per row, '\n'-joined by the caller. Kept so the
// sentence-level assertions and the existing catalogs still hold.
QStringList exportFactsText(const QList<ExportFact>& aFacts);

// Role ids/names for the QML list model that renders these rows. They live here
// so the model and the scene cannot drift about what a row is called.
QList<int> exportFactRoles();
QHash<int, QByteArray> exportFactRoleNames();

} // namespace gui

#endif // GUI_EXPORTFACTS_H
