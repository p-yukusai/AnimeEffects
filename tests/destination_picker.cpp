// The destination picker, offscreen: what the user actually gets.
//
// Every question about this tree is a question about a person, and two of the
// answers are already in the application: MainWindow passes the REMEMBERED
// destination in (QSettings "export_destination"), and the picker's own header says
// that id "preselects a destination". It did not, once: the constructor set the
// current item and then applyFilter("") replaced it with the first row, so the
// picker opened on "A video for a chat app" and Enter exported there whatever the
// user chose last time. A remembered setting the screen contradicts is worse than no
// remembered setting.
//
// The rest of this file is about the STRUCTURE, because a tree has three ways to be
// wrong that a list cannot be: the platforms could be open (which is the flat list
// with bold lines in it, and was shipped once), the preselection could be current
// but inside a closed platform (on screen is the requirement), and a platform node
// could be treated as an answer (it has no destination id, so it must never be one).
//
// Display-free on purpose: the GUI probes need a display, an accessibility bus and a
// window manager to ask the same question, which is how the first of those defects
// went unnoticed.
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/destination_picker
#include <QApplication>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QTranslator>
#include <QTextStream>
#include <QTreeWidget>
#include <cstdio>

#include "ctrl/ExportTarget.h"
#include "gui/DestinationPicker.h"
#include "gui/GUIResources.h"

static int failures = 0;

