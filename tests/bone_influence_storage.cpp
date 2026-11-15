// Bone-influence attribute storage.
//
// A map stores only the influence slots its busiest vertex uses: one uint8
// index per vertex and no weights when one influence suffices (its weight is
// exactly 1); otherwise one uint8 index and one float32 weight per slot, slot
// major. A map at the eight-slot cap keeps the historical dense two-set layout
// (4 x uint8 + 4 x float32 per set) because the scalar form would need more
// than the guaranteed 16 vertex attributes. Weights stay float32: the uint16
// experiment exceeded the 1-LSB pixel gate on 2.anie.
//
// This gate drives the writer with synthetic work attributes (the test probe is
// a friend) so every slot count 1..8, the default values, the slot-major
// layout and the dense group layout are pinned without a project fixture. The
// GL binding and the shader's per-slot variations are exercised by
// render_probe's pixel and logical-hash gates.
#include <QApplication>
#include <QJsonObject>
#include <cmath>
#include <cstdio>
#include <vector>
#include "XCAssert.h"
#include "core/BoneInfluenceMap.h"
#include "gui/AudioPlaybackWidget.h"

// Project.cpp references this GUI entry point; the test never opens a .aemus.
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

struct core::BoneInfluenceMapTestProbe {
    struct Influence {
        int id;
        float weight;
    };

