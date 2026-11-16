// Phase 2's authored-weight ACTIONS, as headless behaviour:
//
//   * the Envelope/Baked/Mixed state a panel shows comes from the same target
//     set the bake writes;
//   * Bake writes the live layers only, Rebake replaces the field and refreshes
//     the mesh signature, Revert removes the state, Clear empties it but keeps
//     the bone Baked - each one undo step;
//   * one stroke is one value-replay command, rewritten while modifiable;
//   * a structural mesh edit remaps the authored field (index shifts drop, a
//     split's new vertex interpolates) in the same undo step;
//   * duplicating a node copies the fields onto the COPY's descendants and onto
//     an outside rig owner, instead of pointing at the originals.
//
// The brush's own visual result is the GUI probe/pixel gate; this one pins the
// document state and the undo semantics that the brush depends on.
#include <QApplication>
#include <QJsonObject>
#include <QOpenGLWidget>
#include <QOpenGLVersionFunctionsFactory>
#include <QMatrix4x4>
#include <QVector2D>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
#include "XC.h"
#include "XCAssert.h"
#include "cmnd/Stack.h"
#include "core/Bone2.h"
#include "core/BoneKey.h"
#include "core/FolderNode.h"
#include "core/GridMesh.h"
#include "core/ImageKey.h"
#include "core/MeshKey.h"
#include "core/Project.h"
#include "core/SkinWeights.h"
#include "img/ResourceNode.h"
#include "ctrl/TreeActions.h"
#include "ctrl/BoneParam.h"
#include "ctrl/bone/bone_BakeWeights.h"
#include "ctrl/bone/bone_MeshWeightRemap.h"
#include "ctrl/bone/bone_WeightCommands.h"
#include "ctrl/bone/bone_WeightState.h"
#include "ctrl/TimeLineUtil.h"
#include "gl/Global.h"
#include "util/Triangle2DPos.h"
#include "gui/AudioPlaybackWidget.h"

// Project.cpp references this GUI entry point; this gate never loads a .aemus.
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

namespace {

int failures = 0;

void check(bool aOk, const char* aWhat) {
    std::printf("%s  %s\n", aOk ? "PASS" : "FAIL", aWhat);
    if (!aOk) {
        ++failures;
    }
}

bool near(float aLhs, float aRhs) { return std::fabs(aLhs - aRhs) <= 1.0e-5f; }

// The deposit rule of the three brush modes, in isolation. The live probe only
// exercises Subtract, so Add and Replace need a headless gate: they are the modes
// a caller could silently get wrong without any screenshot looking odd.
void testWeightMath() {
    check(
        near(ctrl::applyWeightStamp(ctrl::WeightPaint_Add, 0.4f, 0.25f), 0.65f),
        "add: the stamp adds to the value underneath"
    );
    check(
        near(ctrl::applyWeightStamp(ctrl::WeightPaint_Add, 0.9f, 0.35f), 1.0f),
        "add: the field clamps at 1"
    );
    check(
        near(ctrl::applyWeightStamp(ctrl::WeightPaint_Subtract, 0.4f, 0.15f), 0.25f),
        "subtract: the stamp removes from the value underneath"
    );
    check(
        near(ctrl::applyWeightStamp(ctrl::WeightPaint_Subtract, 0.1f, 0.5f), 0.0f),
        "subtract: the field clamps at 0"
    );
    check(
        near(ctrl::applyWeightStamp(ctrl::WeightPaint_Replace, 0.9f, 0.2f), 0.2f),
        "replace: the value underneath is ignored"
    );
    check(
        near(ctrl::applyWeightStamp(ctrl::WeightPaint_Replace, 0.0f, 0.0f), 0.0f),
        "replace: a zero stamp writes zero rather than keeping the old value"
    );
}

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

class NullAnimator: public core::Animator {
public:
    core::Frame currentFrame() const override { return core::Frame(); }
    void stop() override {}
    void suspend() override {}
    void resume() override {}
    bool isSuspended() const override { return false; }
};

core::BoneShape makeShape(const QVector2D& aStart, const QVector2D& aDir) {
    core::BoneShape shape;
    shape.setSegment(util::Segment2D(aStart, aDir));
    shape.setRadius(QVector2D(1.0f, 1.0f), QVector2D(0.5f, 0.5f));
    QPolygonF polygon;
    polygon << QPointF(-4, -4) << QPointF(4, -4) << QPointF(4, 4) << QPointF(-4, 4);
    shape.setPolygon(polygon);
    return shape;
}

// One owner rig at frame 0 and one folder target carrying a MeshKey. A folder
// is enough for the influence path (`getAreaMesh` reads its timeline), and it
// keeps this gate free of a GL context.
struct Fixture {
    NullAnimator animator;
    core::Project project;
    core::FolderNode* owner = nullptr;
    core::FolderNode* target = nullptr;
    core::BoneKey* key = nullptr;
    core::Bone2* bone = nullptr;
    core::MeshKey* mesh = nullptr;

