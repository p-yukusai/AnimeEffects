# Authored bone weights: painting, constraints, and the 0.22 format

Status: **Phases 1-2 landed; Phases 3-4 are proposals; Phase 5's mesh remap,
grid remap and duplication copy are landed.** Revised 2026-11-12 around the per-bone
Envelope/Baked state (the rasterize analogy), implemented through the headless
half and then the PaintWeights panel/brush. "Landed" below means code and a
named gate exist; "proposal" means open to being overturned before code lands.
The envelope model, the influence cache and the weight heatmap are the starting
point; the product source is `notes.md` (lines 82, 92, 94, 129, 141, 143, 145)
and the open questions in `docs/design/skeletal/influence-display.md` §6.

### Landed in this phase (2026-11-12)

- Format **0.22**: `Bone2` writes a stable project-local id, and every object
  node carries a length-checked `SkinWgt_` block of authored fields inside its
  own `ObjNode` block. A 0.21 file loads envelope-only and gains ids on its
  first save. `core::FormatVersion.h` has the history entry.
- `core::SkinWeights` on `ObjectNode`: a node's baked records, keyed by bone id;
  `BakedBone` presence is the state; entries sorted/sparse; records are
  life-linked so a deleted target is dormant, not dangling.
- `BoneInfluenceMap::writeAsync` takes the target node's record and merges it
  per bone; an absent record keeps the old envelope path exactly. The authored
  snapshot is copied before the worker task starts.
- `ctrl::bone::BakeWeightsCommand` + `bakeBoneFields`: the headless bake, the
  shared-rig refusal, and one undo step. The command stores values and never
  recomputes from the editor.
- `Project::onSkinWeightsModified` / `BoneKeyUpdater::onSkinWeightsModified`
  reset every BoneKey of the owner, because one field serves all frames.
- Gates: `tests/bone_ids.cpp`, `tests/skin_weights.cpp`,
  `tests/weight_bake.cpp`; `tests/weight_roundtrip.cpp`. The influence-merge
  branch is also covered by `weight_bake` rather than a second writer in
  `bone_influence_storage`.
- Measured: the full Debug suite is **82 pass / 1 fail**; the one failure is the
  documented pre-existing `object_tree_panel` six-check rename/selection defect
  (`influence-cache-plan.md` §7), unchanged by this work. `test.anie` and
  `tart.anie` load as 0.21, save as 0.22 and reload under `gpu_headless`
  (0 load failures); `--bone-stats` on `2.anie` still reports 38 keys, 134
  bones, the two-key node 164 intact.

Deviations from the first proposal, recorded because a later reader will ask:

- The block tag is the codebase's `SkinWgt_` 8-byte signature plus a `uint64`
  length (`Serializer::beginBlock`), not a `"WGT0"` tag plus an `int32`. It is the
  same self-describing boundary the plan required and the same layout every
  other block uses. Mesh signatures are two `int32` words after the vertex count.
- A save **does not refresh** a record's mesh signature. If it did, every save
  would hide a mesh edit the load warning exists to report. The signature is the
  bake-time mesh, and a changed mesh is expected to keep the old field dormant
  until Phase 5 remaps it.
- A bake writes a record for every influenced node with a mesh at the key's
  frame, even when the selected bone's field is empty on that node. The record's
  presence is the Baked state; a node with no mesh is skipped and stays
  Envelope (Mixed on the bone).
- The bake computes through `BoneKey::collectInfluenceTargets`, a refactor of
  the old `resetCacheListRecursive` selection, so the bake target set and the
  cache target set cannot drift apart.

### Landed in Phase 2 (2026-11-12)

- `BoneEditMode_PaintWeights` + `ctrl::bone::PaintWeightsMode`: right-click
  selects a bone, left paints only the BAKED nodes of the selected bone; the
  stamp is a world-space circle (each vertex is mapped with the target matrix
  times the influence map matrix, the exact product the build uses) and is
  accelerated by a per-stroke coarse cell grid, so a dense mesh does not cost a
  full vertex pass per stamp. Add/Subtract/Replace is `BoneParam::weightMode`.
- One stroke is one `AssignSkinWeightsCommand`, rewritten while
  `Stack::isModifiable` holds; stamps are queued and drained by
  `flushPendingWork`, so the cache rebuild is once per painted FRAME, not per
  stamp. The command owns the `onSkinWeightsModified` event on every replay;
  `Driver::updateSkinWeights` marks one re-blend for the next paint without
  recreating the mode mid-stroke.
- The actions are `ctrl::bone::bakeBoneWeights` (Bake writes only live nodes;
  an already-baked layer keeps its paint), the same with `aReplace` for Rebake
  (replaces the selected bone and refreshes vertex count/signature),
  `revertBoneWeights` (removes the record / the bone from it) and
  `clearBoneWeights` (empty entries, stays Baked). Each is one
  `AssignSkinWeightsCommand`/`BakeWeightsCommand` undo step.
- The panel: `bone::WeightState` (Envelope/Baked/Mixed + baked/live nodes,
  entry count, mesh mismatch) as the dock's stat row (a "Weights" label, the
  state word, and the count on its own muted line), one primary action button
  (Bake on an envelope/mixed bone, Rebake once baked) and an overflow menu
  (Rebake All on mixed, Revert to Envelope, Clear Weights), the
  Add/Subtract/Replace chooser and the shared brush (disabled with a tooltip on
  a live bone). The explanations live in the state row's and the buttons'
  tooltips, not in visible paragraphs. The canvas draws
  only the skeleton and the heatmap: an earlier draft's red envelope fills, a
  floating state label and a baked-joint dot were removed in UI review (they
  read as a strange overlay and competed with the field; the bone visual itself
  is still the undecided note in `notes.md`).
- The mesh-signature rule is now **topological** (`hash(vertexCount, index
  buffer)`), and a structural edit is remapped in the same undo step by
  `createMeshWeightsRemap` (see §3.4/§7). Duplication copies authored fields
  with the node pointers remapped (§4.3).
- `tests/weight_paint_commands` (GL, Xvfb in the suite): state, Bake/Bake
  refusal/Rebake/Revert/Clear and their undo, the one-command stroke rewrite,
  the index-shift remap and the split interpolation (0.2/0.8 at the midpoint ->
  0.5), and duplication's internal/external record copy.
- The live `scripts/gui/probe_weights_bake_paint.sh` (pagedoll.anie): mode and
  panel reachable, brush disabled before a bone is selected, a canvas press
  selects the nearest bone and refuses to paint a live one, Bake is
  **pixel-identical** on an artwork crop (0 differing pixels; the bake does not
  move the artwork), a Subtract stroke changes the **canvas** (the weight wash;
  3853 differing pixels), one undo restores it exactly (0), redo repaints it
  exactly (0), Revert returns Envelope and disables the brush, Rebake
  reproduces the initial bake exactly (0), Clear removes the wash while keeping
  the bone Baked and undo restores it exactly (0), and Ctrl+S plus a fresh
  process reads the baked state back.

Decisions taken for Phase 2 (the open items in §12): **explicit Bake**, not
Bake-and-paint; the `Mixed` rule is **Bake with live nodes only** (a second
Bake on a fully baked bone refuses and names Rebake), and a layer with no mesh
at the key's frame neither counts as baked nor live. Multi-selection and the
multi-key UX are unchanged (Phase 3 / §12.4).

Still open: Phase 3 (the paint target set), Phase 4 (constraints), the grid
cell-size remap, and the multi-key sharing policy beyond the refusal.

Scope: authoring per-vertex, per-bone influence weights on top of the envelope
model, the on-disk format that makes them authored data, the brush and
point/curve tools, multi-selection, and the policy for mesh edits. Not in scope:
the skinning math, the dual-quaternion palette, pseudo-3D/perspective depth, and
a full reusable-rig export (a prerequisite is noted, the export/import is a
separate plan).

## 1. What exists, and what is missing

Existing, measured:

- `core::BoneInfluenceMap` builds a per-(BoneKey, node) weight map from
  `BoneShape` envelopes. It stores only the slots a map uses: 1 B/vertex for one
  influence (the weight is implicit), 5 B/slot for 2-7, 40 B/vertex at the
  eight-slot cap. The build is 8.6-10.8 ms in Release on `2.anie`
  (`influence-cache-plan.md` §7); it is derived state, rebuilt at load by
  `ProjectLoader` and on edits by `BoneKeyUpdater`.
