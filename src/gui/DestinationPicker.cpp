#include "gui/DestinationPicker.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QFont>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "gui/GUIResources.h"
#include "gui/theme/Colors.h"

namespace gui {

namespace {

// What an item carries. Column 0 is the only column (the header is hidden).
//  - a DESTINATION has an id. Every route that answers the user asks for one.
//  - a SECTION (a platform you can open) has no id, because "Telegram" is not a
//    destination: it is the question whose answers are its children.
enum {
    IdRole = Qt::UserRole,
    HaystackRole,  // what the search field matches
    SectionRole    // true on a platform node
};

// What search matches, per destination: everything the row declares about itself,
// in the words a user might type for it. The label (what they read), the SECTION
// TITLE (the platform: "telegram", "insta"), the id ("telegram.sticker", the name
// this row has in the data file), the family key and its title ("sticker",
// "Stickers and emoji" - both shown in the tooltip, so a match on them is a hover
// away from being explained), the container, and the artifact.
//
// A platform match keeps the whole platform (its title is in every one of its rows'
// haystacks), which is legible in a tree and was not in a flat list: the results
// arrive under the platform that matched, so the user can see why they are there.
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

bool isSection(const QTreeWidgetItem* aItem) {
    return aItem && aItem->data(0, SectionRole).toBool();
}

QString idOf(const QTreeWidgetItem* aItem) {
    return aItem ? aItem->data(0, IdRole).toString() : QString();
}

} // namespace

// The application's own rows and sections: one data file, so the tree cannot be
// grouped by a table the exporter is not planning from.
DestinationPicker::DestinationPicker(GUIResources& aResources, const QString& aCurrentId,
                                     QWidget* aParent)
    : DestinationPicker(ctrl::ExportTargets::load(), ctrl::ExportTargets::loadGroups(),
                        aResources, aCurrentId, aParent) {
    // A destination table that cannot be read leaves an EMPTY picker, and an empty
    // picker says "this build knows no platforms" rather than "the table is broken".
    // The reasons are already written and translated (ExportTargets::load takes the
    // error out-parameter), so ask for one and state it in the tree. The second read
    // costs a file parse, and only on this path: the normal path never gets here
    // because the table it loaded has rows.
    if (mTree && mTree->topLevelItemCount() == 0) {
        QString error;
        ctrl::ExportTargets::load(ctrl::ExportTargets::defaultPath(), &error);
        if (!error.isEmpty()) {
            auto* row = new QTreeWidgetItem(mTree);
            row->setText(0, error);
            // A statement, not a choice: it cannot be selected, searched for or
            // exported to, and the escape hatch below still lets the user state the
            // format by hand.
            row->setFlags(Qt::NoItemFlags);
        }
    }
}

DestinationPicker::DestinationPicker(const QList<ctrl::ExportTarget>& aTargets,
                                     const QList<ctrl::ExportGroup>& aGroups,
                                     GUIResources& aResources, const QString& aCurrentId,
                                     QWidget* aParent)
    : QDialog(aParent) {
    setWindowTitle(tr("Where are you posting this?"));
    setObjectName(QString::fromUtf8("destinationPicker"));
    setModal(true);
    resize(560, 620);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);

    mSearch = new QLineEdit(this);
    mSearch->setObjectName(QString::fromUtf8("destinationSearch"));
    mSearch->setPlaceholderText(
        tr("Search: telegram, sticker, reels, illustration, 微信..."));
    mSearch->setClearButtonEnabled(true);
    // A name for the accessibility tree: a QLineEdit with only a placeholder is
    // announced as "text" and cannot be addressed by name at all (see
    // scripts/gui/ae_ui.py, which drives the probes through this tree).
    mSearch->setAccessibleName(tr("Search destinations"));
    layout->addWidget(mSearch);

    mTree = new QTreeWidget(this);
    mTree->setObjectName(QString::fromUtf8("destinationTree"));
    mTree->setHeaderHidden(true);
    mTree->setRootIsDecorated(true); // the disclosure triangle: this is a tree
    mTree->setItemsExpandable(true);
    mTree->setIconSize(QSize(22, 22));
    mTree->setUniformRowHeights(true);
    mTree->setAllColumnsShowFocus(false);
    // The picker owns both gestures: a click on a platform opens or closes it (see
    // the itemClicked connection), and a double click must not do it a second time.
    mTree->setExpandsOnDoubleClick(false);
    layout->addWidget(mTree, 1);