    Fixture(): project(QString(), animator, nullptr) {
        owner = new core::FolderNode(QStringLiteral("owner"));
        project.objectTree().grabTopNode(owner);
        target = new core::FolderNode(QStringLiteral("target"));
        owner->children().pushBack(target);

        key = new core::BoneKey();
        auto* root = new core::Bone2();
        key->data().topBones().push_back(root);
        root->updateWorldTransform();
        bone = new core::Bone2();
        root->children().pushBack(bone);
        bone->setWorldPos(QVector2D(0.0f, 1.0f), root);
        bone->setRange(0, QVector2D(1.0f, 1.0f));
        bone->setRange(1, QVector2D(1.0f, 1.0f));
        bone->setShape(makeShape(QVector2D(0.0f, 0.0f), QVector2D(0.0f, 4.0f)));
        bone->updateWorldTransform();
        owner->timeLine()->adoptKey(core::TimeKeyType_Bone, 0, key);

        mesh = new core::MeshKey();
        target->timeLine()->adoptKey(core::TimeKeyType_Mesh, 0, mesh);
    }

    void pushTriangle(const QVector<QVector2D>& aPos, const QVector<core::MeshVtx*>& aRef) {
        core::MeshFace* face = nullptr;
        project.commandStack().push(mesh->createTrianglePusher(aPos, aRef, &face));
    }

    void finishMesh() {
        mesh->updateVtxIndices();
        mesh->updateGLAttribute();
    }

    ctrl::bone::WeightState state() {
        return ctrl::bone::computeWeightState(project, *owner, *key, bone);
    }
};

// A 3x3 triangle grid; the corner/edge deletions in a shared mesh are the ones
// that actually shift later vertex indices (a single triangle's removal takes
// the whole mesh with it).
void buildGrid(Fixture& aFixture) {
    QVector<QVector2D> pos;
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            pos.push_back(QVector2D(float(x) * 2.0f, float(y) * 2.0f));
        }
    }
    QVector<core::MeshVtx*> verts(9, nullptr);
    const int tris[8][3] = {{0, 1, 3}, {0, 3, 4}, {1, 2, 4}, {1, 4, 5},
                            {3, 4, 6}, {3, 6, 7}, {4, 5, 7}, {4, 7, 8}};
    for (const auto& tri : tris) {
        const int before = aFixture.mesh->data().vertices().size();
        QVector<QVector2D> triPos;
        QVector<core::MeshVtx*> refs;
        for (int i = 0; i < 3; ++i) {
            triPos.push_back(pos[tri[i]]);
            refs.push_back(verts[tri[i]]);
        }
        aFixture.pushTriangle(triPos, refs);
        int next = before;
        for (int i = 0; i < 3; ++i) {
            if (!verts[tri[i]]) {
                verts[tri[i]] = aFixture.mesh->data().vertices()[next++];
            }
        }
    }
    aFixture.finishMesh();
}

core::SkinWeights::NodeWeights recordFor(
    core::ObjectNode& aNode, const core::LayerMesh& aMesh, int aBoneId,
    const QVector<core::SkinWeights::Entry>& aEntries
) {
    core::SkinWeights::NodeWeights record(
        aNode, aMesh.vertexCount(), core::SkinWeights::meshSignature(aMesh)
    );
    record.setBakedBone(aBoneId, aEntries);
    return record;
}

