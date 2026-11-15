# Bone influence feedback: the weight heatmap and bone-scoped painting

Status: **landed 2026-11-11.** No format change. This is the feedback half of
influence editing; it does not author weights.

Scope: how the built envelope weights are shown while a bone is edited, how a
bone becomes the target of the brush, and the one overlay-engine capability the
display needed. The influence cache itself is covered by
`docs/design/skeletal/influence-cache-plan.md`.

## 1. The gap

The Bone editor's envelope tools say **where** a bone reaches (the filled
polygon) but not **how strongly**: the envelope's falloff is only visible when
the layer is skinned and posed. The painter's notes ask for a heatmap next to
"proper weight painting", and the cache work (rebuild 3.4-4.5x cheaper, 3.3-5.7x
smaller) is what makes reading the built map every painted frame practical.

The two halves landed here are deliberately separate:

- the **view** reads `BoneInfluenceMap`'s stored arrays; it creates no cache and
  changes no data;
- the **target** is one bone, so the brush stops widening every envelope whose
  polygon passes under the stroke. This is a behaviour change in the two
  influence paint modes, stated in §3.

## 2. What the heatmap is

For one selected bone, over every layer the key skins, the mesh **triangles** are
drawn on the GPU with one weight per vertex:

- the GPU rasteriser interpolates the three vertex weights across each triangle
  and the fragment shader evaluates the ramp: blue (0) -> cyan -> green -> yellow
  -> red (1), with a fragment below `0.002` discarded, so an uninfluenced area
  stays clear instead of a blue wash;
- the wash is alpha 120. It is drawn OVER the artwork and before the skeleton, so
  the rig and the brush stay legible above the field;
- the weights are read once per vertex with
  `BoneInfluenceMap::Accessor::weight(bone, vertex)` and the triangles come from
  the mesh's own index buffer; the per-fragment interpolation is what makes this a
  field rather than a set of samples.
- there is no screen-sized image: the field is one indexed triangle draw per mesh
  through `ctrl::GLCorePaintEngine::drawWeightField`, at the framebuffer's own
  resolution (multisampled when the widget is).

The position is **the build's own matrix**, not an approximation:
`cache->innerMatrix()` translated by `TimeKeyBlender::getOriginOffset()`, then
the key owner's target matrix and the camera. `BoneKey::updateCaches` transforms
the mesh positions by exactly the same product before the weights are computed,
so a fragment sits where its weight was measured. A cache whose map and mesh have
different vertex counts is skipped (its indices could read past the weight
array); a map with no influence slots, or whose selected bone has no slot at all,
is skipped.

The weight lookup is `BoneInfluenceMap::Accessor::weight(bone, vertex)`. It is
the only reader that knows both storage forms:

- one slot: the index array only, and the weight is the implicit `1`;
- two to seven slots: slot-major `uint8` index plus `float32` weight;
- eight slots: the dense two sets of four.

Unused slots are `(index 0, weight 0)`, so the same first-match rule returns bone
0's real weight when bone 0 has one and zero when it does not. The bone index is
`BoneKey::Data::boneIndex(bone)`, the pre-order index over the top bones - the
same order `BoneInfluenceMap::makeBoneList` pushes. A bone outside that list is
`-1`; a bone past the map's cap is not shown.

**Why not points.** The first version drew one 6 px point per vertex. A vertex
is a control value, not a sample: the field between vertices was the thing the
artist was editing, and the point cloud read as a scatter of dots rather than a
surface. The first correction rasterized the triangles into a viewport-sized
image **on the CPU** and composited it with `QPainter::drawImage` (2026-11-12);
that was still the wrong shortcut - it is the GPU's own rasterisation written by
hand, resolution-bound by the viewport and with a full-frame upload per painted
frame. The current form is one indexed triangle draw through the overlay's GL
engine: the GPU interpolates, the fragment shader ramps, and nothing is uploaded
except the mesh's own vertex and weight arrays.

**Why the image path's alpha is gone.** The CPU image needed straight alpha and a
scheduled white brush because the overlay's texture shader is `uColor * texture`
and a `usePen = false` draw is dropped without a brush. The GL primitive has
neither problem: its blend is `GL_SRC_ALPHA/GL_ONE_MINUS_SRC_ALPHA` with the
destination kept opaque, exactly as the ink pass does, and the fragment writes the
ramp directly.

