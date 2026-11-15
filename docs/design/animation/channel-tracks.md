# Channel tracks: the end-state animation model

Status: implemented. State keys are first-class timeline keys; Image is an ImageStateKey (resource/grid/cache/FFD) plus two step-held offset channels.

The purpose of this note is to state the model before the code. Migration is a
separate exercise: old files and clipboard payloads are inputs to a converter,
not constraints on the shape of the document.

## The model

A property is a **container of tracks**, not a key. There are three kinds of
track, because there are three kinds of animation data:

1. **Channel track** — one scalar value per key, with its own easing and its own
   proportional state. This is the unit that is keyed, selected, moved,
   interpolated and copied independently.
2. **State track** — a discrete property state per key: an image resource, a
   blur's directional mode, a wave's shape settings, a move's spline mode. A
   state key changes the state at its frame; it is step-held.
3. **Structural track** — a full structural key (bone, pose, mesh, FFD, mask)
   with its existing data and attachments. These are not decomposable into
   scalars and are not channel keys.

The renderer-facing boundary does not change: `core::TimeKeyExpans` remains the
flattened per-frame result. The blender samples tracks/state/structural keys and
writes the expansions.

## Why not a bundle key with a component mask

A bundle key is one object per `(type, frame)` carrying several channel values.
A mask says which components the object authors. That model cannot represent an
X key and a Y key independently at the same frame; it has no slot for the second
object. "Separate X/Y" would be a flag plus special-case split/merge logic in
every command. It also forces a key object to hold unrelated state and value
data, and it needs per-component time bounds for proportional keys — which is a
channel key inside the bundle. In the implemented model the "linked" bit is
authored metadata on a channel key: the property key itself remains a view
aggregate of the keys at one frame.

The track model represents the user's operation directly: X and Y are already
independent; "linked" is an authored per-key grouping, not a key object.

## Identity

The key identity is one of:

- `ChannelKeyRef`: `(node/timeline, type, component, frame)`
- `StateKeyRef`: `(node/timeline, type, frame)`
- `StructuralKeyRef`: `(node/timeline, type, frame)`

A property key / grouped timeline marker is a **view aggregate** of the
channel/state refs at one frame. It is not stored and it is not undoable as a
document object.

## Storage

```
TimeLine
 ├─ Property "Move"
 │   ├─ ChannelTrack X
 │   ├─ ChannelTrack Y
 │   ├─ ChannelTrack centroid X      (hidden, non-graphable)
 │   ├─ ChannelTrack centroid Y
 │   └─ StateTrack   spline
 ├─ Property "Scale"
 │   ├─ ChannelTrack X
 │   └─ ChannelTrack Y
 ├─ Property "Image"
 │   ├─ ChannelTrack offset X        (step-held)
 │   ├─ ChannelTrack offset Y        (step-held)
 │   └─ StateTrack   image state     (resource, blend, grid, cache)
 └─ Structural "Bone" / "Pose" / "Mesh" / "FFD" / "Mask"
     └─ full structural keys, attachments unchanged
```

`ChannelKey` is a value:

```cpp
struct ChannelKey {
    float value;
    int id;
    util::Easing::Param easing;
    std::optional<KeyProportional> proportional;
};
```

`ChannelTrack` owns a frame-ordered map of those, a base value, and the
sampling rule. `StateTrack` owns frame-ordered typed states and a base state.
`PropertyTrack` owns the channel tracks and the state track.

## The state/channel rule

- **If it interpolates as a number, it is a channel.**
- **If it is a discrete state/resource/mode, it is a state key.**
- **If it is structural data with attachments, it is a structural key.**

| Type | Channels | State |
|---|---|---|
| Move | pos X/Y, centroid X/Y | spline |
| Rotate | angle | — |
| Scale | X/Y | — |
| Depth | depth | — |
| Opacity | opacity | — |
| Blur | X/Y/angle | directional |
| HSV | hue/sat/value | absolute |
| Wave | strength/wavelength/angle/phase/mix | speed, crest, dual angle/scale, time offset, deform |
| Image | offset X/Y (step-held) | resource, blend, grid, cache |
| Bone/Pose/Mesh/FFD/Mask | — | whole structural key |

Consequences:

- Changing the image asset keys the image state, not the offset channels.
- Changing a wave shape setting keys the wave state, not the value channels.
- The shared property dot keys every channel and state track at the playhead.
- A field context menu keys one channel only.

## Sampling

`ChannelTrack::sample(time)` mirrors the existing gatherer:

- no keys: base value;
- exact key at an integral frame: that key's value;
- before the first/after the last key: hold, unless looping, in which case the
  wrapped segment from the last key back to the first is evaluated;
- otherwise: the lower key's easing param shapes the segment, with the
  neighbours' values available for derived (Vector/Auto/AutoClamped) handles;
- a step-held channel (Image offset) uses the last key at or before the frame,
  else the base; it never interpolates and never loops.

Proportional keys are stored at their resolved frame/value; sampling does not
look at their weights. Projection is an editing-time operation over the same
track, so a proportional key's bounds are always keys of its own channel.

## Move

Move's spatial Catmull-Rom path needs the same knots in both channels. The
end-state rule is explicit:

- if the X and Y tracks have the same key-frame set, the property evaluator
  builds the spatial spline and samples it;
- if the key-frame sets differ, X and Y are sampled independently as scalar
  eased curves;
- the spline mode is a state key, step-held.

The graph and the blender ask the same property evaluator, so the drawn curve
and the played curve cannot disagree. The UI can show that smooth paths are
available while the channels share key frames, and that they are not when the
channels are separate.

Centroid is a pair of hidden channel tracks, not an implicit follower of X. The
shared property key and the transform tools key it with the position; the field
context menu for X/Y does not. If it becomes a first-class UI property later, it
already has the right storage shape.

## Property evaluator

Type-specific interpolation lives in one place:

```cpp
class PropertyEvaluator {
public:
    float sampleChannel(TimeKeyType, int component, const TimeInfo&) const;
    State sampleState(TimeKeyType, const TimeInfo&) const;
};
```

Most types are pure scalar sampling. Move is the one place that adds the spatial
spline coupling. Structural types keep their existing blend paths.

## Grouping and UI

- **Property dot:** one command keys/removes every channel and state track at
  the playhead. Its state is derived: static / animated / partial / keyed.
- **Field context menu:** add/remove this channel's key at the playhead.
- **Timeline key context menu:** "Separate Channels" marks the selected
  keyframe's channel keys split; "Link Channels" marks them linked again when
  every graphable channel has a key at that frame. This is authored document
  state (0.24), one undo step, because it changes how later edits behave. The
  action is offered only when all targets name one multi-channel property and
  one frame.
- **Timeline lanes:** linked by default. A linked keyframe is a property key:
  every graphable channel has a key there, all linked, and one marker is drawn
  at the property centre. As soon as one keyframe is split or partial, the row
  shows the property's channel lanes with **no separator between them**; linked
  frames stay one centred marker, split frames draw their channel markers in
  their own lanes. Labels follow the channel metadata.
- **Authoring follows the frame:** on a linked frame (or a property that has no
  split frame yet), a field edit writes/keys every graphable channel. On a split
  or partial frame, it writes/keys only the channel the field names. New channel
  keys from a field menu are split; the property dot writes linked keys.
- **Graph:** always per-channel. Insert/remove/move operate on one channel key.
- **Transform tools:** a combined drag keys every channel it changes; a static
  channel writes its base value.

## Serialization

The new format writes per property:

- channel track blocks: component, base value, and each key's frame, value,
  stable id, easing, proportional state and **linked bit** (0.24; older files
  read every key as linked);
- state key blocks, using the state key's own serialization;
- the base state;
- Image's two step-held offset channel tracks in an optional `ImgOffs_` block
  (0.25); a file with no authored offset omits it. A reader older than 0.25
  migrates the ImageKey's legacy offset field into channel keys; a 0.25 reader
  treats an absent block as deliberately unauthored and does not resurrect the
  compatibility field.

Structural types keep their existing blocks. Old files are converted on load:
each old key becomes channel keys at its frame plus a state key for its
non-graphable fields. Old proportional states are copied per channel. Clipboard
payloads are converted the same way.

## Progress at the time of writing