void testStateAndActions() {
    Fixture fixture;
    fixture.pushTriangle(
        {QVector2D(0, 0), QVector2D(4, 0), QVector2D(0, 4)},
        {nullptr, nullptr, nullptr}
    );
    fixture.finishMesh();

    int notifications = 0;
    fixture.project.onSkinWeightsModified.connect(
        [&notifications](core::ObjectNode&, bool) { ++notifications; }
    );

    {
        const auto state = fixture.state();
        check(state.kind == ctrl::bone::WeightState::Kind_Envelope, "a live bone starts Envelope");
        check(state.liveNodes == 1 && state.bakedNodes == 0, "the target set is the one mesh");
        check(state.canBake() && !state.canPaint(), "Envelope can be baked, not painted");
    }

    QString error;
    check(
        ctrl::bone::bakeBoneWeights(
            fixture.project, *fixture.owner, *fixture.key, *fixture.bone, false, &error
        ),
        "Bake writes the field"
    );
    check(error.isEmpty(), "Bake succeeds with no error");
    check(notifications == 1, "one bake emits one owner event");
    {
        const auto state = fixture.state();
        check(state.kind == ctrl::bone::WeightState::Kind_Baked, "after Bake the bone is Baked");
        check(state.bakedVertices > 0, "the baked field has vertices");
        check(state.canPaint() && state.canRebake() && state.canRevert() && state.canClear(),
              "a Baked bone offers every field action");
        check(!state.canBake(), "and Bake is no longer offered");
    }

    // The mesh-signature warning is a STATE, not a load failure: flipping the
    // stored hash makes the panel report a mismatch, and restoring it clears it.
    {
        core::SkinWeights::NodeWeights* stored = fixture.owner->skinWeights().find(*fixture.target);
        check(stored != nullptr, "the baked record is readable for the mismatch check");
        if (stored) {
            const quint64 signature = stored->meshSignature;
            stored->meshSignature ^= 1ULL;
            check(fixture.state().meshMismatch, "a changed mesh signature is surfaced as a mismatch");
            stored->meshSignature = signature;
            check(!fixture.state().meshMismatch, "and a restored signature clears it");
        }
    }

    // Bake is additive to the STATE: a second Bake must not replace the field.
    check(
        !ctrl::bone::bakeBoneWeights(
            fixture.project, *fixture.owner, *fixture.key, *fixture.bone, false, &error
        ),
        "a second Bake refuses (use Rebake)"
    );
    check(error.contains(QStringLiteral("already baked")), "the refusal says how to replace it");

    // One undo step, and the state comes back exactly.
    fixture.project.commandStack().undo();
    check(fixture.state().kind == ctrl::bone::WeightState::Kind_Envelope, "undo returns Envelope");
    fixture.project.commandStack().redo();
    check(fixture.state().kind == ctrl::bone::WeightState::Kind_Baked, "redo returns Baked");

    // Clear empties the field but leaves the STATE (presence) alone.
    check(
        ctrl::bone::clearBoneWeights(fixture.project, *fixture.owner, *fixture.bone, &error),
        "Clear empties the field"
    );
    {
        const auto state = fixture.state();
        check(state.kind == ctrl::bone::WeightState::Kind_Baked, "Clear keeps the bone Baked");
        check(state.bakedVertices == 0, "and the field is empty");
        check(!state.canClear(), "Clear is no longer offered");
    }
    fixture.project.commandStack().undo();
    check(fixture.state().bakedVertices > 0, "undo restores the cleared entries");

    // Revert removes the record itself, which is the Envelope state.
    check(
        ctrl::bone::revertBoneWeights(fixture.project, *fixture.owner, *fixture.bone, &error),
        "Revert removes the authored field"
    );
    check(fixture.state().kind == ctrl::bone::WeightState::Kind_Envelope, "Revert returns Envelope");
    fixture.project.commandStack().undo();
    check(fixture.state().kind == ctrl::bone::WeightState::Kind_Baked, "undo restores the field");

    // Rebake REPLACES the field and refreshes the signature (the mesh can have
    // changed since the first bake). Plant an entry the envelope does not
    // produce, then check the rebake reproduces the envelope field exactly.
    const int boneId = fixture.bone->boneId();
    {
        core::SkinWeights::NodeWeights* stored =
            fixture.owner->skinWeights().find(*fixture.target);
        check(stored != nullptr, "the baked record is on the owner");
        if (stored) {
            auto* baked = stored->bakedFor(boneId);
            check(baked != nullptr, "and it names the selected bone");
            if (baked) {
                baked->setWeight(2, 1.0f);
            }
            stored->meshSignature = 0xdeadbeefULL;
        }
    }
    check(
        ctrl::bone::bakeBoneWeights(
            fixture.project, *fixture.owner, *fixture.key, *fixture.bone, true, &error
        ),
        "Rebake replaces the field"
    );
    {
        QVector<core::SkinWeights::NodeWeights> expected;
        QString fieldsError;
        check(
            ctrl::bone::bakeBoneFields(
                fixture.project, *fixture.owner, *fixture.key, *fixture.bone, expected,
                &fieldsError
            ),
            "the envelope can be evaluated for the comparison"
        );
        const core::SkinWeights::NodeWeights* stored =
            fixture.owner->skinWeights().find(*fixture.target);
        check(
            stored && expected.size() == 1 && stored->sameWeightsAs(expected[0]),
            "the rebaked record is the current envelope field"
        );
        check(
            stored && stored->meshSignature == core::SkinWeights::meshSignature(fixture.mesh->data()),
            "Rebake refreshes the mesh signature"
        );
        check(fixture.state().meshMismatch == false, "so the state no longer warns");
    }
}

