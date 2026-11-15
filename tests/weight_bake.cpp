// Baked influence weights: the build branch, the bake command, and the
// shared-rig refusal.
//
// The bake is only safe if it is EXACT: at the bake frame the authored entries
// must produce the same work attributes (and therefore the same packed map) the
// live envelope produced. The probe below runs the production makeBoneList ->
// writeWeights -> writeVertexAttribute path twice, once from the envelopes and
// once from the records SkinWeights::bakeBone returns, and compares the stored
// slots vertex by vertex.
//
// The other half is the state rule: a baked bone's missing vertex is 0, never
// the envelope. A record that names only one of two envelope bones must not
// resurrect the other one.
#include <QApplication>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QVector2D>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
#include "XCAssert.h"
#include "cmnd/Stack.h"
#include "core/Bone2.h"
#include "core/BoneInfluenceMap.h"
#include "core/BoneKey.h"
#include "core/FolderNode.h"
#include "core/Project.h"
#include "core/SkinWeights.h"
#include "ctrl/bone/bone_BakeWeights.h"
#include "gui/AudioPlaybackWidget.h"

// Project.cpp references this GUI entry point; this gate never loads a .aemus.
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

struct core::BoneInfluenceMapTestProbe {
    /// Sets the work buffer from the mesh positions already transformed by the
    /// same matrix the production build would apply, then runs the real
    /// makeBoneList -> writeWeights -> writeVertexAttribute path.
    static void buildEnvelope(
        core::BoneInfluenceMap& aMap, const QList<core::Bone2*>& aTopBones,
        const std::vector<QVector2D>& aVertices, const QMatrix4x4& aMatrix
    ) {
        aMap.mVertexCount = int(aVertices.size());
        aMap.freeStorage();
        aMap.mWorks.reset(new core::BoneInfluenceMap::WorkAttribute[aMap.mVertexCount]);
        for (size_t i = 0; i < aVertices.size(); ++i) {
            aMap.mWorks[i].clear();
            aMap.mWorks[i].vertex = aMatrix.map(QVector3D(aVertices[i])).toVector2D();
        }
        aMap.makeBoneList(aTopBones);
        aMap.writeWeights();
        aMap.writeVertexAttribute();
        aMap.mWorks.reset();
    }

    /// The same, with authored records installed. The params still carry their
    /// envelope shapes: the baked branch must ignore them for baked ids and use
    /// the envelope only for ids with no record.
    static void buildBaked(
        core::BoneInfluenceMap& aMap, const QList<core::Bone2*>& aTopBones,
        const std::vector<QVector2D>& aVertices, const QMatrix4x4& aMatrix,
        const QVector<core::SkinWeights::BakedBone>& aBaked
    ) {
        aMap.mVertexCount = int(aVertices.size());
        aMap.freeStorage();
        aMap.mWorks.reset(new core::BoneInfluenceMap::WorkAttribute[aMap.mVertexCount]);
        for (size_t i = 0; i < aVertices.size(); ++i) {
            aMap.mWorks[i].clear();
            aMap.mWorks[i].vertex = aMatrix.map(QVector3D(aVertices[i])).toVector2D();
        }

        core::SkinWeights::NodeWeights record;
        for (const core::SkinWeights::BakedBone& bone : aBaked) {
            record.setBakedBone(bone);
        }
        aMap.mBakedBones = record.bakedBones;

        aMap.makeBoneList(aTopBones);
        aMap.writeWeights();
        aMap.writeVertexAttribute();
        aMap.mWorks.reset();
        aMap.mBakedBones.clear();
    }
};

namespace {
class HarnessAssertHandler: public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};
class HarnessErrorHandler: public XCErrorHandler {
public:
    void critical(const QString&, const QString&, const QString&) const override { std::abort(); }
};
HarnessAssertHandler gAssertHandler;
HarnessErrorHandler gErrorHandler;
int failures = 0;

void check(bool aCondition, const char* aWhat) {
    if (aCondition) {
        std::printf("PASS  %s\n", aWhat);
    } else {
        std::printf("FAIL  %s\n", aWhat);
        ++failures;
    }
}

