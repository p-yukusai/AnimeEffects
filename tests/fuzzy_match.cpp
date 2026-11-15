// Fuzzy matching for the destination picker's search field (src/gui/qml/FuzzyMatch.h).
//
// The picker's search was `haystack.contains(query)`, a plain case-insensitive
// substring test, under which "telegram sticker" (two words, never contiguous),
// "telegrm" (a typo) and "discordemote" (no space) all return nothing. The header
// replaces that with a scored match. This file pins the CONTRACT: every rule that
// must fire, the two that must not, and the orderings between rules. Orderings are
// asserted rather than the literal constants wherever a rule's WEIGHT is the thing
// that matters, and each comparison prints the two scores so a run that could not
// observe the ordering fails loudly instead of passing quietly.
//
// The load-bearing rule is a NEGATIVE: no rule may match a subsequence that spans
// several words. Every capped destination's haystack ends in " capped size limit"
// (DestinationPickerModel::haystackFor), so a cross-word subsequence would let a
// 3-letter query match nearly the whole table -- the picker would stop narrowing at
// all (tests/destination_picker.cpp asserts visibleRowCount() < targets.size() for
// "wec"). If that test ever starts failing, look here first.
//
// A haystack may be several fields joined with gui::fuzzy::kFieldSeparator (U+001F),
// the way DestinationPickerModel::haystackFor builds one. A token matches in ANY
// field, but the phrase bonus and rule 5 are FIELD-LOCAL: below, a phrase inside one
// field beats the same two words split across two fields, and a run that would have
// to cross the separator does not match at all.
//
// Headless and QtCore-only: no QApplication, no QML engine, no display.
//
// Run: ./build/tests/Debug/fuzzy_match
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <cstdio>

#include "src/gui/qml/FuzzyMatch.h"

static int failures = 0;