void testStrokeCommand() {
    Fixture fixture;
    fixture.pushTriangle(
        {QVector2D(0, 0), QVector2D(4, 0), QVector2D(0, 4)},
        {nullptr, nullptr, nullptr}
    );
    fixture.finishMesh();

    const int boneId = fixture.bone->boneId();
    core::SkinWeights::NodeWeights before = recordFor(
        *fixture.target, fixture.mesh->data(), boneId,
        {{0, 0.25f}, {1, 0.5f}, {2, 0.75f}}
    );
    core::SkinWeights::NodeWeights after = before;
    after.bakedFor(boneId)->setWeight(0, 0.9f);
    after.bakedFor(boneId)->setWeight(2, 0.0f);   // a stroke can remove an entry

    auto* command = new ctrl::bone::AssignSkinWeightsCommand(
        fixture.project, *fixture.owner, {before}, {after}, QStringLiteral("Paint bone weights")
    );
    fixture.project.commandStack().push(command);
    {
        const auto* stored = fixture.owner->skinWeights().find(*fixture.target);
        check(stored && near(stored->bakedFor(boneId)->weightAt(0), 0.9f), "the stroke writes the value");
        check(stored && stored->bakedFor(boneId)->find(2) == nullptr, "and removes a zeroed entry");
    }
    check(
        fixture.project.commandStack().isModifiable(command),
        "one stroke command is the stack's modifiable command"
    );

    // The rewrite path: the same command carries the newer state, and an undo
    // still returns the stroke's FIRST state - not the last rewritten one.
    core::SkinWeights::NodeWeights rewrote = after;
    rewrote.bakedFor(boneId)->setWeight(1, 0.2f);
    command->setAfter(rewrote);
    fixture.owner->skinWeights().setNodeWeights(rewrote);
    fixture.project.commandStack().undo();
    {
        const auto* stored = fixture.owner->skinWeights().find(*fixture.target);
        check(stored && near(stored->bakedFor(boneId)->weightAt(0), 0.25f),
              "undo returns the value from BEFORE the stroke");
        check(stored && near(stored->bakedFor(boneId)->weightAt(1), 0.5f),
              "and the rewritten entry too");
    }
    fixture.project.commandStack().redo();
    {
        const auto* stored = fixture.owner->skinWeights().find(*fixture.target);
        check(stored && near(stored->bakedFor(boneId)->weightAt(1), 0.2f),
              "redo applies the stroke's final state");
    }

    check(
        ctrl::bone::AssignSkinWeightsCommand(
            fixture.project, *fixture.owner, {before}, {before}, QStringLiteral("noop")
        ).isUseless(),
        "a stroke that changes nothing is a useless command"
    );
}

