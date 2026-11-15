#ifndef CTRL_TREEACTIONS_H
#define CTRL_TREEACTIONS_H

#include <QString>
#include <QVector>
#include "util/TreePos.h"

namespace img {
class ResourceNode;
class ResourceHandle;
}

namespace cmnd {
class ScopedMacro;
}

namespace core {
class Project;
class ObjectNode;
}

namespace ctrl {

// THE OBJECT TREE'S EDITS, as document operations.
//
// These used to live inside the tree's own widget (gui::ObjectTreeWidget), because
// the view's items were part of the undo record: a restructure had to push a command
// that moved the QTreeWidgetItems alongside the one that moved the nodes. The QML tree
// DERIVES its rows from the document, so there is nothing about the view to undo, and
// every edit here is exactly the document's:
//
//   * ONE undo step per user action (one macro, one notifier), whatever the action
//     creates -- a duplicated folder with 40 layers and 300 keys is one Ctrl+Z;
//   * the notifier is the DOCUMENT's (ObjectTreeNotifier / TimeLineUtil::Notifier /
//     ObjectNodeUtil::AttributeNotifier), so every view -- the tree, the inspector, the
//     graph panel -- hears the same event and re-derives from the same state.
//
// The GUI's asset plumbing (the resource dialog, the PSD relink walker) is NOT here:
// those open widgets and walk the resource tree, so they stay in gui and call these
// primitives once per node.
namespace tree {

// Rename one node (one command, one attribute event).
void rename(core::Project& aProject, core::ObjectNode& aNode, const QString& aName);

// Show/hide one node. Undoable, with an attribute event: the widget wrote the flag
// directly, so hiding a layer could not be taken back and no other view heard about it.
void setVisibility(core::Project& aProject, core::ObjectNode& aNode, bool aVisible);

// Contract/Enlarge: the slimmed row. A document edit, so it is undoable -- the widget
// wrote the flag directly, which left no way back but the menu item again.
void setSlimmed(core::Project& aProject, core::ObjectNode& aNode, bool aSlimmed);

// Whether nodes can be deleted: every one is non-null, has a parent (the document root
// cannot be deleted) and is still in the document.
bool canDelete(const QVector<core::ObjectNode*>& aNodes);

// Delete nodes as part of a macro the CALLER owns (the asset remover deletes the layers
// that referenced an image in the same undo step as the image). One ObjectTreeEvent
// names every target. Returns false, and pushes nothing, when canDelete() is false.
//
// Ordering is load-bearing and is done here: siblings are removed highest index first
// (a removal renumbers the ones after it), and a node whose ancestor is also being
// deleted is dropped (the ancestor takes it, and its own command would then name a node
// that no longer exists).
bool appendDeleteNodes(
    core::Project& aProject, const QVector<core::ObjectNode*>& aNodes, cmnd::ScopedMacro& aMacro
);

// Delete nodes as one undo step of their own.
void deleteNodes(core::Project& aProject, const QVector<core::ObjectNode*>& aNodes);

// Reorder/reparent: the document mover plus one ObjectTreeEvent(Type_Move). The two
// position lists are what ctrl::tree::DropTarget computed (see ObjectTreeModel).
void moveNodes(
    core::Project& aProject,
    const QVector<util::TreePos>& aRemoved,
    const QVector<util::TreePos>& aInserted
);

// A new empty folder directly after aIndex in aParent, with the given initial depth.
core::ObjectNode* createFolder(
    core::Project& aProject, core::ObjectNode& aParent, int aIndex, float aDepth
);

// A new layer from an imported asset (the resource dialog, and the PSD relink walker):
// the node takes the resource's own name, image, posture, blend mode, rect and
// visibility, and its z-order comes from aResIndex.
core::ObjectNode* createLayerFromResource(
    core::Project& aProject,
    core::ObjectNode& aParent,
    int aIndex,
    const img::ResourceNode& aResource,
    int aResIndex
);

// The folder counterpart of the above (a resource-tree folder becomes a clipped folder
// holding the assets below it).
core::ObjectNode* createFolderFromResource(
    core::Project& aProject,
    core::ObjectNode& aParent,
    int aIndex,
    const img::ResourceNode& aResource,
    int aResIndex
);

// A deep copy of aSource, inserted directly after it: the node, every key of every
// channel (with the keys that hang off a mesh/image key re-linked to their clones), its
// children, its visibility, blend mode, rect and depth. One undo step.
core::ObjectNode* duplicateNode(core::Project& aProject, core::ObjectNode& aSource);

// A Move/Rotate/Scale/Depth/Opacity/HSV key at frame 0 for the node and -- on a layer --
// its Pose/FFD channels, recursively for a folder. ONE undo step and ONE PushKey event,
// where the widget pushed one command per key.
void addDefaultKeys(core::Project& aProject, core::ObjectNode& aNode);

} // namespace tree
} // namespace ctrl

#endif // CTRL_TREEACTIONS_H
