#include "util/RecentFiles.h"

namespace util {
namespace recent {

namespace {

bool isUsable(const QString& aPath) { return !aPath.trimmed().isEmpty(); }

} // namespace

QStringList record(const QStringList& aCurrent, const QString& aOpen, int aMax) {
    if (!isUsable(aOpen) || aMax <= 0) {
        return aCurrent;
    }
    QStringList next;
    next.reserve(qMin(aMax, aCurrent.size() + 1));
    next.push_back(aOpen);
    for (const QString& path : aCurrent) {
        if (path == aOpen || !isUsable(path) || next.contains(path)) {
            continue;
        }
        if (next.size() >= aMax) {
            break;
        }
        next.push_back(path);
    }
    return next;
}

QStringList without(const QStringList& aCurrent, const QString& aPath) {
    if (!isUsable(aPath)) {
        return aCurrent;
    }
    QStringList next;
    next.reserve(aCurrent.size());
    for (const QString& path : aCurrent) {
        if (path != aPath) {
            next.push_back(path);
        }
    }
    return next;
}

} // namespace recent
} // namespace util