void testMeshRemap() {
    Fixture fixture;
    buildGrid(fixture);
    check(fixture.mesh->data().vertices().size() == 9, "the grid has nine vertices");
    check(fixture.mesh->data().faces().size() == 8, "and eight triangles");

    const int boneId = fixture.bone->boneId();
    QVector<core::SkinWeights::Entry> entries;
    for (int i = 0; i < 9; ++i) {
        entries.push_back(core::SkinWeights::Entry{quint32(i), 0.1f * float(i + 1)});
    }
    core::SkinWeights::NodeWeights record =
        recordFor(*fixture.target, fixture.mesh->data(), boneId, entries);
    fixture.owner->skinWeights().setNodeWeights(record);
    const quint64 oldSignature = record.meshSignature;

    // Delete vertex 1: it takes the three faces it belongs to with it, and
    // vertex 2 too (nothing else uses it), while the rest of the grid survives.
    // The surviving indices after the deleted pair shift down, which is exactly
    // what an index-addressed field must follow.
    const QList<core::MeshVtx*> oldVertices = fixture.mesh->data().vertices();
    core::MeshVtx* deleted = oldVertices[1];
    check(deleted != nullptr, "the vertex to delete exists");
    // Constructed BEFORE the geometry runs, exactly like the editor's macro
    // (the old edge set has to describe the pre-edit mesh).
    auto* remap = ctrl::bone::createMeshWeightsRemap(
        fixture.project, *fixture.target, *fixture.mesh, oldVertices
    );
    check(remap != nullptr, "a mesh edit with a record creates the remap command");
    fixture.project.commandStack().push(fixture.mesh->createRemover(*deleted));
    fixture.mesh->updateVtxIndices();
    fixture.mesh->updateGLAttribute();
    if (remap) {
        remap->tryExec();
        fixture.mesh->updateVtxIndices();
        fixture.mesh->updateGLAttribute();
    }

    {
        const auto* stored = fixture.owner->skinWeights().find(*fixture.target);
        check(stored != nullptr, "the record survives the edit");
        const auto* baked = stored ? stored->bakedFor(boneId) : nullptr;
        check(baked != nullptr, "and the bone stays Baked");
        check(stored && stored->vertexCount == fixture.mesh->data().vertices().size(),
              "the record's vertex count follows the mesh");
        check(stored && stored->meshSignature == core::SkinWeights::meshSignature(fixture.mesh->data()),
              "the signature is refreshed (not left warning about a handled edit)");
        check(stored && stored->meshSignature != oldSignature, "the structural edit did change it");
        check(baked && near(baked->weightAt(0), 0.1f), "a surviving vertex keeps its value");
        // Every surviving vertex, wherever the removal shifted it to, still
        // carries its own value; every removed vertex's entry is gone.
        bool mapped = baked != nullptr;
        int survivors = 0;
        if (baked) {
            for (int i = 0; i < oldVertices.size(); ++i) {
                const int at = fixture.mesh->data().vertices().indexOf(oldVertices[i]);
                if (at < 0) {
                    continue;
                }
                ++survivors;
                mapped = mapped && near(baked->weightAt(quint32(at)), 0.1f * float(i + 1));
            }
            check(mapped, "every surviving vertex keeps its value at its new index");
            check(survivors < oldVertices.size(), "the deletion actually removed a vertex");
            check(
                int(baked->entries.size()) == survivors,
                "the field has one entry per surviving vertex"
            );
        }
    }

    if (remap) {
        remap->tryUndo();
        fixture.mesh->updateVtxIndices();
        fixture.mesh->updateGLAttribute();
        const auto* stored = fixture.owner->skinWeights().find(*fixture.target);
        check(
            stored && stored->meshSignature == oldSignature && stored->bakedFor(boneId)
                && near(stored->bakedFor(boneId)->weightAt(1), 0.2f),
            "undo restores the pre-edit indices and signature"
        );
        remap->tryExec();
        fixture.mesh->updateGLAttribute();
    }

    // SPLIT: the new centre vertex interpolates from the two vertices of the
    // edge it was created on (the plan's rule), in the same undo step.
    {
        Fixture split;
        split.pushTriangle(
            {QVector2D(0, 0), QVector2D(4, 0), QVector2D(0, 4)},
            {nullptr, nullptr, nullptr}
        );
        split.finishMesh();
        const int edgeCount = split.mesh->data().edges().size();
        core::MeshEdge* edge = nullptr;
        for (core::MeshEdge* candidate : split.mesh->data().edges()) {
            if ((candidate->vtx(0) == split.mesh->data().vertices()[0]
                 && candidate->vtx(1) == split.mesh->data().vertices()[1])
                || (candidate->vtx(1) == split.mesh->data().vertices()[0]
                    && candidate->vtx(0) == split.mesh->data().vertices()[1])) {
                edge = candidate;
            }
        }
        check(edgeCount == 3 && edge != nullptr, "the split fixture has the 0-1 edge");
        core::MeshFace* face = split.mesh->data().faces().first();
        const int splitBoneId = split.bone->boneId();
        split.owner->skinWeights().setNodeWeights(recordFor(
            *split.target, split.mesh->data(), splitBoneId, {{0, 0.2f}, {1, 0.8f}, {2, 0.4f}}
        ));
        const QList<core::MeshVtx*> old = split.mesh->data().vertices();
        core::MeshVtx* tail = nullptr;
        // Pre-edit construction, as the editor's macro does (the old edge set
        // is what tells the split centre which pair it was created on).
        auto* splitRemap = ctrl::bone::createMeshWeightsRemap(
            split.project, *split.target, *split.mesh, old
        );
        check(splitRemap != nullptr, "the split creates a remap command");
        if (edge) {
            split.project.commandStack().push(
                split.mesh->createSplitter(*face, *edge, QVector2D(2.0f, 0.0f), &tail)
            );
        }
        split.mesh->updateVtxIndices();
        split.mesh->updateGLAttribute();
        if (splitRemap) {
            splitRemap->tryExec();
            split.mesh->updateVtxIndices();
            split.mesh->updateGLAttribute();
        }
        const auto* stored = split.owner->skinWeights().find(*split.target);
        const auto* baked = stored ? stored->bakedFor(splitBoneId) : nullptr;
        check(baked != nullptr, "the split keeps the field");
        check(
            baked && tail && near(baked->weightAt(quint32(tail->index())), 0.5f),
            "the split centre interpolates 0.2/0.8 at the midpoint to 0.5"
        );
        check(
            baked && near(baked->weightAt(0), 0.2f) && near(baked->weightAt(1), 0.8f),
            "the flank vertices keep their own values"
        );
        if (splitRemap) {
            splitRemap->tryUndo();
            split.mesh->updateGLAttribute();
            const auto* before = split.owner->skinWeights().find(*split.target);
            check(
                before && before->bakedFor(splitBoneId)->entries.size() == 3,
                "undo removes the interpolated centre entry"
            );
        }
    }
}


void fillSolidResource(img::ResourceNode& aResource, const QSize& aSize) {
    uint8* pixels = new uint8[size_t(aSize.width()) * aSize.height() * 4];
    std::fill(pixels, pixels + size_t(aSize.width()) * aSize.height() * 4, uint8(255));
    aResource.updateHandle(
        XCMemBlock(pixels, size_t(aSize.width()) * aSize.height() * 4),
        QRect(QPoint(0, 0), aSize)
    );
}

// A target whose area mesh is an ImageKey's own grid. A cell-size change
// regenerates that grid, and the authored field must follow the new vertices
// in the same undo step: by barycentric interpolation inside the old mesh and
// by a neighbour fill outside it. The old scheme (leave the record alone and
// warn) applied the values to the wrong vertices silently whenever the new
// grid happened to keep the same vertex count and indices.
struct GridFixture {
    NullAnimator animator;
    core::Project project;
    core::FolderNode* owner = nullptr;
    core::FolderNode* target = nullptr;
    core::BoneKey* key = nullptr;
    core::Bone2* bone = nullptr;
    core::Bone2* second = nullptr;
    img::ResourceNode resource;
    core::ImageKey* image = nullptr;

