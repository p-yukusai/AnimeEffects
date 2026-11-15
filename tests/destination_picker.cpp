// The destination picker, offscreen: what the user actually gets.
//
// Every question about this list is a question about a person, and two of the
// answers are already in the application: MainWindow passes the REMEMBERED
// destination in (QSettings "export_destination"), and the picker's own header says
// that id "preselects a destination". It did not, once: the constructor set the
// current item and then the filter replaced it with the first row, so the picker
// opened on "A video for a chat app" and Enter exported there whatever the user
// chose last time. A remembered setting the screen contradicts is worse than no
// remembered setting.
//
// The rest of this file is about the STRUCTURE, because a tree has three ways to be
// wrong that a list cannot be: the platforms could be open (which is the flat list
// with bold lines in it, and was shipped once), the preselection could be current
// but inside a closed platform (on screen is the requirement), and a platform node
// could be treated as an answer (it has no destination id, so it must never be one).
// The fourth is the reported bug this rewrite fixes: a platform was an ordinary
// SELECTABLE item, so the keyboard could land on it and the default button had to
// guess what "Telegram" meant. Here it is not selectable at all, and Enter on it
// only opens or closes it.
//
// Display-free on purpose: the GUI probes need a display, an accessibility bus and a
// window manager to ask the same question, which is how the first of those defects
// went unnoticed.
//
// It drives gui::DestinationPickerModel directly - no QWidget, no QTreeWidget, no
// QML engine - so it is the same code the scene binds to and not a parallel
// reimplementation of it.
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/destination_picker
#include <QApplication>
#include <QAbstractListModel>
#include <QHash>
#include <QMetaObject>
#include <QSet>
#include <QTextStream>
#include <QTranslator>
#include <cstdio>

#include "ctrl/ExportTarget.h"
#include "gui/qml/DestinationPickerModel.h"

static int failures = 0;