- **Opacity, Depth, Rotate and Scale are migrated end to end**: channel storage,
  playback, 0.23 serialization, 0.22 conversion, graph, timeline lanes,
  inspector, tree add-keys, copy/paste, easing and undo. `tests/channel_timeline.cpp`
  covers all four legacy conversions, and `tests/srt_keys.cpp` drives the SRT
  Rotate proxy through its static/base, existing-key and forced-key paths.
- **The graph's key move is channel-aware**: `MoveFrameOfKey` resolves one map
  per context, selection key identity is `(line, type, frame)` for a track-backed
  key, and `TimeLine::moveKeys` moves every channel of a multi-channel key in one
  validated batch. The tangent reflow reads the channel map too.
- **All graphable properties are migrated**: Opacity, Depth, Rotate, Scale, Move,
  Blur, HSV and Wave.
- **Image offsets are channels now (0.25).** The two offset values are step-held
  `ChannelTrack`s owned by the `TimeLine`, serialized in an optional `ImgOffs_`
  block; playback reads them through `TimeKeyExpans::imageOffset()`. The Image
  state key owns the resource, grid mesh, texture cache and FFD children. The
  offsets are keyed independently (inspector field menus, timeline lanes,
  copy/delete/paste and frame moves), and older files migrate each `ImageKey`
  offset into channel keys.
- **Image state payload is migrated.** `ImageStateKey` derives `StateKey`,
  lives in the Image property's `StateTrack`, owns the resource, blend mode,
  grid mesh, texture cache and FFD children, and is exposed through the same
  `map`/`baseValue`/`timeKey` accessors the old structural key used. `ImageKey`
  is the compatibility alias for that class. Offsets remain the two channel
  tracks (0.25). The on-disk Image block is unchanged, so old files load into the
  state track without a format bump.
- **UI**: the shared property dot keys every channel/state through the
  accessors; numeric inspector fields for Move, Scale, Rotate, Depth and
  Opacity have an Add/Remove <channel> Key context menu.
- **Linked/split keyframes are implemented (0.24).** Keys are linked by
  default. A linked frame is one property key and one timeline marker; a split
  frame's channel markers select and edit independently. The timeline key menu
  sets the linked bit on one frame in one undo step. The bit is serialized and
  moves with its key. Playback is unchanged by grouping.
- **State keys are first-class timeline keys.** `StateKey` is a `TimeKey` and
  uses the same identity, focus, selection, move, delete and clipboard paths.
  A linked property marker represents the state at its frame; a state-only or
  split-frame state change draws a diamond at the property centre and can be
  selected, moved, deleted and copied on its own. Deleting a whole property key
  also carries its state key.
- **Field-edge authoring is split-aware for Move, Scale, Blur, HSV and Wave.**
  Linked frames write/key all graphable channels; split frames write/key only
  the named channel. The inspector/filter fields pass their component. Move's
  position edits no longer silently key the hidden centroid pair; the centroid
  fields follow the same linked/split rule on their own channels.
- **Filter cards have per-field key menus** for Blur X/Y/angle, HSV H/S/V and
  Wave's five value channels; Blur amount and Wave shape settings stay
  property-level.

## Implementation sequence

1. Core: channel specs, channel keys/tracks, key ids, tests.
2. Core: state keys, property tracks, property evaluator, tests.
3. TimeLine: property/section storage and lookup; structural maps retained.
4. Playback: port the blender type by type; old renders are the control.
5. Commands/selection/identity: channel and state refs; graph/timeline/property
   editing; undo and copy/paste.
6. UI: timeline grouping/menus, shared property dot + field menus, transform
   tools.
7. Migration/serialization and removal of the old graphable bundle maps.

## Acceptance criteria

1. X and Y keys can exist at the same frame and move/delete independently.
2. The shared dot keys all tracks; a field menu keys one.
3. Separate/Link change grouping and authoring only; playback is unchanged.
4. Every channel has its own easing, proportional bounds and key time.
5. State changes do not create value keys, and value keys do not create state
   keys unless a property-level command explicitly keys both.
6. No component masks, bundle merge/split commands or "which sibling is
   authored" checks remain.
7. Adding a channel/property is metadata + evaluator work.
8. Old projects play exactly as before; migration tests prove it.
9. The graph and blender sample through the same property evaluator.