    GridFixture():
        project(QString(), animator, nullptr), resource(QStringLiteral("grid-art")) {
        owner = new core::FolderNode(QStringLiteral("owner"));
        project.objectTree().grabTopNode(owner);
        target = new core::FolderNode(QStringLiteral("target"));
        owner->children().pushBack(target);

        key = new core::BoneKey();
        auto* root = new core::Bone2();
        key->data().topBones().push_back(root);
        root->updateWorldTransform();
        bone = new core::Bone2();
        root->children().pushBack(bone);
        bone->setWorldPos(QVector2D(0.0f, 1.0f), root);
        bone->setRange(0, QVector2D(1.0f, 1.0f));
        bone->setRange(1, QVector2D(1.0f, 1.0f));
        bone->setShape(makeShape(QVector2D(0.0f, 0.0f), QVector2D(0.0f, 4.0f)));
        bone->updateWorldTransform();
        second = new core::Bone2();
        root->children().pushBack(second);
        second->setWorldPos(QVector2D(1.0f, 1.0f), root);
        second->setRange(0, QVector2D(1.0f, 1.0f));
        second->setRange(1, QVector2D(1.0f, 1.0f));
        second->setShape(makeShape(QVector2D(1.0f, 0.0f), QVector2D(0.0f, 4.0f)));
        second->updateWorldTransform();
        owner->timeLine()->adoptKey(core::TimeKeyType_Bone, 0, key);

        fillSolidResource(resource, QSize(64, 64));
    }

    core::ImageKey* addImage(int aFrame, int aCell) {
        auto* added = new core::ImageKey();
        added->setImage(resource.handle());
        added->resetGridMesh(aCell);
        target->timeLine()->adoptKey(core::TimeKeyType_Image, aFrame, added);
        return added;
    }

    core::SkinWeights::NodeWeights makeRecord(const core::GridMesh& aMesh) {
        QVector<core::SkinWeights::Entry> constant;
        QVector<core::SkinWeights::Entry> gradient;
        constant.reserve(aMesh.vertexCount());
        gradient.reserve(aMesh.vertexCount());
        for (int i = 0; i < aMesh.vertexCount(); ++i) {
            const float x = aMesh.positions()[i].x;
            constant.push_back(core::SkinWeights::Entry{quint32(i), 0.7f});
            gradient.push_back(core::SkinWeights::Entry{quint32(i), x / 64.0f});
        }
        core::SkinWeights::NodeWeights record(
            *target, aMesh.vertexCount(), core::SkinWeights::meshSignature(aMesh)
        );
        record.setBakedBone(bone->boneId(), constant);
        record.setBakedBone(second->boneId(), gradient);
        return record;
    }

    ctrl::bone::WeightState state() {
        return ctrl::bone::computeWeightState(project, *owner, *key, bone);
    }
};