- The envelope itself is authored as `Bone2::range(0/1)` and `BoneShape`; the
  envelope brush (its Value and Flow) changes those ranges (now one
  selected bone at a time, `influence-display.md` §3). The UI says envelope for
  this procedural
  shape; "influence" is the generic concept, and the baked weights are influence
  too. The internal `Influence*` names are historical/persisted and are not
  renamed.
- The weight heatmap shows the **final normalised** weights of one selected bone
  over every layer the key skins, at the positions the build used
  (`influence-display.md` §2). It is a view; it changes nothing.
- The format is **0.21**. There is no authored weight data in a project.
- **Bone keys are per keyed frame, not per node.** The model allows one per
  frame. Measured 2026-11-12 with `render_probe --bone-stats` over the 35
  loadable projects in `~/Downloads/ANIE` (the `gif COPY2` file does not load):
  32 have one BoneKey per node; 3 have a node with **two** - `2.anie` node 164,
  `cmws81` nodes 109/112/117/120, `TaigaBoykisser EXTENDED cropped.anie` nodes
  13/31 - and none has more than two. "One rig per node" is the common case, not
  a document guarantee; §3.2 keeps the weights on the owner node and §3.3
  requires the keys to share the baked bone's geometry.
- `TimeKey` has a stable `id()` (format 0.16) that survives map moves and undo.
  `Bone2` has no stable id: its `boneIDAssigner` is a per-serializer reference
  map for its JSON form, not an identity. The influence map's bone index is the
  pre-order position in `BoneKey::Data::topBones()` and is only as stable as the
  tree order.
- The object tree already supports multi-selection (`ObjectTreeWidget::
  selectedNodes()`), but `ctrl::Driver` has one target node (`mCurrentNode`,
  `setTarget`), and the Bone editor edits that one node's rig.
- The mesh does not carry stable vertex ids: `MeshVtx::index()` is recomputed by
  `MeshKey::Data::updateVtxIndices()`, `MeshKey` add/split/delete changes the
  order, and `ImageKey::resetGridMesh()` regenerates the whole grid.

Missing, in the user's terms:

- a way to paint an actual per-vertex weight, rather than an envelope's shape;
- a weight field editable at a few hand-placed points or curves, with
  interpolation, pinning and additive blending (`notes.md` line 143);
- weight painting and FFD working across a multi-selection (line 141);
- a place to store all of that, and a format that keeps it authored;
- eventually, reusing a painted rig in another project (line 145).

The first version of this document treated painting as sparse per-vertex
overrides over a live envelope. The model below is the per-bone **bake** (the
Photoshop rasterize analogy): a bone is Envelope or Baked. That is a cleaner
state for the UI, a simpler build branch, and it makes "what is envelope and
what is baked" a property of the bone rather than of each vertex.

## 2. Product model: a per-bone envelope or baked state

The envelope is the live, procedural definition of a bone's weights. It is what
every existing project has, and it stays the default. The authored state is
chosen **per bone**, not per vertex:

| state | source of the bone's weights | analogue |
|---|---|---|
| **Envelope** | `BoneShape::influence`, evaluated every rebuild | a text layer, still editable as text |
| **Baked** | a per-vertex weight map authored for the influenced meshes | a rasterized text layer, now pixels |

**Baking** a bone evaluates its envelope once, for every influenced vertex of
every influenced mesh at the current rig, and stores the result as authored
per-vertex data. From then on the envelope is not consulted for that bone;
manual paint edits change the baked map. It is the weight-painting equivalent
of Rasterize Layer: the source of truth changes from a live definition to an
authored one.

- The state is per bone. A rig can have some bones baked (a leg whose envelope is
  close but not precise) and the rest live (the ears, still being shaped).
  Normalisation and the top-eight selection stay the final stage across both.
- A baked bone's map is the **complete** field for that bone: a vertex with no
  entry is **0**, not "use the envelope". That is what makes the state
  unambiguous. The bake writes every nonzero envelope value; painting can remove
  entries.
- An empty baked map is still a state (the bone is baked and contributes
  nothing); the presence of the baked record is what says so. Do not infer the
  state from the entry count.
- **Bake is explicit** (recommended; §12 keeps it as an open UX decision). Like
  rasterizing a layer, it is a document change: a named, undoable command, not a
  side effect of a stray stroke. Entering Paint weights on an envelope bone
  shows the state and a "Bake bone weights" action; the brush is disabled until
  the bone is baked. A "Bake and paint" variant performs the same command and
  then starts the stroke; one click either way, the difference is whether the
  conversion is named.
- **Clear visual distinction.** The panel shows the selected bone's state
  ("Envelope" or "Baked"; the authored count is in the state's tooltip, not in
  the row) with the Bake/Revert action. The
  viewport marks baked bones differently from envelope bones; the exact ink is a
  UI decision (the user's bone-visual note is still UNDECIDED in `notes.md`, so
  do not lock a shape here). The heatmap carries a state label so the field
  being drawn is known to be live or authored.
- **Revert to envelope** removes the baked map (confirm if it has manual edits)
  and returns the bone to the live field; one undo restores the baked map.
- **Rebake** replaces the baked map with the current envelope (confirm: manual
  edits are lost). This is the relation to the envelope brush: editing an
  envelope bone's ranges changes the render; editing a baked bone's ranges
  changes the shape but not the weights until a rebake.
- **Clear weights** on a baked bone empties the map but leaves the bone baked
  (it contributes nothing). It is distinct from Revert, which removes the baked
  state entirely.
- A new influenced layer under an already-baked bone has no baked data. Proposal:
  it uses the envelope for that layer until the user bakes it, and the bone's
  panel state says "Mixed (this layer is live)" rather than silently filling in.
  Whether the bone's state should be global (auto-bake new layers) or per
  (bone, layer) is an open question (§12).
- The brush and constraints only edit **baked** bones. Painting a live bone is a
  no-op with the bake affordance; it does not implicitly bake unless the chosen
  UX is the "Bake and paint" variant.

Two authored tools, one baked map:

1. **Paint weights** (new mode): a per-bone, per-vertex brush with
   add/subtract/replace, the shared brush parameters, the heatmap, one gesture =
   one command. It edits only baked bones.
2. **Weight constraints** (later phase): place points/curves with values and
   radii, box-select vertices, pin some, interpolate the rest, then commit. It
   builds the same baked map; the bone must be baked first.

The envelope brush stays: its Value changes the broad
shape. For an envelope bone they remain live; for a baked bone they change the
shape only, and the panel offers Rebake. This is the same distinction as editing
the text of a rasterized layer: it does not change the pixels until you
rasterize again.

## 3. Data model

### 3.1 A stable bone id

The influence map's one-byte index is a **GPU attribute** and must remain the
pre-order index into the built bone list. The authored data needs a different,
stable identity.

Landed:

- `Bone2` has a persistent `int boneId()`. The counter follows `TimeKey`'s
  pattern: a fresh id on a new bone, `reserveBoneId()` on load so stored ids stay
  unique, and the id is serialised at the end of the bone block in 0.22.
- The copy constructor and `Data` clone **preserve** the id: a key clone must
  address the same bones, and a posed shadow reports its origin's id. A new bone
  (Creation mode) gets a fresh id. If a "duplicate bone" command is added, the
  duplicate gets a fresh id unless the duplicate is explicitly meant to share
  weights; that is a product decision to make then.
- 0.21 files have no bone ids. The reader keeps the fresh constructor id (the
  version gate does not consume the four bytes an old block does not have), and
  the first 0.22 save writes it. This is not pre-order deterministic across
  processes, unlike the first proposal: the counter is process-wide exactly like
  `TimeKey`'s. It only has to be unique inside the loaded project and stable
  after the save, which it is.
- The influence build keeps a per-bone `(index, boneId)` pair; the map's
  `BoneParam` carries the id. The shader continues to see the index; only the CPU
  build and the authoring layer know the id.
- The bone's clipboard JSON carries `StableBoneId`, so a key pasted inside the
  project keeps addressing the same field; an older clipboard has no id and gets
  a fresh one.