class NullAnimator: public core::Animator {
public:
    core::Frame currentFrame() const override { return core::Frame(); }
    void stop() override {}
    void suspend() override {}
    void resume() override {}
    bool isSuspended() const override { return false; }
};

core::BoneShape makeShape(const QVector2D& aSegmentStart, const QVector2D& aDir) {
    core::BoneShape shape;
    shape.setSegment(util::Segment2D(aSegmentStart, aDir));
    shape.setRadius(QVector2D(1.0f, 1.0f), QVector2D(0.5f, 0.5f));
    QPolygonF polygon;
    polygon << QPointF(-4, -4) << QPointF(4, -4) << QPointF(4, 4) << QPointF(-4, 4);
    shape.setPolygon(polygon);
    return shape;
}

/// root at (0,0) with two children whose envelopes live around x = -1 and x = 1,
/// so one vertex can be covered by one, both or neither.
struct TwoBoneRig {
    core::BoneKey key;
    core::Bone2* left = nullptr;
    core::Bone2* right = nullptr;

    TwoBoneRig() {
        auto* root = new core::Bone2();
        key.data().topBones().push_back(root);
        root->updateWorldTransform();

        left = new core::Bone2();
        root->children().pushBack(left);
        left->setWorldPos(QVector2D(-1.0f, 0.0f), root);
        left->setRange(0, QVector2D(0.5f, 0.5f));
        left->setRange(1, QVector2D(0.5f, 0.5f));
        left->setShape(makeShape(QVector2D(-1.0f, 0.0f), QVector2D(0.0f, 2.0f)));
        left->updateWorldTransform();

        right = new core::Bone2();
        root->children().pushBack(right);
        right->setWorldPos(QVector2D(1.0f, 0.0f), root);
        right->setRange(0, QVector2D(0.5f, 0.5f));
        right->setRange(1, QVector2D(0.5f, 0.5f));
        right->setShape(makeShape(QVector2D(1.0f, 0.0f), QVector2D(0.0f, 2.0f)));
        right->updateWorldTransform();
    }
};