void testGridWeightsRemap() {
    {
        // The barycentric coordinates used for the attribute are the same ones
        // the transition uses for the position.
        const util::Triangle2D tri(QVector2D(0, 0), QVector2D(4, 0), QVector2D(0, 4));
        const QVector2D point(1.0f, 2.0f);
        const util::Triangle2DPos pos = util::Triangle2DPos::make(tri, point);
        const std::array<float, 3> weights = pos.weights();
        check(
            pos.isValid()
                && std::fabs(weights[0] + weights[1] + weights[2] - 1.0f) < 1.0e-6f
                && (pos.get(tri) - point).length() < 1.0e-5f,
            "Triangle2DPos::weights sums to one and reconstructs the point"
        );
    }

    GridFixture fixture;
    fixture.image = fixture.addImage(0, 16);
    const core::GridMesh& oldMesh = fixture.image->data().gridMesh();
    check(oldMesh.vertexCount() > 4, "the cell-16 grid has vertices");
    const core::SkinWeights::NodeWeights before = fixture.makeRecord(oldMesh);
    fixture.owner->skinWeights().setNodeWeights(before);
    const quint64 oldSignature = before.meshSignature;
    check(
        fixture.state().kind == ctrl::bone::WeightState::Kind_Baked
            && !fixture.state().meshMismatch,
        "the field starts Baked against the old grid"
    );

    int notifications = 0;
    fixture.project.onSkinWeightsModified.connect(
        [&notifications](core::ObjectNode&, bool) { ++notifications; }
    );

    ctrl::TimeLineUtil::assignImageKeyCellSize(fixture.project, *fixture.target, 0, 8);
    check(fixture.image->data().gridMesh().cellSize() == 8, "the cell size edit regenerates the grid");
    check(notifications == 1, "the remap announces one owner change");
    check(
        fixture.state().kind == ctrl::bone::WeightState::Kind_Baked
            && !fixture.state().meshMismatch,
        "the state is Baked against the new grid (no stale signature)"
    );

    const core::GridMesh& newMesh = fixture.image->data().gridMesh();
    const auto* after = fixture.owner->skinWeights().find(*fixture.target);
    check(after != nullptr, "the record survives the grid change");
    check(
        after && after->vertexCount == newMesh.vertexCount()
            && after->meshSignature == core::SkinWeights::meshSignature(newMesh)
            && after->meshSignature != oldSignature,
        "the record carries the new vertex count and signature"
    );
    const auto* constant = after ? after->bakedFor(fixture.bone->boneId()) : nullptr;
    const auto* gradient = after ? after->bakedFor(fixture.second->boneId()) : nullptr;
    check(constant && gradient, "both baked bones survive");

    int constantWrong = 0;
    int gradientWrong = 0;
    int gradientChecked = 0;
    if (constant && gradient) {
        for (int i = 0; i < newMesh.vertexCount(); ++i) {
            if (std::fabs(constant->weightAt(quint32(i)) - 0.7f) > 1.0e-4f) {
                ++constantWrong;
            }
            const gl::Vector3& p = newMesh.positions()[i];
            if (p.x > 8.0f && p.x < 56.0f && p.y > 8.0f && p.y < 56.0f) {
                ++gradientChecked;
                if (std::fabs(gradient->weightAt(quint32(i)) - p.x / 64.0f) > 0.02f) {
                    ++gradientWrong;
                }
            }
        }
    }
    check(
        constant && constantWrong == 0,
        "the constant field is preserved on every new vertex, the filled boundary included"
    );
    check(
        gradient && gradientChecked > 0 && gradientWrong == 0,
        "the gradient is barycentrically exact across the old triangles"
    );

    fixture.project.commandStack().undo();
    {
        const auto* restored = fixture.owner->skinWeights().find(*fixture.target);
        check(restored && restored->sameWeightsAs(before), "undo restores the exact pre-edit records");
        check(fixture.image->data().gridMesh().cellSize() == 16, "and the old grid");
        check(
            fixture.state().kind == ctrl::bone::WeightState::Kind_Baked
                && !fixture.state().meshMismatch,
            "with no signature warning"
        );
        check(notifications == 2, "undo announces the owner change too");
    }

    fixture.project.commandStack().redo();
    {
        const auto* redone = fixture.owner->skinWeights().find(*fixture.target);
        check(
            redone && redone->vertexCount == newMesh.vertexCount()
                && redone->meshSignature == core::SkinWeights::meshSignature(newMesh),
            "redo restores the remapped record"
        );
        check(fixture.image->data().gridMesh().cellSize() == 8, "and the new grid");
    }

    {
        // The same path serves an image replacement (and an external reload):
        // the grid is regenerated from the new resource, and the field is
        // remapped from the old grid in the replacement's own undo step.
        GridFixture replace;
        replace.image = replace.addImage(0, 16);
        const core::GridMesh& replaceOld = replace.image->data().gridMesh();
        const core::SkinWeights::NodeWeights replaceBefore = replace.makeRecord(replaceOld);
        replace.owner->skinWeights().setNodeWeights(replaceBefore);

        img::ResourceNode other(QStringLiteral("grid-art-2"));
        fillSolidResource(other, QSize(32, 32));
        ctrl::TimeLineUtil::assignImageKeyResource(replace.project, *replace.target, 0, other);

        const core::GridMesh& replaceNew = replace.image->data().gridMesh();
        const auto* replaced = replace.owner->skinWeights().find(*replace.target);
        check(
            replaced && replaceNew.vertexCount() > 0
                && replaced->vertexCount == replaceNew.vertexCount()
                && replaced->meshSignature == core::SkinWeights::meshSignature(replaceNew),
            "an image replacement remaps the record onto the new grid"
        );
        const auto* replacedConstant =
            replaced ? replaced->bakedFor(replace.bone->boneId()) : nullptr;
        int replacedWrong = 0;
        if (replacedConstant && replaced) {
            for (int i = 0; i < replaceNew.vertexCount(); ++i) {
                if (std::fabs(replacedConstant->weightAt(quint32(i)) - 0.7f) > 1.0e-4f) {
                    ++replacedWrong;
                }
            }
        }
        check(
            replacedConstant && replacedWrong == 0,
            "the replacement keeps the authored field on every new vertex"
        );
        replace.project.commandStack().undo();
        const auto* restored = replace.owner->skinWeights().find(*replace.target);
        check(
            restored && restored->sameWeightsAs(replaceBefore)
                && replace.image->data().gridMesh().cellSize() == 16,
            "undo restores the old image's grid and field together"
        );
    }

    {
        // A second image key with the same image and cell size has the same
        // signature as the first. Changing THAT key must not touch a field the
        // bone frame does not read from it: the topological signature alone
        // cannot tell the two grids apart.
        GridFixture guard;
        core::ImageKey* first = guard.addImage(0, 16);
        core::ImageKey* second = guard.addImage(10, 16);
        const core::SkinWeights::NodeWeights guarded = guard.makeRecord(first->data().gridMesh());
        guard.owner->skinWeights().setNodeWeights(guarded);
        int guardedNotifications = 0;
        guard.project.onSkinWeightsModified.connect(
            [&guardedNotifications](core::ObjectNode&, bool) { ++guardedNotifications; }
        );
        ctrl::TimeLineUtil::assignImageKeyCellSize(guard.project, *guard.target, 10, 8);
        const auto* untouched = guard.owner->skinWeights().find(*guard.target);
        check(
            untouched && untouched->sameWeightsAs(guarded) && guardedNotifications == 0,
            "a grid no bone frame reads is left alone (same signature is not identity)"
        );
        check(
            second->data().gridMesh().cellSize() == 8, "while the edited key did regenerate"
        );
    }
}