    // THE FIRST LEVEL IS THE PLATFORM, and it is CLOSED. A person knows where they
    // are posting before they know what the platform will take from them; a list that
    // shows all fifty-five destinations at once is the flat list with bold lines in
    // it, which is what this replaced.
    //
    // Nested only when there is a choice to make: a platform with several
    // destinations is a node, a platform with one IS that destination, and a group
    // whose rows are not variants of one platform (the table marks it `variants="false"`)
    // is listed directly. That last case is also what keeps the safe default (a 720p
    // MP4 every chat app accepts) one Enter away on a fresh install.
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

    auto addDestination = [&](QTreeWidgetItem* aSection, const ctrl::ExportTarget& t,
                              const QString& aGroupTitle) {
        auto* item = aSection ? new QTreeWidgetItem(aSection) : new QTreeWidgetItem(mTree);
        item->setIcon(0, aResources.icon(t.icon));
        item->setText(0, t.label);
        item->setData(0, IdRole, t.id);
        const QString title = familyTitle(t.family);
        item->setData(0, HaystackRole, haystackFor(t, aGroupTitle, title));
        // What the row is, on hover: the kind of handover, and the id it has in the
        // data file (which is what a bug report should quote).
        item->setToolTip(0, title.isEmpty() ? t.id : title + QStringLiteral("\n") + t.id);
        if (t.id == aCurrentId) mTree->setCurrentItem(item);
        return item;
    };

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

        if (rows.size() == 1 || !g.variants) {
            for (const ctrl::ExportTarget& t : rows) addDestination(nullptr, t, g.title);
            continue;
        }

        auto* section = new QTreeWidgetItem(mTree);
        section->setText(0, g.title);
        section->setData(0, SectionRole, true);
        // Quiet, but structural: the platforms are the scan column, so they must not
        // compete with the destinations inside them. The stylesheet has no `color` on
        // QAbstractItemView::item precisely so a per-item brush (this one, and the
        // object tree's dimmed labels) survives.
        QFont face = section->font(0);
        face.setBold(true);
        section->setFont(0, face);
        section->setForeground(0, theme::Colors::current().textMuted);
        // A platform's badge is its destinations' badge: every row in a section
        // carries the same brand icon, so the closed tree still says WHICH platform
        // each line is, at a glance. Taken from the first row rather than added as
        // data, because a platform has no icon of its own anywhere else.
        section->setIcon(0, aResources.icon(rows.first().icon));
        section->setToolTip(0, tr("%1 destinations").arg(rows.size()));