bool sameWeight(const core::BoneInfluenceMap::Accessor& aLhs, const core::BoneInfluenceMap::Accessor& aRhs, int aBone, int aVertex) {
    return aLhs.weight(aBone, aVertex) == aRhs.weight(aBone, aVertex);
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    (void)app;

    using core::BoneInfluenceMap;
    using core::SkinWeights;
    using Probe = core::BoneInfluenceMapTestProbe;

    // 1. THE EXACTNESS GATE. One bone, a nontrivial matrix, vertices inside,
    // at the edge and outside. The baked map must be the envelope map, slot for
    // slot and float for float.
    {
        core::BoneKey key;
        auto* root = new core::Bone2();
        key.data().topBones().push_back(root);
        root->updateWorldTransform();
        auto* bone = new core::Bone2();
        root->children().pushBack(bone);
        bone->setWorldPos(QVector2D(0.0f, 1.0f), root);
        bone->setRange(0, QVector2D(0.5f, 0.5f));
        bone->setRange(1, QVector2D(0.5f, 0.5f));
        bone->setShape(makeShape(QVector2D(0.0f, 0.0f), QVector2D(0.0f, 2.0f)));
        bone->updateWorldTransform();

        QMatrix4x4 matrix;
        matrix.translate(2.0f, -1.0f);
        matrix.scale(1.5f, 0.75f);

        std::vector<QVector2D> vertices = {
            QVector2D(0.0f, 0.0f), QVector2D(0.0f, 1.0f), QVector2D(0.5f, 0.5f),
            QVector2D(0.0f, 3.0f), QVector2D(-20.0f, 20.0f), QVector2D(1.0f, 3.9f),
        };

        BoneInfluenceMap envelope;
        envelope.setMaxBoneCount(32);
        Probe::buildEnvelope(envelope, key.data().topBones(), vertices, matrix);

        // The baked values come from the production helper; the positions it
        // sees are the mesh positions, not the transformed ones.
        std::vector<gl::Vector3> positions;
        for (const QVector2D& v : vertices) {
            positions.push_back(gl::Vector3::make(v.x(), v.y(), 0.0f));
        }
        const SkinWeights::BakedBone baked =
            SkinWeights::bakeBone(bone->shape(), matrix, positions.data(), int(positions.size()), bone->boneId());

        BoneInfluenceMap authored;
        authored.setMaxBoneCount(32);
        Probe::buildBaked(authored, key.data().topBones(), vertices, matrix, {baked});

        const auto env = envelope.accessor();
        const auto auth = authored.accessor();
        check(env.slotCount() == auth.slotCount(), "the bake keeps the envelope's slot count");
        check(envelope.storedBytes() == authored.storedBytes(), "the bake keeps the envelope's map bytes");

        int boneIndex = -1;
        {
            int index = 0;
            for (core::Bone2* top : key.data().topBones()) {
                for (core::Bone2::Iterator itr(top); itr.hasNext();) {
                    if (itr.next() == bone) {
                        boneIndex = index;
                    }
                    ++index;
                }
            }
        }
        check(boneIndex == 1, "the influenced bone is the map's second index");
        bool identical = true;
        for (int v = 0; v < int(vertices.size()); ++v) {
            for (int b = 0; b < env.slotCount(); ++b) {
                const int index = env.indices(b) ? env.indices(b)[v] : -1;
                if (index >= 0) {
                    identical = identical && sameWeight(env, auth, index, v);
                }
            }
            identical = identical && sameWeight(env, auth, boneIndex, v);
        }
        check(identical, "the baked map is the envelope map at the bake frame");

        // Empty file-level control: an empty baked record is Baked-to-zero, not
        // an envelope fallback. The default one-slot map means "no influence".
        SkinWeights::BakedBone empty;
        empty.boneId = bone->boneId();
        BoneInfluenceMap cleared;
        cleared.setMaxBoneCount(32);
        Probe::buildBaked(cleared, key.data().topBones(), vertices, matrix, {empty});
        const auto clearedAccessor = cleared.accessor();
        check(
            clearedAccessor.slotCount() == 1 && clearedAccessor.weight(boneIndex, 1) == 0.0f,
            "an empty baked record contributes exactly zero"
        );
    }

    // 2. MISSING = 0, NO ENVELOPE FALLBACK. The left bone is baked and then a
    // covered vertex is removed from its field; the right bone is baked empty.
    // Neither may fall back to its envelope: a baked bone's missing vertex is
    // exactly 0, and an empty baked record contributes nothing.
    {
        TwoBoneRig rig;
        std::vector<QVector2D> vertices = {
            QVector2D(-1.0f, 0.0f), // inside the left envelope
            QVector2D(-1.0f, 1.0f), // inside the left envelope, removed from the field
            QVector2D(1.0f, 0.0f),  // inside the right envelope, which is baked empty
        };
        QMatrix4x4 identity;
        const auto leftShape = rig.left->shape();
        std::vector<gl::Vector3> positions;
        for (const QVector2D& v : vertices) {
            positions.push_back(gl::Vector3::make(v.x(), v.y(), 0.0f));
        }
        SkinWeights::BakedBone leftBaked =
            SkinWeights::bakeBone(leftShape, identity, positions.data(), int(positions.size()), rig.left->boneId());
        check(leftBaked.entries.size() == 2, "the left bake covers its two vertices");
        check(leftBaked.removeWeight(1), "a covered vertex can be removed from the authored field");

        SkinWeights::BakedBone rightBaked;
        rightBaked.boneId = rig.right->boneId();

        BoneInfluenceMap authored;
        authored.setMaxBoneCount(32);
        Probe::buildBaked(authored, rig.key.data().topBones(), vertices, identity, {leftBaked, rightBaked});
        const auto accessor = authored.accessor();
        check(accessor.slotCount() == 1, "one authored influence stores one slot");
        check(
            accessor.indices(0) && accessor.indices(0)[0] == 1 && accessor.indices(0)[1] == 0
                && accessor.indices(0)[2] == 0,
            "only the stored left vertex picks bone 1; the removed and right-only vertices fall to the default"
        );
        check(accessor.weight(1, 0) == 1.0f, "a single baked influence normalises to one");
        check(accessor.weight(1, 1) == 0.0f, "a baked bone's missing vertex is exactly zero");
        check(accessor.weight(2, 2) == 0.0f, "an empty baked record never leaks its envelope");

        // The same map built from envelopes does show both bones (with two
        // slots), which is the control that the checks above are not vacuous.
        BoneInfluenceMap envelope;
        envelope.setMaxBoneCount(32);
        Probe::buildEnvelope(envelope, rig.key.data().topBones(), vertices, identity);
        const auto envelopeAccessor = envelope.accessor();
        check(
            envelopeAccessor.weight(1, 1) > 0.0f && envelopeAccessor.weight(2, 2) > 0.0f,
            "the envelope control uses the vertices a baked field drops"
        );
    }

    // 3. TWO BAKED BONES: authored values normalise by the build's own rule.
    {
        TwoBoneRig rig;
        std::vector<QVector2D> vertices = {QVector2D(0.0f, 0.5f)};
        QMatrix4x4 identity;
        SkinWeights::BakedBone left;
        left.boneId = rig.left->boneId();
        left.setWeight(0, 0.25f);
        SkinWeights::BakedBone right;
        right.boneId = rig.right->boneId();
        right.setWeight(0, 0.75f);

        BoneInfluenceMap map;
        map.setMaxBoneCount(32);
        Probe::buildBaked(map, rig.key.data().topBones(), vertices, identity, {left, right});
        const auto accessor = map.accessor();
        check(accessor.slotCount() == 2, "two authored influences store two slots");
        check(
            accessor.weight(1, 0) == 0.25f && accessor.weight(2, 0) == 0.75f,
            "authored 0.25/0.75 is stored as 0.25/0.75"
        );
    }

    // 4. THE COMMAND: one undo step, presence preserved through undo/redo, and
    // the whole-owner SkinWeightsModified event fires on every replay.
    {
        NullAnimator animator;
        core::Project project(QString(), animator, nullptr);
        auto* top = new core::FolderNode(QStringLiteral("owner"));
        auto* child = new core::FolderNode(QStringLiteral("child"));
        top->children().pushBack(child);
        project.objectTree().grabTopNode(top);

        int notifications = 0;
        project.onSkinWeightsModified.connect([&notifications](core::ObjectNode&, bool) { ++notifications; });

        QVector<SkinWeights::NodeWeights> fields;
        SkinWeights::NodeWeights field(*child, 3, 0x1122334455667788ULL);
        SkinWeights::BakedBone baked;
        baked.boneId = 11;
        baked.setWeight(1, 0.5f);
        field.setBakedBone(baked);
        fields.push_back(field);

        project.commandStack().push(new ctrl::bone::BakeWeightsCommand(project, *top, fields));
        const SkinWeights::NodeWeights* stored = top->skinWeights().find(*child);
        check(stored && stored->isBaked(11), "the bake writes the record");
        check(stored && stored->bakedFor(11)->weightAt(1) == 0.5f, "the baked value is stored");
        check(notifications == 1, "the bake notifies the owner once");

        project.commandStack().undo();
        check(top->skinWeights().find(*child) == nullptr, "undo removes the record");
        check(notifications == 2, "undo notifies again");

        project.commandStack().redo();
        stored = top->skinWeights().find(*child);
        check(stored && stored->isBaked(11) && stored->bakedFor(11)->weightAt(1) == 0.5f, "redo restores it");
        check(notifications == 3, "redo notifies again");

        // An empty baked record is a state: undo must remove the bone's record,
        // not merely clear an entry list. A second bake is bone-scoped: it must
        // merge into the node's field and leave the first bone alone.
        SkinWeights::NodeWeights emptyField(*child, 3, 0);
        emptyField.setBakedBone(12, {});
        project.commandStack().push(new ctrl::bone::BakeWeightsCommand(project, *top, {emptyField}));
        stored = top->skinWeights().find(*child);
        check(stored && stored->isBaked(11) && stored->isBaked(12), "a second bake merges, keeping the first bone");
        check(stored && stored->bakedFor(11)->weightAt(1) == 0.5f, "the first bone's entries survive the merge");
        check(stored && stored->bakedFor(12)->entries.isEmpty(), "the second bone is Baked-to-zero");
        project.commandStack().undo();
        stored = top->skinWeights().find(*child);
        check(stored && stored->isBaked(11) && !stored->isBaked(12), "undo removes only the second bone");
        project.commandStack().undo();
        check(top->skinWeights().find(*child) == nullptr, "undoing the first bake removes the node record");
        project.commandStack().redo();
        stored = top->skinWeights().find(*child);
        check(stored && stored->isBaked(11) && !stored->isBaked(12), "redo restores the first bone alone");
    }

    // 5. THE SHARED-RIG REFUSAL. Two keys on one owner must agree on the
    // same-id bone's authored geometry; the failure names the other frame.
    {
        NullAnimator animator;
        core::Project project(QString(), animator, nullptr);
        auto* top = new core::FolderNode(QStringLiteral("owner"));
        project.objectTree().grabTopNode(top);

        auto* key1 = new core::BoneKey();
        auto* root = new core::Bone2();
        key1->data().topBones().push_back(root);
        auto* bone = new core::Bone2();
        root->children().pushBack(bone);
        bone->setWorldPos(QVector2D(1.0f, 0.0f), root);
        bone->setRange(0, QVector2D(0.5f, 0.5f));
        bone->setRange(1, QVector2D(0.5f, 0.5f));
        bone->setShape(makeShape(QVector2D(1.0f, 0.0f), QVector2D(0.0f, 2.0f)));
        bone->updateWorldTransform();
        top->timeLine()->adoptKey(core::TimeKeyType_Bone, 1, key1);

        auto* key2 = new core::BoneKey();
        key2->data() = key1->data();
        top->timeLine()->adoptKey(core::TimeKeyType_Bone, 2, key2);
        check(bone->hasSameAuthoredGeometry(*key2->data().topBones()[0]->children().front()), "a cloned key shares the bone geometry");

        QVector<SkinWeights::NodeWeights> fields;
        QString error;
        const bool sameRig = ctrl::bone::bakeBoneFields(project, *top, *key1, *bone, fields, &error);
        check(!sameRig && !error.contains(QStringLiteral("differs")), "the shared rig passes the geometry check");

        // A pose rotation is not authored rig geometry: the keys still share
        // the field and the check must not refuse on it.
        core::Bone2* otherBone = key2->data().topBones()[0]->children().front();
        otherBone->setRotate(0.5f);
        fields.clear();
        error.clear();
        const bool posed = ctrl::bone::bakeBoneFields(project, *top, *key1, *bone, fields, &error);
        check(!posed && !error.contains(QStringLiteral("differs")), "a pose rotation does not count as divergence");
        otherBone->setRotate(0.0f);

        // Diverge the second key's same-id bone. The bake must refuse, name its
        // frame, and write nothing.
        core::Bone2* otherPtr = key2->data().topBones()[0]->children().front();
        otherPtr->setRange(0, QVector2D(0.75f, 0.25f));
        fields.clear();
        error.clear();
        const bool refused = ctrl::bone::bakeBoneFields(project, *top, *key1, *bone, fields, &error);
        check(!refused && fields.isEmpty(), "a divergent key refuses the bake");
        check(error.contains(QStringLiteral("differs")) && error.contains(QStringLiteral("frame 2")), "the refusal names the frame");

        // A missing same-id bone is also a divergence, not a silent skip.
        key2->data().deleteAll();
        fields.clear();
        error.clear();
        const bool missing = ctrl::bone::bakeBoneFields(project, *top, *key1, *bone, fields, &error);
        check(!missing && fields.isEmpty(), "a key without the bone refuses the bake");
        check(error.contains(QStringLiteral("does not exist")) && error.contains(QStringLiteral("frame 2")), "the missing-bone refusal names the frame");
    }

    if (failures) {
        std::printf("%d failure(s)\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
