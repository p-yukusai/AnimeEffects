#ifndef CORE_BONEKEY_H
#define CORE_BONEKEY_H

#include <memory>
#include <QList>
#include <QMatrix4x4>
#include "util/LinkPointer.h"
#include "core/TimeKey.h"
#include "core/Bone2.h"
#include "core/BoneInfluenceMap.h"
#include "core/ObjectNode.h"
namespace core {
class Project;
}

namespace core {

class BoneKey: public TimeKey {
public:
    class Data {
        QList<Bone2*> mTopBones;

    public:
        Data();
        Data(const Data& aRhs);
        Data& operator=(const Data& aRhs);
        ~Data();

        QList<Bone2*>& topBones();
        const QList<Bone2*>& topBones() const;
        void deleteAll();
        bool isBinding(const ObjectNode& aNode) const;
        Bone2* findBinderBone(const ObjectNode& aNode);
        /// The bone's index in the influence map's pre-order bone list, or -1
        /// when it is not in this skeleton. Clamp against the built map's
        /// boneCount() before using it as an index attribute.
        int boneIndex(const Bone2& aBone) const;
        /// The bone whose envelope is nearest to a target-space point, skipping
        /// parentless roots (they carry no envelope). This is how a paint stroke
        /// that starts on the mesh chooses the bone it edits.
        Bone2* nearestBone(const QVector2D& aPos);
    };

    class Cache {
        BoneInfluenceMap mInfluence;
        util::LinkPointer<ObjectNode> mNode;
        QMatrix4x4 mInnerMtx;
        Frame mFrameSign; // for debug that the cache updating be forgotten
    public:
        Cache();
        void setNode(ObjectNode& aNode);
        ObjectNode* node() { return mNode.get(); }
        const ObjectNode* node() const { return mNode.get(); }
        BoneInfluenceMap& influence() { return mInfluence; }
        const BoneInfluenceMap& influence() const { return mInfluence; }
        void setInnerMatrix(const QMatrix4x4& aMtx) { mInnerMtx = aMtx; }
        const QMatrix4x4& innerMatrix() const { return mInnerMtx; }
        void setFrameSign(const Frame& aFrame) { mFrameSign = aFrame; }
        const Frame& frameSign() const { return mFrameSign; }
    };
    typedef QList<Cache*> CacheList;

    class BindingCache {
    public:
        BindingCache();
        ObjectNode* node;
        int boneIndex;
        QMatrix4x4 innerMtx;
    };
    typedef QList<BindingCache> BindingCacheList;

    BoneKey();
    ~BoneKey();

    Data& data() { return mData; }
    const Data& data() const { return mData; }

    ObjectNode* cacheOwner() const { return mCacheOwner.get(); }
    const CacheList& caches() const { return mCaches; }
    Cache* findCache(const ObjectNode& aNode) const;
    const BindingCacheList& bindingCaches() const { return mBindingCaches; }

    // reset cache list and rewrite influences
    void resetCaches(Project& aProject, ObjectNode& aOwner);
    // rewrite influences of unique roots specified
    void updateCaches(Project& aProject, ObjectNode& aOwner, const QVector<ObjectNode*>& aUniqueRoots);

    // The nodes this key actually skins at aTime: the same subtree, cache-owner
    // and rigid-binding rules resetCaches uses, without building a cache list.
    // The bake evaluates exactly this set, so the state a bake writes is the
    // state the build reads.
    void collectInfluenceTargets(const TimeInfo& aTime, ObjectNode& aOwner, QVector<ObjectNode*>& aTargets);

    virtual TimeKeyType type() const { return TimeKeyType_Bone; }
    virtual bool canHoldChild() const { return true; }
    virtual TimeKey* createClone(); ///@note a new key have to reset caches.
    virtual bool serialize(Serializer& aOut) const;
    virtual bool deserialize(Deserializer& aIn);

private:
    void collectInfluenceTargetsRecursive(const TimeInfo& aTime, ObjectNode& aNode, QVector<ObjectNode*>& aTargets);
    void updateCaches(Project& aProject, const QList<Cache*>& aTargets);
    Cache* popCache(ObjectNode& aNode);
    void destroyCaches();
    bool serializeBone(Serializer& aOut, const Bone2* aBone) const;
    bool deserializeBone(Deserializer& aIn, Bone2* aBone);
    // Format <= 0.20 appended the derived influence cache to this block. Read
    // it only to stay in sync, and discard it: ProjectLoader rebuilds it.
    bool deserializeLegacyCaches(Deserializer& aIn);
    Data mData;
    CacheList mCaches;
    util::LinkPointer<ObjectNode> mCacheOwner;
    BindingCacheList mBindingCaches;
};

} // namespace core

#endif // CORE_BONEKEY_H
