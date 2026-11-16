#ifndef CTRL_BONE_MESHWEIGHTREMAP_H
#define CTRL_BONE_MESHWEIGHTREMAP_H

#include <QHash>
#include <QList>
#include <QSet>
#include <QVector>
#include "cmnd/Base.h"
#include "core/MeshKey.h"
#include "core/SkinWeights.h"
#include "util/LinkPointer.h"

namespace core {
class ObjectNode;
class Project;
} // namespace core

namespace ctrl {
namespace bone {

// ONE structural mesh edit as ONE undoable remap of every authored field that
// addresses the node.
//
// Vertex indices ARE the storage key, so an edit that removes or inserts a
// vertex changes what an entry names. This command captures the mesh's vertex
// objects and records BEFORE the geometry commands execute (it is pushed after
// them in the same macro, so its exec runs with the new geometry) and remaps:
//
//   * a surviving vertex keeps its weights at its new index;
//   * a removed vertex's entries are dropped;
//   * a NEW vertex on a former edge interpolates from that edge's two vertices
//     (the plan's split rule), using the old edge set to pick the edge the new
//     point lies on; a new vertex with no such edge starts at 0.
//
// The record's vertex count and mesh signature are refreshed to the edited mesh,
// so a successful remap leaves the field current instead of warning about a
// change it already handled. Undo restores the exact before records. The index
// buffer is rebuilt by mesh_Notifier after the macro, which is why the signature
// is hashed from the vertex indices here rather than from the stale GL array.
//
// Returns null when no rig owner above the node carries a record for it (the
// common case: most mesh edits touch no authored weights) - the caller then
// pushes nothing.
cmnd::Base* createMeshWeightsRemap(
    core::Project& aProject, core::ObjectNode& aNode, core::MeshKey& aMeshKey,
    const QList<core::MeshVtx*>& aOldVertices
);

} // namespace bone
} // namespace ctrl

#endif // CTRL_BONE_MESHWEIGHTREMAP_H
