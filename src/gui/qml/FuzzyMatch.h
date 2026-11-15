#ifndef GUI_QML_FUZZYMATCH_H
#define GUI_QML_FUZZYMATCH_H

#include <QChar>
#include <QString>
#include <QStringList>

namespace gui {
namespace fuzzy {

// Fuzzy matching for the destination picker's search field.
//
// The search used to be `haystack.contains(query)` -- a plain case-insensitive
// substring test -- so the three shapes a person actually types all came back
// empty: "telegram sticker" (two words, never contiguous), "telegrm" (a typo) and
// "discordemote" (two words, space left out). This scores a query against a
// haystack instead of asking one string to contain the other.
//
// Header-only on purpose: the model, the application and the unit test all see the
// same code, with no library to link and no CMake churn. `inline`, no state, no
// dependency beyond QtCore (QString/QStringList/QChar).
//
// FIELDS. A haystack may be several fields joined with kFieldSeparator (U+001F, the
// unit separator): DestinationPickerModel::haystackFor joins a row's label, id,
// group, family and so on that way, and a token may match in ANY of them. So the
// per-token score is a maximum over the fields as well, but every rule is evaluated
// inside ONE field's words -- rule 5 and the phrase bonus never reach across the
// separator.
//
// SCORING. Every rule scores at least 0, and kNoMatch is -1, so a fired rule can
// never be confused with "no match". A query matches only if EVERY one of its tokens
// matches; the score is the sum of the per-token scores plus at most one phrase
// bonus. Per token, L is the token's length and its score is the MAXIMUM, first over
// the haystack's fields and then over the rules that fire in that field:
//
//   1. the token is a whole word:                    1000 + 8L
//   2. the token is a prefix of a word:               700 + 8L - (wordLen - L)
//   3. the token is the initials of >= 2 consecutive
//      words ("cgv" for "chat generic video"):        600 + 8L
//   4. the token is a substring of one word:          400 + 8L - offset
//   5. the token is a contiguous run in ONE field's
//      words joined with NO separator ("discordanim"
//      for "discord animated image"):                350 + 8L - offset
//   6. the token is a subsequence of ONE word, only
//      when L >= 3:                                   200 + 8L - 4*gaps
//      with gaps = span - L, span = last - first + 1, and no fire when
//      span > 2*L + 2.
//
// The ordering is the point, not the constants: an exact word (1) beats a prefix
// (2), which beats initials (3), a substring (4) and a run across one field's words
// (5), and a subsequence (6) is last. The 8L term makes a longer token a stronger
// signal at the same rule; the offset/gap terms make an earlier, tighter match win.
//
// NO RULE MATCHES A SUBSEQUENCE THAT SPANS SEVERAL WORDS. Rule 6 is confined to one
// word deliberately, and measured: every capped destination's haystack ends in
// " capped size limit" (haystackFor in DestinationPickerModel.cpp), so a cross-word
// subsequence would let a 3-letter query match nearly the whole table -- "wec" would
// find every capped row -- and the picker would stop narrowing at all.
// tests/destination_picker.cpp asserts visibleRowCount() < targets.size() for "wec".
// Do not add it back.
//
// PHRASE BONUS: with q = the query's words joined, the bonus adds 400 once per query
// when SOME ONE FIELD's words joined contain q. It is a FIELD-local hint, and the
// reason is measured. haystackFor joins the row's fields into a single string, so
// before this the group title "Telegram" abutted the family key "sticker" and gave
// telegram.emoji the same +400 as telegram.sticker's own label "Telegram sticker";
// typing the exact label selected "Telegram custom emoji" on the tie. Two words typed
// adjacently AND present adjacently inside one field is the strongest signal that the
// user knows the name -- a phrase that only appears by bridging the end of one field
// and the start of the next is not that signal and earns nothing.

// What a token that does not match scores. Every rule scores positive, so this can
// never collide with a weak match.
constexpr int kNoMatch = -1;

// The separator between the fields of a haystack: U+001F, the unit separator. Not a
// letter and not a digit, so words() drops it like any other separator and no
// field's word list can span two fields.
constexpr QChar kFieldSeparator = QChar(QLatin1Char(0x1f));

// Lower-cases, then splits on every character that is not a letter or a digit
// (QChar::isLetterOrNumber). The field separator kFieldSeparator is dropped like any
// other non-letter, so the words of a joined haystack are exactly the words of its
// fields: the separator leaves nothing behind that could bridge two of them. Empty
// words are dropped.
inline QStringList words(const QString& aText) {
    const QString text = aText.toLower();
    QStringList out;
    QString current;
    for (const QChar c : text) {
        if (c.isLetterOrNumber()) {
            current.append(c);
        } else if (!current.isEmpty()) {
            out.append(current);
            current.clear();
        }
    }
    if (!current.isEmpty()) out.append(current);
    return out;
}

// Scores ONE field: -1 (`kNoMatch`) when the token does not match the word list;
// otherwise the score, higher is better. aWords is a single field's words -- the
// caller splits the haystack (queryScore below) -- so rules 1-6 are all confined to
// the field. Both sides are lower-cased here, so a caller may pass raw text in any
// case.
inline int tokenScore(const QStringList& aWords, const QString& aToken) {
    const QString token = aToken.toLower();
    const int L = token.size();
    if (L == 0) return kNoMatch;

    int best = kNoMatch;
    // A rule that fires IS a match, so its score is floored at 0: the offset and gap
    // terms of rules 4/5 can drive a formula negative on an absurdly long string, and
    // a fired rule must never read as kNoMatch.
    const auto bump = [&best](int aScore) {
        if (aScore < 0) aScore = 0;
        if (aScore > best) best = aScore;
    };

    for (int w = 0; w < aWords.size(); ++w) {
        const QString word = aWords.at(w).toLower();
        const int wordLen = word.size();
        if (wordLen == 0) continue;

        // 1. the whole word.
        if (word == token) bump(1000 + 8 * L);

        // 2. a prefix, penalised by what the word adds after the token.
        if (wordLen >= L && word.startsWith(token)) bump(700 + 8 * L - (wordLen - L));

        // 4. a substring of the word, penalised by where it sits in it.
        const int at = word.indexOf(token);
        if (at >= 0) bump(400 + 8 * L - at);

        // 6. a subsequence of this ONE word. Gated at 3 characters: two characters
        // that happen to be in order is a coincidence, three is a typo. The span
        // guard rejects a match whose letters are so far apart that the "word" they
        // spell is not in the word at all (last-first+1 > 2L+2).
        if (L >= 3) {
            int first = -1;
            int last = -1;
            int matched = 0;
            for (int i = 0; i < wordLen && matched < L; ++i) {
                if (word.at(i) == token.at(matched)) {
                    if (first < 0) first = i;
                    last = i;
                    ++matched;
                }
            }
            if (matched == L) {
                const int span = last - first + 1;
                if (span <= 2 * L + 2) bump(200 + 8 * L - 4 * (span - L));
            }
        }
    }

    // 3. the initials of two or more CONSECUTIVE words ("cgv" for "chat generic
    // video"). A single initial is a prefix and rule 2 already covers it.
    if (L >= 2) {
        for (int i = 0; i + 1 < aWords.size(); ++i) {
            QString initials;
            for (int j = i; j < aWords.size() && initials.size() < L; ++j) {
                const QString w = aWords.at(j).toLower();
                if (w.isEmpty()) break;
                initials.append(w.at(0));
                if (initials == token && j - i + 1 >= 2) {
                    bump(600 + 8 * L);
                    break;
                }
            }
        }
    }

    // 5. a contiguous run of the words joined with no separator ("discordanim" for
    // "discord animated image"): more forgiving than a word-internal substring
    // because it tolerates the missing space, and scored just below it.
    {
        QString joined;
        for (int i = 0; i < aWords.size(); ++i) joined.append(aWords.at(i).toLower());
        const int at = joined.indexOf(token);
        if (at >= 0) bump(350 + 8 * L - at);
    }

    return best;
}

// The whole query against a haystack of one or more kFieldSeparator-joined fields:
// every query token must match in SOME field, and the score is the sum of the best
// per-token scores over the fields, plus the phrase bonus. -1 when a token matches in
// no field. Empty query = 0.
inline int queryScore(const QString& aHaystack, const QString& aQuery) {
    const QStringList queryWords = words(aQuery);
    if (queryWords.isEmpty()) return 0;

    // Split once, then score each field's words on their own: that is what keeps rule
    // 5 and the phrase bonus inside ONE field instead of letting them span the join.
    const QStringList fields = aHaystack.split(kFieldSeparator, Qt::SkipEmptyParts);
    QList<QStringList> fieldWords;
    fieldWords.reserve(fields.size());
    for (const QString& field : fields) fieldWords.append(words(field));

    int total = 0;
    for (int i = 0; i < queryWords.size(); ++i) {
        const QString& token = queryWords.at(i);
        int best = kNoMatch;
        for (const QStringList& field : fieldWords) {
            const int score = tokenScore(field, token);
            if (score > best) best = score;
        }
        if (best == kNoMatch) return kNoMatch;
        total += best;
    }

    const QString q = queryWords.join(QString());
    if (!q.isEmpty()) {
        // The bonus needs the query's words adjacent inside one field. Bridging the
        // end of a field and the start of the next earns nothing: it is how the group
        // title "Telegram" + the family key "sticker" used to tie telegram.emoji with
        // telegram.sticker's own label "Telegram sticker".
        for (const QStringList& field : fieldWords) {
            if (field.join(QString()).contains(q)) {
                total += 400;
                break;
            }
        }
    }
    return total;
}

} // namespace fuzzy
} // namespace gui

#endif // GUI_QML_FUZZYMATCH_H
