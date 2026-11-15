#ifndef CORE_FORMAT_VERSION_H
#define CORE_FORMAT_VERSION_H

// Single source of truth for the ANIMFX on-disk format version.
//
// The four literals that used to be duplicated ~9x across src/common.pri and
// every src/*/CMakeLists.txt target_compile_definitions() block now live here.
// Keep the // version bump here and add an include to any new consumer.
//
// Bump history (minor):
//   0.4  oldest minor version the loader still reads (AE_PROJECT_FORMAT_OLDEST_*)
//   0.8 -> 0.9   Blur filter introduced (BlurKey layout, src/core/BlurKey.h)
//   0.9 -> 0.10  folder blend mode 4CC in the FolderNd object block
//                (src/core/FolderNode.cpp, read under minor >= 10)
//   0.10 -> 0.11 the global block carries the frame range start (the first 4
//                bytes of its reserve region; older files default to 0, so
//                they load as [0, maxFrame])
//   0.11 -> 0.12 the global block's reserve also carries the optional viewport
//                background (a presence byte then an ARGB word, after the
//                frame range start); older files load as no override
//   0.12 -> 0.13 a key's easing param carries the handle-link flag (the field
//                0.15 replaced with the two per-side modes) after its bezier
//                controls; older files load it as FREE, which is what their
//                keys did
//   0.13 -> 0.14 a key writes ONE easing param PER GRAPHABLE COMPONENT (Move x
//                and y, Scale x and y, Blur x/y/angle, HSV hue/sat/value,
//                Image x/y; see core::easingComponentCount in
//                src/core/KeyEasing.h) instead of one shared by every
//                component. Older files held exactly one param, which the
//                reader broadcasts to every component (Deserializer::
//                readEasings) - lossless, since every component shared it
//   0.14 -> 0.15 the single handle-link flag (Param::aligned) became TWO
//                per-side modes, HandleMode handleOut then handleIn (Free,
//                Aligned, Vector, Auto, AutoClamped; Blender's five). The
//                retired Type_Auto id (14) is NOT reused: a 0.14 param of that
//                type loads as Type_Custom with BOTH sides AutoClamped (the
//                clamp the retired type always applied). An older file's one
//                flag maps to both sides
//                (Aligned/Free), and a pre-0.13 file has neither and loads Free
//   0.15 -> 0.16 every key writes its stable id and, when proportional, its
//                bound ids + per-axis weights (see core/TimeKey.h /
//                core/TimeLine.cpp). Older files carry neither: the loader
//                keeps the fresh constructor id and the absolute default
//   0.16 -> 0.17 the TimeLine block carries the filter stack: each filter's type
//                token and its enabled flag, in application order. The filter
//                VALUES remain the existing HSV/Blur key maps. Older files have
//                no stack and derive one from the filter lanes that exist, in
//                the historical blur-then-HSV order
//   0.17 -> 0.18 the Wave deform filter (core/WaveKey.h: five graphable
//                components). Its key layout is new in this version. The
//                PROPORTIONAL payload also grows from three component weights
//                to five (core/TimeKey::kMaxProportionalComponents), because a
//                Wave key has five components; the reader takes the count from
//                the version, so a 0.16/0.17 file still reads three
//   0.18 -> 0.19 the TimeLine block carries the PAINTED FILTER MASKS: after the
//                filter stack, one entry per lane that has a field (type token,
//                content rect, texel size, PNG blob). Older files have none, and
//                a lane without a mask renders exactly as before
//   0.19 -> 0.20 the LAYER MASK key type (core/MaskKey.h): a node's matte as an
//                8-bit alpha field in its content space, with the node's content
//                rect and texel grid, and the key's single shared easing. The
//                type name in the TimeKeyMap block is "Mask". Older files have
//                no layer mask, and a node without one renders as before
//   0.20 -> 0.21 the BONE KEY block no longer carries the derived influence
//                cache (cache owner, per-node influence maps, binding caches).
//                A <= 0.20 file's section is still consumed, then discarded;
//                ProjectLoader rebuilds every map after the ID solver.
//   0.21 -> 0.22 AUTHORED SKIN WEIGHTS and stable bone ids. Every bone writes
//                its project-local id (after its historical fields, so only a
//                0.22 reader knows it); every object node carries a "SkinWgt_"
//                block inside its ObjNode block: the baked per-bone weight
//                fields of the layers it skins. A 0.21 file has neither, loads
//                with the live envelope and assigns fresh bone ids (which its
//                first save writes). The weight block is authored data and is
//                read, not skipped: a corrupt one fails the load at its own
//                length-checked boundary. See docs/design/skeletal/
//                influence-painting-plan.md §4.
//   0.22 -> 0.23 the first per-channel property storage step. A TimeMap_ block
//                now starts with a bool: true means channel tracks (Rotate first;
//                component, base value, frame/value/easing/id/proportional keys),
//                false means the legacy bundle-key layout. A 0.22 file is read
//                through the legacy branch and its Rotate keys are converted to
//                channel tracks once, so old playback is preserved. See
//                docs/design/animation/channel-tracks.md.
//   0.23 -> 0.24 each channel key records whether it belongs to its frame's
//                linked property key (default true for older files). Linked
//                frames behave as one property key; split frames author only
//                their own channel. See docs/design/animation/channel-tracks.md.
//   0.24 -> 0.25 Image offsets become two step-held channel tracks beside the Image
//                state key's resource/grid/FFD payload, serialized in an
//                "ImgOffs_" block. Older files migrate each ImageKey's offset
//                to channel keys.
//
// Versioning rule (the 0.21 change is the worked example): a change to AUTHORED
// document data bumps the minor and may gate the reader. DERIVED state - anything
// recomputable from authored data, like the influence caches - must never be
// required to load: if it is persisted at all, its block carries a tag and a
// byte length so an unknown version can be skipped and recomputed, never failed.
#define AE_PROJECT_FORMAT_MAJOR_VERSION 0
#define AE_PROJECT_FORMAT_MINOR_VERSION 25

#define AE_PROJECT_FORMAT_OLDEST_MAJOR_VERSION 0
#define AE_PROJECT_FORMAT_OLDEST_MINOR_VERSION 4

static_assert(
    AE_PROJECT_FORMAT_MAJOR_VERSION >= AE_PROJECT_FORMAT_OLDEST_MAJOR_VERSION,
    "format major version regressed below the oldest readable version");
static_assert(
    AE_PROJECT_FORMAT_MINOR_VERSION >= AE_PROJECT_FORMAT_OLDEST_MINOR_VERSION,
    "format minor version regressed below the oldest readable version");

#endif // CORE_FORMAT_VERSION_H