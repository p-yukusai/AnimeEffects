// THE RECENT-PROJECT LIST, as a list.
//
// util::recent::record is the one writer the loader and the menu agree on. The
// code it replaces dropped the newest project at the eight-entry cap (it popped
// the oldest and then forgot to append the file that had just opened), so the
// checks below are about the two properties that bug broke: the newest path is
// FIRST, and a cap of eight keeps it.
#include <cstdio>
#include <QStringList>

#include "util/RecentFiles.h"

namespace {

int failures = 0;
int checks = 0;

void check(bool aPassed, const char* aWhat) {
    ++checks;
    if (!aPassed) {
        std::printf("FAIL  %s\n", aWhat);
        ++failures;
    }
}

QStringList paths(int aCount, const char* aPrefix = "p") {
    QStringList list;
    for (int i = 0; i < aCount; ++i)
        list.push_back(QString::fromLatin1(aPrefix) + QString::number(i));
    return list;
}

} // namespace

int main() {
    using namespace util::recent;

    check(record(QStringList(), "a") == QStringList{"a"}, "an empty list records the first path");
    check(record(QStringList{"a"}, "b") == (QStringList{"b", "a"}), "a new path goes to the front");
    check(record(QStringList{"a", "b"}, "a") == (QStringList{"a", "b"}), "re-opening the first path is stable");
    check(record(QStringList{"a", "b"}, "b") == (QStringList{"b", "a"}), "re-opening an older path moves it to the front");
    check(record(QStringList{"a", "b", "a"}, "c") == (QStringList{"c", "a", "b"}), "duplicates are collapsed");
    check(record(QStringList{"a"}, "") == QStringList{"a"}, "an empty open path changes nothing");

    {
        // The old failure: at the cap, the newest path must survive, and the
        // list must stay at the cap.
        const QStringList full = paths(kMaxCount);
        const QStringList next = record(full, "newest");
        check(next.size() == kMaxCount, "the list stays at the cap");
        check(next.front() == "newest", "the newest path is first at the cap");
        check(next.back() == full.at(kMaxCount - 2), "the oldest path was dropped at the cap");
        check(!next.contains(full.back()), "the dropped path is gone");
    }
    {
        const QStringList full = paths(kMaxCount);
        check(record(full, full.back()).front() == full.back(), "an old path re-opened at the cap becomes first");
        check(record(full, full.back()).size() == kMaxCount, "re-opening at the cap does not grow the list");
    }

    check(without(QStringList{"a", "b"}, "a") == QStringList{"b"}, "without removes a path");
    check(without(QStringList{"a", "b", "a"}, "a") == QStringList{"b"}, "without removes every occurrence");
    check(without(QStringList{"a"}, "missing") == QStringList{"a"}, "without a missing path changes nothing");
    check(without(QStringList{"a"}, "") == QStringList{"a"}, "without an empty path changes nothing");

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