static void check(bool ok, const QString& what) {
    QTextStream(stdout) << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

using Model = gui::DestinationPickerModel;

// A platform node is a section. Structural on purpose: a platform is the only thing
// in this tree that has children (a platform with one destination is listed as that
// destination, not as a section holding one line).
static QList<int> sections(const Model& aModel) {
    QList<int> out;
    for (int i = 0; i < aModel.rowCount(); ++i) {
        if (aModel.isSection(i)) out.push_back(i);
    }
    return out;
}

// Every destination row, on screen or not.
static QList<int> destinations(const Model& aModel) {
    QList<int> out;
    for (int i = 0; i < aModel.rowCount(); ++i) {
        if (!aModel.isSection(i) && !aModel.idOf(i).isEmpty()) out.push_back(i);
    }
    return out;
}

static int sectionNamed(const Model& aModel, const QString& aText) {
    for (int row : sections(aModel)) {
        if (aModel.labelOf(row) == aText) return row;
    }
    return -1;
}

// The rows inside a section. Children are contiguous after their section (the model
// keeps tree order), which is what makes this arithmetic rather than a search.
static QList<int> children(const Model& aModel, int aSection) {
    QList<int> out;
    for (int c = 1; c <= aModel.childCount(aSection); ++c) out.push_back(aSection + c);
    return out;
}

// A destination is ON SCREEN when it matches the query AND its platform is open.
// Asking "visible" alone would answer true for the children of a closed platform,
// which is precisely the state this file exists to prevent. A platform's own row is
// drawn whether or not it is open, so its expansion says nothing about the node.
static bool onScreen(const Model& aModel, int aRow) { return aModel.isOnScreen(aRow); }

static int countOnScreen(const Model& aModel) {
    int n = 0;
    for (int row : destinations(aModel)) {
        if (onScreen(aModel, row)) ++n;
    }
    return n;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
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
        Model model(targets, groups, QStringLiteral("telegram.sticker"));
        check(model.rowCount() > 0, "the picker has a destination list");
        if (model.rowCount() == 0) return 1;
        const QString shown = model.idOf(model.currentRow());
        check(shown == QStringLiteral("telegram.sticker"),
              QStringLiteral("the remembered destination is selected, not the first row "
                             "(selected: %1)").arg(shown.isEmpty() ? QStringLiteral("nothing") : shown));
        check(onScreen(model, model.currentRow()),
              "and it is on screen: the platform holding it is open");
    }

    // 2. The platforms are CLOSED, which is the whole point of a tree: the first
    //    level is the platform, not fifty-five destinations. The one exception on
    //    this run is the section holding the remembered destination, above.
    {
        Model model(targets, groups, QString());
        const QList<int> heads = sections(model);
        check(heads.size() >= 10,
              QStringLiteral("the platforms are sections (%1 of them)").arg(heads.size()));
        int open = 0, nested = 0;
        for (int head : heads) {
            if (model.isExpanded(head)) ++open;
            if (model.childCount(head) > 0) ++nested;
        }
        check(open == 0, QStringLiteral("every platform starts closed (%1 open)").arg(open));
        check(nested == heads.size(), "every platform has destinations inside it");
        check(countOnScreen(model) < targets.size(),
              QStringLiteral("so the picker does not open on the whole flat list (%1 of %2 "
                             "destinations on screen)")
                  .arg(countOnScreen(model)).arg(targets.size()));
        check(!model.isSection(model.currentRow()) && !model.idOf(model.currentRow()).isEmpty(),
              QStringLiteral("with no remembered destination a destination is selected (%1)")
                  .arg(model.idOf(model.currentRow())));
        check(onScreen(model, model.currentRow()), "and it is on screen");
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

        Model model(targets, groups, QString());

        QSet<QString> gotNested;
        for (int head : sections(model)) {
            QString key;
            for (const ctrl::ExportGroup& g : groups) {
                if (g.title == model.labelOf(head) && nested.contains(g.id)) key = g.id;
            }
            check(!key.isEmpty(),
                  QStringLiteral("the section '%1' is one that should nest").arg(model.labelOf(head)));
            if (!key.isEmpty()) gotNested.insert(key);
            check(model.childCount(head) > 1,
                  QStringLiteral("'%1' is a section because there is a choice inside it (%2)")
                      .arg(model.labelOf(head)).arg(model.childCount(head)));
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
            for (int item : destinations(model)) {
                if (model.idOf(item) == t.id && model.sectionOf(item) < 0) found = true;
            }
            if (!found) {
                check(false, QStringLiteral("%1 is listed directly, not under a section").arg(t.id));
            }
        }
        check(flat > 10, QStringLiteral("the single-destination platforms are listed directly (%1)").arg(flat));
        check(destinations(model).size() == targets.size(),
              QStringLiteral("every destination is in the list once (%1 of %2)")
                  .arg(destinations(model).size()).arg(targets.size()));

        // C18 (contract line 97): "A destination with no children is not indented as
        // though it had a parent." The row's LEVEL is the model's one authoritative
        // statement of that - 0 = listed directly (a section, or a destination whose
        // platform has no section row above it), 1 = nested under a section - and it
        // is what the delegate draws every horizontal lane from. `isSection` cannot
        // answer it: a platform with a single destination, or a group whose rows are
        // not variants of one platform, is appended with NO section above it, so such
        // a destination is top-level (and was, before this role existed, drawn with
        // the CHILD offsets 38/62 instead of 24/48 - the reported bug).
        //
        // Read the role by the NAME the scene binds, not by enum: that is the
        // contract with QML, and it lets this same test compile against the tree
        // before the fix (where the failure is "no such role" plus wrong geometry).
        const int levelRole = [&model] {
            const QHash<int, QByteArray> names = model.roleNames();
            for (auto it = names.cbegin(); it != names.cend(); ++it) {
                if (it.value() == "level") return it.key();
            }
            return -1;
        }();
        check(levelRole >= 0, "C18: the model exposes a 'level' role for the scene to bind");
        auto levelOf = [&](int aRow) {
            return levelRole < 0 ? -1 : model.data(model.index(aRow, 0), levelRole).toInt();
        };

        int flatRow = -1;   // a childless TOP-LEVEL destination: no section above it
        int nestedRow = -1; // a destination that really is inside a section
        for (int i = 0; i < model.rowCount(); ++i) {
            if (model.isSection(i) || model.idOf(i).isEmpty()) continue;
            if (model.sectionOf(i) < 0) {
                if (flatRow < 0) flatRow = i;
            } else if (nestedRow < 0) {
                nestedRow = i;
            }
        }
        check(flatRow >= 0, "precondition (C18): a childless top-level destination exists to measure");
        check(nestedRow >= 0, "precondition (C18): a destination under a section exists to measure");
        if (flatRow >= 0 && nestedRow >= 0) {
            check(model.sectionOf(flatRow) < 0 && model.sectionOf(nestedRow) >= 0,
                  "C18: the two measured rows really are one top-level and one nested (sectionOf)");
            check(levelOf(flatRow) == 0,
                  QStringLiteral("C18: a CHILDLESS top-level destination is level 0, not 1 (%1)")
                      .arg(levelOf(flatRow)));
            check(levelOf(nestedRow) == 1,
                  QStringLiteral("C18: a destination under a section is level 1 (%1)")
                      .arg(levelOf(nestedRow)));
            const int flatSection = model.sectionOf(flatRow);
            const int nestedSection = model.sectionOf(nestedRow);
            check(flatSection < 0 && levelOf(nestedSection) == 0,
                  "C18: the section a child was measured against is itself level 0");
            check(model.childCount(flatRow) == 0 && model.childCount(nestedRow) == 0,
                  "C18: childCount keeps its meaning (a section's children; 0 for a destination)");
            check(model.childCount(nestedSection) > 0,
                  "C18: the measured section still reports the children it has");
        }
        // Both directions, every row: level is exactly "was appended under a section",
        // and a section is always level 0. A missing role reads -1 and fails every row.
        int wrongLevel = 0;
        for (int i = 0; i < model.rowCount(); ++i) {
            const int want = model.sectionOf(i) < 0 ? 0 : 1;
            if (levelOf(i) != want) ++wrongLevel;
        }
        check(wrongLevel == 0,
              QStringLiteral("C18: every row's level states its own parent fact (%1 wrong)").arg(wrongLevel));
    }

    // 4. A platform is not an answer, and (the reported bug) it is not SELECTABLE:
    //    the keyboard cannot land on it as a current item, so "Use this destination"
    //    can never be asked what a platform means. Enter on it opens it rather than
    //    exporting something the user never chose.
    {
        Model model(targets, groups, QString());
        check(!model.okText().isEmpty() && !model.cancelText().isEmpty(),
              "the picker has its buttons");
        const int telegram = sectionNamed(model, QStringLiteral("Telegram"));
        check(telegram >= 0, "the Telegram platform is a section");
        if (telegram < 0) return 1;

        int accepted = 0;
        const QMetaObject::Connection conn = QObject::connect(
            &model, &Model::accepted, [&accepted]() { ++accepted; });

        check(!(model.flags(model.index(telegram, 0)) & Qt::ItemIsSelectable),
              "a platform row is not selectable");
        const int beforeSelect = model.currentRow();
        model.select(telegram);
        check(model.currentRow() == beforeSelect && !model.isSection(model.currentRow()),
              "and the selection never lands on a platform");

        model.activate(telegram);
        check(model.chosenId().isEmpty(),
              QStringLiteral("the default button does not answer with a platform (%1)")
                  .arg(model.chosenId()));
        check(model.isExpanded(telegram), "it opens the platform instead");
        check(accepted == 0, "and a platform is never accepted as the answer");
        model.activate(telegram);
        check(!model.isExpanded(telegram), "and pressing it again closes it");

        // On a destination it is the answer, and the answer is a row of the table.
        int sticker = -1;
        for (int c : children(model, telegram)) {
            if (model.idOf(c) == QLatin1String("telegram.sticker")) sticker = c;
        }
        check(sticker >= 0, "telegram.sticker is inside the Telegram section");
        if (sticker >= 0) {
            if (!model.isExpanded(telegram)) model.toggleSection(telegram);
            model.select(sticker);
            model.activateCurrent();
            const QString chosen = model.chosenId();
            check(chosen == QLatin1String("telegram.sticker"),
                  QStringLiteral("the default button answers with the selected destination (%1)")
                      .arg(chosen));
            check(ctrl::ExportTargets::find(targets, chosen).id == chosen,
                  "and it is a row of the table");
        }
        QObject::disconnect(conn);
    }

    // 5. Typing narrows, OPENS what it found, and selects a destination rather than
    //    the platform above it: "wec" then Enter is the fast path, and it has to be
    //    two keystrokes rather than three.
    {
        Model model(targets, groups, QString());
        check(model.rowCount() > 0, "the picker can be typed at");
        if (model.rowCount() == 0) return 1;

        model.setQuery(QStringLiteral("wec"));
        const QString shown = model.idOf(model.currentRow());
        check(!model.isSection(model.currentRow()),
              "a search selects a destination, never the platform above it");
        check(shown.contains(QStringLiteral("wechat")),
              QStringLiteral("and selects one of its matches (%1)").arg(shown));
        check(onScreen(model, model.currentRow()), "which is on screen");
        check(model.visibleRowCount() >= 1 && model.visibleRowCount() < targets.size(),
              QStringLiteral("a search narrows the list (%1 of %2 destinations)")
                  .arg(model.visibleRowCount()).arg(targets.size()));

        // A platform with one destination is not a section, so its match selects the
        // destination itself: there is nothing to open.
        model.setQuery(QStringLiteral("youtube"));
        check(model.idOf(model.currentRow()) == QLatin1String("youtube.video"),
              QStringLiteral("a single-destination platform matches directly (%1)")
                  .arg(model.idOf(model.currentRow())));

        // A platform name keeps the whole platform, and the results are legible
        // because they arrive under the platform that matched.
        model.setQuery(QStringLiteral("instagram"));
        int shownRows = 0;
        for (int item : destinations(model)) {
            if (onScreen(model, item) && model.haystackOf(item).contains(QStringLiteral("instagram"))) {
                ++shownRows;
            }
        }
        check(shownRows >= 2, QStringLiteral("a platform match shows its destinations (%1)").arg(shownRows));
        const int instagram = sectionNamed(model, QStringLiteral("Instagram"));
        check(instagram >= 0 && model.isVisible(instagram) && model.isExpanded(instagram),
              "the platform that matched is open");
        const int telegram = sectionNamed(model, QStringLiteral("Telegram"));
        check(telegram >= 0 && !model.isVisible(telegram), "a platform with no match is not shown");

        model.setQuery(QString());
        check(model.visibleRowCount() == countOnScreen(model),
              "visibleRowCount counts what is on screen");
        bool anyHidden = false;
        for (int item : destinations(model)) {
            if (!model.isVisible(item)) anyHidden = true;
        }
        check(!anyHidden, "clearing the search restores every destination");
        // Clearing is not "collapse everything again": a platform the user opened, or
        // one a search opened, stays open. It is not a reset button.
        check(model.isExpanded(instagram), "and leaves the platforms it opened open");
    }

    // 6. Enter on a platform is the only thing it can be, never a jump into it, and
    //    the arrows only ever move between things that are on screen.
    {
        Model model(targets, groups, QString());
        const int telegram = sectionNamed(model, QStringLiteral("Telegram"));
        if (telegram < 0) return 1;

        const int before = model.currentRow();
        model.select(telegram);
        model.activate(telegram);
        check(model.chosenId().isEmpty() && model.currentRow() == before,
              "opening a platform does not move the highlight into it");

        if (!model.isExpanded(telegram)) model.toggleSection(telegram);
        const QList<int> kids = children(model, telegram);
        if (kids.isEmpty()) return 1;
        model.select(kids.first());
        int offscreen = 0, onSection = 0;
        for (int i = 0; i < 120; ++i) {
            model.step(1);
            if (model.currentRow() < 0 || !onScreen(model, model.currentRow())) ++offscreen;
            if (model.isSection(model.currentRow())) ++onSection;
        }
        check(offscreen == 0,
              QStringLiteral("walking down never lands on something that is not on screen "
                             "(%1 times)").arg(offscreen));
        check(onSection == 0,
              QStringLiteral("and never lands on a platform (%1 times)").arg(onSection));
        check(!model.idOf(model.currentRow()).isEmpty(),
              QStringLiteral("and the walk reaches destinations (%1)").arg(model.idOf(model.currentRow())));
    }

    // 7. The dialog in a language that is not English, which is the one question a
    //    catalog full of translations cannot answer by itself.
    //
    //    The strings are looked up in the "gui::DestinationPicker" context, which is
    //    the context the widget class filled and the one the shipped catalogs
    //    translated. Moving them to the QML module's own context would have left
    //    every existing translation under a context nothing reads, so the model asks
    //    for that context explicitly and the dialog sets its title from the model:
    //    the string the test reads is the string the window shows.
    //
    //    So this asks the MODEL the scene binds to, and it asks it with the shipping
    //    .qm installed: a title that comes back Spanish proves the context the
    //    picker actually asks for is the one translators filled.
    {
        QTranslator translator;
        const QString qmDir = QStringLiteral(AE_I18N_QM_DIR);
        const bool loaded = translator.load(QStringLiteral("translation_es"), qmDir);
        check(loaded, QStringLiteral("the Spanish catalog loads from the build (%1)").arg(qmDir));
        if (loaded) {
            QApplication::installTranslator(&translator);
            Model model(targets, groups, QString());
            const QString title = model.windowTitleText();
            check(title != QStringLiteral("Where are you posting this?"),
                  QStringLiteral("the picker title is translated, not the English source (%1)")
                      .arg(title));
            const QString ok = model.okText();
            check(ok != QStringLiteral("Use this destination") && !ok.isEmpty(),
                  QStringLiteral("and so is its confirm button (%1)").arg(ok));
            // The export dialog answers through its own context, and it was unreachable
            // for a different reason: a hand rolled tr() that forwarded to "ExportDiag",
            // a context no catalog has. Both are asked here so the pair cannot regress
            // together unnoticed.
            //
            // The context asked has to be one the code STILL USES. It was
            // "ExportWidgetUI" - the widget form's context - which this check kept
            // passing against long after exportdiag.{h,cpp} was deleted, until lupdate
            // marked it vanished and lrelease dropped it from the .qm. The Qt Quick
            // form's C++ strings live in gui::ExportViewModel, so that is the context
            // a translator actually fills now.
            const QString exported =
                QCoreApplication::translate("gui::ExportViewModel", "Export with transparency");
            check(exported != QStringLiteral("Export with transparency"),
                  QStringLiteral("the export dialog's own context answers in Spanish (%1)")
                      .arg(exported));
            QApplication::removeTranslator(&translator);
        }
    }

    QTextStream(stdout) << (failures == 0 ? "=== destination_picker: OK ===\n"
                                          : QStringLiteral("=== destination_picker: %1 FAILED ===\n").arg(failures));
    return failures == 0 ? 0 : 1;
}