static void check(bool ok, const QString& what) {
    QTextStream(stdout) << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

// A platform node is a section. Structural on purpose: a platform is the only thing
// in this tree that has children (a platform with one destination is listed as that
// destination, not as a section holding one line).
static bool isSection(QTreeWidgetItem* aItem) {
    return aItem && aItem->childCount() > 0;
}

static QString idOf(QTreeWidgetItem* aItem) {
    return aItem ? aItem->data(0, Qt::UserRole).toString() : QString();
}

static QTreeWidget* treeOf(gui::DestinationPicker& aPicker) {
    return aPicker.findChild<QTreeWidget*>(QStringLiteral("destinationTree"));
}

static QLineEdit* searchOf(gui::DestinationPicker& aPicker) {
    return aPicker.findChild<QLineEdit*>(QStringLiteral("destinationSearch"));
}

static QString currentId(QTreeWidget* aTree) {
    return idOf(aTree ? aTree->currentItem() : nullptr);
}

static QList<QTreeWidgetItem*> sections(QTreeWidget* aTree) {
    QList<QTreeWidgetItem*> out;
    for (int i = 0; i < aTree->topLevelItemCount(); ++i) {
        if (isSection(aTree->topLevelItem(i))) out.push_back(aTree->topLevelItem(i));
    }
    return out;
}

static QTreeWidgetItem* sectionNamed(QTreeWidget* aTree, const QString& aText) {
    for (QTreeWidgetItem* item : sections(aTree)) {
        if (item->text(0) == aText) return item;
    }
    return nullptr;
}

// Every destination the picker could answer with, in tree order.
static QList<QTreeWidgetItem*> destinations(QTreeWidget* aTree) {
    QList<QTreeWidgetItem*> out;
    for (int i = 0; i < aTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = aTree->topLevelItem(i);
        if (!isSection(top)) {
            out.push_back(top);
            continue;
        }
        for (int c = 0; c < top->childCount(); ++c) out.push_back(top->child(c));
    }
    return out;
}

// A destination is ON SCREEN when it is not hidden AND every platform above it is
// open. Asking isHidden() alone would answer "not hidden" for the children of a
// closed platform, which is precisely the state this file exists to prevent. A
// platform's own row is drawn whether or not it is open, so its expansion says
// nothing about the node itself.
static bool onScreen(QTreeWidgetItem* aItem) {
    if (!aItem || aItem->isHidden()) return false;
    for (QTreeWidgetItem* parent = aItem->parent(); parent; parent = parent->parent()) {
        if (parent->isHidden() || !parent->isExpanded()) return false;
    }
    return true;
}

static int countOnScreen(QTreeWidget* aTree) {
    int n = 0;
    for (QTreeWidgetItem* item : destinations(aTree)) {
        if (onScreen(item)) ++n;
    }
    return n;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    gui::GUIResources resources(QStringLiteral(AE_RESOURCE_DIR));
    const QList<ctrl::ExportTarget> targets =
        ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH));
    const QList<ctrl::ExportGroup> groups =
        ctrl::ExportTargets::loadGroups(QStringLiteral(AE_EXPORT_TARGETS_PATH));
    check(targets.size() > 40,
          QStringLiteral("the destination table loads (%1 rows)").arg(targets.size()));
    if (targets.isEmpty()) return 1;

    // 1. The remembered destination is the selected one, and its platform is OPEN:
    //    "selected" inside a closed platform is an answer the user cannot see.
    {
        gui::DestinationPicker picker(targets, groups, resources,
                                      QStringLiteral("telegram.sticker"), nullptr);
        QTreeWidget* tree = treeOf(picker);
        check(tree != nullptr, "the picker has a destination tree");
        if (!tree) return 1;
        const QString shown = currentId(tree);
        check(shown == QStringLiteral("telegram.sticker"),
              QStringLiteral("the remembered destination is selected, not the first row "
                             "(selected: %1)").arg(shown.isEmpty() ? QStringLiteral("nothing") : shown));
        check(onScreen(tree->currentItem()),
              "and it is on screen: the platform holding it is open");
    }

    // 2. The platforms are CLOSED, which is the whole point of a tree: the first
    //    level is the platform, not fifty-five destinations. The one exception on
    //    this run is the section holding the remembered destination, above.
    {
        gui::DestinationPicker picker(targets, groups, resources, QString(), nullptr);
        QTreeWidget* tree = treeOf(picker);
        if (!tree) return 1;
        const QList<QTreeWidgetItem*> heads = sections(tree);
        check(heads.size() >= 10,
              QStringLiteral("the platforms are sections (%1 of them)").arg(heads.size()));
        int open = 0, nested = 0;
        for (QTreeWidgetItem* head : heads) {
            if (head->isExpanded()) ++open;
            if (head->childCount() > 0) ++nested;
        }
        check(open == 0, QStringLiteral("every platform starts closed (%1 open)").arg(open));
        check(nested == heads.size(), "every platform has destinations inside it");
        check(countOnScreen(tree) < targets.size(),
              QStringLiteral("so the picker does not open on the whole flat list (%1 of %2 "
                             "destinations on screen)")
                  .arg(countOnScreen(tree)).arg(targets.size()));
        check(!isSection(tree->currentItem()) && !currentId(tree).isEmpty(),
              QStringLiteral("with no remembered destination a destination is selected (%1)")
                  .arg(currentId(tree)));
        check(onScreen(tree->currentItem()), "and it is on screen");
    }

    // 3. Two levels exactly when there is something to choose between: a platform
    //    with several destinations is a node, a platform with one is that
    //    destination, and a group whose rows are not variants of one platform
    //    ("general") is listed directly.
    {
        QHash<QString, int> rowsPerGroup;
        for (const ctrl::ExportTarget& t : targets) {
            rowsPerGroup[t.group.isEmpty() ? QStringLiteral("general") : t.group] += 1;
        }
        QSet<QString> nested;
        for (const ctrl::ExportGroup& g : groups) {
            if (g.variants && rowsPerGroup.value(g.id) > 1) nested.insert(g.id);
        }

        gui::DestinationPicker picker(targets, groups, resources, QString(), nullptr);
        QTreeWidget* tree = treeOf(picker);
        if (!tree) return 1;

        QSet<QString> gotNested;
        for (QTreeWidgetItem* head : sections(tree)) {
            QString key;
            for (const ctrl::ExportGroup& g : groups) {
                if (g.title == head->text(0) && nested.contains(g.id)) key = g.id;
            }
            check(!key.isEmpty(),
                  QStringLiteral("the section '%1' is one that should nest").arg(head->text(0)));
            if (!key.isEmpty()) gotNested.insert(key);
            check(head->childCount() > 1,
                  QStringLiteral("'%1' is a section because there is a choice inside it (%2)")
                      .arg(head->text(0)).arg(head->childCount()));
        }
        check(gotNested == nested,
              QStringLiteral("every platform with variants is a section (%1 of %2)")
                  .arg(gotNested.size()).arg(nested.size()));

        // The flat ones: no node, and one destination each.
        int flat = 0;
        for (const ctrl::ExportTarget& t : targets) {
            const QString key = t.group.isEmpty() ? QStringLiteral("general") : t.group;
            if (nested.contains(key)) continue;
            ++flat;
            bool found = false;
            for (QTreeWidgetItem* item : destinations(tree)) {
                if (idOf(item) == t.id && !item->parent()) found = true;
            }
            if (!found) {
                check(false, QStringLiteral("%1 is listed directly, not under a section").arg(t.id));
            }
        }
        check(flat > 10, QStringLiteral("the single-destination platforms are listed directly (%1)").arg(flat));
        check(destinations(tree).size() == targets.size(),
              QStringLiteral("every destination is in the tree once (%1 of %2)")
                  .arg(destinations(tree).size()).arg(targets.size()));
    }

    // 4. A platform is not an answer. It has no destination id, so the default
    //    button opens it rather than exporting something the user never chose, and
    //    it deliberately does NOT pick the first destination inside it.
    {
        gui::DestinationPicker picker(targets, groups, resources, QString(), nullptr);
        QTreeWidget* tree = treeOf(picker);
        QDialogButtonBox* box = picker.findChild<QDialogButtonBox*>();
        check(box != nullptr, "the picker has its buttons");
        QTreeWidgetItem* telegram = sectionNamed(tree, QStringLiteral("Telegram"));
        check(telegram != nullptr, "the Telegram platform is a section");
        if (!tree || !box || !telegram) return 1;

        tree->setCurrentItem(telegram);
        box->button(QDialogButtonBox::Ok)->click();
        check(picker.chosenId().isEmpty(),
              QStringLiteral("the default button does not answer with a platform (%1)")
                  .arg(picker.chosenId()));
        check(telegram->isExpanded(), "it opens the platform instead");
        box->button(QDialogButtonBox::Ok)->click();
        check(!telegram->isExpanded(), "and pressing it again closes it");

        // On a destination it is the answer, and the answer is a row of the table.
        QTreeWidgetItem* sticker = nullptr;
        for (int c = 0; c < telegram->childCount(); ++c) {
            if (idOf(telegram->child(c)) == QLatin1String("telegram.sticker")) {
                sticker = telegram->child(c);
            }
        }
        check(sticker != nullptr, "telegram.sticker is inside the Telegram section");
        if (sticker) {
            telegram->setExpanded(true);
            tree->setCurrentItem(sticker);
            box->button(QDialogButtonBox::Ok)->click();
            const QString chosen = picker.chosenId();
            check(chosen == QLatin1String("telegram.sticker"),
                  QStringLiteral("the default button answers with the selected destination (%1)")
                      .arg(chosen));
            check(ctrl::ExportTargets::find(targets, chosen).id == chosen,
                  "and it is a row of the table");
        }
    }

    // 5. Typing narrows, OPENS what it found, and selects a destination rather than
    //    the platform above it: "wec" then Enter is the fast path, and it has to be
    //    two keystrokes rather than three.
    {
        gui::DestinationPicker picker(targets, groups, resources, QString(), nullptr);
        QTreeWidget* tree = treeOf(picker);
        QLineEdit* search = searchOf(picker);
        check(search != nullptr, "the picker can be typed at");
        if (!tree || !search) return 1;

        search->setText(QStringLiteral("wec"));
        const QString shown = currentId(tree);
        check(!isSection(tree->currentItem()),
              "a search selects a destination, never the platform above it");
        check(shown.contains(QStringLiteral("wechat")),
              QStringLiteral("and selects one of its matches (%1)").arg(shown));
        check(onScreen(tree->currentItem()), "which is on screen");
        check(picker.visibleRowCount() >= 1 && picker.visibleRowCount() < targets.size(),
              QStringLiteral("a search narrows the tree (%1 of %2 destinations)")
                  .arg(picker.visibleRowCount()).arg(targets.size()));

        // A platform with one destination is not a section, so its match selects the
        // destination itself: there is nothing to open.
        search->setText(QStringLiteral("youtube"));
        check(currentId(tree) == QLatin1String("youtube.video"),
              QStringLiteral("a single-destination platform matches directly (%1)")
                  .arg(currentId(tree)));

        // A platform name keeps the whole platform, and the results are legible
        // because they arrive under the platform that matched.
        search->setText(QStringLiteral("instagram"));
        int shownRows = 0;
        for (QTreeWidgetItem* item : destinations(tree)) {
            if (onScreen(item) && item->data(0, Qt::UserRole + 1).toString().contains(
                                      QStringLiteral("instagram"))) {
                ++shownRows;
            }
        }
        check(shownRows >= 2, QStringLiteral("a platform match shows its destinations (%1)").arg(shownRows));
        QTreeWidgetItem* instagram = sectionNamed(tree, QStringLiteral("Instagram"));
        check(instagram && !instagram->isHidden() && instagram->isExpanded(),
              "the platform that matched is open");
        QTreeWidgetItem* telegram = sectionNamed(tree, QStringLiteral("Telegram"));
        check(telegram && telegram->isHidden(), "a platform with no match is not shown");

        search->clear();
        check(picker.visibleRowCount() == countOnScreen(tree),
              "visibleRowCount counts what is on screen");
        bool anyHidden = false;
        for (QTreeWidgetItem* item : destinations(tree)) {
            if (item->isHidden()) anyHidden = true;
        }
        check(!anyHidden, "clearing the search restores every destination");
        // Clearing is not "collapse everything again": a platform the user opened, or
        // one a search opened, stays open. It is not a reset button.
        check(instagram->isExpanded(), "and leaves the platforms it opened open");
    }

    // 6. Enter on a platform is the only thing it can be, never a jump into it, and
    //    the arrows only ever move between things that are on screen.
    {
        gui::DestinationPicker picker(targets, groups, resources, QString(), nullptr);
        QTreeWidget* tree = treeOf(picker);
        if (!tree) return 1;
        QTreeWidgetItem* telegram = sectionNamed(tree, QStringLiteral("Telegram"));
        QDialogButtonBox* box = picker.findChild<QDialogButtonBox*>();
        if (!telegram || !box) return 1;

        tree->setCurrentItem(telegram);
        const QString before = currentId(tree);
        box->button(QDialogButtonBox::Ok)->click();
        check(picker.chosenId().isEmpty() && currentId(tree) == before,
              "opening a platform does not move the highlight into it");

        telegram->setExpanded(true);
        tree->setCurrentItem(telegram);
        int offscreen = 0;
        for (int i = 0; i < 120; ++i) {
            QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
            QApplication::sendEvent(tree, &down);
            if (tree->currentItem() && !onScreen(tree->currentItem())) ++offscreen;
        }
        check(offscreen == 0,
              QStringLiteral("walking down never lands on something that is not on screen "
                             "(%1 times)").arg(offscreen));
        check(!currentId(tree).isEmpty(),
              QStringLiteral("and the walk reaches destinations (%1)").arg(currentId(tree)));
    }

    // 5. The dialog in a language that is not English, which is the one question a
    //    catalog full of translations cannot answer by itself.
    //
    //    tr() resolves through the class's staticMetaObject. Without Q_OBJECT on
    //    DestinationPicker the call bound to QDialog, the lookup asked for the
    //    "QDialog" context, and every string in this dialog stayed English in all six
    //    languages while the .ts files shipped translations under
    //    "gui::DestinationPicker" that nothing ever read. lrelease reported 708
    //    finished translations; the window title was "Where are you posting this?".
    //
    //    So this asks the WIDGET, not the catalog, and it asks it with the shipping
    //    .qm installed: a title that comes back Spanish proves the context the class
    //    actually asks for is the one translators filled.
    {
        QTranslator translator;
        const QString qmDir = QStringLiteral(AE_I18N_QM_DIR);
        const bool loaded = translator.load(QStringLiteral("translation_es"), qmDir);
        check(loaded, QStringLiteral("the Spanish catalog loads from the build (%1)").arg(qmDir));
        if (loaded) {
            QApplication::installTranslator(&translator);
            gui::DestinationPicker picker(targets, groups, resources, QString(), nullptr);
            const QString title = picker.windowTitle();
            check(title != QStringLiteral("Where are you posting this?"),
                  QStringLiteral("the picker title is translated, not the English source (%1)")
                      .arg(title));
            QDialogButtonBox* box = picker.findChild<QDialogButtonBox*>();
            const QString ok = box ? box->button(QDialogButtonBox::Ok)->text() : QString();
            check(ok != QStringLiteral("Use this destination") && !ok.isEmpty(),
                  QStringLiteral("and so is its confirm button (%1)").arg(ok));
            // The export dialog answers through its own context, and it was unreachable
            // for a different reason: a hand rolled tr() that forwarded to "ExportDiag",
            // a context no catalog has. Both are asked here so the pair cannot regress
            // together unnoticed.
            const QString exported = QCoreApplication::translate("ExportWidgetUI", "Export");
            check(exported != QStringLiteral("Export"),
                  QStringLiteral("the export dialog's own context answers in Spanish (%1)")
                      .arg(exported));
            QApplication::removeTranslator(&translator);
        }
    }

    QTextStream(stdout) << (failures == 0 ? "=== destination_picker: OK ===\n"
                                          : QStringLiteral("=== destination_picker: %1 FAILED ===\n").arg(failures));
    return failures == 0 ? 0 : 1;
}