        for (const ctrl::ExportTarget& t : rows) addDestination(section, t, g.title);
    }

    mManualButton = new QPushButton(tr("My platform isn't listed (I'll choose the format)"), this);
    mManualButton->setObjectName(QString::fromUtf8("destinationManualButton"));
    mManualButton->setToolTip(tr("Everything above is a destination we know the rules for. "
                                 "This one lets you state the format and codec yourself."));
    layout->addWidget(mManualButton);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Use this destination"));
    layout->addWidget(buttons);

    // Typing filters and the arrows move the tree, without leaving the field: a
    // chooser you have to Tab out of is a chooser you fight.
    connect(mSearch, &QLineEdit::textChanged, this, [this](const QString& aText) {
        applyFilter(aText);
    });
    mSearch->installEventFilter(this);
    // One click on a platform opens or closes it. A person browsing has already
    // decided WHERE they are posting, so the click that means "show me that one" and
    // the click that means "close it again" are the same click.
    connect(mTree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* aItem, int) {
        if (isSection(aItem)) toggleSection(aItem);
    });
    connect(mTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* aItem, int) {
        if (!isSection(aItem)) chooseCurrent();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() { chooseCurrent(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(mManualButton, &QPushButton::clicked, this, [this]() {
        mChosenId.clear();
        accept();
    });

    mSearch->setFocus();
    applyFilter(QString());
}

int DestinationPicker::visibleRowCount() const {
    int n = 0;
    for (int i = 0; i < mTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = mTree->topLevelItem(i);
        if (isSection(top)) {
            // A closed platform shows nothing of its own, so its destinations are not
            // on screen however they are flagged: isHidden() answers "not hidden" for
            // a child of a closed platform, which is the state that made the export
            // form's own container tell lies about itself once.
            if (top->isHidden() || !top->isExpanded()) continue;
            for (int c = 0; c < top->childCount(); ++c) {
                if (!top->child(c)->isHidden()) ++n;
            }
        } else if (!top->isHidden()) {
            ++n;
        }
    }
    return n;
}

void DestinationPicker::applyFilter(const QString& aQuery) {
    const QString q = aQuery.trimmed().toLower();
    QTreeWidgetItem* firstDestination = nullptr;
    for (int i = 0; i < mTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = mTree->topLevelItem(i);
        if (!isSection(top)) {
            const bool match = q.isEmpty() || top->data(0, HaystackRole).toString().contains(q);
            top->setHidden(!match);
            if (match && !firstDestination) firstDestination = top;
            continue;
        }
        bool anyChild = false;
        for (int c = 0; c < top->childCount(); ++c) {
            QTreeWidgetItem* child = top->child(c);
            const bool match = q.isEmpty() || child->data(0, HaystackRole).toString().contains(q);
            child->setHidden(!match);
            anyChild = anyChild || match;
            if (match && !firstDestination) firstDestination = child;
        }
        // A platform is shown while it has something to show, and never on its own:
        // "Telegram" over no Telegram destination is a claim the tree cannot back up.
        top->setHidden(!anyChild);
        // A search OPENS what it found: a match inside a closed platform would be an
        // answer the user cannot see. Expansion is never undone here, because closing
        // a platform the user opened themselves - or one they are about to browse -
        // is not what clearing a search field means.
        if (anyChild && !q.isEmpty()) top->setExpanded(true);
    }

    // Move the highlight only when the current row can no longer be chosen: the
    // constructor preselects the REMEMBERED destination (MainWindow passes QSettings
    // "export_destination" in as aCurrentId) and calls this at the end of that
    // constructor, so selecting the first visible row here would contradict the
    // remembered setting on screen. tests/destination_picker.cpp asks that question
    // without a display.
    QTreeWidgetItem* current = mTree->currentItem();
    const bool currentIsAnswer = current && !isSection(current) && !current->isHidden();
    QTreeWidgetItem* target = currentIsAnswer ? current : firstDestination;
    if (target) {
        // Never select something inside a closed platform: the preselection has to be
        // on screen, not merely current.
        if (QTreeWidgetItem* parent = target->parent()) parent->setExpanded(true);
        mTree->setCurrentItem(target);
        mTree->scrollToItem(target);
    }
}

void DestinationPicker::toggleSection(QTreeWidgetItem* aSection) {
    aSection->setExpanded(!aSection->isExpanded());
}

void DestinationPicker::chooseCurrent() {
    QTreeWidgetItem* item = mTree->currentItem();
    if (isSection(item)) {
        // A platform is not a destination, so this is not an answer: open it (or
        // close it), which is the only thing it can mean. Deliberately NOT "pick the
        // first destination under it": that would export something the user never
        // chose, and every platform here has more than one destination to choose
        // between, so there is nothing to guess.
        toggleSection(item);
        return;
    }
    if (!item || item->isHidden() || idOf(item).isEmpty()) {
        // Nothing matched: the honest answer is the escape hatch, which is what the
        // user would have to use anyway.
        mChosenId.clear();
        accept();
        return;
    }
    mChosenId = idOf(item);
    accept();
}

bool DestinationPicker::eventFilter(QObject* aWatched, QEvent* aEvent) {
    if (aWatched == mSearch && aEvent->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(aEvent);
        switch (key->key()) {
        case Qt::Key_Down:
        case Qt::Key_Up:
        case Qt::Key_PageDown:
        case Qt::Key_PageUp:
        case Qt::Key_Left:
        case Qt::Key_Right:
            // Forward to the tree: the search field keeps the text cursor, the tree
            // moves the highlight and opens platforms. Left/Right are here because a
            // tree is where those keys mean something (close, open).
            QApplication::sendEvent(mTree, aEvent);
            return true;
        default:
            break;
        }
    }
    return QDialog::eventFilter(aWatched, aEvent);
}

} // namespace gui
