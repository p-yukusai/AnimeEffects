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
#define AE_PROJECT_FORMAT_MAJOR_VERSION 0
#define AE_PROJECT_FORMAT_MINOR_VERSION 16

#define AE_PROJECT_FORMAT_OLDEST_MAJOR_VERSION 0
#define AE_PROJECT_FORMAT_OLDEST_MINOR_VERSION 4

static_assert(
    AE_PROJECT_FORMAT_MAJOR_VERSION >= AE_PROJECT_FORMAT_OLDEST_MAJOR_VERSION,
    "format major version regressed below the oldest readable version");
static_assert(
    AE_PROJECT_FORMAT_MINOR_VERSION >= AE_PROJECT_FORMAT_OLDEST_MINOR_VERSION,
    "format minor version regressed below the oldest readable version");

#endif // CORE_FORMAT_VERSION_H