void testDuplication() {
    NullAnimator animator;
    core::Project project(QString(), animator, nullptr);
    auto* top = new core::FolderNode(QStringLiteral("top"));
    project.objectTree().grabTopNode(top);
    auto* group = new core::FolderNode(QStringLiteral("group"));
    auto* a = new core::FolderNode(QStringLiteral("a"));
    auto* b = new core::FolderNode(QStringLiteral("b"));
    top->children().pushBack(group);
    group->children().pushBack(a);
    group->children().pushBack(b);

    // group is a rig owner with fields for a and b; top is an OUTSIDE owner that
    // also carries a field for group and for a (the duplicate must inherit both).
    core::SkinWeights::NodeWeights groupA(*a, 3, 0x55ULL);
    groupA.setBakedBone(11, {{0, 0.3f}, {2, 0.6f}});
    group->skinWeights().setNodeWeights(groupA);
    core::SkinWeights::NodeWeights groupB(*b, 2, 0x66ULL);
    groupB.setBakedBone(12, {{1, 0.7f}});
    group->skinWeights().setNodeWeights(groupB);

    core::SkinWeights::NodeWeights topGroup(*group, 4, 0x77ULL);
    topGroup.setBakedBone(21, {{3, 0.9f}});
    top->skinWeights().setNodeWeights(topGroup);
    core::SkinWeights::NodeWeights topA(*a, 3, 0x55ULL);
    topA.setBakedBone(22, {{1, 0.2f}});
    top->skinWeights().setNodeWeights(topA);

    core::ObjectNode* copy = ctrl::tree::duplicateNode(project, *group);
    check(copy != nullptr, "the group duplicated");
    check(copy && copy->children().size() == 2, "with its two children");
    if (copy) {
        core::ObjectNode* copyA = copy->children().front();
        core::ObjectNode* copyB = copy->children().back();
        const auto* copiedA = copy->skinWeights().find(*copyA);
        const auto* copiedB = copy->skinWeights().find(*copyB);
        check(copiedA && copiedA->isBaked(11), "the copy's own rig field for its child is copied");
        check(copiedB && copiedB->isBaked(12), "and the other child's too");
        check(
            copiedA && near(copiedA->bakedFor(11)->weightAt(2), 0.6f),
            "with the authored values"
        );
        check(
            copy->skinWeights().find(*a) == nullptr,
            "the copy does not address the ORIGINAL's children"
        );
        check(
            top->skinWeights().find(*copy) != nullptr,
            "an outside rig owner gets a field for the copied group"
        );
        check(
            top->skinWeights().find(*copyA) != nullptr,
            "and for the copied descendant it already skinned"
        );
    }

    project.commandStack().undo();
    check(
        copy && top->skinWeights().find(*copy) == nullptr,
        "undo removes the outside owner's copied field"
    );
    check(
        group->children().size() == 2 && top->children().size() == 1,
        "and the copy is gone from the tree"
    );
    project.commandStack().redo();
    check(
        copy && top->skinWeights().find(*copy) != nullptr,
        "redo restores the outside owner's field"
    );
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // A live core-profile context: MeshKey::updateGLAttribute builds the index
    // buffer the mesh signature is hashed from, and that is a GL buffer. The
    // suite runs this target under Xvfb (scripts/run_suites.sh's GL list).
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setVersion(gl::Global::kVersion.first, gl::Global::kVersion.second);
    QSurfaceFormat::setDefaultFormat(format);
    QOpenGLWidget widget;
    widget.resize(4, 4);
    widget.show();
    widget.makeCurrent();
    if (!widget.context()) {
        std::printf("FAIL  no GL context\n");
        return 1;
    }
    auto* funcs = QOpenGLVersionFunctionsFactory::get<gl::Global::Functions>(widget.context());
    if (!funcs || !funcs->initializeOpenGLFunctions()) {
        std::printf("FAIL  GL functions init\n");
        return 1;
    }
    gl::Global::setContext(widget);
    gl::Global::setFunctions(*funcs);

    testWeightMath();
    testStateAndActions();
    testStrokeCommand();
    testMeshRemap();
    testGridWeightsRemap();
    testDuplication();

    if (failures) {
        std::printf("%d failure(s)\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
