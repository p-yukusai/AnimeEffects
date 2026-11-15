#ifndef GUI_QML_DESTINATIONPICKERMODEL_H
#define GUI_QML_DESTINATIONPICKERMODEL_H

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QString>

#include "ctrl/ExportTarget.h"

namespace gui {

// The destination picker's list, as a model the Qt Quick scene can bind to AND a
// headless test can drive with no QML engine and no display.
//
// It owns the whole behaviour the widget version kept in its QTreeWidget:
//  - the platform grouping (a platform with several destinations is a node; a
//    platform with one, or a group whose rows are not variants of one platform,
//    is listed directly);
//  - closed-by-default expansion, with section rows that are NOT answers;
//  - the search haystack filter, which REVEALS what it matched (opens the section)
//    and selects a destination, never the section above it;
//  - the remembered destination's preselection, on screen and not merely current;
//  - selection (a destination only) and activation (a section toggles; a
//    destination answers; anything else is the manual escape hatch).
//
// ONE FLAT ROW LIST, deliberately: every node in the tree is a row, in tree
// order, and a row that is not on screen is still a row - it is marked
// onScreen=false and the scene collapses it to zero height. That keeps row
// indices stable across expansion and filtering (a ListView delegate's `index`
// never changes meaning), and it lets the test ask a tree-shaped question
// ("every platform has destinations inside it") of a QAbstractListModel.
//
// A SECTION ROW HAS NO Qt::ItemIsSelectable AND NO DESTINATION ID. That is the
// fix for the reported bug: in the widget version a platform was an ordinary
// selectable QTreeWidgetItem, the keyboard could land on it, and "Use this
// destination" would have had to guess which destination inside it was meant.
// Here flags() withholds ItemIsSelectable, select()/step() refuse sections
// outright, and activate() on a section only ever toggles it.
class DestinationPickerModel : public QAbstractListModel {
    Q_OBJECT
    // The current ROW is always a destination (or -1); a section is never current.
    Q_PROPERTY(int currentRow READ currentRow NOTIFY currentRowChanged)
    Q_PROPERTY(int visibleRowCount READ visibleRowCount NOTIFY visibleRowCountChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    // The dialog's own words. They are read from the catalog's gui::DestinationPicker
    // context (the one the widget class filled, and the one translators have
    // already translated); see the definitions for why this class asks for that
    // context explicitly rather than through its own metaobject.
    Q_PROPERTY(QString windowTitleText READ windowTitleText CONSTANT)
    Q_PROPERTY(QString searchPlaceholder READ searchPlaceholder CONSTANT)
    Q_PROPERTY(QString searchAccessibleName READ searchAccessibleName CONSTANT)
    Q_PROPERTY(QString okText READ okText CONSTANT)
    Q_PROPERTY(QString cancelText READ cancelText CONSTANT)
    Q_PROPERTY(QString manualText READ manualText CONSTANT)
    Q_PROPERTY(QString manualTooltip READ manualTooltip CONSTANT)

public:
    // Row roles the scene binds to (lower-cased names, see roleNames()).
    enum Role {
        LabelRole = Qt::UserRole + 1,
        IconNameRole,
        IsSectionRole,
        ExpandedRole,
        OnScreenRole,
        VisibleRole, // matches the query, regardless of whether its section is open
        IdRole,
        HaystackRole,
        TooltipRole,
        SelectedRole,
        ChildCountRole,
        // C18: how far in the row sits, as ONE statement the scene can bind
        // every horizontal lane to. 0 = listed directly (a section, or a
        // destination whose platform has no section row above it), 1 = nested
        // under a section. It is NOT "is a section": a childless top-level
        // destination is level 0 and must not be indented as though it had a
        // parent, which is the whole of the contract line this role exists for.
        LevelRole,
    };

    // aCurrentId is the remembered destination (empty = none). Its platform is
    // opened so the preselection is ON SCREEN, not merely current - a child of a
    // closed section reports "not hidden" while being invisible, which is the trap
    // this model exists to not fall into.
    DestinationPickerModel(const QList<ctrl::ExportTarget>& aTargets,
                           const QList<ctrl::ExportGroup>& aGroups,
                           const QString& aCurrentId, QObject* aParent = nullptr);

    int rowCount(const QModelIndex& aParent = QModelIndex()) const override;
    QVariant data(const QModelIndex& aIndex, int aRole) const override;
    Qt::ItemFlags flags(const QModelIndex& aIndex) const override;
    QHash<int, QByteArray> roleNames() const override;

    // The remembered/selected destination id, or empty for "my platform isn't
    // listed". Set by activate()/acceptManually(); read after accepted().
    QString chosenId() const { return mChosenId; }

    int currentRow() const { return mCurrentRow; }
    // Destinations currently on screen: a section is not a destination, an error
    // statement is not one, and a child of a closed section is not on screen.
    int visibleRowCount() const;
    // Every destination row, on screen or not (used by the dialog's broken-table
    // check and by tests).
    int destinationCount() const;

    QString query() const { return mQuery; }

    // ---- introspection (QML bindings and the headless test) ----
    Q_INVOKABLE bool isSection(int aRow) const;
    Q_INVOKABLE bool isExpanded(int aRow) const;
    // On screen: matches the query AND (for a child) its section is open. A section
    // row is on screen whenever it matches.
    Q_INVOKABLE bool isOnScreen(int aRow) const;
    // Matches the query, whether or not its section is open (the widget's
    // isHidden(), which is deliberately a different question from isOnScreen()).
    Q_INVOKABLE bool isVisible(int aRow) const;
    Q_INVOKABLE QString idOf(int aRow) const;
    Q_INVOKABLE QString labelOf(int aRow) const;
    Q_INVOKABLE QString haystackOf(int aRow) const;
    Q_INVOKABLE int childCount(int aRow) const;
    // The section a destination is nested under, or -1 when it is listed directly.
    Q_INVOKABLE int sectionOf(int aRow) const;
    // The translated tooltip of a section ("%1 destinations"); empty elsewhere.
    Q_INVOKABLE QString sectionTooltip(int aRow) const;

    // ---- persistence (C17, decided 2026-09-30: expansion state is remembered) ----
    // The ids of every expanded section, in row order. What persists is a set of
    // STABLE ids - never row numbers: rows are rebuilt and reordered on every
    // construction and a saved index would open the wrong platform.
    Q_INVOKABLE QStringList expandedIds() const;
    // Restore a persisted expansion state. Absent/empty input is the first run and
    // leaves every group collapsed. The preselection invariant survives the
    // restore: the current row's platform re-opens if the given state would hide
    // the selection ("the preselection has to be on screen, not merely current").
    Q_INVOKABLE void applyExpandedIds(const QStringList& aIds);

    // ---- the dialog's own words (translated; see the Q_PROPERTY block) ----
    QString windowTitleText() const;
    QString searchPlaceholder() const;
    QString searchAccessibleName() const;
    QString okText() const;
    QString cancelText() const;
    QString manualText() const;
    QString manualTooltip() const;

    // Append the "the destination table could not be read" statement as a row that
    // is not selectable and not an answer (the widget did the same).
    void setError(const QString& aError);

public slots:
    // The search field. Matches label, section title, id, family and its title,
    // container and artifact; reveals (opens) what it matched; keeps the current
    // destination if it is still visible, else selects the first match. Never
    // closes what it or the user opened.
    void setQuery(const QString& aQuery);
    // Open or close a platform node. The one thing a platform can do, whatever
    // route asked: Enter, a click, or the dialog's own default button.
    Q_INVOKABLE void toggleSection(int aRow);
    // Select a destination row (a section is refused: sections are not selectable).
    Q_INVOKABLE void select(int aRow);
    // Enter on a row: a section toggles (never a guess about a destination inside
    // it), a destination is the answer, anything else is the manual escape hatch.
    Q_INVOKABLE void activate(int aRow);
    // Enter on the current row (the OK button and the list's own Return key).
    Q_INVOKABLE void activateCurrent();
    // Move the selection by aDelta among the destinations ON SCREEN, skipping
    // sections and anything hidden. Used by Up/Down and PageUp/PageDown, which the
    // search field forwards rather than keeping.
    Q_INVOKABLE void step(int aDelta);
    // Left/Right: open or close the section that holds the current destination.
    Q_INVOKABLE void expandCurrentSection();
    Q_INVOKABLE void collapseCurrentSection();
    // "My platform isn't listed": clear the id and accept.
    Q_INVOKABLE void acceptManually();
    // Cancel.
    Q_INVOKABLE void reject();
    // Preselect a remembered destination id (and open its platform). Called by the
    // constructor; public so a caller can re-remember without rebuilding the rows.
    void selectRemembered(const QString& aId);

signals:
    void accepted();
    void rejected();
    void currentRowChanged();
    void visibleRowCountChanged();
    void queryChanged();
    // Any expansion change, whatever route made it (a toggle, a search that opens
    // what it found, the preselection opening its platform). The dialog persists
    // on this; it is also what a test can wait on.
    void expansionChanged();

private:
    struct Row {
        QString label;
        QString icon;
        QString id;
        QString haystack; // what the search field matches (destinations only)
        QString tooltip;  // destinations only; sections build theirs from childCount
        bool section = false;
        bool expanded = false;
        bool visible = true; // matches the query, before expansion is considered
        bool error = false;  // the "table is broken" statement
        int parent = -1;     // the section row holding this destination, or -1
        int childCount = 0;  // sections only
    };

    void build(const QList<ctrl::ExportTarget>& aTargets,
               const QList<ctrl::ExportGroup>& aGroups);
    // Recompute visible/expanded from mQuery and move the selection if the current
    // row is no longer a possible answer.
    void applyQuery();
    void setCurrentRow(int aRow);
    bool validRow(int aRow) const;

    QList<Row> mRows;
    QString mQuery;
    int mCurrentRow = -1;
    QString mChosenId;
};

} // namespace gui

#endif // GUI_QML_DESTINATIONPICKERMODEL_H
