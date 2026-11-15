#ifndef UTIL_RECENTFILES_H
#define UTIL_RECENTFILES_H

#include <QString>
#include <QStringList>

namespace util {
namespace recent {

// The recent-project list, as a pure list operation.
//
// It lives here, not in the loader, because two writers and one reader have to agree:
// ctrl::System records a successful open, ctrl::ProjectLoader removes a path that has
// gone missing, and gui::MainMenuBar reads the list for the menu. The old inline code
// disagreed with itself: at the eight-entry cap it popped the oldest and then never
// appended the file just opened, so the newest project was silently dropped.
constexpr int kMaxCount = 8;

// Record aOpen at the front, removing any earlier occurrence first and keeping at
// most aMax entries. An empty path changes nothing.
QStringList record(const QStringList& aCurrent, const QString& aOpen, int aMax = kMaxCount);

// Remove every occurrence of aPath. An empty path changes nothing.
QStringList without(const QStringList& aCurrent, const QString& aPath);

} // namespace recent
} // namespace util

#endif // UTIL_RECENTFILES_H