The id is project-local, not globally unique. A future reusable-rig export maps
the source ids to the destination skeleton explicitly; do not assume two
projects' ids mean the same bone.

### 3.2 Where the weights live

Landed: **on the bone key owner node** (the node that holds the `BoneKey`), not
on a `BoneKey` instance and not on the mesh key.

- The owner node is the rig anchor. A node may have several BoneKeys, one per
  keyed frame; the weights are a property of that rig on that node, not of a
  frame. Measured 2026-11-12 with `render_probe --bone-stats` over the 35
  loadable projects in `~/Downloads/ANIE` (the `gif COPY2` file does not load):
  32 have one BoneKey per node; 3 have a node with **two** - `2.anie` node 164,
  `cmws81` nodes 109/112/117/120, `TaigaBoykisser EXTENDED cropped.anie` nodes
  13/31 - and none has more than two. "One rig per node" is the common case, not
  a document guarantee.
- A `BoneKey` instance is a time key. Storing weights there would duplicate them
  per key and make a new key start blank, which is not how skin weights behave.
  It would also make the three dataset projects with two keys require two
  independent paints for one rig.
- A mesh key knows vertices but not bones, and grid meshes are regenerated; the
  weights would be re-created or lost with the geometry.
- The object tree serialises each node in its own block; the owner's block can
  carry the records and resolve descendant node references through the existing
  ID solver (the pre-0.21 cache did exactly this: `aOut.writeID(node)` and
  `aIn.orderIDData`). No new global block is needed.
- The owner-node choice is robust to one key or several. If the product later
  decides one rig per node, the same data fits; that would be a UI/document
  policy, not a storage reason. The multi-key case is handled in §3.3 by
  requiring the keys to share the baked bone's authored geometry.

`core::SkinWeights` (new, on `ObjectNode`; the implementation is
`src/core/SkinWeights.{h,cpp}`):

```
struct Entry { quint32 vertex; float weight; };          // sorted by vertex
struct BakedBone {
    int boneId;                     // the authoring identity, §3.1
    QVector<Entry> entries;         // nonzero field; empty = baked to all-zero
};
struct NodeWeights {
    util::LinkPointer<ObjectNode> node;                  // null when dormant
    int vertexCount;
    quint64 meshSignature;                               // §3.4
    QVector<BakedBone> bakedBones;                       // sorted by bone id
};
struct SkinWeights { QVector<NodeWeights> nodes; };      // linear find by node
```

- **The presence of a `BakedBone` is the state**: absent means the bone is in
  Envelope mode for this node; present means it is baked. An empty `entries`
  list is a baked bone that contributes zero everywhere, not an envelope bone.
- A baked bone's missing vertex is **0**, not the envelope. The map is the whole
  field for that bone.
- The weights are per node because the vertex arrays are; a bone can influence
  several nodes, and each has its own record. The bone's panel state aggregates:
  baked when every influenced node is baked, "Mixed" when some are.
- The per-bone entries are sorted by vertex. The build walks them as a cursor
  beside its vertex loop, so the baked merge is O(vertices + entries), not a
  hash lookup per bone/vertex.
- The object is a value type for undo (copy one node's `NodeWeights` or the whole
  `SkinWeights`; a bake is potentially whole-rig, a brush stroke local).
- The node records use a life-linked pointer so a deleted descendant goes null
  instead of dangling. A record whose node is null is ignored and can be pruned
  on save.

### 3.3 Build integration

`BoneInfluenceMap::writeAsync(Project&, topBones, groupMtx, mesh, const
SkinWeights::NodeWeights* weights)`:

```
for each bone index i with bone id b:
    const BakedBone* baked = weights ? weights->bakedFor(b) : nullptr
    if (baked):
        cursor = baked->entries (sorted by vertex)
        for each vertex v:
            float w = (cursor has v) ? cursor.weight : 0.0f
            advance cursor
            if (w >= FLT_EPSILON) tryPushBoneWeight(i, w)
    else:
        for each vertex v:
            float w = envelopeInfluence(bone[i], v)
            if (w >= FLT_EPSILON) tryPushBoneWeight(i, w)
```

- A baked bone's missing entry is a real zero; the envelope is never consulted
  for it. This is the difference from the earlier override proposal, and it is
  what makes "baked vs envelope" a state the user can see.
- No baked bones in the record (null, or a record that names none) takes the
  current code path exactly: no snapshot copy, no extra allocation, no change to
  any existing project.
- The authored snapshot is copied into the map before the worker task is queued.
  A command can undo or rewrite the document while a build is queued; the task
  must read the field it was started with.
- The output arrays, top-eight selection, normalisation and slot packing are
  unchanged. This is the invariant the format and the pixel gates protect.
- The build remains per key and per node; the owner's `SkinWeights` is read-only
  during the build. The cache is still derived: it is never persisted.

Landed for the bake's target set: `BoneKey::collectInfluenceTargets` is the old
`resetCacheListRecursive` selection split out. `resetCaches` and the bake both use
it, so the set a bake writes is the set the cache reads - the same subtree,
cache-owner and rigid-binding rules. `ctrl::bone::bakeBoneFields` computes the
records without touching the document and `BakeWeightsCommand` applies them as a
value replay. It mirrors the build's own guard: a parentless bone or one with no
valid range contributes an empty baked field even if a shape was authored.

**Baking is a command.** The bake evaluates `BoneShape::influence` for the
selected bone at the key's frame, through the exact map matrix the build uses,
and stores the nonzero values in a `BakedBone` on every target node. It is one
`cmnd` command ("Bake bone weights"); undo removes the record and returns the
bone to Envelope mode. A bake whose envelope field is empty still writes the
record (baked to zero), so the state survives. Landed in
`src/ctrl/bone/bone_BakeWeights.{h,cpp}`; the gate is `tests/weight_bake.cpp`.

**Multi-key rule (landed).** A baked bone's weights are shared by every key of
the owner. The bake evaluates the envelope from the selected key. Before it
writes, it checks every BoneKey of the owner for a bone with the same id and
compares the authored geometry that the envelope depends on (`Bone2`'s parent
presence, local position/angle, range, and `BoneShape`'s segment/radius/polygon/
bend angles). If they all match, the bake proceeds: the keys share the rig and
the baked field describes all of them. If any differs, or a key does not contain
the id at all, the bake is refused with the key's frame named; the user can make
the rig shared (or remove the divergent key) and bake again. Silently applying
one skeleton's field to another key's different skeleton would be the worst
outcome. Pose rotations and frame-specific poses are not part of this
comparison; the envelope is built from the rest bones and the node's mesh, not
from the pose.

**Dirtying (landed).** The weights are on the owner node, not a timeline key.
`Project::onSkinWeightsModified(ObjectNode&, bool)` is a dedicated signal;
`BoneKeyUpdater::onSkinWeightsModified` resets the caches of **every** BoneKey on
that owner (all frames), not just the current key. A bake, a stroke or a revert
pushes one command; the command emits the signal on every redo/undo. Do not
reuse a pose/move event: those rebuild only the affected key and are also used by
unrelated edits.

### 3.4 Mesh identity

Vertex indices are the storage key, and they are not stable across mesh edits.
The node record stores `meshSignature = hash(vertexCount, index buffer)` and the
vertex count.

- **The signature is topological, not positional.** The storage key is the
  vertex array POSITION: moving a vertex or editing a uv keeps every entry
  pointing at the same vertex and must not warn; only a reorder/insert/remove
  changes the mapping. This is the Phase 2 correction to the first draft (which
  hashed positions/uvs and would have warned on every legitimate position edit).
- On build, entries with `vertex >= vertexCount` are ignored. If the signature
  mismatches, all entries are still loaded, but the tool warns once that the
  mesh changed and some weights may no longer line up. The PaintWeights panel
  shows the warning line and the mode logs it once per selected bone.
