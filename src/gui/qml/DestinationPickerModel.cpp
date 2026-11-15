#include "gui/qml/DestinationPickerModel.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>

namespace gui {

namespace {

// What search matches, per destination: everything the row declares about itself,
// in the words a user might type for it. The label (what they read), the SECTION
// TITLE (the platform: "telegram", "insta"), the id ("telegram.sticker", the name
// this row has in the data file), the family key and its title ("sticker",
// "Stickers and emoji" - both shown in the tooltip, so a match on them is a hover
// away from being explained), the container, and the artifact.
//
// A platform match keeps the whole platform (its title is in every one of its rows'
// haystacks), which is legible in a tree: the results arrive under the platform
// that matched, so the user can see why they are there.
QString haystackFor(const ctrl::ExportTarget& aTarget, const QString& aGroupTitle,
                    const QString& aFamilyTitle) {
    QString h = aTarget.label + QLatin1Char(' ') + aTarget.id + QLatin1Char(' ') + aGroupTitle
                + QLatin1Char(' ') + aTarget.family + QLatin1Char(' ') + aFamilyTitle
                + QLatin1Char(' ') + aTarget.container;
    switch (aTarget.artifact) {
    case ctrl::ArtifactKind::Still:
        h += QStringLiteral(" still image illustration png jpg");
        break;
    case ctrl::ArtifactKind::Sequence:
        h += QStringLiteral(" sequence frames numbered stills png jpg");
        break;
    case ctrl::ArtifactKind::Video:
        h += QStringLiteral(" video clip animation");
        break;
    }
    if (aTarget.caps.maxBytes > 0) h += QStringLiteral(" capped size limit");
    return h.toLower();
}

// The human name of a destination's family, for the tooltip and for search. The
// family is what KIND of handover the row is (a sticker, a chat attachment, an
// upload); it is not the layout axis, but it is still a word the user knows, so it
// stays searchable where it is also visible.
QString familyTitle(const QString& aKey) {
    struct Family { const char* key; const char* title; };
    static const Family families[] = {
        { "chat", QT_TR_NOOP("Send in a chat") },
        { "post", QT_TR_NOOP("Post to a platform") },
        { "sticker", QT_TR_NOOP("Stickers and emoji") },
        { "overlay", QT_TR_NOOP("Overlays and transparency") },
        { "file", QT_TR_NOOP("A file") },
        { "archive", QT_TR_NOOP("Archive") },
    };
    for (const Family& f : families) {
        if (aKey == QLatin1String(f.key)) return QObject::tr(f.title);
    }
    return QString();
}

// The section a row belongs to. A row with no group (or one the table does not
// declare) still has to be reachable, so it lands in `general`.
QString sectionKeyOf(const ctrl::ExportTarget& aTarget) {
    return aTarget.group.isEmpty() ? QStringLiteral("general") : aTarget.group;
}

// The picker's own words are looked up in the catalog context the dialog class
// has always filled: gui::DestinationPicker. The strings were translated there in
// the shipped catalogs, and moving them to the QML module's own context
// (AnimeEffects.Dialogs) would leave every existing translation sitting under a
// context nothing reads - the same failure the widget dialog's header documents.
//
// WHY EVERY CALL IS A FULL QCoreApplication::translate WITH THE CONTEXT SPELLED
// OUT, and not a file-local helper that forwards to one: lupdate extracts the
// calls it can SEE and does not expand a helper, so a helper makes every string
// invisible to the translator. This file was written that way first, and the
// effect was silent: the source strings were still there, but lupdate marked all
// of the picker's entries VANISHED on the next run, lrelease dropped them from
// the .qm, and the picker shipped English in five languages while the catalogs
// looked complete. gui/ExportFacts.cpp documents the same trap from its history.

} // namespace

DestinationPickerModel::DestinationPickerModel(const QList<ctrl::ExportTarget>& aTargets,
                                               const QList<ctrl::ExportGroup>& aGroups,
                                               const QString& aCurrentId, QObject* aParent)
    : QAbstractListModel(aParent) {
    build(aTargets, aGroups);
    selectRemembered(aCurrentId);
    // The same closing move the widget constructor made: validate the current row
    // against an empty query and, with nothing remembered, select the first
    // destination. Doing it here (rather than only on the first keystroke) is why
    // the picker opens with an answer already highlighted.
    setQuery(QString());
}

int DestinationPickerModel::rowCount(const QModelIndex& aParent) const {
    return aParent.isValid() ? 0 : mRows.size();
}

bool DestinationPickerModel::validRow(int aRow) const {
    return aRow >= 0 && aRow < mRows.size();
}

void DestinationPickerModel::build(const QList<ctrl::ExportTarget>& aTargets,
                                   const QList<ctrl::ExportGroup>& aGroups) {
    // THE FIRST LEVEL IS THE PLATFORM, and it is CLOSED. A person knows where they
    // are posting before they know what the platform will take from them; a list
    // that shows all fifty-five destinations at once is the flat list with bold
    // lines in it, which is what this replaced.
    //
    // Nested only when there is a choice to make: a platform with several
    // destinations is a node, a platform with one IS that destination, and a group
    // whose rows are not variants of one platform (the table marks it
    // `variants="false"`) is listed directly. That last case is also what keeps the
    // safe default one Enter away on a fresh install.
    QList<ctrl::ExportGroup> groups = aGroups;
    {
        // A row whose group the table does not declare still has to be reachable, so
        // it gets a section of its own, named by its key, at the end (`general` if it
        // names no group at all).
        QSet<QString> declared;
        for (const ctrl::ExportGroup& g : groups) declared.insert(g.id);
        for (const ctrl::ExportTarget& t : aTargets) {
            const QString key = sectionKeyOf(t);
            if (declared.contains(key)) continue;
            declared.insert(key);
            ctrl::ExportGroup extra;
            extra.id = key;
            extra.title = key;
            groups.push_back(extra);
        }
    }

    for (const ctrl::ExportGroup& g : groups) {
        QList<ctrl::ExportTarget> rows;
        for (const ctrl::ExportTarget& t : aTargets) {
            if (sectionKeyOf(t) == g.id) rows.push_back(t);
        }
        // A platform over nothing is a line the user can open and find no
        // destination in, so an empty section is not drawn at all.
        if (rows.isEmpty()) continue;

        // Within a section, by the label. Every row's label begins with the platform
        // the section names, so what is being ordered here is the variants
        // ("Instagram Reels" before "Instagram (photo)").
        std::stable_sort(rows.begin(), rows.end(),
                         [](const ctrl::ExportTarget& a, const ctrl::ExportTarget& b) {
                             return QString::localeAwareCompare(a.label, b.label) < 0;
                         });

        auto appendDestination = [&](const ctrl::ExportTarget& t, int aParent) {
            Row r;
            r.label = t.label;
            r.icon = t.icon;
            r.id = t.id;
            const QString title = familyTitle(t.family);
            r.haystack = haystackFor(t, g.title, title);
            // What the row is, on hover: the kind of handover, and the id it has in
            // the data file (which is what a bug report should quote).
            r.tooltip = title.isEmpty() ? t.id : title + QStringLiteral("\n") + t.id;
            r.parent = aParent;
            mRows.push_back(r);
        };

        if (rows.size() == 1 || !g.variants) {
            for (const ctrl::ExportTarget& t : rows) appendDestination(t, -1);
            continue;
        }

        Row section;
        section.label = g.title;
        section.section = true;
        // The group's stable key, same as every destination row's. Persistence
        // keys the remembered expansion state off THIS (C17): a section identity
        // of "" would make "remember which platforms were open" remember nothing,
        // and a row number would open the wrong platform in a reordered tree.
        section.id = g.id;
        // A platform's badge is its destinations' badge: every row in a section
        // carries the same brand icon, so the closed tree still says WHICH platform
        // each line is, at a glance. Taken from the first row rather than added as
        // data, because a platform has no icon of its own anywhere else.
        section.icon = rows.first().icon;
        const int sectionRow = mRows.size();
        mRows.push_back(section);
        for (const ctrl::ExportTarget& t : rows) appendDestination(t, sectionRow);
        // childCount is how the scene draws the caret and how the test asks whether
        // a section has anything to open.
        mRows[sectionRow].childCount = rows.size();
    }
}

int DestinationPickerModel::destinationCount() const {
    int n = 0;
    for (const Row& r : mRows) {
        if (!r.section && !r.error) ++n;
    }
    return n;
}

int DestinationPickerModel::visibleRowCount() const {
    int n = 0;
    for (int i = 0; i < mRows.size(); ++i) {
        // A closed platform shows nothing of its own, so its destinations are not
        // on screen however they are flagged: "visible" answers "matches the query"
        // for a child of a closed platform, which is the state that made the export
        // form's own container tell lies about itself once.
        if (!mRows[i].section && !mRows[i].error && isOnScreen(i)) ++n;
    }
    return n;
}

bool DestinationPickerModel::isSection(int aRow) const {
    return validRow(aRow) && mRows[aRow].section;
}

bool DestinationPickerModel::isExpanded(int aRow) const {
    return validRow(aRow) && mRows[aRow].section && mRows[aRow].expanded;
}

bool DestinationPickerModel::isVisible(int aRow) const {
    return validRow(aRow) && mRows[aRow].visible;
}

bool DestinationPickerModel::isOnScreen(int aRow) const {
    if (!validRow(aRow)) return false;
    const Row& r = mRows[aRow];
    if (!r.visible) return false;
    // A section's own row is drawn whether or not it is open, so its expansion
    // says nothing about the node itself.
    if (r.section || r.error) return true;
    return r.parent < 0 || mRows[r.parent].expanded;
}

QString DestinationPickerModel::idOf(int aRow) const {
    return validRow(aRow) ? mRows[aRow].id : QString();
}

QString DestinationPickerModel::labelOf(int aRow) const {
    return validRow(aRow) ? mRows[aRow].label : QString();
}

QString DestinationPickerModel::haystackOf(int aRow) const {
    return validRow(aRow) ? mRows[aRow].haystack : QString();
}

int DestinationPickerModel::childCount(int aRow) const {
    return validRow(aRow) ? mRows[aRow].childCount : 0;
}

int DestinationPickerModel::sectionOf(int aRow) const {
    return validRow(aRow) ? mRows[aRow].parent : -1;
}

QString DestinationPickerModel::sectionTooltip(int aRow) const {
    if (!isSection(aRow)) return QString();
    return QCoreApplication::translate("gui::DestinationPicker", "%1 destinations").arg(mRows[aRow].childCount);
}

QVariant DestinationPickerModel::data(const QModelIndex& aIndex, int aRole) const {
    if (!aIndex.isValid() || !validRow(aIndex.row())) return QVariant();
    const int row = aIndex.row();
    const Row& r = mRows[row];
    switch (aRole) {
    case LabelRole:     return r.label;
    case IconNameRole:  return r.icon;
    case IsSectionRole: return r.section;
    case ExpandedRole:  return r.section && r.expanded;
    case OnScreenRole:  return isOnScreen(row);
    case VisibleRole:   return r.visible;
    case IdRole:        return r.id;
    case HaystackRole:  return r.haystack;
    case TooltipRole:   return r.section ? sectionTooltip(row) : r.tooltip;
    case SelectedRole:  return row == mCurrentRow;
    case ChildCountRole: return r.childCount;
    // C18. Derived from a BUILD-TIME field: `parent` is the section row this
    // destination was appended under, or -1 when `appendDestination` was called
    // with no section (a group whose rows are not variants of one platform, or
    // has exactly one row). No QML walks a parent chain to find this.
    case LevelRole:     return r.parent < 0 ? 0 : 1;
    default:            return QVariant();
    }
}

Qt::ItemFlags DestinationPickerModel::flags(const QModelIndex& aIndex) const {
    if (!aIndex.isValid() || !validRow(aIndex.row())) return Qt::NoItemFlags;
    const Row& r = mRows[aIndex.row()];
    // THE BUG THIS FIXES: a platform (and the "table is broken" statement) is not a
    // choice, so it is enabled (it has to be clickable to open) but NOT selectable.
    // Nothing can land on it as a current item, and "Use this destination" can
    // never be asked what a platform means.
    if (r.section || r.error) return Qt::ItemIsEnabled;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QHash<int, QByteArray> DestinationPickerModel::roleNames() const {
    return {
        { LabelRole, "label" },
        { IconNameRole, "iconName" },
        { IsSectionRole, "isSection" },
        { ExpandedRole, "expanded" },
        { OnScreenRole, "onScreen" },
        { VisibleRole, "visible" },
        { IdRole, "id" },
        { HaystackRole, "haystack" },
        { TooltipRole, "tooltip" },
        { SelectedRole, "selected" },
        { ChildCountRole, "childCount" },
        { LevelRole, "level" },
    };
}

QString DestinationPickerModel::windowTitleText() const {
    return QCoreApplication::translate("gui::DestinationPicker", "Where are you posting this?");
}

QString DestinationPickerModel::searchPlaceholder() const {
    return QCoreApplication::translate("gui::DestinationPicker", "Search: telegram, sticker, reels, illustration, 微信...");
}

QString DestinationPickerModel::searchAccessibleName() const {
    return QCoreApplication::translate("gui::DestinationPicker", "Search destinations");
}

QString DestinationPickerModel::okText() const {
    return QCoreApplication::translate("gui::DestinationPicker", "Use this destination");
}

QString DestinationPickerModel::cancelText() const {
    // The same context QDialogButtonBox's Cancel button resolves through, so the
    // wording stays Qt's own and does not need a catalog entry of ours.
    return QCoreApplication::translate("QPlatformTheme", "Cancel");
}

QString DestinationPickerModel::manualText() const {
    return QCoreApplication::translate("gui::DestinationPicker", "My platform isn't listed (I'll choose the format)");
}

QString DestinationPickerModel::manualTooltip() const {
    return QCoreApplication::translate("gui::DestinationPicker", "Everything above is a destination we know the rules for. "
                    "This one lets you state the format and codec yourself.");
}

void DestinationPickerModel::setError(const QString& aError) {
    Row r;
    r.label = aError;
    r.error = true;
    // A statement, not a choice: it cannot be selected, searched for or exported
    // to, and the escape hatch still lets the user state the format by hand.
    r.visible = true;
    beginInsertRows(QModelIndex(), mRows.size(), mRows.size());
    mRows.push_back(r);
    endInsertRows();
    applyQuery();
}

void DestinationPickerModel::setCurrentRow(int aRow) {
    if (aRow == mCurrentRow) return;
    mCurrentRow = aRow;
    emit currentRowChanged();
    if (!mRows.isEmpty()) {
        emit dataChanged(index(0), index(mRows.size() - 1), {SelectedRole});
    }
}

void DestinationPickerModel::setQuery(const QString& aQuery) {
    const QString q = aQuery.trimmed().toLower();
    const bool changed = q != mQuery;
    mQuery = q;
    applyQuery();
    if (changed) emit queryChanged();
}

void DestinationPickerModel::applyQuery() {
    const QString q = mQuery;
    int firstDestination = -1;
    bool expansionMoved = false;
    for (int i = 0; i < mRows.size(); ++i) {
        Row& r = mRows[i];
        if (r.section) {
            bool anyChild = false;
            for (int c = 0; c < r.childCount; ++c) {
                Row& child = mRows[i + 1 + c];
                const bool match = q.isEmpty() || child.haystack.contains(q);
                child.visible = match;
                anyChild = anyChild || match;
                if (match && firstDestination < 0) firstDestination = i + 1 + c;
            }
            // A platform is shown while it has something to show, and never on its
            // own: "Telegram" over no Telegram destination is a claim the tree
            // cannot back up.
            r.visible = anyChild;
            // A search OPENS what it found: a match inside a closed platform would
            // be an answer the user cannot see. Expansion is never undone here,
            // because closing a platform the user opened themselves - or one they
            // are about to browse - is not what clearing a search field means.
            if (anyChild && !q.isEmpty() && !r.expanded) {
                r.expanded = true;
                expansionMoved = true;
            }
        } else if (r.error) {
            r.visible = true;
        } else {
            r.visible = q.isEmpty() || r.haystack.contains(q);
            if (r.visible && firstDestination < 0) firstDestination = i;
        }
    }

    // Move the highlight only when the current row can no longer be chosen: the
    // constructor preselects the REMEMBERED destination (MainWindow passes
    // QSettings "export_destination" in) and calls this at the end of that
    // constructor, so selecting the first visible row here would contradict the
    // remembered setting on screen.
    const bool currentIsAnswer = validRow(mCurrentRow) && !mRows[mCurrentRow].section
                                 && !mRows[mCurrentRow].error && mRows[mCurrentRow].visible;
    const int target = currentIsAnswer ? mCurrentRow : firstDestination;
    if (target >= 0) {
        // Never select something inside a closed platform: the preselection has to
        // be on screen, not merely current.
        const int parent = mRows[target].parent;
        if (parent >= 0 && !mRows[parent].expanded) {
            mRows[parent].expanded = true;
            expansionMoved = true;
        }
        setCurrentRow(target);
    }

    if (!mRows.isEmpty()) {
        emit dataChanged(index(0), index(mRows.size() - 1));
    }
    emit visibleRowCountChanged();
    if (expansionMoved) emit expansionChanged();
}

void DestinationPickerModel::selectRemembered(const QString& aId) {
    if (aId.isEmpty()) return;
    for (int i = 0; i < mRows.size(); ++i) {
        if (mRows[i].section || mRows[i].error || mRows[i].id != aId) continue;
        setCurrentRow(i);
        const int parent = mRows[i].parent;
        if (parent >= 0 && !mRows[parent].expanded) {
            mRows[parent].expanded = true;
            emit dataChanged(index(parent), index(parent), {ExpandedRole, OnScreenRole});
            const int first = parent + 1;
            const int last = parent + mRows[parent].childCount;
            if (last >= first) emit dataChanged(index(first), index(last), {OnScreenRole});
            emit visibleRowCountChanged();
            emit expansionChanged();
        }
        return;
    }
}

QStringList DestinationPickerModel::expandedIds() const {
    QStringList out;
    for (const Row& r : mRows)
        if (r.section && !r.error && r.expanded && !r.id.isEmpty()) out << r.id;
    return out;
}

void DestinationPickerModel::applyExpandedIds(const QStringList& aIds) {
    const QSet<QString> want(aIds.begin(), aIds.end());
    bool moved = false;
    for (int i = 0; i < mRows.size(); ++i) {
        Row& r = mRows[i];
        if (!r.section || r.error) continue;
        const bool open = want.contains(r.id);
        if (r.expanded != open) {
            r.expanded = open;
            moved = true;
        }
    }
    // The preselection invariant survives the restore (C17 decides WHICH groups
    // start open, not whether the current answer may be hidden): the current
    // row's platform re-opens so the selection is on screen, not merely current.
    if (validRow(mCurrentRow)) {
        const int parent = mRows[mCurrentRow].parent;
        if (parent >= 0 && !mRows[parent].expanded) {
            mRows[parent].expanded = true;
            moved = true;
        }
    }
    if (!moved) return;
    if (!mRows.isEmpty()) emit dataChanged(index(0), index(mRows.size() - 1),
                                           {ExpandedRole, OnScreenRole});
    emit visibleRowCountChanged();
    emit expansionChanged();
}

void DestinationPickerModel::select(int aRow) {
    if (!validRow(aRow)) return;
    // A platform is not selectable, whatever route asks (see flags()).
    if (mRows[aRow].section || mRows[aRow].error) return;
    setCurrentRow(aRow);
}

void DestinationPickerModel::toggleSection(int aRow) {
    if (!isSection(aRow)) return;
    mRows[aRow].expanded = !mRows[aRow].expanded;
    emit dataChanged(index(aRow), index(aRow), {ExpandedRole, OnScreenRole});
    // The children's on-screen state changed too.
    const int first = aRow + 1;
    const int last = aRow + mRows[aRow].childCount;
    if (last >= first) emit dataChanged(index(first), index(last), {OnScreenRole});
    emit visibleRowCountChanged();
    emit expansionChanged();
}

void DestinationPickerModel::activate(int aRow) {
    if (validRow(aRow) && mRows[aRow].section) {
        // A platform is not a destination, so this is not an answer: open it (or
        // close it), which is the only thing it can mean. Deliberately NOT "pick
        // the first destination under it": that would export something the user
        // never chose.
        toggleSection(aRow);
        return;
    }
    if (!validRow(aRow) || mRows[aRow].error || mRows[aRow].id.isEmpty()
        || !mRows[aRow].visible) {
        // Nothing matched: the honest answer is the escape hatch, which is what the
        // user would have to use anyway.
        mChosenId.clear();
        emit accepted();
        return;
    }
    mChosenId = mRows[aRow].id;
    emit accepted();
}

void DestinationPickerModel::activateCurrent() {
    activate(mCurrentRow);
}

void DestinationPickerModel::step(int aDelta) {
    QList<int> onScreen;
    for (int i = 0; i < mRows.size(); ++i) {
        if (!mRows[i].section && !mRows[i].error && isOnScreen(i)) onScreen.push_back(i);
    }
    if (onScreen.isEmpty()) return;
    const int pos = onScreen.indexOf(mCurrentRow);
    int next = 0;
    if (pos < 0) {
        next = aDelta >= 0 ? 0 : onScreen.size() - 1;
    } else {
        next = qBound(0, pos + aDelta, onScreen.size() - 1);
    }
    setCurrentRow(onScreen[next]);
}

void DestinationPickerModel::expandCurrentSection() {
    if (validRow(mCurrentRow)) {
        if (mRows[mCurrentRow].section) {
            if (!mRows[mCurrentRow].expanded) toggleSection(mCurrentRow);
            return;
        }
        const int parent = mRows[mCurrentRow].parent;
        if (parent >= 0 && !mRows[parent].expanded) toggleSection(parent);
        return;
    }
    for (int i = 0; i < mRows.size(); ++i) {
        if (mRows[i].section && mRows[i].visible) {
            if (!mRows[i].expanded) toggleSection(i);
            return;
        }
    }
}

void DestinationPickerModel::collapseCurrentSection() {
    if (validRow(mCurrentRow)) {
        if (mRows[mCurrentRow].section) {
            if (mRows[mCurrentRow].expanded) toggleSection(mCurrentRow);
            return;
        }
        const int parent = mRows[mCurrentRow].parent;
        if (parent >= 0 && mRows[parent].expanded) toggleSection(parent);
        return;
    }
    for (int i = 0; i < mRows.size(); ++i) {
        if (mRows[i].section && mRows[i].visible && mRows[i].expanded) {
            toggleSection(i);
            return;
        }
    }
}

void DestinationPickerModel::acceptManually() {
    mChosenId.clear();
    emit accepted();
}

void DestinationPickerModel::reject() {
    emit rejected();
}

} // namespace gui
