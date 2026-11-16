#ifndef UTIL_IDSOLVER
#define UTIL_IDSOLVER

#include <functional>
#include <QDebug>
#include <QMap>
#include <QVector>

namespace util {

template<typename tData>
class IDSolver {
public:
    typedef int IdType;
    typedef std::function<void(tData)> Solver;
    typedef std::pair<IdType, Solver> Referencer;

    IDSolver(): mDataMap(), mReferencers(), mUnboundIds() {}

    void pushData(IdType aId, tData aData) { mDataMap[aId] = aData; }

    void pushReferencer(IdType aId, const Solver& aSolver) { mReferencers.push_back(Referencer(aId, aSolver)); }

    // we guarantee to call each solver by pushing order.
    // The ids that had no binding, for whoever reports the failure.
    const QVector<IdType>& unboundIds() const { return mUnboundIds; }

    bool solve() {
        // Every solver dereferences the data it is handed -- a resource node's
        // handle() is the usual body -- so an id that was never bound must fail
        // the load, not be handed a value. QMap::operator[] would silently
        // default-construct one (a null), which made this function unable to
        // return false at all and left ctrl::ProjectLoader's "Failed to solve
        // id references." unreachable: a truncated project loaded as far as it
        // could and then dereferenced the null. The unbound ids are recorded and
        // reported so the failure can name them, the references that do have a
        // binding are solved, and the load is refused.
        mUnboundIds.clear();
        for (auto refer : mReferencers) {
            if (!mDataMap.contains(refer.first)) {
                mUnboundIds.push_back(refer.first);
                qDebug() << "Failed to solve id reference at" << refer.first;
                continue;
            }
            refer.second(mDataMap.value(refer.first));
        }
        if (!mUnboundIds.isEmpty()) {
            qDebug() << "Unresolved id references:" << mUnboundIds;
            return false;
        }
        return true;
    }

private:
    QMap<IdType, tData> mDataMap;
    QVector<Referencer> mReferencers;
    QVector<IdType> mUnboundIds;
};

} // namespace util

#endif // UTIL_IDSOLVER