- **A structural edit is remapped, not left dormant.** `createMeshWeightsRemap`
  captures the old vertex objects, edge set and records before the geometry
  command executes, then in the same undo step: keeps a survivor's value at its
  new index, drops a removed vertex's entries, and interpolates a split centre
  from the two vertices of the old edge it lies on (the old edge set is what
  identifies that pair; the current index buffer is not yet rebuilt, so the new
  signature is hashed from the faces' indices). The mesh modes push it after the
  geometry command; undo restores the exact before records.
- On load, a mismatched signature is **not** a load failure: the mesh is
  authored data too, and a user may have changed it deliberately. The entries
  are kept; out-of-range ones are dormant until the mesh is repaired or the
  weights are cleared.
- A save writes the signature **stored on the record**, not the current mesh's.
  Refreshing it on save would erase the mismatch the previous bullet exists to
  report (and would make the warning impossible to see across a save/load). The
  signature is captured by the bake with `SkinWeights::meshSignature(mesh)`
  (FNV-1a over the vertex count and the raw position/uv bits); a mesh edit does
  not rewrite it. This is the deviation from the first draft of this section.

The remap is exact for add/remove/split. A grid regeneration
(`ImageKey::resetGridMesh`, reached by a cell-size edit, an image replacement
and an external reload) is remapped by the mesh's own transition:
`core::GridWeightsRemap` puts each new vertex in the old triangle that contains
it and interpolates the old field with that triangle's barycentric weights, and
fills new vertices outside the old coverage from their mapped neighbours in the
new mesh. It runs inside the image command's own undo step
(`ImageKeyUpdater`), so undo restores the old image/grid and the exact old
records together. A record is touched only when its stored signature is the OLD
grid's **and** that ImageKey supplies the area mesh at one of the owner's bone
frames; a record made against a MeshKey, or another image key with the same
topology, is left alone and keeps its warning. §7 and §11 record the gate.

## 4. Format 0.22

Authored data, so the minor bumps and the reader gates it. A 0.21 file has no
weight block and loads envelope-only; a 0.22 reader knows both. The existing
version guard rejects a 0.22 file in an older binary, as it does for every
newer minor.

### 4.1 Block

The block is appended to the **owner node's object block** (inside
`ObjectTree::serializeNode`, after `LayerNode`/`FolderNode::serialize`, so one
implementation covers both node types). The node's own type block is unchanged.

```
"SkinWgt_" (8-byte block signature), uint64 byteLength
int32 recordCount
per record:
    int32 nodeReferenceId        // Serializer::writeID, resolved by orderIDData
    int32 vertexCount
    int32 meshSignatureLow, int32 meshSignatureHigh   // the serializer has no 64-bit scalar
    int32 bakedBoneCount
    per baked bone:
        int32 boneId
        int32 entryCount         // 0 is a baked bone that contributes zero
        per entry: int32 vertex, float32 weight (LE, as every other float)
```

The fixed cost is **20 bytes per node** (8 signature + 8 length + 4 count) plus
20 bytes per record; a baked bone costs 8 bytes plus 8 per entry. The gate
`tests/weight_roundtrip.cpp` pins the layout by patching a saved file at these
offsets.

- **The record is the state.** A bone listed here is Baked for this node; a bone
  with no record is Envelope. There is no mode byte: the presence of the baked
  bone record is the mode, and an empty entry list is the all-zero baked field,
  not an envelope bone. This is the one invariant the format must never lose.
- A node with no records writes `recordCount = 0`; every node gets the block in
  0.22 (landed). The fixed 20-byte cost above is per node.
- The tag and byte length make the block self-describing for a future minor
  that wants to skip or reinterpret it; the reader validates both. (Authored
  data is read, not skipped, but the length is cheap and makes a corrupt block
  fail at its own boundary.) `Deserializer::remainingBytes()` bounds every
  count before a reserve or loop.
- Counts are checked against the remaining stream and a sane maximum before
  allocation. Negative or overrunning counts fail. A non-finite weight or a
  weight outside `[0, 1]` is a corrupt record and fails the load. Entries must
  be strictly increasing by vertex and baked bones strictly increasing by id;
  the reader refuses an unsorted file because the build's cursor depends on the
  order, and a duplicate would make which value wins depend on the reader. A
  **vertex index beyond the built mesh's actual vertex count is tolerated**: the
  record is kept, the build ignores the dormant entry, and the tool warns once.
  A mesh is authored data and may legitimately have changed; an old index must
  not make the project unopenable.
- A bone id that is not in any `BoneKey` of the owner is **not** a load failure:
  bones can be deleted while their baked records remain dormant for undo/save.
  The build ignores it. Only a record whose target node is gone (a null
  life-link) is dropped by a save; an orphaned bone id is kept.
- `FormatVersion.h` is at 0.22 with the history line (landed). The 0.21 rule is
  the worked example: authored data may gate the reader; the influence cache
  remains derived and is never required.

### 4.2 Reader order

`ObjectTree::deserializeNode` reads the node's own type block, then the weight
block (both inside the `ObjNode_` block). The node references are not bound yet
(descendants are serialised after the owner in the tree walk), so the reader
pushes an `orderIDData` solver that binds each record after the whole tree has
been read, exactly like the pre-0.21 cache. A negative id fails at the block
("invalid weight node reference"); an id that is never bound fails the loader's
solver ("Failed to solve id references"). This is authored data, not a cache, so
silently dropping it would lose the user's paint. The records are published to
the node only after the block's end validates, so a failed read leaves no
partially bound field.

### 4.3 Deletion, undo and pruning

- A deleted descendant's record is life-linked to null and ignored. If the
  delete is undone and the same object returns, the record becomes live again;
  if the object is gone for good, the record is dormant. A save skips a null-node
  record (landed); it does not resurrect it.
- A deleted bone leaves its baked record dormant (the build ignores an absent
  id). This keeps a bone-delete undo from losing the paint. The cost is the
  dormant map until the user saves; a "Remove unused weights" action can prune
  it, and the save can report the count rather than dropping data silently.
  This is a deliberate no-surprise rule, not an optimisation.
- A node moved out of the owner's subtree is not influenced any more; its
  record stays with the old owner and can be pruned by the same action. A node
  moved in starts with the envelope only. (Landed storage keeps the record and
  the build ignores it; save does not yet prune an out-of-subtree target.)
- **Duplicating a node copies the authored fields.** `TreeActions.cpp`'s
  `DuplicateCommand` records source->copy while it builds the subtree: an
  internal rig owner's records are re-pointed at the copy's descendants (a
  record for a node outside the copied subtree is not duplicated), and every rig
  owner ABOVE the subtree gets a record for the copied target (removed on undo,
  restored on redo). The copy is owned by the command only while detached, so a
  project close with the duplicate attached cannot double-delete it.
- `resetCaches`/`updateCaches` already walk the owner's descendants; the
  weights lookup uses the same nodes. No second tree walk is needed.

### 4.4 Compatibility tests (landed as `tests/weight_roundtrip.cpp`)

- A 0.21 object-tree stream with no weight block loads; the 0.22 reader rejects
  the same stream as missing its block. Two real 0.21 projects (`test.anie`,
  `tart.anie`) load, save as 0.22 and reload with zero load failures under
  `gpu_headless`.
- A 0.22 save/reload reproduces the in-memory records (entries are normalised
  when set; the file validates that order). A baked bone with zero entries
  round-trips as **baked**, not as envelope; this is the state-preservation
  test.
- A corrupt block (overrunning count, NaN weight, unresolved node) fails with a
  named deserializer error and no partial load. A vertex index beyond the stored
  count is not corruption; it loads dormant and the build ignores it.
- A 0.22 file with baked bones in one node and envelope bones in another
  round-trips both (the storage gate covers both records).
- The legacy 0.20 cache discard path is unaffected (`tests/bonekey_regression`).

## 5. From envelope to baked: bake, paint, revert

A new Bone editor mode, `PaintWeights`. The mode never operates on a live
envelope directly: the selected bone must be **baked** first, because baking is
the document change and the brush is the edit after it. Phase 2 is not built;
the headless bake command it will call is landed (§3.3, `bone_BakeWeights`).

### 5.1 The state and the action

The panel names the selected bone's state and offers its action:

| state | panel | allowed actions |
|---|---|---|
| Envelope | "Envelope" | **Bake**; the brush is disabled |
| Baked | "Baked" (the authored count is in the tooltip) | paint; **Clear Weights**; **Rebake**; **Revert to Envelope** |
| Mixed (some influenced layers baked, some live) | "Partly baked" (the live-layer count is in the tooltip) | **Bake** (fills the live layers); **Rebake All**, Revert and Clear in the overflow menu |

