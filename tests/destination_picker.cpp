// The destination picker, offscreen: what the user actually gets.
//
// Every question about this list is a question about a person, and two of the
// answers are already in the application: MainWindow passes the REMEMBERED
// destination in (QSettings "export_destination"), and the picker's own header says
// that id "preselects a destination". It did not, once: the constructor set the
// current item and then the filter replaced it with the first row, so the picker
// opened on "Video" (the first row, generic.video) and Enter exported there whatever
// the user chose last time. A remembered setting the screen contradicts is worse than
// no remembered setting.
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

// Every destination row, on screen or not. A row that is neither a section nor a
// destination exists now - the structural separator - and it is excluded here by the
// thing that makes it not a destination: it has NO ID. That is asserted on its own
// below rather than assumed from this filter.
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

// The row holding a destination id, or -1. The search assertions name destinations
// by id, so this is how they ask "is THIS one on screen".
static int rowOfId(const Model& aModel, const QString& aId) {
    for (int row : destinations(aModel)) {
        if (aModel.idOf(row) == aId) return row;
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
    //    the keyboard cannot land on it as a current item, so "Continue"
    //    can never be asked what a platform means. Enter on it opens it rather than
    //    exporting something the user never chose.
    {
        Model model(targets, groups, QString());
        check(!model.okText().isEmpty() && !model.cancelText().isEmpty(),
              "the picker has its buttons");
        // THE APP'S OWN DIALOG VOICE (decided 2026-10-08): a noun-phrase title and an
        // action verb on the confirm button, not a question aimed at the user. Asserted
        // HERE, before the translated block below installs a catalog, because this is
        // the only place the ENGLISH source is what comes back.
        check(model.windowTitleText() == QStringLiteral("Destination")
                  && model.okText() == QStringLiteral("Continue"),
              QStringLiteral("the picker speaks the app's convention (%1 / %2)")
                  .arg(model.windowTitleText(), model.okText()));
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

        // ---- fuzzy search (the shape of query the substring test could not answer) ----
        // The four cases below FAIL on the pre-change code, whose whole search was
        // `haystack.contains(query)`: "telegram sticker" (two words, never contiguous),
        // "sticker telegram" (the same words the other way round), "telegrm" (a typo)
        // and "discord emoji" (two words whose winner is decided by the SCORE, not by
        // which row a substring test happened to find first) all typed something no
        // haystack contains verbatim, so the old code answered none of them. The rest
        // are INVARIANTS that must keep holding: an id typed in full still selects its
        // row, a query that matches nothing still shows nothing, and a two-character
        // query still matches only contiguously (as a word or inside one), never as
        // letters gathered from across a word.
        //
        // One part of the SCORE reads the haystack's SHAPE: the phrase bonus is
        // FIELD-LOCAL, so it fires only for words that are adjacent inside one field
        // (haystackFor in DestinationPickerModel.cpp separates the row's fields with
        // 0x1f for exactly this). The two telegram cases below turn on that: a bonus
        // which crossed a field boundary would reward a coincidence of the layout -
        // the group title sitting next to the family key - as though the user had
        // typed the row's own name.

        // Two words the user typed adjacently, and they ARE adjacent inside one field.
        // The phrase bonus is field-local: telegram.sticker's label "Telegram sticker"
        // (and its id, whose words join the same way) carries "telegramsticker" inside
        // a single field, so it earns the bonus. telegram.emoji does NOT: its
        // "Telegram" is the group title field and its "sticker" is the family key
        // field, so the two words are adjacent only if that boundary is ignored. Both
        // rows score the two tokens as whole words alike (1000 + 8L each), so the
        // bonus is the whole difference and it selects the row whose OWN label is the
        // phrase - no tie. telegram.emoji still MATCHES (both its tokens match, so it
        // is revealed); it just scores 400 lower.
        model.setQuery(QStringLiteral("telegram sticker"));
        check(model.idOf(model.currentRow()) == QLatin1String("telegram.sticker"),
              QStringLiteral("'telegram sticker' (never a contiguous substring) selects the row "
                             "whose own label is that phrase: telegram.sticker, by the field-local "
                             "phrase bonus (telegram.emoji's 'Telegram' and 'sticker' sit in "
                             "different fields, so it earns no bonus and loses by 400) (%1)")
                  .arg(model.idOf(model.currentRow())));
        check(onScreen(model, model.currentRow()),
              "'telegram sticker': the winning match is on screen");
        const int telegramEmoji = rowOfId(model, QStringLiteral("telegram.emoji"));
        check(telegramEmoji >= 0 && model.isVisible(telegramEmoji),
              "'telegram sticker' still reveals telegram.emoji (it matches both words too, 400 lower)");

        // The words in the other order. The phrase bonus is field-local, and NO field
        // of either Telegram sticker row carries "stickertelegram" (that would need a
        // label reading "sticker telegram"; both rows read the words the other way
        // round), so neither row earns it. With no bonus the token sums are EQUAL -
        // each row scores "sticker" and "telegram" as whole words - so this is a tie,
        // and the tie keeps the EARLIER row, which is row order and not the table.
        // Assert the platform the tie lands in, not one id.
        model.setQuery(QStringLiteral("sticker telegram"));
        const QString reversedMatch = model.idOf(model.currentRow());
        check(!model.isSection(model.currentRow()) && onScreen(model, model.currentRow())
                  && reversedMatch.contains(QLatin1String("telegram")),
              QStringLiteral("'sticker telegram' (words reversed) selects a Telegram destination: "
                             "the token sums tie (both rows score both words as whole words, and "
                             "the field-local phrase bonus fires for neither), so the earlier row "
                             "wins (%1)").arg(reversedMatch));

        // A typo. Several Telegram rows carry the word "telegram" and all score the same
        // subsequence, so the exact row is decided by row order, not by the table:
        // assert the platform (the typo found a Telegram destination), not one id.
        model.setQuery(QStringLiteral("telegrm"));
        const QString typoMatch = model.idOf(model.currentRow());
        check(!model.isSection(model.currentRow()) && onScreen(model, model.currentRow())
                  && typoMatch.contains(QLatin1String("telegram")),
              QStringLiteral("'telegrm' (a typo) still finds an on-screen Telegram "
                             "destination (%1)").arg(typoMatch));

        // THE BEST SCORE WINS, asserted rather than assumed. "discord emoji" matches
        // exactly two rows - discord.emoji and discord.sticker, which share the word
        // "discord" - and their token sums are identical: discord.sticker carries an
        // "emoji" whole word too, from its family title "Stickers and emoji". Only the
        // PHRASE BONUS separates them: discord.emoji's joined words start
        // "discordemoji" (its label), discord.sticker's never do ("discordsticker...",
        // "...stickersandemoji"). So the row NAMED by the multi-word query is the one
        // selected, by 400.
        model.setQuery(QStringLiteral("discord emoji"));
        check(model.idOf(model.currentRow()) == QLatin1String("discord.emoji"),
              QStringLiteral("'discord emoji' selects discord.emoji, the best-scoring of the two "
                             "rows it matches (discord.sticker shares the word 'discord' and also "
                             "carries 'emoji', from its family title) (%1)")
                  .arg(model.idOf(model.currentRow())));
        const int discordSticker = rowOfId(model, QStringLiteral("discord.sticker"));
        check(discordSticker >= 0 && model.isVisible(discordSticker),
              "'discord emoji' still reveals the other discord row (discord.sticker)");

        // An id is still an id: "generic" and "video" are words of the row's own
        // haystack, and generic.video is a surviving id whose text is stable, which is
        // what the field matched before the change too.
        model.setQuery(QStringLiteral("generic.video"));
        check(model.idOf(model.currentRow()) == QLatin1String("generic.video"),
              QStringLiteral("typing the id generic.video selects that destination (%1)")
                  .arg(model.idOf(model.currentRow())));

        // Nothing matches: no destination is on screen (the list collapses rather than
        // falling back to the whole table).
        model.setQuery(QStringLiteral("zzqqjj"));
        check(model.visibleRowCount() == 0,
              QStringLiteral("'zzqqjj' matches nothing (%1 destinations on screen)")
                  .arg(model.visibleRowCount()));

        // The manual path. The picker's own bottom button ("My platform isn't listed...")
        // was removed (decided 2026-10-08), so the model's fallback is now the ONLY route
        // into manual mode: activateCurrent() on a query that matches nothing accepts with
        // an EMPTY id, and that empty id IS manual mode (the export form applies no
        // destination's rules). Both halves are asserted, and non-vacuously: a real match
        // first answers with its id, so the empty id below is the fallback and not the
        // model's untouched state.
        model.setQuery(QStringLiteral("generic.video"));
        model.activateCurrent();
        check(!model.chosenId().isEmpty(),
              "precondition: a query that matches answers with its id");
        model.setQuery(QStringLiteral("zzqqjj"));
        int manualAccepted = 0;
        const QMetaObject::Connection manualConn = QObject::connect(
            &model, &Model::accepted, [&manualAccepted]() { ++manualAccepted; });
        model.activateCurrent();
        check(manualAccepted == 1 && model.chosenId().isEmpty(),
              QStringLiteral("a query that matches nothing + Enter is manual mode - the only "
                             "route into it now that the picker's own button is gone: accepted "
                             "once, chosenId empty (%1)").arg(model.chosenId()));
        QObject::disconnect(manualConn);

        // Two characters must not match by gathering letters from across a word. The
        // Facebook video row's words carry no 'a' next to an 'e' (label "Facebook
        // (video)", id post.facebook.video, group Facebook, family post, title "Post to
        // a platform", container mp4, artifact words "video clip animation"), so "ae"
        // must leave it hidden.
        model.setQuery(QStringLiteral("ae"));
        const int facebookVideo = rowOfId(model, QStringLiteral("post.facebook.video"));
        check(facebookVideo >= 0, "precondition: the Facebook video destination is in the list");
        if (facebookVideo >= 0) {
            check(!model.haystackOf(facebookVideo).contains(QLatin1String("ae")),
                  "precondition: no word of post.facebook.video carries the letters 'ae'");
            check(!model.isVisible(facebookVideo),
                  QStringLiteral("'ae' does not match post.facebook.video by scattering "
                                 "letters across its words"));
        }

        // ...while two characters that ARE a word still match: "qq" is a whole word in
        // the QQ rows (label "QQ" / "QQ sticker", id chat.qq.video / qq.sticker), so the
        // query finds one of them. Which one is row order, not the table, so assert the
        // platform.
        model.setQuery(QStringLiteral("qq"));
        const QString qqMatch = model.idOf(model.currentRow());
        check(!model.isSection(model.currentRow()) && onScreen(model, model.currentRow())
                  && qqMatch.contains(QLatin1String("qq")),
              QStringLiteral("'qq' selects an on-screen QQ destination whose word is "
                             "exactly 'qq' (%1)").arg(qqMatch));

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

    // 7. THE STRUCTURAL SEPARATOR (decided 2026-10-08): the table asks for a rule after
    //    the `general` block (`separatorAfter="true"`), and the model inserts it as a
    //    row of its OWN KIND - not a section, not a destination, not an answer.
    //
    //    The three kinds differ in exactly three ways, and all three are asserted here:
    //    WHERE the row is (immediately after the four general rows, whatever the `order`
    //    sort did with them, and above every platform section), WHAT it is
    //    (isSeparator(), and none of the other predicates), and what it is NOT -
    //    selectable, current, counted, in the keyboard path, or affected by expansion
    //    state. The drawing rule the model owns is the last part: on screen with an
    //    EMPTY query, hidden under a query. (That the SCENE draws the hairline, and that
    //    the row cannot be hovered or clicked there, is a geometry question - asserted
    //    on the built scene in tests/picker_keyboard.cpp.)
    //
    //    NOTE ON THE NUMBERS: this block was APPENDED after the keyboard block, so the
    //    language block below kept its own numbering and moved to 8. No existing
    //    assertion in this file had to change for the new row: the three helpers that
    //    walk rows (destinations(), sections(), and the level loop) select by PREDICATE
    //    - an id, isSection() - and a row with no id and no section flag is excluded by
    //    the same facts that make it neither of those things. That is why they are
    //    asserted here explicitly rather than discovered by a broken count.
    {
        // The row kind is DATA, so the test starts where the data does.
        int requested = 0;
        QString requestedOn;
        for (const ctrl::ExportGroup& g : groups) {
            if (g.separatorAfter) { ++requested; requestedOn = g.id; }
        }
        check(requested == 1 && requestedOn == QLatin1String("general"),
              QStringLiteral("the table asks for one separator and it is after the general "
                             "block (%1 requested, on '%2')")
                  .arg(requested).arg(requestedOn));

        Model model(targets, groups, QString());
        QList<int> separators;
        for (int i = 0; i < model.rowCount(); ++i) {
            if (model.isSeparator(i)) separators.push_back(i);
        }
        check(separators.size() == 1,
              QStringLiteral("the model inserts it exactly once (%1)").arg(separators.size()));
        if (separators.size() != 1) return 1;
        const int sep = separators.first();

        // WHERE IT IS. The general block is the four rows the table files under
        // `general` - the ones carrying the explicit `order` - and the rule is the row
        // directly below the last of them. Read as facts about those ROWS rather than as
        // "index 4", so a fifth general row moves the assertion with it.
        QSet<QString> generalIds;
        for (const ctrl::ExportTarget& t : targets) {
            if (t.group == QLatin1String("general")) generalIds.insert(t.id);
        }
        QList<int> generalRows;
        for (int row : destinations(model)) {
            if (generalIds.contains(model.idOf(row))) generalRows.push_back(row);
        }
        check(generalRows.size() == 4,
              QStringLiteral("the general block is four rows (%1)").arg(generalRows.size()));
        check(generalRows == QList<int>{0, 1, 2, 3},
              QStringLiteral("and it is the top of the list, in the `order` the rows carry "
                             "(rows %1..%2)")
                  .arg(generalRows.isEmpty() ? -1 : generalRows.first())
                  .arg(generalRows.isEmpty() ? -1 : generalRows.last()));
        check(sep == generalRows.last() + 1,
              QStringLiteral("the separator is immediately after the last of them (separator %1, "
                             "last general row %2)")
                  .arg(sep).arg(generalRows.isEmpty() ? -1 : generalRows.last()));
        check(model.sectionOf(sep) < 0,
              "and it is not inside any section (it is a sibling of the blocks, not a child)");
        const QList<int> heads = sections(model);
        check(!heads.isEmpty() && heads.first() > sep,
              QStringLiteral("with every platform section below it (first section %1, separator %2)")
                  .arg(heads.isEmpty() ? -1 : heads.first()).arg(sep));

        // WHAT IT IS, and what it is not. No id, no label, no tooltip, no haystack, no
        // children: none of the things a row can be asked about, so nothing downstream
        // can mistake it for a destination ("which destination is this" has no answer it
        // could give) or for a section (there is nothing to disclose).
        check(!model.isSection(sep) && !model.isSeparator(-1) && !model.isSeparator(model.rowCount()),
              "isSeparator() is the separator's own predicate (and refuses an invalid row)");
        check(model.idOf(sep).isEmpty() && model.labelOf(sep).isEmpty(),
              "it has no id and no label");
        check(model.haystackOf(sep).isEmpty(),
              "and no haystack, so no query can match it");
        check(model.data(model.index(sep, 0), gui::DestinationPickerModel::TooltipRole).toString().isEmpty(),
              "and no tooltip to hover for");
        check(model.data(model.index(sep, 0), gui::DestinationPickerModel::IsSeparatorRole).toBool(),
              "and the role the scene binds ('isSeparator') reports it");
        check(model.childCount(sep) == 0 && !model.isExpanded(sep),
              "it has no children and no expansion state");

        // NOT SELECTABLE AND NEVER CURRENT - the same two facts the section row is held
        // to, for the same reason: nothing may land on a row that is not an answer.
        check(!(model.flags(model.index(sep, 0)) & Qt::ItemIsSelectable),
              "the separator is not selectable");
        const int beforeSelect = model.currentRow();
        model.select(sep);
        check(model.currentRow() == beforeSelect && !model.isSeparator(model.currentRow()),
              "and the selection never lands on it");

        // Enter on it does NOTHING - not even the manual escape hatch a row that matched
        // nothing falls back to, which would end the dialog on a keystroke aimed at the
        // gap between two blocks.
        int accepted = 0;
        const QMetaObject::Connection conn = QObject::connect(
            &model, &Model::accepted, [&accepted]() { ++accepted; });
        const QString chosenBefore = model.chosenId();
        model.activate(sep);
        check(accepted == 0 && model.chosenId() == chosenBefore,
              QStringLiteral("activate() on the separator is refused: no answer, no fallback "
                             "into manual mode (accepted %1 times, id '%2')")
                  .arg(accepted).arg(model.chosenId()));

        // The kernel of the keyboard contract: step() moves among DESTINATIONS, so the
        // rule is stepped over rather than landed on. The neighbours are named by id, so
        // the assertion is about where the walk went and not about row numbers.
        const int bilibili = rowOfId(model, QStringLiteral("post.bilibili.video"));
        check(bilibili > sep,
              "precondition: the flat destination listed right below the rule is in the list");
        model.select(sep - 1);
        check(model.currentRow() == sep - 1,
              "precondition: the selection is on the last general row, directly above the rule");
        model.step(1);
        check(model.currentRow() == bilibili,
              QStringLiteral("step(1) steps OVER the separator to the next destination (%1, "
                             "expected %2, separator %3)")
                  .arg(model.currentRow()).arg(bilibili).arg(sep));
        model.step(-1);
        check(model.currentRow() == sep - 1, "and step(-1) steps back over it");
        int landedOnSeparator = 0;
        model.select(0);
        for (int i = 0; i < model.rowCount() * 2; ++i) {
            model.step(1);
            if (model.isSeparator(model.currentRow())) ++landedOnSeparator;
        }
        check(landedOnSeparator == 0,
              QStringLiteral("and no walk of the whole list ever lands on it (%1 times)")
                  .arg(landedOnSeparator));

        // COUNTS. destinationCount() is what the dialog's broken-table check reads and
        // visibleRowCount() is what the form's own label reads, so a decorative row
        // leaking into either would be a lie about how many destinations there are.
        check(model.destinationCount() == targets.size(),
              QStringLiteral("destinationCount() does not count it (%1, table has %2)")
                  .arg(model.destinationCount()).arg(targets.size()));
        check(model.visibleRowCount() == countOnScreen(model),
              QStringLiteral("visibleRowCount() does not count it either: it equals the number of "
                             "destinations on screen (%1 vs %2)")
                  .arg(model.visibleRowCount()).arg(countOnScreen(model)));

        // DRAWING, the one part of the rule the model owns: shown while the list shows
        // its structure (an empty query), hidden the moment there is a query - a
        // filtered list is a list of MATCHES, and a rule about the structure says
        // nothing about them.
        check(model.isVisible(sep) && model.isOnScreen(sep),
              "with no query the rule is drawn (the list is showing its structure)");
        model.setQuery(QStringLiteral("telegram"));
        check(!model.isVisible(sep) && !model.isOnScreen(sep),
              "a query hides it: a filtered list is a list of matches");
        check(model.visibleRowCount() == countOnScreen(model),
              "and the counts still exclude it under a query");
        model.setQuery(QString());
        check(model.isOnScreen(sep), "clearing the query draws it again");

        // The manual fallback is not reachable THROUGH it, which is the difference
        // between "this row is not an answer" and "this row is not here": on a query that
        // matches nothing, Enter still has to be the escape hatch, but Enter on the rule
        // is nothing at all.
        model.setQuery(QStringLiteral("zzqqjj"));
        const int acceptedBefore = accepted;
        model.activate(sep);
        check(accepted == acceptedBefore,
              "a query that matches nothing + Enter on the RULE is not the manual fallback");
        model.setQuery(QString());
        QObject::disconnect(conn);

        // EXPANSION STATE, the model's only other persisted fact: the rule is not a
        // section, so a toggle is refused, it never appears among the expanded ids (which
        // are sections' stable keys), and restoring a state leaves it exactly as it was.
        model.toggleSection(sep);
        check(!model.isExpanded(sep), "toggleSection() refuses the separator");
        check(!model.expandedIds().contains(QString()),
              "expandedIds() carries no entry for a row with no id");
        model.applyExpandedIds(QStringList() << QStringLiteral("telegram"));
        check(model.isOnScreen(sep),
              "applyExpandedIds() leaves it alone (there is no expansion of its own to restore)");

        // AND THE INSERTION IS INDEPENDENT OF THE RANK SORT. The real table cannot show
        // this: its general rows are in `order` 1..4 in the FILE too, so file order and
        // rank order agree and either implementation passes. So the question is asked of
        // a synthetic table whose file order is deliberately the reverse of its rank
        // order - a group with `separatorAfter` whose second-in-file row is the ranked
        // one - and the rule must still land AFTER both of them (a rule inserted at the
        // group's file position, or inside the loop that appends rows, would land
        // between them).
        {
            ctrl::ExportGroup flat;
            flat.id = QStringLiteral("g");
            flat.title = QStringLiteral("G");
            flat.variants = false;
            flat.separatorAfter = true;
            ctrl::ExportGroup platform;
            platform.id = QStringLiteral("p");
            platform.title = QStringLiteral("P"); // one row, so it is listed flat too

            auto makeRow = [](const QString& aId, const QString& aGroup, const QString& aLabel,
                              int aOrder) {
                ctrl::ExportTarget t;
                t.id = aId;
                t.label = aLabel;
                t.group = aGroup;
                t.order = aOrder;
                return t;
            };
            const QList<ctrl::ExportTarget> table = {
                makeRow(QStringLiteral("g.second"), QStringLiteral("g"), QStringLiteral("second"), 1),
                makeRow(QStringLiteral("g.first"), QStringLiteral("g"), QStringLiteral("first"), 2),
                makeRow(QStringLiteral("p.only"), QStringLiteral("p"), QStringLiteral("only"), 0),
            };
            Model synthetic(table, QList<ctrl::ExportGroup>{flat, platform}, QString());
            check(synthetic.rowCount() == 4,
                  QStringLiteral("a synthetic table of three destinations and one rule is four rows "
                                 "(%1)").arg(synthetic.rowCount()));
            check(synthetic.idOf(0) == QLatin1String("g.second")
                      && synthetic.idOf(1) == QLatin1String("g.first"),
                  QStringLiteral("precondition: the group's rows are listed by RANK, not by file "
                                 "order (%1 then %2)")
                      .arg(synthetic.idOf(0)).arg(synthetic.idOf(1)));
            check(synthetic.isSeparator(2) && synthetic.idOf(3) == QLatin1String("p.only"),
                  QStringLiteral("the rule follows the group's rows whatever the rank sort did, and "
                                 "the next group starts after it (row 2 is the separator: %1, row 3 "
                                 "is '%2')")
                      .arg(synthetic.isSeparator(2) ? 1 : 0).arg(synthetic.idOf(3)));
        }
    }

    // 8. The dialog in a language that is not English, which is the one question a
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
            // The copy itself is pinned in block 4 above, where no catalog is installed
            // (the English source is the only thing that comes back there). What THIS
            // block exists for is the MECHANISM: that the context the picker asks is the
            // one translators FILLED. It is asserted on a string that has been in that
            // context all along, so it does not go quiet the next time the copy moves.
            const QString field = model.searchAccessibleName();
            check(field != QStringLiteral("Search destinations") && !field.isEmpty(),
                  QStringLiteral("and the picker's own context reaches the catalog (%1)")
                      .arg(field));
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