static void check(bool ok, const QString& what) {
    QTextStream(stdout) << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

using gui::fuzzy::kFieldSeparator;
using gui::fuzzy::kNoMatch;
using gui::fuzzy::queryScore;
using gui::fuzzy::tokenScore;
using gui::fuzzy::words;

// A haystack the way DestinationPickerModel::haystackFor builds one: the row's fields
// joined with the unit separator. The fields are JOINED here rather than written into
// a literal because "\x1f" followed by a hex digit is one escape --
// "Discord\x1femojis" would not contain a separator at all.
static QString fieldJoined(const QStringList& aFields) {
    return aFields.join(kFieldSeparator);
}

static QString show(const QStringList& aList) {
    return QStringLiteral("{") + aList.join(QStringLiteral(", ")) + QStringLiteral("}");
}

// A comparison of two scores where BOTH must be real matches: a run that could not
// fire either rule must fail, not pass on two equal kNoMatch values.
static void checkGreater(int aHigh, int aLow, const QString& what) {
    check(aHigh != kNoMatch && aLow != kNoMatch && aHigh > aLow,
          QStringLiteral("%1: high=%2 low=%3").arg(what).arg(aHigh).arg(aLow));
}

static void checkWords(const QString& aInput, const QStringList& aExpected) {
    const QStringList got = words(aInput);
    check(got == aExpected, QStringLiteral("words(\"%1\") == %2 (got %3)")
                                .arg(aInput, show(aExpected), show(got)));
}

int main() {
    // --- words(): lower-case, split on every non-letter-or-digit, drop empties ---
    checkWords(QStringLiteral("chat.generic.video"), {"chat", "generic", "video"});
    checkWords(QStringLiteral("Discord (animated image)"), {"discord", "animated", "image"});
    checkWords(QStringLiteral("Discord__animated"), {"discord", "animated"});
    checkWords(QStringLiteral(""), QStringList());
    checkWords(QStringLiteral("...   ---/  "), QStringList());   // only separators
    checkWords(QStringLiteral("  Telegram  "), {"telegram"});    // whitespace is a separator too

    // --- ORDER: rule 1 (whole word) beats rule 6 (subsequence) for the same token ---
    const int wholeWord = tokenScore({"abc"}, "abc");
    const int mereSubsequence = tokenScore({"aubec"}, "abc");
    checkGreater(wholeWord, mereSubsequence,
                 QStringLiteral("rule 1 (whole word) > rule 6 (subsequence) for \"abc\""));

    // --- ORDER: rule 2 (prefix) beats rule 4 (non-prefix substring), same token ---
    const int prefix = tokenScore({"generic"}, "gen");
    const int substring = tokenScore({"hydrogen"}, "gen");
    checkGreater(prefix, substring,
                 QStringLiteral("rule 2 (prefix of a word) > rule 4 (substring) for \"gen\""));

    // --- rule 3: the initials of two or more CONSECUTIVE words ---
    const int initials = tokenScore({"chat", "generic", "video"}, "cgv");
    check(initials != kNoMatch,
          QStringLiteral("rule 3 (initials of consecutive words): {chat, generic, video} x "
                         "\"cgv\" -> %1").arg(initials));

    // --- rule 5: a contiguous run across the words joined with no separator ---
    const int acrossWords = tokenScore({"discord", "animated", "image"}, "discordanim");
    check(acrossWords != kNoMatch,
          QStringLiteral("rule 5 (joined without separator): {discord, animated, image} x "
                         "\"discordanim\" -> %1").arg(acrossWords));

    // --- rule 7: a typo that is not a SUBSEQUENCE matches by edit distance ---
    //
    // This is the rule the filter catalog needed and did not have: "blurr" has one
    // letter TOO MANY, so it cannot be a subsequence of "blur" and every rule above
    // answered nothing - the catalog's list went empty on a typo. These cases are the
    // discriminating ones (each printed), and the NEGATIVES matter as much: a tolerant
    // matcher that matches everything is worse than none.
    {
        const int extra = tokenScore({"blur"}, "blurr");
        check(extra != kNoMatch,
              QStringLiteral("rule 7 (one letter too many): {blur} x \"blurr\" -> %1").arg(extra));
        const int swapped = tokenScore({"blur"}, "bulr");
        check(swapped != kNoMatch,
              QStringLiteral("rule 7 (adjacent transposition): {blur} x \"bulr\" -> %1").arg(swapped));
        const int wrongLetter = tokenScore({"generic"}, "generoc");
        check(wrongLetter != kNoMatch,
              QStringLiteral("rule 7 (one wrong letter): {generic} x \"generoc\" -> %1").arg(wrongLetter));
        const int shortToken = tokenScore({"hsv"}, "hsb");
        check(shortToken == kNoMatch,
              QStringLiteral("rule 7 stays out of 3-letter tokens: {hsv} x \"hsb\" -> %1").arg(shortToken));
        const int unrelated = tokenScore({"blur"}, "zzzz");
        check(unrelated == kNoMatch,
              QStringLiteral("rule 7 does not match an unrelated token: {blur} x \"zzzz\" -> %1").arg(unrelated));
        // ONE edit below 7 characters, TWO at 7 and above - and both halves asserted, so
        // a change to either limit fails here instead of silently widening the search.
        const int twoOffShort = tokenScore({"blur"}, "bxxr");
        check(twoOffShort == kNoMatch,
              QStringLiteral("rule 7 allows ONE edit under 7 characters: {blur} x \"bxxr\" -> %1")
                  .arg(twoOffShort));
        const int oneOffLong = tokenScore({"discord"}, "diskord");
        check(oneOffLong != kNoMatch,
              QStringLiteral("rule 7 accepts one edit at 7 characters: {discord} x \"diskord\" -> %1")
                  .arg(oneOffLong));
        const int twoOffLong = tokenScore({"discord"}, "dzskord");
        check(twoOffLong != kNoMatch,
              QStringLiteral("rule 7 accepts two edits at 7 characters: {discord} x \"dzskord\" -> %1")
                  .arg(twoOffLong));
    }

    // --- ORDER: any real rule still beats a correction ---
    checkGreater(tokenScore({"blur"}, "blu"), tokenScore({"blur"}, "blurr"),
                 QStringLiteral("prefix \"blu\" > typo \"blurr\" against {blur}"));

    // --- rule 6: one typo matches a single word ---
    const int typo = tokenScore({"telegram"}, "telegrm");
    check(typo != kNoMatch,
          QStringLiteral("rule 6 (typo = subsequence of one word): {telegram} x \"telegrm\" "
                         "-> %1").arg(typo));

    // --- rule 6 SPAN GUARD: a subsequence whose letters are spread too far is not a
    // match. "archivearchive" contains a@0, a@7, e@13 in order, but the span is
    // 14 > 2*3+2 = 8, so rule 6 must refuse it and no other rule fires.
    const int spread = tokenScore({"archivearchive"}, "aae");
    check(spread == kNoMatch,
          QStringLiteral("rule 6 span guard: {archivearchive} x \"aae\" -> %1 (must be "
                         "kNoMatch=%2): the letters ARE in order but span 14 > 2L+2")
              .arg(spread).arg(kNoMatch));
    // The same guard, on the mandated example: "aecv" is not even a subsequence of
    // "archive" (a then e, and no c or v follows), so it fails the rule as well as it
    // would fail the guard.
    const int noRun = tokenScore({"archive"}, "aecv");
    check(noRun == kNoMatch,
          QStringLiteral("rule 6 span guard: {archive} x \"aecv\" -> %1 (must be kNoMatch=%2)")
              .arg(noRun).arg(kNoMatch));

    // --- rule 6 LENGTH GATE: it does not fire below 3 characters ---
    const int twoChars = tokenScore({"discord"}, "dr");     // a subsequence, but L < 3
    const int threeChars = tokenScore({"discord"}, "drd");  // same word, same shape, L == 3
    check(twoChars == kNoMatch && threeChars != kNoMatch,
          QStringLiteral("rule 6 length gate: {discord} x \"dr\" -> %1 (must be kNoMatch=%2) "
                         "while {discord} x \"drd\" -> %3").arg(twoChars).arg(kNoMatch).arg(threeChars));
    // The mandated pair: "dt" cannot be a subsequence of "discord" at all (no t), and
    // "di" matches as a rule 2 PREFIX, not as rule 6.
    const int dt = tokenScore({"discord"}, "dt");
    const int di = tokenScore({"discord"}, "di");
    check(dt == kNoMatch && di != kNoMatch,
          QStringLiteral("rule 6 length gate / rule 2 prefix: {discord} x \"dt\" -> %1 (must "
                         "be kNoMatch=%2) while {discord} x \"di\" -> %3").arg(dt).arg(kNoMatch).arg(di));

    // --- THE NEGATIVE: no rule matches a subsequence that spans several words ---
    // "dna": d and a are letters of "discord", n and a of "animated"; d, n and a appear
    // in order across the two words, but NO single word spells them and the words
    // joined ("discordanimated") has no contiguous "dna". Rules 1-6 are all confined
    // to one word or to a contiguous run, so nothing may fire here.
    const int across = tokenScore({"discord", "animated"}, "dna");
    check(across == kNoMatch,
          QStringLiteral("no cross-word subsequence: {discord, animated} x \"dna\" -> %1 (must "
                         "be kNoMatch=%2): d, n, a are in order but in DIFFERENT words, and a "
                         "cross-word subsequence is what would make \"wec\" match every "
                         "\"capped size limit\" row").arg(across).arg(kNoMatch));

    // --- queryScore: a real haystack, the way the picker builds one ---
    const QString haystack =
        QStringLiteral("Telegram Sticker (animated image) capped size limit");

    const int emptyOnHaystack = queryScore(haystack, QString());
    const int emptyOnEmpty = queryScore(QString(), QString());
    check(emptyOnHaystack == 0 && emptyOnEmpty == 0,
          QStringLiteral("empty query -> 0 (got %1 and %2): clearing the field means no "
                         "filter, not no results").arg(emptyOnHaystack).arg(emptyOnEmpty));

    const int garbage = queryScore(haystack, "zzqqjj");
    check(garbage == kNoMatch,
          QStringLiteral("queryScore(real haystack, \"zzqqjj\") -> %1 (must be kNoMatch=%2)")
              .arg(garbage).arg(kNoMatch));

    const int twoWords = queryScore(haystack, "telegram sticker");
    check(twoWords != kNoMatch,
          QStringLiteral("queryScore(real haystack, \"telegram sticker\") -> %1 (two words, "
                         "never contiguous)").arg(twoWords));

    const int reversed = queryScore(haystack, "sticker telegram");
    check(reversed != kNoMatch,
          QStringLiteral("queryScore(real haystack, \"sticker telegram\") -> %1 (token order "
                         "does not matter)").arg(reversed));

    const int typoQuery = queryScore(haystack, "telegrm");
    check(typoQuery != kNoMatch,
          QStringLiteral("queryScore(real haystack, \"telegrm\") -> %1 (a typo)").arg(typoQuery));

    const int dotted = queryScore(QStringLiteral("Chat Generic Video"), "chat.generic.video");
    check(dotted != kNoMatch,
          QStringLiteral("queryScore(\"Chat Generic Video\", \"chat.generic.video\") -> %1 "
                         "(dots are separators)").arg(dotted));

    // --- PHRASE BONUS: the same words score strictly higher when they are adjacent
    // in the haystack as well as in the query.
    const int adjacent = queryScore(QStringLiteral("Telegram Sticker"), "telegram sticker");
    const int apart =
        queryScore(QStringLiteral("Telegram something else Sticker"), "telegram sticker");
    checkGreater(adjacent, apart,
                 QStringLiteral("phrase bonus: adjacent haystack > far-apart haystack for "
                                "\"telegram sticker\""));

    // --- PHRASE BONUS is FIELD-LOCAL: 0x1f separates the fields of a haystack, and the
    // bonus needs the query's words adjacent inside ONE field. The same two words in
    // two fields score strictly lower -- but still MATCH, because a token may match a
    // different field than its neighbour.
    const int phraseOneField = queryScore(QStringLiteral("Telegram Sticker"), "telegram sticker");
    const int phraseTwoFields =
        queryScore(fieldJoined({QStringLiteral("Telegram"), QStringLiteral("Sticker")}),
                   "telegram sticker");
    check(phraseOneField != kNoMatch && phraseTwoFields != kNoMatch,
          QStringLiteral("phrase bonus field-local: one field (\"Telegram Sticker\") -> %1, two "
                         "fields (\"Telegram\"|0x1f|\"Sticker\") -> %2 (both must match: a token "
                         "may match in a different field than its neighbour)")
              .arg(phraseOneField)
              .arg(phraseTwoFields));
    checkGreater(phraseOneField, phraseTwoFields,
                 QStringLiteral("phrase bonus field-local: the words adjacent inside ONE field beat "
                                "the same two words split across two fields for \"telegram "
                                "sticker\""));
    // "Earns nothing" exactly, not merely "less": the two-field haystack scores the
    // same as the same words far apart inside ONE field, so no bonus was added at all.
    const int farApartOneField = queryScore(QStringLiteral("Telegram x Sticker"), "telegram sticker");
    check(phraseTwoFields == farApartOneField && farApartOneField != kNoMatch,
          QStringLiteral("phrase bonus field-local: two fields (\"Telegram\"|0x1f|\"Sticker\") -> "
                         "%1 exactly equals the words apart inside one field -> %2, so a phrase "
                         "bridging the separator earns nothing)")
              .arg(phraseTwoFields)
              .arg(farApartOneField));

    // --- rule 5 (the words joined without a separator) is FIELD-LOCAL too: "discordemoji"
    // is a contiguous run of the ONE field "Discord emoji", but the fields "Discord" and
    // "emojis" only spell it across the separator, which rule 5 must not cross.
    const int runInOneField = queryScore(QStringLiteral("Discord emoji"), "discordemoji");
    const int runAcrossFields =
        queryScore(fieldJoined({QStringLiteral("Discord"), QStringLiteral("emojis")}),
                   "discordemoji");
    check(runInOneField != kNoMatch && runAcrossFields == kNoMatch,
          QStringLiteral("rule 5 field-local: \"Discord emoji\" x \"discordemoji\" -> %1 (must "
                         "match) while \"Discord\"|0x1f|\"emojis\" -> %2 (must be kNoMatch=%3)")
              .arg(runInOneField)
              .arg(runAcrossFields)
              .arg(kNoMatch));

    // --- THE MEASURED TIE this split removes. telegram.emoji's fields are its own label
    // "Telegram custom emoji", the id, the group title "Telegram" and the family key
    // "sticker". Under one flat join "telegram" abutted "sticker", so the exact label
    // "telegram sticker" scored the SAME against this row as against telegram.sticker's
    // own label "Telegram sticker" and the earlier row ("Telegram custom emoji") won.
    // Field-local, the bonus lands only on the row whose own field spells the phrase.
    const int emojiRow =
        queryScore(fieldJoined({QStringLiteral("Telegram custom emoji"),
                                QStringLiteral("telegram.emoji"), QStringLiteral("Telegram"),
                                QStringLiteral("sticker")}),
                   "telegram sticker");
    const int stickerRow =
        queryScore(fieldJoined({QStringLiteral("Telegram Sticker"),
                                QStringLiteral("telegram.sticker"), QStringLiteral("Telegram"),
                                QStringLiteral("sticker")}),
                   "telegram sticker");
    checkGreater(stickerRow, emojiRow,
                 QStringLiteral("the measured tie: \"telegram sticker\" ranks telegram.sticker "
                                "above telegram.emoji, whose \"sticker\" is only the family key"));

    QTextStream(stdout) << (failures == 0 ? "=== fuzzy_match: OK ===\n"
                                          : QStringLiteral("=== fuzzy_match: %1 FAILED ===\n").arg(failures));
    return failures == 0 ? 0 : 1;
}