**Bake** evaluates the selected bone's envelope at the current key and frame,
for every influenced vertex of every target node, and stores the nonzero values
as the `BakedBone` record on each node. It is one `cmnd` command ("Bake bone
weights"):

- The target set is §5.2: the selected descendants of the owner, or every
  influenced layer by default.
- A node with no mesh at the current frame is skipped and named; its state stays
  Envelope (Mixed for the bone).
- The multi-key shared-rig check (§3.3) runs first. A divergent same-id bone on
  another key refuses the bake with the key's frame named; it does not write a
  field that describes only one skeleton.
- An envelope that is zero for the whole bone still writes the baked record
  with no entries: the bone is Baked and contributes nothing. The record's
  presence is the state, not its size.
- **The bake is exact, not a resample.** The values are the same floats the
  build computes; immediately after the bake the built map's hashes and the
  rendered frame must be bit-identical to the envelope-only control. This is
  the acceptance gate that makes the state transition safe.
- Undo removes the record(s) and returns the bone to Envelope; redo restores
  them.

**Revert to envelope** removes the baked records for the selected bone (all
target nodes) and returns it to the live field. If the baked map has entries,
confirm first: manual paint edits are lost (undo restores them). It is one
command.

**Clear weights** empties the entries of the baked map but leaves the bone
Baked; the bone contributes nothing. It is the "transparent pixels" state, not
the "convert back to text" state. It is one command.

**Rebake** replaces the baked map with the current envelope (confirm: manual
edits are lost). This is how the envelope and baked states relate: editing an
Envelope bone's ranges changes the render; editing a Baked bone's ranges changes
the shape only, and the panel offers Rebake. A rasterized layer behaves the same
way when its source text changes.

**Bake and paint** is an optional variant: the first left press on an Envelope
bone runs the same bake command and starts the stroke inside one gesture. It
removes one click but hides the conversion; the recommendation is explicit
bake, and §12 keeps the choice open.

### 5.2 Target selection

The Bone editor's target is the bone key owner (the rig). The **paint target
set** is the subset of its influenced layers the user wants to edit:

- the object tree's selected descendants of the owner, if any;
- otherwise every influenced cache node.

Only nodes with an influence cache for the current key and a mesh at the current
frame are bakeable/paintable. A selected node outside the owner's subtree is
ignored with one message; it is not silently painted on another rig. The bake
and the brush use the same set, so the state a bake writes is the state the
brush can edit.

The targeting interaction from `influence-display.md` is reused: right-click a
joint/connector to select the bone (left is the stroke); hovering previews its
heatmap; the first stroke with no selected bone picks the nearest envelope. In
this mode the selection chooses the bone whose state the panel shows, not a
brush target inside the envelope.

### 5.3 One stamp

For a **baked** bone, for each target node, the brush stamp is transformed into
the node's mesh space through the same matrix the cache uses (the cache's
`innerMatrix` translated by the origin offset, then the target matrix; the
heatmap already does this inverse direction). For every vertex within the stamp:

```
t      = brushFalloff(distance, radius, hardness) * amount
before = baked.weight(bone, vertex)        // missing = 0 in a baked bone
after  = clamp(before + t, 0, 1)            // add
       = clamp(before - t, 0, 1)            // subtract
       = clamp(t, 0, 1)                     // replace
if after <= FLT_EPSILON: remove the entry   // missing already means 0
else: store/update the entry
```

- The baked map's missing value is **0**, so removing a zero entry is exact and
  keeps the sparse map sparse. The bone remains Baked; the record is not
  removed. Do not confuse this with Revert, which removes the record.
- The brush never reads the envelope for a baked bone. If the user wants to
  start from the current envelope after changing it, Rebake is the action.
- The target set may contain several layers; one stamp writes each layer's own
  `(bone, vertex)` entries. Each layer's mesh has its own vertex count and
  signature.
- The brush is disabled on an Envelope bone (or the Bake-and-paint variant is
  engaged); a stamp that would paint one is a no-op with the bake affordance.

### 5.4 One stroke, one command

- `cmnd::Gesture` (or a dedicated stroke command) captures the affected
  `NodeWeights` before the first stamp and rewrites one command as the stroke
  moves. On release the command holds the before/after snapshots.
- A command spanning several layers is still one command and one undo step.
- The bake, the stroke, a Clear, a Rebake and a Revert are each their own
  command; Bake-and-paint is the one variant that combines the first two.
- The command's listener emits the `SkinWeightsModified` event (§3.3), which
  resets the owner's keys' caches once after the command (and on every
  undo/redo replay). During the stroke the cache must not rebuild per sample:
  either rebuild once per painted frame after `flushPendingWork`, or defer to
  the release. Do not add a synchronous rebuild in the mouse handler.
- The heatmap reads the **built** map. During the deferral it shows the previous
  frame's field; the plan's acceptance is "one frame of lag during the stroke,
  exact on the next frame", or an explicit live preview evaluated on the CPU
  without rebuilding the whole map. Start with the one-frame lag and measure;
  the influence build's Release cost (8.6-10.8 ms on `2.anie`) is the budget.
- Undo restores the before map and the command's listener rebuilds; redo the
  after. A no-op stroke pushes nothing.

### 5.5 UI and the visual state

- The panel shows the selected bone's state and the Bake/Revert/Rebake/Clear
  actions; the shared brush section with an Add/Subtract/Replace selector.
- The viewport distinguishes Envelope bones from Baked bones. The exact ink is
  a UI decision (the user's bone-visual note is UNDECIDED in `notes.md`); a
  state icon in the panel and a different bone stroke/fill is enough for the
  first version. The panel carries the state ("Envelope" / "Baked" with the
  vertex count on its own line / "Partly baked" with the live-layer count) so the
  field being drawn is never ambiguous; a canvas label was removed in UI review.
- The heatmap toggle already exists; on for this mode.
- A readout of the value under the cursor (baked authored value and the final
  normalised weight) is useful; bone names do not exist, so the readout names
  the joint by its index/path or the selected highlight.
- On an Envelope bone the brush controls are visible but disabled with the Bake
  action beside them; a disabled control that silently does nothing is worse
  than one that explains why.

## 6. Points, curves and interpolation

This is the user's line 143: edit weights at a few hand-placed points/curves,
interpolate the rest; curves resampled first; additive blending; pinning; more
than linear interpolation. Constraints write a **baked** bone's map; an Envelope
bone is baked first with the same explicit action as the brush (§5.1), so the
constraint tool never writes a live envelope.

### 6.1 Constraint model

```
struct WeightConstraint {
    QVector<QVector2D> points;   // one point, or a resampled polyline/bezier
    float weight;                // the value this constraint pins
    float radius;                // influence radius in content space
    bool pin;                    // a hard value at its points, or a soft field
    bool additive;               // add to other constraints instead of blending
};
```

- Points and curves live in the **target layer's content space** (the space the
  image, mesh, and mask all use). A curve is placed and edited in that space;
  it is resampled by arc length with the same spacing rule as a brush stroke
  before use, as the user asks.
- A constraint maps to vertices by proximity: the nearest point on the
  resampled curve, within `radius`; a pin is a hard boundary, a soft constraint
  contributes a falloff.
