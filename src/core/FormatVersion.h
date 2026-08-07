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
#define AE_PROJECT_FORMAT_MAJOR_VERSION 0
#define AE_PROJECT_FORMAT_MINOR_VERSION 10

#define AE_PROJECT_FORMAT_OLDEST_MAJOR_VERSION 0
#define AE_PROJECT_FORMAT_OLDEST_MINOR_VERSION 4

static_assert(
    AE_PROJECT_FORMAT_MAJOR_VERSION >= AE_PROJECT_FORMAT_OLDEST_MAJOR_VERSION,
    "format major version regressed below the oldest readable version");
static_assert(
    AE_PROJECT_FORMAT_MINOR_VERSION >= AE_PROJECT_FORMAT_OLDEST_MINOR_VERSION,
    "format minor version regressed below the oldest readable version");

#endif // CORE_FORMAT_VERSION_H