    /// Writes a map from per-vertex influences, exactly as the build's work
    /// buffer would hold them, then runs the real writer.
    static void build(
        core::BoneInfluenceMap& aMap, const std::vector<std::vector<Influence>>& aVertices
    ) {
        aMap.mWorks.reset();
        aMap.mBoneList.params.clear();
        for (int i = 0; i < core::BoneInfluenceMap::kBonePerVtxMaxAll; ++i) {
            aMap.mBoneList.params.push_back(core::BoneInfluenceMap::BoneParam());
        }

        aMap.mVertexCount = int(aVertices.size());
        aMap.freeStorage();
        aMap.mWorks.reset(new core::BoneInfluenceMap::WorkAttribute[aMap.mVertexCount]);
        for (size_t v = 0; v < aVertices.size(); ++v) {
            aMap.mWorks[v].clear();
            for (const Influence& influence : aVertices[v]) {
                aMap.mWorks[v].tryPushBoneWeight(influence.id, influence.weight);
            }
        }

        aMap.writeVertexAttribute();
        aMap.mWorks.reset();
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

bool near(float aLhs, float aRhs) { return std::fabs(aLhs - aRhs) <= 1e-6f; }

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    (void)argv;

    using core::BoneInfluenceMap;
    using Probe = core::BoneInfluenceMapTestProbe;
    static_assert(sizeof(BoneInfluenceMap::IndicesType) == 1, "bone indices are uint8");
    static_assert(sizeof(BoneInfluenceMap::WeightsType) == 4, "bone weights are float32");
    static_assert(BoneInfluenceMap::kBonePerVtxMaxAll == 8, "the top-eight cap is the dense boundary");
    // The resident contract: one influence is one byte per vertex (its weight is
    // implicit), every further slot costs five; eight slots are 40 B/vertex.
    static_assert(BoneInfluenceMap::storedBytes(3, 1) == 3, "one slot is 1 B/vertex");
    static_assert(BoneInfluenceMap::storedBytes(3, 2) == 30, "two slots are 10 B/vertex");
    static_assert(BoneInfluenceMap::storedBytes(3, 5) == 75, "five slots are 25 B/vertex");
    static_assert(BoneInfluenceMap::storedBytes(3, 8) == 120, "eight slots are 40 B/vertex");

    // The pre-build default: one slot, bone 0, weight 1 (not stored).
    {
        BoneInfluenceMap map;
        map.setMaxBoneCount(32);
        map.allocate(3, true);
        const BoneInfluenceMap::Accessor accessor = map.accessor();
        check(map.vertexCount() == 3, "vertex count is retained");
        check(accessor.slotCount() == 1, "a default map has one slot");
        check(!accessor.dense(), "a default map uses the sparse form");
        const BoneInfluenceMap::IndicesType* indices = accessor.indices(0);
        check(indices && indices[0] == 0 && indices[1] == 0 && indices[2] == 0, "every default index is bone 0");
        check(accessor.weights(0) == nullptr, "a one-slot map stores no weights");
        check(
            near(accessor.weight(0, 0), 1.0f) && near(accessor.weight(1, 0), 0.0f),
            "a one-slot lookup reads the implicit weight and rejects other bones"
        );
        check(map.storedBytes() == 3, "a default map is 1 B/vertex");
    }

    // One influence per vertex: index only, no weight array.
    {
        BoneInfluenceMap map;
        map.setMaxBoneCount(32);
        Probe::build(map, {{{2, 1.0f}}, {{5, 0.5f}}, {{7, 2.0f}}});
        const BoneInfluenceMap::Accessor accessor = map.accessor();
        check(accessor.slotCount() == 1, "one influence stores one slot");
        check(!accessor.dense(), "one influence uses the sparse form");
        const BoneInfluenceMap::IndicesType* indices = accessor.indices(0);
        check(indices && indices[0] == 2 && indices[1] == 5 && indices[2] == 7, "single influences keep their bone");
        check(accessor.weights(0) == nullptr, "a single influence's weight is implicit");
        check(
            near(accessor.weight(2, 0), 1.0f) && near(accessor.weight(5, 1), 1.0f)
                && near(accessor.weight(7, 2), 1.0f) && near(accessor.weight(2, 1), 0.0f),
            "a one-slot lookup follows each vertex's own bone"
        );
        check(map.storedBytes() == 3, "one-influence storage is 1 B/vertex");
    }

    // Two influences: normalised weights, slot-major, unused slots zero.
    {
        BoneInfluenceMap map;
        map.setMaxBoneCount(32);
        Probe::build(map, {{{1, 1.0f}, {3, 3.0f}}, {{0, 1.0f}}, {}});
        const BoneInfluenceMap::Accessor accessor = map.accessor();
        check(accessor.slotCount() == 2, "two influences store two slots");
        const BoneInfluenceMap::IndicesType* index0 = accessor.indices(0);
        const BoneInfluenceMap::IndicesType* index1 = accessor.indices(1);
        const BoneInfluenceMap::WeightsType* weight0 = accessor.weights(0);
        const BoneInfluenceMap::WeightsType* weight1 = accessor.weights(1);
        check(index0 && index1 && weight0 && weight1, "two-slot arrays are present");
        if (index0 && index1 && weight0 && weight1) {
            check(index0[0] == 1 && index1[0] == 3, "slot 0 and slot 1 keep bone order");
            check(near(weight0[0], 0.25f) && near(weight1[0], 0.75f), "two influences are normalised");
            check(index0[1] == 0 && index1[1] == 0, "a vertex with one influence leaves slot 1 empty");
            check(near(weight0[1], 1.0f) && near(weight1[1], 0.0f), "a one-influence vertex weights slot 0 by 1");
            check(index0[2] == 0 && index1[2] == 0, "a vertex with no influence defaults to bone 0");
            check(near(weight0[2], 1.0f) && near(weight1[2], 0.0f), "a no-influence vertex weights slot 0 by 1");
            check(
                near(weight0[0] + weight1[0], 1.0f) && near(weight0[1] + weight1[1], 1.0f)
                    && near(weight0[2] + weight1[2], 1.0f),
                "every two-slot vertex partitions unity"
            );
            check(
                near(accessor.weight(1, 0), 0.25f) && near(accessor.weight(3, 0), 0.75f)
                    && near(accessor.weight(3, 1), 0.0f) && near(accessor.weight(0, 1), 1.0f)
                    && near(accessor.weight(0, 2), 1.0f),
                "a sparse lookup returns the slot weight or zero"
            );
        }
        check(map.storedBytes() == 30, "two-slot storage is 10 B/vertex");
    }

    // Five influences: still sparse (25 B/vertex).
    {
        BoneInfluenceMap map;
        map.setMaxBoneCount(32);
        Probe::build(map, {{{0, 1.0f}, {1, 1.0f}, {2, 1.0f}, {3, 1.0f}, {4, 1.0f}}});
        const BoneInfluenceMap::Accessor accessor = map.accessor();
        check(accessor.slotCount() == 5, "five influences store five slots");
        check(!accessor.dense(), "five influences use the sparse form");
        bool indicesOk = true;
        bool weightsOk = true;
        float sum = 0.0f;
        for (int slot = 0; slot < 5; ++slot) {
            const BoneInfluenceMap::IndicesType* indices = accessor.indices(slot);
            const BoneInfluenceMap::WeightsType* weights = accessor.weights(slot);
            indicesOk = indicesOk && indices && indices[0] == slot;
            weightsOk = weightsOk && weights && near(weights[0], 0.2f);
            if (weights)
                sum += weights[0];
        }
        check(indicesOk, "five influences keep all five bone indices");
        check(weightsOk && near(sum, 1.0f), "five influences are normalised");
        check(
            near(accessor.weight(0, 0), 0.2f) && near(accessor.weight(4, 0), 0.2f)
                && near(accessor.weight(5, 0), 0.0f),
            "a five-slot lookup finds every used slot and no other"
        );
        check(map.storedBytes() == 25, "five-slot storage is 25 B/vertex");
    }

    // Eight influences: the dense fallback, two sets of four, in group order.
    {
        BoneInfluenceMap map;
        map.setMaxBoneCount(32);
        Probe::build(
            map, {{{0, 1.0f}, {1, 1.0f}, {2, 1.0f}, {3, 1.0f}, {4, 1.0f}, {5, 1.0f}, {6, 1.0f}, {7, 1.0f}}}
        );
        const BoneInfluenceMap::Accessor accessor = map.accessor();
        check(accessor.slotCount() == 8, "eight influences store eight slots");
        check(accessor.dense(), "eight influences use the dense form");
        const BoneInfluenceMap::IndicesType* group0 = accessor.denseIndices(0);
        const BoneInfluenceMap::IndicesType* group1 = accessor.denseIndices(1);
        const BoneInfluenceMap::WeightsType* weight0 = accessor.denseWeights(0);
        const BoneInfluenceMap::WeightsType* weight1 = accessor.denseWeights(1);
        check(group0 && group1 && weight0 && weight1, "dense groups are present");
        if (group0 && group1 && weight0 && weight1) {
            bool indicesOk = true;
            bool weightsOk = true;
            for (int k = 0; k < 4; ++k) {
                indicesOk = indicesOk && group0[k] == k && group1[k] == 4 + k;
                weightsOk = weightsOk && near(weight0[k], 0.125f) && near(weight1[k], 0.125f);
            }
            check(indicesOk, "dense groups keep 0..3 then 4..7");
            check(weightsOk, "dense groups normalise eight equal weights to 1/8");
            check(
                near(accessor.weight(0, 0), 0.125f) && near(accessor.weight(7, 0), 0.125f)
                    && near(accessor.weight(8, 0), 0.0f),
                "a dense lookup reads both groups and rejects an unused bone"
            );
        }
        check(map.storedBytes() == 40, "dense storage is 40 B/vertex");
    }

    if (failures == 0) {
        std::printf("PASS: bone influence storage\n");
    } else {
        std::printf("FAILED %d check(s)\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