- The tool can also use a **box selection of mesh vertices** as a hard pin set
  (the user's "box selects the inside points, raises them"). A direct selection
  edit sets those vertices' weights and can feed the solver as pins.

### 6.2 Interpolation

Two stages, in order of fidelity:

1. **Nearest/radius falloff (phase 4a).** Each vertex gets the weighted average
   of the constraints that reach it (inverse-distance or a smooth radial
   kernel), clamped. Additive constraints are summed then clamped. This is
   predictable and fast, and it is enough for "pin the outside to 0, paint a
   line inside to 1".
2. **Mesh-graph solve (phase 4b).** Treat the constraint values as boundary
   conditions and solve a Laplacian (harmonic) system over the mesh's edge
   graph, or an RBF over the constraints. This gives a smooth field that
   respects the mesh topology instead of screen-space distance, and it is the
   "interpolation other than linear" the user asks for. The solver works on
   scratch and commits one `SkinWeights` command, like `BoneDynamics`; it never
   mutates the document during iteration.
- The exact solver is an open decision: harmonic diffusion on the mesh graph is
  the default recommendation because it follows edges and needs no global
  matrix; a thin-plate spline is a later alternative if the field needs to
  ignore topology.
- Additive blending applies to the constraints before the solve; after the solve
  the values are clamped and written into the **baked** map (missing = 0). The
  data model remains one baked value per (bone, vertex); an Envelope bone is
  baked first (§5.1), so the constraint tool never writes a live envelope.

### 6.3 UI

- A "Weight field" tool (possibly the same Paint weights mode with a
  constraint sub-mode): place points, draw a polyline/bezier, set the value and
  radius, toggle pin/additive, box-select vertices, "Apply" or a live preview.
- Undo one apply; a constraint edit is not an undo step until committed.
- The heatmap previews the active bone; a "show authored vs final" distinction
  is useful once normalization is in play.

## 7. Mesh edits

This is the part that decides whether the feature is usable long term. The
current vertex identity is an index into a regenerated array; the notes already
ask for new vertices to keep interpolated attributes until commit (line 137).

- **Position edits** (moving vertices/edges) keep indices: the weights follow.
- **Add/split/delete** changes indices. Proposal for phase 5:
  - new vertex from a split: interpolate its weights from the source edge's two
    vertices (the same interpolation the mesh editor uses for other attributes),
    and keep the transient until the edit commits (the user's note);
  - delete: drop that vertex's entries; a split/delete remaps later indices;
  - a structural edit that cannot be remapped safely: keep the old map dormant
    with a mesh-signature mismatch and warn, rather than silently assigning
    weights to different vertices.
- **Grid changes** (`ImageKey::resetGridMesh`) regenerate the grid: a cell-size
  edit, an image replacement and an external reload all reach it. The landed
  remap carries the field through the mesh transition (barycentric inside the
  old triangles, a neighbour fill outside them) in the image command's own undo
  step; the lock alternative is no longer needed. The record is matched by the
  old grid's signature and by the ImageKey being the area key at a bone frame,
  not by the signature alone (two grids can share a topology and not a shape).
  A signature that does not match the old grid was already mismatched before
  this change and is left alone with its warning.
- **Save/load after a mesh edit** keeps the entries and their signature; the
  UI warns once, and out-of-range entries are dormant. Do not fail the load:
  the mesh is authored data and may be edited independently.
- A future stable vertex id would make this exact; it is not needed for the
  first phases and would touch every mesh writer. Record it as the long-term
  fix, not a prerequisite.

## 8. Multi-selection

The object tree has multi-selection; the Driver does not. The proposed split:

- The Bone editor keeps a **primary target** (the bone key owner) for its
  skeleton, and a **paint target set** for the layer meshes it edits.
- Entering the Bone editor on the owner establishes the rig. Multi-selecting
  descendants adds/removes paint targets; the brush paints every target in the
  set, and one gesture is one command across all of them.
- The Heatmap can show all targets or only the selected ones; the plan's
  default is all influenced layers (as today), with a visual distinction for
  the selected ones if it is needed.
- The Driver's single `setTarget` remains the authority for the tool's primary
  node. A separate `setPaintTargets`/`setTargets` on the Bone editor (fed from
  the tree's selection) is the smallest change. If the tree selection changes
  the primary target to a non-owner layer, the Bone editor must keep the owner
  it was entered with, or refuse and explain; that is a UI rule to design with
  the object-tree selection model.
- FFD's multiselection note is the same plumbing on a different editor. The
  plan includes a shared "target set" abstraction only if it falls out cleanly;
  do not force the Bone editor and FFD into one class until both uses are
  known.

The alternative - multiple bone owners/rigs painted at once - is a larger
Driver change and is out of scope for the first version. A rig per character is
the common case; a layer multi-selection within one rig is the note's request.

## 9. Reusable rigs and perspective

- **Reusable rigging** (line 145) needs the stable bone ids and node-independent
  weight records from §3, plus a mapping between the source and destination
  meshes (UV/vertex correspondence) and a rig export/import format. This plan
  builds the identity and storage prerequisites; the export is a separate
  plan.
- **Pseudo-3D/perspective** (lines 82, 94, 129) needs per-vertex depth and a
  transform that is not a weighted 2D dual quaternion; painting a 2D weight
  cannot make a linear skin transform into a 3D one. It is a separate research
  workstream (see the notes) and must not be smuggled into the weight map.

## 10. Phases

Each phase ends with the focused gates green and a measured number in this
document; the full suite is for the end of a coherent change. The phase order
keeps authored data out of the file until the format and the build are proven.

### Phase 0 - decisions and fixtures - PARTLY LANDED

- Decisions made for Phase 1: owner-node storage (§3.2), per-bone Envelope/Baked
  with record presence as the state (§2), the shared-rig refusal (§3.3), and a
  save keeping the bake-time mesh signature (§3.4). Phase 2 decided explicit
  Bake and Bake-live-nodes-only for Mixed (§12.1, §12.3).
- [x] Two-bone/small-mesh fixtures: `tests/weight_bake.cpp` builds a root with
      two envelope children and a nontrivial transform; the 33-bone/dense cap
      fixture already exists in `tests/bone_cap.cpp` and is unchanged.
- [x] Two-key fixtures: `tests/weight_bake.cpp` has a shared-rig pair and the
      divergent/missing second-key refusals. The dataset's real two-key node
      (2.anie node 164) is the measured case.
- [x] 0.21 compatibility fixture: `tests/weight_roundtrip.cpp` writes a
      0.21-shaped object-tree stream with no weight block; two real 0.21
      projects (`test.anie`, `tart.anie`) load and re-save under `gpu_headless`.

### Phase 1 - ids, baked storage, format, build, bake command (no UI) - LANDED

- [x] `Bone2` id + `reserveBoneId`, serialise/deserialise, copy/clone semantics.
- [x] `core::SkinWeights` on `ObjectNode`; `BakedBone` presence is the state; an
      empty baked map round-trips as Baked; life-linked node records; value
      semantics.
- [x] `BoneInfluenceMap::writeAsync` baked branch; no-baked fast path; build
      invariants.
- [x] `ObjectTree` 0.22 weight block; `orderIDData`; `FormatVersion.h` 0.22;
      null-node prune on save; `SkinWeightsModified` event and cache reset for
      all keys of the owner.
- [x] The headless bake command: evaluate the selected bone's envelope at the
      key's frame/mesh, write the nonzero entries, check the multi-key shared
      rig, one command with undo/redo.
- [x] Gates: `bone_ids`, `skin_weights`, `weight_roundtrip`, `weight_bake`.
      `weight_bake` covers the baked merge in every slot layout it needs; the
      existing `bone_influence_storage` writer gate is unchanged.
- [x] Acceptance, measured on this build: an empty baked map remains Baked after
      save/load; a 0.22 record round-trips; a divergent and a missing second key
      both refuse with their frame named; normalisation and the top-eight
      packing are pinned; a 0.21 stream with no block loads; corrupt counts, NaN
      weights and unresolved nodes fail.
- Not measured: an A/B of influence hashes/pixels against the pre-change binary.
  No 0.21 project has a baked record, so the build takes the exact old branch
  (`mBakedWeights` is null and no allocation happens); `test.anie` and
  `tart.anie` loaded, saved and reloaded as 0.22 with zero load failures. A
  pixel A/B of the whole dataset is the Phase 2 pre-flight, when a baked project
  and a control can be compared from one build.

### Phase 2 - the bake action and the weight brush - LANDED

- [x] New `PaintWeights` mode with the state panel: Envelope / Baked / Mixed,
      the Bake/Rebake/Revert/Clear actions behind one primary button and an
      overflow menu, the shared brush and Add/Subtract/Replace, as the dock's
      stat row.
- [x] Bake and brush commands; per-vertex stamp on a baked bone; one command
      per stroke; deferred rebuild; the envelope brush on a baked bone and the
      Rebake relation. The panel's status line carries the state; the canvas
      stays skeleton + heatmap (the distinct bone ink is deferred to the
      undecided bone-visual design).
- [x] A GUI probe: enter the mode, bake, see the render unchanged, paint a
      stroke, see the heatmap/deformation change, undo, revert, save/reopen;
      assert the brush is disabled on an envelope bone and the refusal is in the
      error line. (The crop is pixel-identical rather than byte-identical; PNG
      metadata differs.)
- [x] Acceptance: baked state survives save/load; the bake frame is
      pixel-identical to the envelope control (the headless map is bit-exact);
      undo restores the before map exactly; Revert returns the live envelope;
      Rebake replaces the map; painting an envelope bone is impossible without
      the explicit bake action; the envelope-only path is unchanged; the
      stroke's rebuild is coalesced once per frame (the probe's stroke changes
      the painted canvas and one undo restores it exactly).

### Phase 3 - multi-selection

- The paint target set from the tree selection; one command across layers; the
  primary owner rule; messages for out-of-rig selections.
- Acceptance: two selected layers both receive the stroke, one undo; unselected
  influenced layers are unchanged; the heatmap covers the set; the FFD target
  set is at least designed, not necessarily implemented.

### Phase 4 - constraints and interpolation

- Constraint points/curves, arc-length resampling, radius, pin, additive;
  vertex box selection; nearest/falloff first, the mesh-graph solve second;
  preview/commit; one command. The tool writes the baked map: if the selected
  bone is still Envelope, it offers the same explicit bake first (or a "Bake and
  apply" variant), because a constraint solve is the same kind of authoring
  transition as a brush stroke.
- Acceptance: a two-pin fixture (0 outside, 1 on a line) produces the expected
  field on the mesh (a numeric check plus a heatmap/pixel check); additive
  overlaps clamp; pins are exact; undo one apply; the bake-before-apply state
  transition is explicit and undoable.

### Phase 5 - mesh edits - REMAP AND DUPLICATION LANDED

- [x] Structural index remap for new/split/delete, in the geometry command's
      own undo step; split interpolation from the old edge's two vertices; the
      mesh-signature warning and the dormant out-of-range policy.
- [x] Grid remap for cell-size edits, image replacements and external reloads
      (`core::GridWeightsRemap` through `ImageKeyUpdater`): barycentric
      interpolation across the old triangles, a neighbour fill outside them,
      the new count/signature, one undo step.
- [ ] Vertex attributes interpolated until commit; a "clear invalid weights"
      action; a policy for a new/removed image or mesh key (see §9 item 8).
- Acceptance: a split keeps the new vertex's weight at the interpolation; a
  grid change preserves the field on the new mesh and undo restores the old
  grid and field exactly; a constant field stays constant and a linear field
  stays exact in the old triangles; an image replacement does the same; no
  crash; save/load keeps the state.

### Phase 6 - reusable rig (separate plan)

- Bone id mapping, mesh correspondence, export/import. Only the prerequisites
  are in this plan; do not schedule the export until Phases 1-2 have shipped.

### Phase 7 - pseudo-3D/perspective (separate research)

- Not a weight-paint feature; record it as a separate workstream.

## 11. Verification

### Unit and format gates

| gate | what it pins |
|---|---|
| `bone_ids` (landed) | fresh ids, reserve, copy/shadow preservation, 0.22 read, 0.21 keeps a fresh id |
| `skin_weights` (landed) | baked presence vs Envelope, an empty baked map staying Baked, entry sorting/sparsity, per-node records, node life-link, null-node policy, `bakeBone`'s exact float, mesh-signature stability and change |
| `weight_bake` (landed) | the baked merge (slot layout, stored bytes, missing = 0, no envelope fallback, two-bone normalisation), bake equals the envelope in the built map, empty baked, divergent/missing second key refusal, one-step undo/redo and the owner event |
| `weight_roundtrip` (landed) | 0.21 no-block load, the 0.22 block required at 0.22, 0.22 save/load with descendant references, empty Baked record, mesh signature, corrupt count/NaN/unresolved node |
| `bone_influence_storage` (existing) | the writer's every slot layout; unchanged by this phase |
| `bonekey_regression` (existing) | the <= 0.20 derived-cache discard and 0.21's no-section stream |
| `weight_paint_commands` (landed) | state/actions, one-command stroke, remap shift/split, grid/replacement remap (constant and linear fields, the area-key guard, undo/redo), duplication copy (GL/Xvfb) |
| `weight_constraints` | not built (Phase 4) |
| `weight_mesh_edits` | not built (Phase 5) |

Not yet gated: a dormant vertex index beyond the live mesh (the reader accepts
it and the build ignores it by construction; add it to `weight_roundtrip` in
Phase 2), the mesh-signature warning (only the hash and its persistence are
landed), and pruning of an out-of-subtree target on save.

### Pixel and rendering

These are the Phase 2 acceptance gates; Phase 1 compared built maps, not frames.

- **The bake is exact.** `weight_bake` pins the built map's stored bytes and
  per-vertex weights; the GUI probe compares an artwork crop before and after
  Bake and finds **0 differing pixels** (`compare -metric AE`), with the map
  hash gate as the strict form.
- A stroke on a baked bone changes the **canvas**: the wash is the field, so the
  probe compares a 360x360 canvas crop before and after the stroke (3853
  differing pixels), and one undo restores the pre-stroke canvas exactly (0).
  Revert returns Envelope and disables the brush; Rebake reproduces the initial
  bake exactly (0); Clear removes the wash while the bone stays Baked and undo
  restores it exactly (0); a fresh process reads the baked state back after
  Ctrl+S.
- **Envelope-only projects are unchanged, measured against the pre-change
  Release binary** (HEAD `873c7b45`, built in a git worktree): all **35
  loadable** `~/Downloads/ANIE` projects have identical
  `--dump-influence` map lines and identical frame-0 PNGs at 320x320
  (`--require-gpu`, offscreen radeonsi). The deep sample is **60/60 frames
  identical** (10 frames at 640x640 on each of 2, pagedoll, skel_practice_12,
  tart, ballerina, dinner lab), so the sample is not frame 0 alone.
- The heatmap's own gate remains `overlay_engine_primitives` (the overlay
  primitive contract: the weight field's full surface, threshold discard, ramp
  orientation and opaque destination alpha) + the GUI probe's state/heatmap
  frames from `influence-display.md`; the GPU surface is checked there, not by a
  pixel hash.

### Performance and memory

Status: the no-baked path structurally allocates nothing new (the snapshot is
null when no record names a baked bone). Measured on this build (Release,
`gpu_headless`, 2.anie, no baked records):

- `--dump-influence`: maps=130, vertices=262165, resident **2,277,514 B**,
  logical **10,486,600 B** (slots1=102, slots2=8, slots3=14, slots5=5) - the
  same resident/logical pair the influence plan recorded for the envelope-only
  build; two runs' 130 map hashes are byte-identical. This is the storage
  compatibility check, not an A/B against the pre-change binary.
- `--bench-influence 3`: 18.073 ms (first, warm-up), then 12.145 and 12.850 ms,
  schedule 3.3-3.6 ms. The plan's older recorded 8.6-10.8 ms is from a
  different session/context. The Phase 2 A/B (pre-change HEAD vs this working
  tree, alternating Release runs) is in the Phase 2 block above / below.
- The format's fixed cost is 20 bytes per object node (8 signature + 8 length +
  4 record count), derived from the layout and pinned by the round-trip test.

- Dataset A/B: 35/35 loadable projects, dump and frame identical (above). Load
  times vary by run (e.g. `2.anie` 5.95 s pre / 5.58 s post; `VioletLeaf` 5.60
  pre / 4.60 post; the spread is the same order as run-to-run noise, not a
  regression claim).
- `--bench-influence 5` A/B (Release, 3 alternating runs; reps 2-4 after the
  warm-up):
  - `2.anie`: pre-change **16.45 / 16.39 / 16.74 ms**, post **13.65 / 14.12 /
    14.65 ms**, schedule 3.5-4.5 ms both. The post is not slower; the pre number
    is higher than the older 12.1-12.8 ms recording, which is session/load
    noise, which is why the A/B is alternating.
  - `skel_practice_12`: pre **33.9 / 34.6 / 35.7 ms**, post **34.4 / 35.1 /
    32.7 ms**; the spread is the same on both sides.
  The no-baked path is structurally unchanged (null snapshot, no copy), so this
  is parity evidence, not an optimisation.
- The baked merge is measured separately from the polygon containment; the
  envelope-only path takes the same build and the no-baked record allocates
  nothing (the snapshot is null).
- The bake itself is a one-off envelope evaluation over the influenced vertices;
  the GUI fixture's 622-vertex field is instantaneous, and the largest dataset
  rig (33 bones, 2.anie's 130 maps) is the upper bound from the build cost.
- A stroke's rebuild is once per painted frame (the stamps are queued); the
  probe's stroke changed 3 entries on a 622-entry field and the panel updated
  after the frame. The heatmap is a full-resolution surface image (no sample cap);
  it skips caches with no weight for the selected bone.
- Save size grows by the baked entries plus the 20-byte per-node block; the
  probe's saved copy carries 622 entries (the exact byte delta was not
  separated from the 0.21 -> 0.22 block overhead).

### Probes and compatibility

- GUI probe (landed as `scripts/gui/probe_weights_bake_paint.sh`): a rig, the
  mode, the state panel, the disabled brush and the one-action explanation on
  an Envelope bone, a canvas press selecting the nearest bone, Bake with a
  pixel-identical canvas crop, an Add stroke, exact undo/redo counts, Revert,
  Rebake, Ctrl+S and a fresh process reading the baked field back. Multi-select
  two layers remains Phase 3.
- All 36 loadable dataset projects open, render envelope-only, and save/reload
  with no baked records; their influence hashes are unchanged. (The dataset
  sweep compares the pre-change Release binary with the working tree: every
  project's `--dump-influence` map lines and its frame-0 PNG are identical;
  numbers in §11's performance section. `--bone-stats` on `2.anie` still
  reports 38 keys / 134 bones / the two-key node 164.)
- A 0.20 file still discards its legacy cache; a 0.21 file loads; a 0.22 file
  is rejected by the old binary, as every newer minor is. (`bonekey_regression`
  pins the first; the second is in `weight_roundtrip` and the real round trip;
  the third is the format guard's existing behaviour.)

## 12. Risks and open questions

1. **Explicit vs implicit bake - DECIDED: explicit.** The
   plan recommends an explicit, named "Bake bone weights" command: baking changes
   the source of truth and the file, so it should be one named undo step, not a
   side effect of a stroke; the envelope stays inspectable until the user
   commits; a stray click cannot convert a bone. The cost is one click. The
   alternative is "Bake and paint": the first stroke performs the same command
   and the stroke in one gesture, which is smoother but hides the conversion. A
   third option is a per-bone toggle (Envelope/Baked) in a bone list, with
   painting allowed only in Baked. Phase 2 shipped the explicit Bake action: a
   stroke on a live bone is refused with the reason, never an implicit
   conversion.
2. **Storage location (decided and landed).** Owner node is the rig anchor. The
   document model allows several BoneKeys per node; measured 3 of 35 projects
   use two on one node, never more. Owner-node storage makes one paint serve
   every key, which is why it beats BoneKey-local storage (per-key duplication,
   blank new keys) and mesh-local storage (no skeleton). If the product later
   enforces one rig per node, the same data fits; that is a document/UI policy,
   not a storage reason.
3. **Per-bone vs per-(bone, node) state, and new layers - DECIDED for Phase 2.**
   A bone can influence several layers; the data is per node and the UI state
   aggregates. A new influenced layer has no baked record: the state is Mixed
   and Bake writes the LIVE nodes only, so baking a newly added layer never
   replaces paint on the rest. Auto-rasterizing a new layer on a bone-level Bake
   was rejected; a per-layer state view is still a UX option.
4. **Multiple keys and the shared-rig rule (refusal landed).** A baked field is
   evaluated from one key's envelope. The bake refuses if another key has a
   same-id bone with different authored geometry/range/shape, and also if the id
   is missing there; the refusal names the frame. Alternatives still open:
   per-key baked maps (reintroduces duplication), a "make the rig shared"
   action, or treating all BoneKeys as pose keys over one rig (a larger model
   change, aligned with the user's intuition that there is "basically one").
5. **Bone identity (landed).** A process-wide int id with `reserveBoneId`
   matches `TimeKey`; a 0.21 file gets fresh ids and saves them. A project-local
   id is enough for this app; reusable rigs need an explicit mapping. The
   duplicate-bone rule and shadow-id rule must be decided with the command that
   duplicates bones (if one is added); shadow bones already report the origin id.
6. **Normalisation visibility - landed.** The heatmap shows final weights; the
   panel's state line and the viewport label say whether the field is the live
   envelope or authored data. The value under the cursor is not shown yet; the
   heatmap is the readout.
7. **The top-eight cap.** A vertex with more contributions than eight loses the
   smallest. The heatmap of the selected bone may not show a dropped influence;
   decide whether the paint tool warns "this bone is not in the final eight".
8. **Mesh topology changes.** Index-based storage is exact until a structural
   edit. Structural edits are now remapped in the same undo step (survivor shift,
   split interpolation), and a save still keeps the bake-time signature. Grid
   regenerations are remapped too (cell size, image replacement, external
   reload; `core::GridWeightsRemap`). Still unhandled: adding or removing an
   image key or a MeshKey changes the area mesh without an old-grid transition;
   such a record keeps its signature warning. The general signature policy
   (warn, out-of-range dormant) is unchanged; stable vertex ids are the
   long-term fix.
9. **Multi-selection architecture.** The tree has multi-selection, the Driver
   does not. The proposed paint target set is the smallest change; a multi-rig
   editor is a bigger one. The primary-owner rule when the selection moves is
   a UI decision.
10. **Constraint solver.** Screen-space falloff is simple and predictable;
    a mesh-graph harmonic solve is the "not just linear" answer but needs a
    solver, boundary conditions and performance work. The plan stages them.
11. **Revert and manual edits.** Reverting to Envelope discards the baked map;
    undo can restore it, but a save after the revert makes the loss permanent.
    The UI must confirm when entries exist, and the undo path must be exact.
12. **Format and release (bumped and gated).** No release has shipped; 0.22 is
    a dev-only bump, but the 0.21 reader still opens old projects and writes ids
    on the next save. The weight block is authored, so it is read, not skipped;
    a corrupt block fails the load at its signed, length-checked boundary. The
    dormant-record policy is a data-retention decision: only a null target node
    is dropped on save, an orphaned bone id is kept.
13. **Mesh attributes and weights.** The notes' "new vertices keep attributes
    interpolated until commit" is a mesh-editor change that this plan depends on
    for Phase 5. It should be designed with the mesh editor's other attributes,
    not as a weight-only special case.
14. **Translation.** New mode, state, action and hint strings need the usual
    catalog pass; the repo's new-string rule (English until the next lupdate)
    applies.
15. **Performance of the heatmap and build during a stroke.** The display is
    already capped; the build is not incremental. Phase 2 measures the stroke
    latency before adding the curve solver, which is heavier.

## 13. Handoff checklist

- [x] Confirm owner-node storage, per-bone Envelope/Baked and the shared-rig
      refusal; record the remaining UX decisions (§5.1, §3.3, §12)
- [x] Phase 0 fixtures: two-bone/mesh, 0.21 compatibility, two-key
      shared/divergent/missing (the 33-bone dense fixture already existed)
- [x] Phase 1: bone ids, `SkinWeights` with BakedBone presence, 0.22 block,
      baked build branch, bake command, event
- [x] `bone_ids`, `skin_weights`, `weight_bake`, `weight_roundtrip` gates
- [ ] 0.21 compatibility: all loadable projects render bit-identically (two
      projects round-tripped; the dataset A/B and influence hashes are Phase 2)
- [x] Explicit Bake, and the new-layer Mixed rule (bake live nodes only)
- [x] Phase 2: `PaintWeights` mode, Bake/Revert/Rebake/Clear, brush, heatmap
      state label, distinct bone ink
- [x] GUI probe for bake/paint/undo/revert/save/reopen
- [x] Mesh-edit remap and duplication copy (Phase 5's first half)
- [ ] Phase 3: paint target set and multi-layer bake/paint command
- [ ] Phase 4: constraints, resampling, falloff, mesh-graph solve, bake-first
- [x] Phase 5: mesh edit policy and grid remap (image key add/remove still warns)
- [ ] Performance numbers recorded: build, bake, stroke, save size, entry count
- [x] Full Debug suite green except the documented pre-existing failure
      (82 pass / 1 fail, `object_tree_panel`'s six checks)
- [x] `AGENTS.md` updated with the landed rules
- [ ] Revisit reusable rig and pseudo-3D as separate plans