**The horizontal-mirror bug it exposed while it was an image.** `GLCorePaintEngine::drawImage`
(and `drawPixmap`) built its texture from `image.flipped(Horizontal | Vertical)`.
The drawer's quad already maps the image's v axis bottom-up, so only the
vertical flip is needed; the horizontal one mirrored every image, which the
glyph-free icons made easy to miss. The first surface wash landed on the
mirror image of the leg, which is how it was found (2026-11-12). The texture is
now flipped **vertically only**; the weight field itself is no longer an image,
and `tests/overlay_engine_primitives` pins the ramp's left-to-right orientation
from three read-back pixels.

## 3. How a bone becomes the target

**Terminology.** The UI calls the procedural `BoneShape` the **envelope**.
Adjust envelope edits its range, and ONE **Envelope brush** moves that range
toward an explicit **Value** (content pixels): **Flow** is the per-stamp rate,
**Size/Hardness** are only the footprint. A Value below the current range
shrinks, above it grows. "Influence" remains the generic concept and the internal code/persisted
enum names (`InfluenceMode`, `BoneInfluenceMap`), because the baked weights are
influence too; naming only the envelope "influence" made the two fields read as
different things. The panel's Envelope/Baked state uses the same word.

- Hovering a joint or a bone connector focuses it (the existing `bone::Focuser`)
  and previews its field; the last hovered bone stays in the display after the
  pointer leaves.
- **Right-click selects**: the bone turns the selected ink and the heatmap stays
  on it. Left is always the stroke, so a press that starts painting can no
  longer also mean "select this bone".
- The **Envelope brush** is a scalar range brush over the procedural band, not a
  per-vertex value. Each stamp moves the range toward its **Value**, scaled by the
  brush falloff and **Flow**; the same brush grows or shrinks depending on whether
  Value is above or below the current range. Size decides only where (the
  footprint) it acts, so it no longer doubles as the target value. The build turns
  the changed band into weights; the heatmap shows that result.
- The Envelope brush edits **only the selected bone**. If no bone is selected
  when a left stroke starts, the nearest envelope to the stroke's start point is
  selected first (`BoneKey::Data::nearestBone`; parentless roots carry no
  envelope and are skipped). The grow/shrink arithmetic, the shared brush and the
  one-command-per-stroke undo are the same for both directions - only the
  population changed from "every bone whose polygon the brush touches" to "the
  bone you chose".
- **Adjust envelope** (the handle mode) keeps its own selection and now shows
  the heatmap of the bone it is dragging (or hovering). Its editing scope was
  already one bone.

The **Show weights** toggle is on by default and offered in the envelope modes and in
Paint weights. The target hint is the envelope modes' line and
Paint weights' state tooltip.

## 4. The overlay capability

The heatmap itself is the engine's **weight-field primitive**:
`GLCorePaintEngine::drawWeightField` builds one small program
(`inPosition` + `inWeight` -> the ramp in the fragment shader), binds the
mesh's own element buffer and draws its triangles with the camera product. One
draw per skinned mesh, no image and no upload of a screen-sized buffer. The quad
is drawn after the artwork and before the skeleton; the destination alpha stays
opaque because this is tool ink over the picture, like every other overlay draw.

The engine still carries a batched `GL_POINTS` primitive: `drawPoints` used to
loop `gl::PrimitiveDrawer::drawPoint`, which is one tiny 8-segment ellipse **per
point**, and now schedules one `GL_POINTS` draw per slot chunk. The overlay's
drawer holds **8192 vertices per slot** (was the default 512). This is an engine
capability and its pixel contract; keep it for overlays that need point marks.

The gate is `tests/overlay_engine_primitives` (the GL overlay's own pixel
contract), now **14 probes** plus a three-pixel ink pass:

| probe | measured in the 400x300 harness |
|---|---|
| `weight_field_surface` (a 60x60 quad, weights 0.25..0.75) | **3600 pixels** - the whole area, not four corner dots |
| `weight_field_below_threshold` (the same quad, weights < 0.002) | **0 pixels** - the fragment shader discards the field's outside |
| `weight_field_ink` (three read-back pixels) | left `0,105,102`, middle `2,108,37`, right `98,104,7`: the ramp runs cool to hot left-to-right and the middle is the green stop |
| `points` (one 10 px point) | **100 pixels** - a 10x10 disc, proving the pen width reaches `glPointSize` |
| `points_batch` (9,000 points, past the 8,192 slot) | **14,400 pixels** over the 100x150 cell, proving the batch is split across slots |

Renderer in that run: `AMD Radeon R9 380 Series (radeonsi, tonga, ACO, Mesa
26.2.2)`. The ink pass also asserts the destination alpha is 255, so a blend-func
regression that punches a hole through the window fails here.

## 5. Interaction verification

The heatmap is interactive, so the committed gates cover its data
(`bone_influence_storage`, `bone_cap`) and the engine (`overlay_engine_primitives`);
the full display was checked in the real application (under Xvfb, `tart.anie`,
layer "head", Bone editor, Envelope brush):

- hovering bone 1 draws its field over the face, and hovering bone 3 draws the
  local left-cheek patch - the fields differ as the weights do;
- the **Show weights** toggle off then on is **pixel-identical** to the original
  (0 differing pixels in the canvas crop);
- right-clicking a connector selects the bone and the field **persists** after
  the pointer moves off the skeleton;
- the skeleton and the envelope ink stay above the field.

This is a manual check, not a probe: the heatmap's fields depend on a live
project and a camera, and a pixel gate for the dock's layout would be a second
subject. The data path and the engine path have their own gates.

## 6. What this is not

- **No authored per-vertex weights.** The envelope is still the only source, and
  the cache remains derived and rebuilt (format 0.21). Painting a field per
  vertex, curve interpolation, multiselection and reusable rigs are the next
  workstream; they need an authored format and a bone identity that survives
  edits, so they follow the plan's versioning rule rather than being slipped into
  this view.
- **No saved state and no format bump.** A heatmap is a function of the built
  map; persisting it would be a second derived cache.
- **No change to the influence build.** The same float weights the shader uses
  are the ones drawn; the view cannot disagree with the deformation.
- **The LBS palette and the shader are untouched.** This is an overlay, not a
  skinning variation.

The questions the authored-weight phase will have to answer, recorded here so
they are not rediscovered: whether the data belongs to a `BoneKey` (per frame)
or a node (shared across keys); how a bone is named across joint/bone edits
(the current index is stable only while the skeleton is); how per-node records
serialise and resolve on load (the legacy cache used reference IDs before the ID
solver); and where painted weights sit relative to the envelope and the top-eight
normalisation.

Those questions, the 0.22 block, the brush/constraint tools and the phased
gates are now written up in
[influence-painting-plan.md](influence-painting-plan.md) (Phases 1-2 landed:
ids, storage, format, baked build and the headless bake; then the PaintWeights
mode, the state panel/actions, the brush, the mesh-edit remap and duplication's
field copy).

## 7. Gates

- `tests/bone_influence_storage` - new `Accessor::weight` checks in every
  storage form: one-slot implicit 1, two-slot and five-slot sparse lookups, the
  dense groups, and a bone that is not in a vertex's slots reading 0.
- `tests/bone_cap` - `BoneKey::Data::boneIndex` (root 0, 33rd 32, outsider -1)
  and `nearestBone` (nearest segment, before-root clamp, root-only skeleton).
- `tests/overlay_engine_primitives` - the weight-field surface, its threshold
  discard, its ramp/orientation/alpha, and the two point-cloud probes.
- `tests/weight_paint_commands` - the authored state/actions, the one-command
  stroke, the mesh remap and the duplication copy (GL; Xvfb in the suite).
- `scripts/gui/probe_weights_bake_paint.sh` - the live panel: disabled brush on
  an envelope bone, Bake with a pixel-identical artwork crop, a stroke, exact
  undo/redo, Revert/Rebake/Clear through the overflow menu and save/reopen. The
  panel's stat row is the state readout; the canvas stays skeleton + heatmap.
- Full Debug suite: 83 pass / 1 fail, the documented pre-existing
  `object_tree_panel` rename/selection defect.
