// core::SkinWeights: the authored-weight value type.
//
// The state is per bone and is carried by the PRESENCE of a BakedBone record,
// not by its entry count: an empty baked record contributes zero and must stay
// Baked. Entries stay sorted and sparse (zero weights are removed), so the
// influence build can walk them as a cursor. A node record is life-linked, so a
// deleted target becomes dormant instead of dangling. The bake helper must
// evaluate the same transformed 2D point the envelope build does.
#include <QApplication>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QVector2D>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>
#include "XCAssert.h"
#include "core/BoneShape.h"
#include "core/FolderNode.h"
#include "core/SkinWeights.h"
#include "gl/Vector3.h"
#include "gui/AudioPlaybackWidget.h"

// Project.cpp references this GUI entry point; this gate never opens a project.
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

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

core::BoneShape makeShape() {
    core::BoneShape shape;
    shape.setSegment(util::Segment2D(QVector2D(0.0f, 0.0f), QVector2D(0.0f, 4.0f)));
    shape.setRadius(QVector2D(1.0f, 1.0f), QVector2D(0.5f, 0.5f));
    QPolygonF polygon;
    polygon << QPointF(-2, -2) << QPointF(2, -2) << QPointF(2, 2) << QPointF(-2, 2);
    shape.setPolygon(polygon);
    return shape;
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    (void)app;

    using core::SkinWeights;

    // Entry invariants: sorted, unique, zero means removed.
    {
        SkinWeights::BakedBone bone;
        bone.boneId = 7;
        check(bone.setWeight(9, 0.4f), "adding a weight changes the bone");
        check(bone.setWeight(3, 0.8f), "adding an earlier vertex changes the bone");
        check(!bone.setWeight(9, 0.4f), "re-setting the same weight is a no-op");
        check(bone.entries.size() == 2, "the field has two entries");
        check(bone.entries[0].vertex == 3 && bone.entries[1].vertex == 9, "entries are sorted by vertex");
        check(bone.weightAt(3) == 0.8f && bone.weightAt(9) == 0.4f, "weightAt finds both entries");
        check(bone.weightAt(4) == 0.0f, "a missing vertex reads zero");
        check(bone.removeWeight(3), "removing a present entry changes the bone");
        check(!bone.removeWeight(3), "removing an absent entry is a no-op");
        check(bone.entries.size() == 1 && bone.entries[0].vertex == 9, "removal keeps the rest sorted");
        bone.setWeight(11, 0.0f);
        check(bone.entries.size() == 1, "a zero weight is not stored");
        bone.setWeight(1, FLT_EPSILON * 0.5f);
        check(bone.entries.size() == 1, "a weight below FLT_EPSILON is not stored");
    }

    // Node record: baked presence, an empty record, sorted bone ids.
    {
        auto* owner = new core::FolderNode(QStringLiteral("owner"));
        auto* child = new core::FolderNode(QStringLiteral("child"));

        SkinWeights weights;
        SkinWeights::NodeWeights record(*child, 4, 0xdeadbeefcafebabeULL);
        record.setBakedBone(42, {{2, 0.5f}, {0, 0.25f}, {2, 0.75f}, {1, 0.0f}});
        record.setBakedBone(7, {});
        check(record.bakedBones.size() == 2, "two baked bones are stored");
        check(record.bakedBones[0].boneId == 7 && record.bakedBones[1].boneId == 42, "baked bones sort by id");
        check(record.bakedBones[1].entries.size() == 2, "duplicate and zero entries are normalised");
        check(
            record.bakedBones[1].entries[0].vertex == 0 && record.bakedBones[1].entries[0].weight == 0.25f
                && record.bakedBones[1].entries[1].vertex == 2
                && record.bakedBones[1].entries[1].weight == 0.75f,
            "normalisation keeps the last weight for a vertex"
        );
        check(record.isBaked(7), "an empty record is Baked, not Envelope");
        check(record.bakedFor(7)->entries.isEmpty(), "the empty baked field has no entries");
        check(!record.isBaked(99), "a bone with no record is Envelope");

        weights.setNodeWeights(record);
        check(weights.count() == 1 && weights.find(*child) != nullptr, "the node record is addressable");
        check(weights.find(*owner) == nullptr, "another node has no record");

        // A second set for the same node replaces, never duplicates.
        SkinWeights::NodeWeights replacement(*child, 5, 1);
        replacement.setBakedBone(42, {{3, 1.0f}});
        weights.setNodeWeights(replacement);
        check(weights.count() == 1, "setNodeWeights replaces the node's record");
        check(weights.find(*child)->isBaked(42) && !weights.find(*child)->isBaked(7), "replacement drops old bones");
        check(weights.find(*child)->vertexCount == 5, "replacement carries the new vertex count");

        check(weights.removeNodeWeights(*child), "removing the record reports success");
        check(weights.isEmpty(), "the store is empty after removal");
        check(!weights.removeNodeWeights(*child), "removing twice is a no-op");

        // Life-link: a deleted target turns the stored pointer null instead of
        // leaving an address a later build could dereference.
        weights.setNodeWeights(SkinWeights::NodeWeights(*child, 1, 0));
        delete child;
        check(weights.nodes()[0].node.get() == nullptr, "a deleted target leaves a dormant record");

        // A record whose node is null is not addressable and is not re-added.
        const SkinWeights::NodeWeights dormant;
        weights.setNodeWeights(dormant);
        check(weights.count() == 1, "a null-node record is ignored");

        delete owner;
    }

    // Bake helper: the entries are exactly the envelope's values at the same
    // transformed positions, including the z=0 transform rule.
    {
        const core::BoneShape shape = makeShape();
        QMatrix4x4 mtx;
        mtx.translate(3.0f, -1.0f);
        mtx.scale(2.0f, 0.5f);

        std::vector<gl::Vector3> positions = {
            gl::Vector3::make(0.0f, 0.0f, 0.0f),
            gl::Vector3::make(0.0f, 1.0f, 99.0f), // z must be ignored, as the build does
            gl::Vector3::make(10.0f, 10.0f, 0.0f),
            gl::Vector3::make(0.25f, 3.5f, 0.0f),
        };
        const SkinWeights::BakedBone baked =
            SkinWeights::bakeBone(shape, mtx, positions.data(), int(positions.size()), 123);

        check(baked.boneId == 123, "the baked bone carries its id");
        bool exact = true;
        for (int i = 0; i < int(positions.size()); ++i) {
            const QVector3D p(positions[i].x, positions[i].y, 0.0f);
            const float expected = shape.influence(mtx.map(p).toVector2D());
            const float actual = baked.weightAt(quint32(i));
            if (expected >= FLT_EPSILON) {
                exact = exact && actual == expected;
            } else {
                exact = exact && actual == 0.0f;
            }
        }
        check(exact, "bakeBone evaluates the build's float and stores every nonzero weight");
        check(baked.weightAt(2) == 0.0f, "a point outside the envelope is not stored");

        const QVector<SkinWeights::Entry> sorted = baked.entries;
        bool ordered = true;
        for (int i = 1; i < sorted.size(); ++i) {
            ordered = ordered && sorted[i - 1].vertex < sorted[i].vertex;
        }
        check(ordered, "bake entries are sorted by vertex");
    }

    // Mesh signature: stable for the same mapping, changes with the count or the
    // index buffer (the two things that make a stored vertex index name another
    // vertex). POSITION AND UV EDITS DO NOT CHANGE IT: those keep the index
    // mapping, and warning on them would report a weight misalignment that did
    // not happen.
    {
        const GLuint indices[] = {0, 1, 2, 2, 1, 3};
        const int indexCount = int(sizeof(indices) / sizeof(indices[0]));
        const quint64 baseline = SkinWeights::meshSignature(4, indices, indexCount);
        check(baseline != 0, "a mesh signature is non-zero");
        check(
            SkinWeights::meshSignature(4, indices, indexCount) == baseline,
            "the same mapping hashes identically"
        );

        GLuint reordered[] = {0, 1, 2, 2, 1, 3};
        std::swap(reordered[0], reordered[1]);
        check(
            SkinWeights::meshSignature(4, reordered, indexCount) != baseline,
            "an index-buffer reorder changes the signature"
        );

        check(
            SkinWeights::meshSignature(5, indices, indexCount) != baseline,
            "a vertex-count change changes the signature"
        );
        check(
            SkinWeights::meshSignature(4, indices, indexCount - 3) != baseline,
            "an index-count change changes the signature"
        );
        check(
            SkinWeights::meshSignature(4, nullptr, indexCount) != baseline,
            "a missing index array changes the signature"
        );
    }

    if (failures) {
        std::printf("%d failure(s)\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
