# The tests are first-party code, so they use the project warning policy
# (see ae_enable_warnings in the top-level CMakeLists.txt): a test whose
# sections can silently not run is a test that reads as coverage.
ae_enable_warnings()

# The shared edge-autoscroll rule (ctrl/AutoScroll.h) used by the timeline and
# the graph panel's marquee drags. Pure geometry, no widget and no GL.
add_executable(autoscroll autoscroll.cpp)
target_include_directories(autoscroll PRIVATE .. ../src)
target_compile_definitions(autoscroll PRIVATE
    AE_MAJOR_VERSION=1 AE_MINOR_VERSION=7 AE_MICRO_VERSION=1 USE_GL_CORE_PROFILE)
target_link_libraries(autoscroll PRIVATE Qt6::Core Qt6::Gui)

# The per-layer mesh content level (img::MeshContentLevel): the mapping and its no-measurement
# fallback, and the creator's content threshold. Pure pixel math, no GL.
add_executable(mesh_content_level mesh_content_level.cpp)
target_include_directories(mesh_content_level PRIVATE .. ../src)
target_compile_definitions(mesh_content_level PRIVATE
    AE_MAJOR_VERSION=1 AE_MINOR_VERSION=7 AE_MICRO_VERSION=1 USE_GL_CORE_PROFILE)
target_link_libraries(mesh_content_level PRIVATE Qt6::Core Qt6::Gui img util)

# BoneKey stream compatibility gate: the <= 0.20 derived influence-cache section
# must be discarded byte-exactly, and 0.21 must carry no such section. It grew
# out of the old deserialize use-after-free regression (dangling
# &mBindingCaches.back() captured into a deferred IDSolver lambda); the discard
# path no longer stores those caches. See bonekey_regression.cpp.
add_executable(bonekey_regression bonekey_regression.cpp)
target_include_directories(bonekey_regression PRIVATE ..)
target_compile_definitions(bonekey_regression PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(bonekey_regression PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# The packed skinning attributes: 4 x uint8 indices + 4 x float32 weights per
# set = 40 B/vertex (from 64). Pins the layout and the default weights. The
# uint16 weight variant was measured and rejected; see the influence plan.
add_executable(bone_influence_storage bone_influence_storage.cpp)
target_include_directories(bone_influence_storage PRIVATE ..)
target_compile_definitions(bone_influence_storage PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(bone_influence_storage PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Stable bone ids (format 0.22): fresh ids, reservation, copy/clone/shadow
# semantics, and the version gate that keeps a 0.21 Bone2 block readable. See
# bone_ids.cpp.
add_executable(bone_ids bone_ids.cpp)
target_include_directories(bone_ids PRIVATE ..)
target_compile_definitions(bone_ids PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(bone_ids PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# core::SkinWeights: BakedBone presence is the state (an empty baked field is
# Baked, not Envelope), entries stay sorted/sparse, node records are
# life-linked, and bakeBone evaluates the build's exact float. See
# skin_weights.cpp.
add_executable(skin_weights skin_weights.cpp)
target_include_directories(skin_weights PRIVATE ..)
target_compile_definitions(skin_weights PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(skin_weights PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# The baked build branch and the bake command: a bake is bit-identical to the
# envelope at the bake frame, a missing baked vertex is 0 (never the envelope),
# and two keys with divergent same-id geometry refuse with their frame named.
# See weight_bake.cpp.
add_executable(weight_bake weight_bake.cpp)
target_include_directories(weight_bake PRIVATE ..)
target_compile_definitions(weight_bake PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(weight_bake PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Format 0.22 weight block round trip: a 0.21 object tree with no block loads,
# a 0.22 save/reload keeps empty Baked records and descendant references, and
# corrupt counts/weights/node references fail the load. See weight_roundtrip.cpp.
add_executable(weight_roundtrip weight_roundtrip.cpp)
target_include_directories(weight_roundtrip PRIVATE ..)
target_compile_definitions(weight_roundtrip PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(weight_roundtrip PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Phase 2's authored-weight actions: the Envelope/Baked/Mixed state, one-step
# Bake/Rebake/Revert/Clear, the one-command stroke rewrite, the structural mesh
# remap and duplication's field copy. See weight_paint_commands.cpp.
add_executable(weight_paint_commands weight_paint_commands.cpp)
target_include_directories(weight_paint_commands PRIVATE ..)
target_compile_definitions(weight_paint_commands PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(weight_paint_commands PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# The skeleton bone cap: 33 bones (the dataset's largest rig) must keep their
# last bone in the influence build and in the pose palette, and 65 must still
# stop at kMaxCount. Drives makeBoneList/writeWeights and the palette's
# matrices/dual quaternions through the production holder (BoneKey::Cache), so
# the cap cannot drift from the shader's MAX_BONES variation. See bone_cap.cpp.
add_executable(bone_cap bone_cap.cpp)
target_include_directories(bone_cap PRIVATE ..)
target_compile_definitions(bone_cap PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(bone_cap PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# The skinning palette's uniform block at a bone index the old default-block
# array could not address: a 201-bone palette, one point on bone 200. Compiles
# the real shader and skins through transform feedback, so the block binding,
# the std140 layout and the high-index fetch are pinned together. Needs a live
# GL context; the suite runs it under xvfb. See mesh_palette_block.cpp.
add_executable(mesh_palette_block mesh_palette_block.cpp
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc)
target_include_directories(mesh_palette_block PRIVATE ..)
target_compile_definitions(mesh_palette_block PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(mesh_palette_block PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# BoneShape's precomputed polygon containment must return exactly what
# QPolygonF::containsPoint(point, OddEvenFill) returns: the influence build is
# bit-identical only while it does. One .cpp and Qt Gui, so this gate needs no
# project, no GL and no core library.
add_executable(bone_polygon_index
    bone_polygon_index.cpp
    ${CMAKE_SOURCE_DIR}/src/core/BonePolygonIndex.cpp
)
target_include_directories(bone_polygon_index PRIVATE .. ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(bone_polygon_index PRIVATE Qt6::Core Qt6::Gui)

# The per-channel track primitive: ChannelSpec's defaults/clamps/interpolation,
# ChannelTrack's base/hold/ease/step/loop sampling and stable key ids. Compiled
# from its own sources so it needs no core::Project, no GUI audio stub and no GL.
add_executable(channel_track channel_track.cpp)
target_include_directories(channel_track PRIVATE ..)
target_compile_definitions(channel_track PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(channel_track PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core gl img thr util pugixml
)

# Move's per-channel evaluator: the spatial spline while X/Y share key frames,
# independent scalar sampling when they do not, and the hidden centroid pair.
add_executable(move_evaluator move_evaluator.cpp)
target_include_directories(move_evaluator PRIVATE ..)
target_compile_definitions(move_evaluator PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(move_evaluator PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core gl img thr util pugixml
)

# The first TimeLine migration: Opacity channel storage, component addressing,
# move/adopt and the 0.23 channel-track round trip.
add_executable(channel_timeline channel_timeline.cpp)
target_include_directories(channel_timeline PRIVATE ..)
target_compile_definitions(channel_timeline PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(channel_timeline PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core gl img thr util pugixml
)

# Regression test for the util::TreeUtil::deleteAll use-after-free write (the
# pre-fix body cleared the child list only AFTER deleting the children, so
# TreeChildren::clear() wrote 24 bytes into every already-freed child). Catches
# it with no sanitizer via a poison-and-leak Node allocator. See
# treeutil_delete_uaf.cpp.
#
# It exercises only header-only code (util/TreeUtil.h + util/TreeNodeBase.h), so
# it needs no library beyond Qt6::Core -- no `util` (which would drag in Widgets/
# Gui/OpenGL) and no other module. The `../src` include dir is what resolves
# "XCAssert.h" and "util/...", the same way the `util` target exports it.
add_executable(treeutil_delete_uaf treeutil_delete_uaf.cpp)
target_include_directories(treeutil_delete_uaf PRIVATE ../src)
target_link_libraries(treeutil_delete_uaf PRIVATE Qt6::Core)

# Regression test for the PackBits noop packet (util::PackBits::decode,
# src/util/PackBits.cpp): a 0x80 header is one byte of "copy nothing" and has to
# consume that byte, but the loop re-read it forever -- so a single 0x80 anywhere
# in a project's image data hung the load on one core with no progress instead of
# reporting a decode error. PackBits.cpp is compiled in directly, so, like
# treeutil above, this needs no library beyond Qt6::Core. See packbits_decode.cpp.
add_executable(packbits_decode packbits_decode.cpp ../src/util/PackBits.cpp)
target_include_directories(packbits_decode PRIVATE ../src)
target_link_libraries(packbits_decode PRIVATE Qt6::Core)

# Regression test for the line length check in core::Deserializer::readImage: the
# length comes from the file and nothing compared it with the per-line buffer
# sized from the image width, so a damaged project made the loader copy an
# arbitrary length into that block. Drives the real Deserializer over a crafted
# stream and asserts the refusal (and, as a control, that a well-formed stream is
# accepted). See readimage_line_length.cpp.
add_executable(readimage_line_length readimage_line_length.cpp)
target_include_directories(readimage_line_length PRIVATE ..)
target_compile_definitions(readimage_line_length PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(readimage_line_length PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Regression test for the noop packet in the PSD importer's own PackBits reader
# (img::PSDUtil::decodePlanePackBits): a 0x80 header was re-read forever, so a
# damaged .psd hung the import instead of failing it. See psd_rle_noop.cpp.
add_executable(psd_rle_noop psd_rle_noop.cpp)
target_include_directories(psd_rle_noop PRIVATE ..)
target_compile_definitions(psd_rle_noop PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(psd_rle_noop PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    gl img thr util pugixml
)

# Contract test for the picker's fuzzy search (src/gui/qml/FuzzyMatch.h): each rule
# that must fire, the two that must not (rule 6's length gate and span guard), the
# ordering between rules, and the rule that is deliberately absent -- no rule may
# match a subsequence spanning several words, because " capped size limit" sits on
# every capped haystack and a cross-word subsequence would stop the search narrowing.
# The header is header-only, so, like treeutil above, this needs no library beyond
# Qt6::Core. `..` is the repository root (the header lives under src/gui/qml).
add_executable(fuzzy_match fuzzy_match.cpp)
target_include_directories(fuzzy_match PRIVATE ..)
target_link_libraries(fuzzy_match PRIVATE Qt6::Core)

# Contract test for the cmnd::Gesture seam (src/cmnd/Gesture.h): the open edit that
# owns one coalescing command. Pins the semantics every editor relies on -- the
# builder runs inside the edit's macro, the gate (continues()) survives in-place
# rewrites, undo/redo/clear close the edit, the grabbed listener executes once per
# commit, and a gesture that is never committed pushes nothing. Headless: QtCore
# only, no display, no assets. See cmnd_gesture.cpp.
add_executable(cmnd_gesture cmnd_gesture.cpp)
target_include_directories(cmnd_gesture PRIVATE ..)
target_link_libraries(cmnd_gesture PRIVATE cmnd)

# Round-trip project loader/saver harness (open -> serialize -> reload -> close).
# Takes .anie paths on the command line; used to exercise real project files.
#
# It needs the embedded resources: loading a project initialises GL, which reads
# :/shaders/*.glsl. Without assets.qrc the harness dies with
# "XC_FATAL FileIO Error No such file or directory :/shaders/MeshTransformVert.glsl"
# on the first real project — i.e. the one tool whose job is to round-trip real
# projects could not open one. The sibling GL harness (media_exporter_e2e) has
# always embedded them; this one was left behind when the assets moved from a
# data directory on disk into the binary.
add_executable(roundtrip roundtrip_main.cpp
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc
)
target_include_directories(roundtrip PRIVATE ..)
target_compile_definitions(roundtrip PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(roundtrip PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Right-click key selection: an unselected key becomes the selection for
# the context menu; also pins the surface->model coordinate mapping the
# widget must apply to context-menu positions. See
# rightclick_transient_selection.cpp. Compiles the real theme::TimeLine so
# ctrl's renderer links without pulling in the whole gui library.
add_executable(rightclick_transient_selection rightclick_transient_selection.cpp
    ../src/gui/theme/TimeLine.cpp)
target_include_directories(rightclick_transient_selection PRIVATE ..)
target_compile_definitions(rightclick_transient_selection PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(rightclick_transient_selection PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Contract test for the "creation selects its output" rule: pushing/copying a
# key (via the project timeline-modified signal) replaces the current selection
# with the created keys, and never does so on an undo or a non-creation event.
# See creation_select_keys.cpp.
add_executable(creation_select_keys creation_select_keys.cpp
    ../src/gui/theme/TimeLine.cpp)
target_include_directories(creation_select_keys PRIVATE ..)
target_compile_definitions(creation_select_keys PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(creation_select_keys PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Proportional-key EDIT contract: a frame move, a value edit and a toggle each
# stay one undo step and restore the selected keys AND the proportional keys
# between their bounds. Drives the real TimeLineUtil commands; headless.
# See mask_mapping.cpp: the ONE content<->screen inverse the brush and the rubylith
# share. Pure matrix maths, so it needs nothing but the camera and the node transform.
# See layer_thumbnails.cpp: the object tree's cached layer thumbnails - a cell always,
# the art inside it, keyed on the pixels so a reimport updates.
add_executable(layer_thumbnails layer_thumbnails.cpp
    ../src/gui/ThumbnailCache.cpp)
target_include_directories(layer_thumbnails PRIVATE .. ../src ../src/gui)
target_compile_definitions(layer_thumbnails PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(layer_thumbnails PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets
    Qt6::OpenGL Qt6::OpenGLWidgets
    img util
)

# See brush_tip_pixels.cpp. The brush tip is a fragment shader (gl::PrimitiveDrawer's
# Type_BrushTip), so its contract - a flat core, a brushFalloff band, nothing past the rim -
# is only measurable by reading pixels back from the overlay's own drawing path.
add_executable(brush_tip_pixels brush_tip_pixels.cpp)
target_include_directories(brush_tip_pixels PRIVATE .. ../src)
target_compile_definitions(brush_tip_pixels PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(brush_tip_pixels PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Gui Qt6::Widgets
    Qt6::OpenGL Qt6::OpenGLWidgets
    ctrl core gl img util
)

# See viewport_input.cpp. The viewport's pointer router: hover feeds the tool, a brush stroke is
# applied per sample, a state-only drag once per frame. Two live defects (the brush ring only moving
# while a button was held, and a stroke that painted ONE dab) had no gate; this is it.
add_executable(viewport_input viewport_input.cpp
    ../src/gui/ViewportInput.cpp
    ../src/gui/PointerSmoothing.cpp
    ../src/gui/theme/TimeLine.cpp)
target_include_directories(viewport_input PRIVATE .. ../src ../src/gui)
target_compile_definitions(viewport_input PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(viewport_input PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Gui Qt6::Widgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

add_executable(mask_mapping mask_mapping.cpp)
target_include_directories(mask_mapping PRIVATE .. ../src)
target_compile_definitions(mask_mapping PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(mask_mapping PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets
    Qt6::OpenGL Qt6::OpenGLWidgets
    cmnd core gl util
)

add_executable(proportional_edits proportional_edits.cpp
    ../src/gui/theme/TimeLine.cpp)

# See layer_mask_commands.cpp. The four edits every mask surface calls, on the same
# Project + node fixture proportional_edits uses.
add_executable(layer_mask_commands layer_mask_commands.cpp
    ../src/gui/theme/TimeLine.cpp)
target_include_directories(layer_mask_commands PRIVATE ..)
target_compile_definitions(layer_mask_commands PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(layer_mask_commands PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)
target_include_directories(proportional_edits PRIVATE ..)
target_compile_definitions(proportional_edits PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(proportional_edits PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# The 0.16 key record on disk: stable id + proportional bound ids/weights survive
# TimeLine serialize/deserialize. Headless, in-memory stream.
add_executable(timekey_roundtrip timekey_roundtrip.cpp)
target_include_directories(timekey_roundtrip PRIVATE ..)
target_compile_definitions(timekey_roundtrip PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(timekey_roundtrip PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core gl img thr util pugixml
)

# Headless reproduction of the "Pull bones" (PoseEditMode_Draw -> pose::
# DrawBoneMode) crash. Loads a real .anie, builds the real ctrl::Driver with the
# Pose tool, and feeds synthetic left-button press/drag/release frames through
# core::AbstractCursor. Run under gdb (and xvfb-run for the GL context):
#   xvfb-run -a gdb -batch -ex run -ex "bt 25" --args \
#       build/tests/Debug/pose_pull_crash <file.anie>
# See pose_pull_crash.cpp.
add_executable(pose_pull_crash pose_pull_crash.cpp
    # ctrl::Driver owns ctrl::GraphEditor, whose renderQt() calls theme::Graph, and
    # the ctrl library leaves that symbol unresolved (the same reason graph_drag
    # and graph_render_pixels compile it). Without this the target links the graph
    # editor's object and fails on theme::Graph::from / keyColor.
    ../src/gui/theme/Graph.cpp
    # ctrl::TimeLineEditor is one TU that also references gui and theme symbols from code
    # this tool never calls (the clipboard paste path, the FFD mesh lookup, the timeline
    # renderer's theme). theme::TimeLine is a leaf and is linked; the rest is dropped by
    # --gc-sections, which is why the function sections are on for this target only.
    ../src/gui/theme/TimeLine.cpp
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc)
target_include_directories(pose_pull_crash PRIVATE ..)
target_compile_options(pose_pull_crash PRIVATE -ffunction-sections -fdata-sections)
target_link_options(pose_pull_crash PRIVATE -Wl,--gc-sections)
# NOT part of the default build: this is the reproduction tool AGENTS.md describes, not a
# gate, and its link depends on how much of the single ctrl::TimeLineEditor TU the linker
# keeps - which pulls gui and theme symbols this tool never calls. Build it explicitly
# (`ninja -C build pose_pull_crash:Debug`) when reproducing the pose-tool crash.
set_target_properties(pose_pull_crash PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_compile_definitions(pose_pull_crash PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(pose_pull_crash PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Regression test for the custom-bezier easing solver swap (WebKit UnitBezier
# port) and its control-point solver cache. See easing_regression.cpp.
find_package(Threads REQUIRED)
add_executable(easing_regression easing_regression.cpp)
target_include_directories(easing_regression PRIVATE ..)
target_compile_definitions(easing_regression PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(easing_regression PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    util
    Threads::Threads
)

# Code-based layout verification for the locked EasingPickWidget. Compiles the
# widget source directly (not the whole gui lib) so it links only util + Qt
# Widgets; the layout is asserted against tests/golden/easing_picker_geometry.json
# (the geometry measured from the browser reference).
add_executable(easing_picker_layout
    easing_picker_layout.cpp
    ../src/gui/prop/EasingPickWidget.cpp
    ../src/gui/prop/bezierCurveEditor.cpp
    ../src/gui/AppStyle.cpp
    ../src/gui/theme/Icons.cpp
    ../src/gui/prop/EasingPickWidget.h)
target_include_directories(easing_picker_layout PRIVATE .. ../src/gui)
target_compile_definitions(easing_picker_layout PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(easing_picker_layout PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    util
)

# Interactive behavior probe for the EasingPickWidget (mode switch, slider
# drag-outside, knob clip, bezier spin input). Same source set as the layout
# gate; prints PASS/FAIL lines for each behavior.
# What the destination picker shows, offscreen: the list's own state, which is the
# part a person is affected by (what is selected when it opens, what typing selects,
# whether a platform can become an answer). It drives gui::DestinationPickerModel
# directly - no QWidget, no QTreeWidget, no QML engine - because the model is where
# the behaviour now lives and the Qt Quick scene binds to the same object. No
# display: the GUI probes ask the same question and need a bus and a window manager
# to do it, which is why this went unnoticed.
add_executable(destination_picker
    destination_picker.cpp
    ../src/gui/qml/DestinationPickerModel.cpp
    ../src/ctrl/ExportTarget.cpp)
target_include_directories(destination_picker PRIVATE .. ../src/gui)
target_compile_definitions(destination_picker PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_EXPORT_TARGETS_PATH="${CMAKE_SOURCE_DIR}/assets/export/ExportTargets.txt"
    # The .qm are build artifacts (not sources) and they are what the app actually
    # loads, so the language assertions need the built catalog and a dependency on
    # it: without release_translations this test would read a stale file and pass.
    AE_I18N_QM_DIR="${PROJECT_BINARY_DIR}/src/gui"
)
add_dependencies(destination_picker release_translations)
target_link_libraries(destination_picker PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml Qt6::Svg
    media
)

# The picker's KEYBOARD contract, offscreen and windowless: which key belongs to the
# search field's caret and which one moves the list. Neither of the two tests above
# can ask it - the model test has no scene, so nothing routes a key, and the GUI
# probes cannot inject a keystroke without a window manager. Measured before relying
# on it: a key sent straight to a Qt Quick item reaches the item's Keys filter with NO
# window and NO active focus, and everything the filter does not consume still reaches
# the editor. So this loads the real DestinationPicker.qml, injects the keys, and
# reads the model - the same object the scene binds to.
add_executable(picker_keyboard
    picker_keyboard.cpp
    ../src/gui/qml/DestinationPickerModel.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/theme/Icons.cpp
    ../src/ctrl/ExportTarget.cpp)
target_include_directories(picker_keyboard PRIVATE .. ../src ../src/gui)
target_compile_definitions(picker_keyboard PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_EXPORT_TARGETS_PATH="${CMAKE_SOURCE_DIR}/assets/export/ExportTargets.txt"
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
add_dependencies(picker_keyboard release_translations)
target_link_libraries(picker_keyboard PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickControls2
    media
)

# The new-project dialog, headless: the model's defaults and clamps, and the scene's
# own wiring. The form used to be a QWidget/QFormLayout, so only a shown widget could
# be driven; it is a Qt Quick scene now, and both halves of the contract are asked
# without a window - the model directly, and the REAL NewProjectDialog.qml loaded and
# clicked so the "Live link" checkbox is proven to write back (AeCheckBox records that
# setting `checked` does not fire toggled(), so a click is the only honest driver).
# The MASK CARD, headless: the real PropertiesPanel.qml with the real
# gui::InspectorViewModel over a real Project + node. It is also the only check that
# the whole inspector scene still loads, which is what a card's binding error breaks.
add_executable(inspector_mask_card inspector_mask_card.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/theme/Icons.cpp)
target_include_directories(inspector_mask_card PRIVATE .. ../src ../src/gui)
target_compile_definitions(inspector_mask_card PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
target_link_libraries(inspector_mask_card PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickControls2)
set_target_properties(inspector_mask_card PROPERTIES AUTOMOC ON)

add_executable(new_project_form new_project_form.cpp
    ../src/gui/qml/NewProjectViewModel.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/theme/Icons.cpp)
target_include_directories(new_project_form PRIVATE .. ../src ../src/gui)
target_compile_definitions(new_project_form PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
target_link_libraries(new_project_form PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickControls2
    media
)

# The project-settings dialog, headless: the model's defaults and clamps (canvas
# dimensions bounded by the device buffer, fps floor, the frame range's
# start <= end invariant) and the scene's own wiring. Same shape as
# new_project_form: the model directly, and the REAL ProjectSettingDialog.qml
# loaded so the fields are proven to write back.
add_executable(project_setting_form project_setting_form.cpp
    ../src/gui/qml/ProjectSettingViewModel.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/theme/Icons.cpp)
target_include_directories(project_setting_form PRIVATE .. ../src ../src/gui)
target_compile_definitions(project_setting_form PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
target_link_libraries(project_setting_form PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickControls2
    media
)

# The key-bindings dialog, headless: the model's capture rules (Delete/Backspace
# clears, a modifier-free key after an existing chord becomes the sub-key, a valid
# chord replaces, conflicts are reported both ways, reset-to-defaults restores),
# and that a real key press through KeyBindingDialog.qml writes the row.
# ctrl/KeyBinding.cpp is compiled in directly (the model is Core-only plus that
# type, the way destination_picker compiles ExportTarget.cpp).
add_executable(key_binding_form key_binding_form.cpp
    ../src/gui/qml/KeyBindingViewModel.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/theme/Icons.cpp
    ../src/ctrl/KeyBinding.cpp)
target_include_directories(key_binding_form PRIVATE .. ../src ../src/gui)
target_compile_definitions(key_binding_form PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
target_link_libraries(key_binding_form PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickControls2
    media
)

# The general-settings dialog, headless: the model's defaults and that each edit
# flips the matching has-changed predicate the shell reports to the menu bar, plus
# the REAL GeneralSettingDialog.qml loaded so a control is proven to write back.
add_executable(general_setting_form general_setting_form.cpp
    ../src/gui/qml/GeneralSettingViewModel.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/theme/Icons.cpp
    # The model now builds its easing list from util::Easing's own display table (the
    # one place the names live, so they translate - see GeneralSettingViewModel.cpp).
    # The two files are header-only in their dependencies, so the light target stays
    # light; the `util` library would drag the whole Qt/GL stack in for two symbols.
    ../src/util/Easing.cpp
    # Easing.cpp's solver funnels go through EasingSegment (the prepared curve), so a
    # target that compiles Easing.cpp directly needs it beside it.
    ../src/util/EasingSegment.cpp
    ../src/util/EasingName.cpp
    ../src/gui/MouseSetting.cpp)
target_include_directories(general_setting_form PRIVATE .. ../src ../src/gui)
target_compile_definitions(general_setting_form PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
target_link_libraries(general_setting_form PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickControls2
    media
)

# The numeric fields' Escape contract (F34, decided 2026-09-30): Escape restores
# the value from when THIS edit began - not the value the property happens to hold
# at Escape time (the owner can re-sync it mid-edit) and not the session's opening
# value - with the AeNumberField sentinel ("Auto") as part of the same line.
# Loads the REAL ScrubSpinBox.qml and AeNumberField.qml and drives the edit
# lifecycle through a real offscreen window, because the lifecycle IS focus:
# beginEdit() focuses the editor, Escape blurs it, and onActiveFocusChanged is
# where the edit starts and ends. See scrub_escape.cpp for the measured routing
# facts this rests on.
add_executable(scrub_escape
    scrub_escape.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/theme/Icons.cpp)
target_include_directories(scrub_escape PRIVATE .. ../src ../src/gui)
target_compile_definitions(scrub_escape PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
target_link_libraries(scrub_escape PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickControls2
    media
)

# C21's hover-fade rule (item 21): no wrong intermediate colour. The ghost
# button's rest fill must fade through its OWN RGB at varying alpha - the old
# "transparent" endpoint made the ColorAnimation pass through semi-transparent
# black ("Add range hover flashes black"). Samples the ANIMATED background.color
# property per frame (a grabToImage is blind here: it composites over opaque
# black and hides alpha - measured). See hover_parity.cpp.
add_executable(hover_parity
    hover_parity.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/theme/Icons.cpp)
target_include_directories(hover_parity PRIVATE .. ../src ../src/gui)
target_compile_definitions(hover_parity PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
target_link_libraries(hover_parity PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickControls2
    media
)

# The tooltip contract (E26/E27/E28/E29, decided 2026-09-30): WHICH controls
# keep a tooltip (the set is explicit - the Auto/Reset affordances and truncated
# picker rows), WHERE the bubble sits (below-first, flipping above at the bottom
# edge) and that it is NEVER clipped at the dialog's edge. Hosts the REAL
# call-site files and the real ExportDialog.qml (so the set cannot silently
# grow) with the real view model. See tooltip_contract.cpp.
add_executable(tooltip_contract
    tooltip_contract.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/theme/Icons.cpp
    ../src/gui/qml/ExportViewModel.cpp
    ../src/gui/qml/ExportViewModel.h
    ../src/gui/ExportFacts.cpp
    ../src/ctrl/ExportTarget.cpp
    # The embedded destination table (see export_form below: the dialog's own
    # lookups go through the static loaders to ":/export/ExportTargets.txt").
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc)
target_include_directories(tooltip_contract PRIVATE .. ../src ../src/gui)
target_compile_definitions(tooltip_contract PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
add_dependencies(tooltip_contract release_translations)
target_link_libraries(tooltip_contract PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickControls2
    media
)

# The export form's overflow contract (D25 + Q9): the content scrolls by wheel,
# drag and keys while the Cancel/Export row stays pinned and reachable, every
# control is scrolled into view on focus, the bar appears only on overflow, and
# the tooltip/combo popups inside the scrolling content are clipped by NOTHING
# (E28's clamp narrows to the visible viewport). Hosts the REAL ExportDialog.qml
# with the real view model. See form_layout.cpp.
# A FILTER CARD'S HEADER DOES NOT MOVE WHEN THE CARD EXPANDS (notes.md, 2026-10-26).
# The rule - "expansion may change ink, never geometry" - is a QML geometry fact that no
# model test can see: inspector_view_model pins the STATE, and this pins the pixels it
# must not move, through the marker each card publishes for its own disclosure.
add_executable(filter_card_geometry
    filter_card_geometry.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/theme/Icons.cpp
    ../src/gui/qml/InspectorViewModel.cpp
    ../src/gui/qml/InspectorViewModel.h
    ../src/gui/qml/InspectorRowModel.cpp
    ../src/gui/qml/InspectorRowModel.h
    ../src/gui/qml/FilterStackModel.cpp
    ../src/gui/qml/FilterStackModel.h
    ../src/gui/qml/QmlFormAccessibility.cpp
    ../src/gui/prop/prop_KeyAccessor.cpp
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc)
target_include_directories(filter_card_geometry PRIVATE .. ../src ../src/gui)
target_compile_definitions(filter_card_geometry PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
target_link_libraries(filter_card_geometry PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml media
)

add_executable(form_layout
    form_layout.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/theme/Icons.cpp
    ../src/gui/qml/ExportViewModel.cpp
    ../src/gui/qml/ExportViewModel.h
    ../src/gui/ExportFacts.cpp
    ../src/ctrl/ExportTarget.cpp
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc)
target_include_directories(form_layout PRIVATE .. ../src ../src/gui)
target_compile_definitions(form_layout PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
add_dependencies(form_layout release_translations)
target_link_libraries(form_layout PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test
    media
)

# Persistence: C17 (group expansion remembered across sessions) and A5 (the
# dialog remembers its size). Drives the MACHINERY (DestinationPickerModel's
# expandedIds/applyExpandedIds) and the WIRING (gui::DestinationPicker and
# gui::ExportDialog reading and writing QSettings) through the real dialog
# classes. Their QML scenes live in the application's qml module and are not
# linked here (the scene routes are picker_keyboard's and the probes' job).
add_executable(persist_state
    persist_state.cpp
    ../src/gui/DestinationPicker.cpp
    ../src/gui/ExportDialog.cpp
    ../src/gui/GUIResources.cpp
    ../src/gui/AppStyle.cpp
    ../src/gui/qml/QmlFormAccessibility.cpp
    ../src/gui/qml/DestinationPickerModel.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/qml/ExportViewModel.cpp
    ../src/gui/theme/Theme.cpp
    ../src/gui/theme/Icons.cpp
    ../src/gui/ExportFacts.cpp
    ../src/ctrl/ExportTarget.cpp
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc)
target_include_directories(persist_state PRIVATE .. ../src ../src/gui)
target_compile_definitions(persist_state PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_EXPORT_TARGETS_PATH="${CMAKE_SOURCE_DIR}/assets/export/ExportTargets.txt"
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
add_dependencies(persist_state release_translations)
target_link_libraries(persist_state PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickWidgets Qt6::QuickControls2
    media
)

# The export dialog, headless: what it STATES and what it OFFERS per destination.
# This is the coverage hole the session named twice: every export-form bug was either
# a fact contradicting a control or a control the destination did not decide, and both
# are questions about state and text rather than about pixels. Since the port to Qt
# Quick it drives gui::ExportViewModel directly - no QWidget, no offscreen show, no
# isVisible() - so its sources compile GUI-free: no GL, no Widgets, no Quick. See
# tests/export_form.cpp.
add_executable(export_form
    export_form.cpp
    ../src/gui/ExportFacts.cpp
    ../src/gui/qml/ExportViewModel.cpp
    ../src/gui/qml/ExportViewModel.h
    ../src/ctrl/ExportTarget.cpp
    # The destination TABLE, embedded the way the application embeds it.
    #
    # The table is loaded two ways: this test passes a path (AE_EXPORT_TARGETS_PATH) for
    # the plan and the facts, while the DIALOG's own lookups - the codec's quality span,
    # the quality buckets, the time budgets - call the static loaders, whose default path
    # is the embedded ":/export/ExportTargets.txt". Without this resource the test's
    # dialog sees an EMPTY table: every codec looks like it has no quality knob, the
    # override control is never offered (measured: 0 of 56 rows), and the assertions that
    # ask about it were skipped for every row while the suite stayed green. A dialog test
    # that reads a different table than the application is not testing the application.
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc)
target_include_directories(export_form PRIVATE .. ../src ../src/gui)
target_compile_definitions(export_form PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/assets"
    AE_EXPORT_TARGETS_PATH="${CMAKE_SOURCE_DIR}/assets/export/ExportTargets.txt"
    AE_EXPORT_STRINGS_PATH="${CMAKE_SOURCE_DIR}/src/ctrl/ExportTargetStrings.h"
    # .qm are build artifacts (release_translations); the runtime translation
    # assertion loads one from the build output, never from source.
    AE_I18N_QM_DIR="${PROJECT_BINARY_DIR}/src/gui"
)
target_link_libraries(export_form PRIVATE
    Qt6::Core Qt6::Gui Qt6::Xml Qt6::Svg
    media
)
# The runtime translation assertion reads a BUILT catalog, so the test has to depend on
# the catalog being rebuilt: without this it can run against a stale .qm and report that
# the table arrives untranslated, which is exactly what happened the first time.
add_dependencies(export_form release_translations)

add_executable(picker_probe
    picker_probe.cpp
    ../src/gui/prop/EasingPickWidget.cpp
    ../src/gui/prop/bezierCurveEditor.cpp
    ../src/gui/AppStyle.cpp
    ../src/gui/theme/Icons.cpp
    ../src/gui/prop/EasingPickWidget.h)
target_include_directories(picker_probe PRIVATE .. ../src/gui)
target_compile_definitions(picker_probe PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    AE_ICONS_DIR="${CMAKE_SOURCE_DIR}/assets/icons"
    USE_GL_CORE_PROFILE
)
target_link_libraries(picker_probe PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    util
)

# Screenshot harness for the EasingPickWidget: renders the popup with every
# text glyph hidden (setHideText, alpha-0 text so the geometry is unchanged)
# in the normal In-Out state and in the Custom (bezier) state, writing the two
# PNGs so the Qt replica can be compared with the browser mock at a glance.
# Same source set as the layout gate / behavior probe.
add_executable(easing_picker_shot
    easing_picker_shot.cpp
    ../src/gui/prop/EasingPickWidget.cpp
    ../src/gui/prop/bezierCurveEditor.cpp
    ../src/gui/AppStyle.cpp
    ../src/gui/theme/Icons.cpp
    ../src/gui/prop/EasingPickWidget.h)
target_include_directories(easing_picker_shot PRIVATE .. ../src/gui)
target_compile_definitions(easing_picker_shot PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(easing_picker_shot PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    util
)

# Popup-position probe for the EasingPickWidget: shows the picker as a real
# top-level Qt::Popup, toggles Custom<->normal, and asserts the left edge does
# not drift rightward (the width is fixed; only the height should change).
add_executable(picker_pos_probe
    picker_pos_probe.cpp
    ../src/gui/prop/EasingPickWidget.cpp
    ../src/gui/prop/bezierCurveEditor.cpp
    ../src/gui/AppStyle.cpp
    ../src/gui/theme/Icons.cpp
    ../src/gui/prop/EasingPickWidget.h)
target_include_directories(picker_pos_probe PRIVATE .. ../src/gui)
target_compile_definitions(picker_pos_probe PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(picker_pos_probe PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    util
)

# Observable project attribute: setting an attribute value drives
# onProjectAttributeModified (and an unchanged set does not), so a settings
# dialog can apply a value live and scope undo to a separate command grain. See
# attribute_observable.cpp.
add_executable(attribute_observable attribute_observable.cpp)
target_include_directories(attribute_observable PRIVATE ..)
target_compile_definitions(attribute_observable PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(attribute_observable PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Model 2 at the accessor layer: static properties edit their base value, and
# animated properties edit/create a key at the playhead. The unified Inspector
# depends on this split; see key_accessor_model2.cpp.
# The Transform tool's key policy (tested with the real SRTEditor/MoveMode): a
# static channel edits its base value, an animated one auto-keys at the playhead,
# and a checked "Create ... key" box forces exactly that channel. See srt_keys.cpp.
add_executable(srt_keys srt_keys.cpp)
target_include_directories(srt_keys PRIVATE .. ../src)
target_compile_definitions(srt_keys PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(srt_keys PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

add_executable(key_accessor_model2
    key_accessor_model2.cpp
    ../src/gui/prop/prop_KeyAccessor.cpp)
target_include_directories(key_accessor_model2 PRIVATE .. ../src)
target_compile_definitions(key_accessor_model2 PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(key_accessor_model2 PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)


# The QML inspector's document model: model-2 writes, key dots, filter stack and
# undo. See inspector_view_model.cpp.
add_executable(inspector_view_model
    inspector_view_model.cpp
    ../src/gui/qml/InspectorViewModel.cpp
    ../src/gui/qml/InspectorRowModel.cpp
    ../src/gui/qml/FilterStackModel.cpp
    ../src/gui/prop/prop_KeyAccessor.cpp)
target_include_directories(inspector_view_model PRIVATE .. ../src)
target_compile_definitions(inspector_view_model PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(inspector_view_model PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# The Tools dock's MODEL: the tool registry, each tool's own parameters, which rows a
# mode shows, the brush retargeting, the mask targets and the signals the host acts on.
# Headless and widget-free - the same split as inspector_view_model, on the other side
# of the dock. See tool_view_model.cpp.
add_executable(tool_view_model
    tool_view_model.cpp
    ../src/gui/qml/ToolViewModel.cpp
    ../src/ctrl/PoseParam.cpp)
target_include_directories(tool_view_model PRIVATE .. ../src)
target_compile_definitions(tool_view_model PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(tool_view_model PRIVATE
    Qt6::Core Qt6::Gui Qt6::OpenGL
)

# The header's widget-free models: menu commands, the view settings that moved out
# of the Tools dock, and the slot toggles. See header_model.cpp.
add_executable(header_model
    header_model.cpp
    ../src/gui/qml/MenuCommand.cpp
    ../src/gui/qml/ViewSettingsModel.cpp
    ../src/gui/qml/DockSlotsModel.cpp)
target_include_directories(header_model PRIVATE .. ../src)
target_compile_definitions(header_model PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
)
target_link_libraries(header_model PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test
)

# The header SCENE: the MenuBar, its delegates and the Action<->MenuCommand bridge,
# loaded offscreen with a fake registry. See header_scene.cpp.
add_executable(header_scene
    header_scene.cpp
    ../src/gui/qml/MenuCommand.cpp
    ../src/gui/qml/ViewSettingsModel.cpp
    ../src/gui/qml/DockSlotsModel.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/theme/Icons.cpp)
target_include_directories(header_scene PRIVATE .. ../src ../src/gui)
target_compile_definitions(header_scene PRIVATE
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(header_scene PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Widgets Qt6::Test
)

# AeComboBox's popup geometry: the optional scrollbar lane is reserved only when
# the list actually scrolls, and it is flush with the popup's inner edge. See
# combo_geometry.cpp.
add_executable(combo_geometry
    combo_geometry.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/theme/Icons.cpp
)
target_include_directories(combo_geometry PRIVATE .. ../src ../src/gui)
target_compile_definitions(combo_geometry PRIVATE
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    USE_GL_CORE_PROFILE
)
target_link_libraries(combo_geometry PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Widgets
)

# The Tools dock's SCENE: loads the REAL ToolsPanel.qml and drives its controls through
# QAccessible, i.e. the same route a screen reader and scripts/gui/ae_ui.py take. Every
# assertion is a defect the widget dock had (unaddressable icons, the seventh tool
# wrapped onto its own row, unlabeled mask glyphs, a read-only hardness readout, no
# scrollbar). See tools_panel.cpp.
add_executable(tools_panel
    tools_panel.cpp
    ../src/gui/qml/ToolViewModel.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/qml/QmlFormAccessibility.cpp
    ../src/gui/theme/Icons.cpp
    ../src/ctrl/PoseParam.cpp)
target_include_directories(tools_panel PRIVATE .. ../src ../src/gui)
target_compile_definitions(tools_panel PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
target_link_libraries(tools_panel PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test Qt6::OpenGL
)

# The Animation dock's TREE, as a model: the rows (height-identical to the lane blocks the
# timeline draws), the selection's anchor/cursor rules, the keyboard verbs, the drop target a
# drag computes, and that every action is ONE undo step. Headless apart from the GL a
# core::LayerNode needs to exist. See object_tree_model.cpp.
add_executable(object_tree_model
    object_tree_model.cpp
    ../src/gui/qml/ObjectTreeModel.cpp
    ../src/gui/ThumbnailCache.cpp
    # a core::LayerNode compiles its mesh shader from :/shaders, which only the app's resource
    # bundle provides (the same reason asset_remove_layers links it)
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc)
target_include_directories(object_tree_model PRIVATE .. ../src ../src/gui)
target_compile_definitions(object_tree_model PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(object_tree_model PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# The Animation dock's tree SCENE: loads the real ObjectTreePanel.qml over the real model and
# drives it through QAccessible - the route a screen reader and scripts/gui/ae_ui.py take. The
# row protocol (marker "aeAccessibleRow", role Cell so AT-SPI still says "table cell") is what
# makes the tree addressable at all: a Qt Quick item's own accessible object reports itself
# invisible here. See object_tree_panel.cpp.
add_executable(object_tree_panel
    object_tree_panel.cpp
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc
    ../src/gui/qml/ObjectTreeModel.cpp
    ../src/gui/qml/QmlTheme.cpp
    ../src/gui/qml/QmlFormAccessibility.cpp
    ../src/gui/ThumbnailCache.cpp
    ../src/gui/theme/Icons.cpp)
target_include_directories(object_tree_panel PRIVATE .. ../src ../src/gui)
target_compile_definitions(object_tree_panel PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_QML_DIR="${CMAKE_SOURCE_DIR}/src/gui/qml"
    AE_ICON_SRC="${CMAKE_SOURCE_DIR}/assets/icons"
)
target_link_libraries(object_tree_panel PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets Qt6::Gui Qt6::Xml
    Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test Qt6::OpenGL
    Qt6::OpenGLWidgets Qt6::Svg Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Filter stack command grain: add creates the entry and a key as one undo step,
# remove drops the entry and its explicit keys together, and reorder/eye are
# independently undoable. See filter_stack_model.cpp.
add_executable(filter_stack_model
    filter_stack_model.cpp
    ../src/gui/prop/prop_KeyAccessor.cpp)
target_include_directories(filter_stack_model PRIVATE .. ../src)
target_compile_definitions(filter_stack_model PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(filter_stack_model PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# The shared brush model (ctrl/Brush.h): the falloff every painting target
# deposits with and the stroke sampler that turns cursor samples into spaced
# stamps. Headless. See brush_stroke.cpp.
add_executable(brush_stroke brush_stroke.cpp
    ../src/ctrl/Brush.cpp)
target_include_directories(brush_stroke PRIVATE .. ../src)
target_compile_definitions(brush_stroke PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(brush_stroke PRIVATE
    Qt6::Core Qt6::Gui
    util
)

# The Wave deform filter: the key's data contract and clamps, the five graph
# channels, playback (each channel eased, the shape step-held, a base value
# playing), the 0.18 record on disk and the pass mapping. See wave_deform.cpp.
add_executable(wave_deform wave_deform.cpp)
target_include_directories(wave_deform PRIVATE ..)
target_compile_definitions(wave_deform PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(wave_deform PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Animation-settings ViewModel session undo/redo: discrete edits record one
# step, a scrub gesture coalesces into one, and reset() clears history per open.
# See anim_settings_undo.cpp.
# The wave deform PASS on a real GL context: identity copy, the sine field, the
# two deform modes' axes, the content clamp and that every field reaches the
# shader. Needs a context and its own FBO, so run_suites.sh runs it under Xvfb.
# See wave_pass_pixels.cpp.
# assets.qrc: the pass shaders live in :/shaders, exactly as they do in the app
# (FilterFrame compiles the layer fragment shader for its HSV/present variants).
add_executable(wave_pass_pixels wave_pass_pixels.cpp
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc
)

# See layer_mask_pixels.cpp. The LAYER MASK's contract is a pixel fact (a field
# sampled in content space, multiplied into the present alpha), so it is measured on
# a real context through the SHIPPED shader variant table (core::ShaderHolder).
add_executable(layer_mask_pixels layer_mask_pixels.cpp
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc
)
target_include_directories(layer_mask_pixels PRIVATE .. ../src)
target_compile_definitions(layer_mask_pixels PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(layer_mask_pixels PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)
target_include_directories(wave_pass_pixels PRIVATE .. ../src)
target_compile_definitions(wave_pass_pixels PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(wave_pass_pixels PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# See density_meter.cpp. The density pass (core::MeshTransformer::callGL filling
# gl::FrameStats::dens / dens_max) is a MEASUREMENT, so its gate hands it deformations
# whose answer is known - undeformed, 2x in x, 4x in both - through the same mesh path an
# image layer uses (GridMesh::createFromImage), and reads the frame line back. A wrong uv
# space, a transposed matrix or a readback that never arrives fails there. It needs a real
# context (transform feedback, a geometry shader, an FBO, a PBO), so it runs under Xvfb.
# assets.qrc supplies :/shaders/MeshTransformVert.glsl, the pass's own shader.
add_executable(density_meter density_meter.cpp
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc
)
target_include_directories(density_meter PRIVATE .. ../src)
target_compile_definitions(density_meter PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(density_meter PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

add_executable(anim_settings_undo
    anim_settings_undo.cpp
    ../src/gui/qml/AnimationSettingViewModel.cpp
    ../src/gui/qml/AnimationSettingViewModel.h)
target_include_directories(anim_settings_undo PRIVATE .. ../src ../src/gui)
target_compile_definitions(anim_settings_undo PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(anim_settings_undo PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
)

# QML-module i18n: the Animation settings dialog's qsTr() strings must resolve
# through the SAME QTranslator the app installs on QCoreApplication, using the
# module's explicit pragma Translator context (AnimeEffects.Dialogs). Verifies
# the catalog content under that context AND that a QML qsTr() under the same
# pragma renders the translation at runtime. See anim_setting_i18n.cpp. Run
# from the repo root:
#   QT_QPA_PLATFORM=offscreen ./build/tests/Debug/anim_setting_i18n ja
add_executable(anim_setting_i18n anim_setting_i18n.cpp)
target_include_directories(anim_setting_i18n PRIVATE ..)
target_compile_definitions(anim_setting_i18n PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    # .qm are build artifacts (release_translations); load them from the build
    # output dir, never from the committed source data/.
    AE_I18N_QM_DIR="${PROJECT_BINARY_DIR}/src/gui"
)
target_link_libraries(anim_setting_i18n PRIVATE
    Qt6::Core Qt6::Gui
    Qt6::Qml Qt6::Quick
)
# Mirror the app: the .qm are build artifacts embedded under :/i18n, so the
# test exercises the same resource path the app loads at runtime (not just a
# raw file from the build dir).
qt_add_resources(anim_setting_i18n "translations"
    PREFIX "/i18n"
    BASE "${PROJECT_BINARY_DIR}/src/gui"
    FILES
        ${PROJECT_BINARY_DIR}/src/gui/translation_en.qm
        ${PROJECT_BINARY_DIR}/src/gui/translation_es.qm
        ${PROJECT_BINARY_DIR}/src/gui/translation_ja.qm
        ${PROJECT_BINARY_DIR}/src/gui/translation_ko.qm
        ${PROJECT_BINARY_DIR}/src/gui/translation_ru.qm
        ${PROJECT_BINARY_DIR}/src/gui/translation_zh.qm
)
add_dependencies(anim_setting_i18n release_translations)

# V0 — build/link gate for the media (libav*) rearchitecture. Links the `media`
# module directly (GL-free) and asserts the libav* libraries link + report a
# usable encoder set, and whether GPL encoders (libx264/libx265) are available.
# Headless; no QApplication. See media_link_probe.cpp.
add_executable(media_link_probe media_link_probe.cpp)
target_include_directories(media_link_probe PRIVATE ..)
target_compile_definitions(media_link_probe PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(media_link_probe PRIVATE
    Qt6::Core
    media
)

# V1 — media::VideoEncoder correctness + edge cases. Uses the `media` module
# (libav*) to encode and round-trips output back through the same linked
# decode path, asserting pixel-exact lossless fidelity + structural/metadata
# edge cases. Headless (offscreen). See media_video_encoder.cpp.
add_executable(media_video_encoder media_video_encoder.cpp)
target_include_directories(media_video_encoder PRIVATE ..)
target_compile_definitions(media_video_encoder PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(media_video_encoder PRIVATE
    Qt6::Core
    Qt6::Gui
    media
)

# V2 — media::AudioMixer multi-track correctness + edge cases. Uses the `media`
# module (libav*) to decode/mix real PCM WAV tracks, asserting the mixed output
# against the analytic sum (offsets/volumes/overlap) plus edge cases and rate
# conversion. Headless. See media_audio_mixer.cpp.
add_executable(media_audio_mixer media_audio_mixer.cpp)
target_include_directories(media_audio_mixer PRIVATE ..)
target_compile_definitions(media_audio_mixer PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(media_audio_mixer PRIVATE
    Qt6::Core
    media
)

# Export-destination table prototype: loads assets/export/ExportTargets.txt,
# validates every row against the linked libav* build, checks the size/cap
# rules, resolves the codec ladder, and encodes real files from resolved plans.
# Compiles ExportTarget.cpp directly (GL-free) so it links only media + Qt
# Core/Gui/Xml. Headless. See media_export_target.cpp.
add_executable(media_export_target
    media_export_target.cpp
    ../src/ctrl/ExportTarget.cpp
    ../src/ctrl/ExportTarget.h
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc)
target_include_directories(media_export_target PRIVATE ..)
target_compile_definitions(media_export_target PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
    AE_EXPORT_TARGETS_PATH="${CMAKE_SOURCE_DIR}/assets/export/ExportTargets.txt"
    AE_EXPORT_STRINGS_PATH="${CMAKE_SOURCE_DIR}/src/ctrl/ExportTargetStrings.h"
)
target_link_libraries(media_export_target PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Xml
    media
)
# (Warning flags come from the directory-wide ae_enable_warnings() call at the
# top of this file; this target used to set -Wall -Wextra on its own, from when
# it was the only target that had them.)

# End-to-end export harness: offscreen (GL via a QOpenGLWidget) render of a
# minimal project -> ctrl::Exporter -> a real mp4, verified with ffprobe. This
# is the end-to-end gate for the media/export rearchitecture. Headless.
add_executable(media_exporter_e2e media_exporter_e2e.cpp
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc
)
target_include_directories(media_exporter_e2e PRIVATE ..)
target_compile_definitions(media_exporter_e2e PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(media_exporter_e2e PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    media cmnd core ctrl gl img thr util pugixml
)

# Regression test for the encoder open-time fallback (src/ctrl/Exporter.cpp): a
# hardware encoder that passes the small capability probe can still be refused at
# the real resolution/option set, and the export must step down to the same family's
# software encoder instead of aborting. The test makes nvenc fail device-independently
# by handing it an option only libx264 knows, so it stays red on a box that has working
# NVENC. See export_encoder_fallback.cpp.
add_executable(export_encoder_fallback export_encoder_fallback.cpp
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc
)
target_include_directories(export_encoder_fallback PRIVATE ..)
target_compile_definitions(export_encoder_fallback PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(export_encoder_fallback PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    media cmnd core ctrl gl img thr util pugixml
)

# Regression test for the PSD layer identity defects (src/img/PSDFormat.h,
# PSDReader.cpp, Util.cpp, ResourceData, ResourceNode, core/ImageKey.cpp):
# identity was the display name, the lazy loader captured a pointer into a
# stack-owned PSDFormat, and JSON resolution took the LAST matching tree. Drives
# the real reader and loader through a crafted PSD. See psd_layer_identity.cpp.
add_executable(psd_layer_identity psd_layer_identity.cpp
    # The importer constructs core::LayerNode, whose MeshTransformer reads
    # :/shaders/MeshTransformVert.glsl (and whose shaders are then compiled), so
    # the loader cases need the embedded assets exactly like `roundtrip` above.
    # Without the resource the harness aborts on the first imported layer.
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc
)
target_include_directories(psd_layer_identity PRIVATE ..)
target_compile_definitions(psd_layer_identity PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(psd_layer_identity PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Contract test for the PSD live-link safety rules (src/gui/res/res_ResourceUpdater.cpp,
# src/gui/ResourceTreeWidget.cpp, src/core/ImageKeyUpdater.cpp, src/util/IDSolver.h):
# a relink had to be transactional (the failure paths returned out of a ScopedMacro
# whose destructor committed half a batch), forceReload compared the live tree with
# itself and then freed it, a layer missing from the source was silently kept or
# dropped, a name-colliding rename re-pointed one layer's content onto another's
# live node, and the file watcher ignored which file changed while accumulating a
# handler per watch cycle. Headless: QApplication, no GL. See psd_relink_safety.cpp.
add_executable(psd_relink_safety psd_relink_safety.cpp
    ../src/gui/ResourceTreeWidget.cpp
    ../src/gui/ProjectHook.cpp
    # ViaPoint::pushLog / pushCommandLog dispatch to the concrete log view, so this
    # target links the real one (its own ViaPoint subclass overrides pushLog, but
    # ViaPoint's body - and the vtable that names it - still needs the symbol).
    ../src/gui/CommandLogView.cpp
    ../src/gui/CommandLogView.h
    ../src/gui/ViaPoint.cpp
    ../src/gui/EasyDialog.cpp
    # The real ResourceDialog, not a stub: the test links its moc key functions,
    # and it needs no MainMenuBar at link time (only the type name in a connect).
    ../src/gui/ResourceDialog.cpp
    ../src/gui/res/res_ResourceUpdater.cpp
    ../src/gui/res/res_Item.cpp
    ../src/gui/res/res_Notifier.cpp)
target_include_directories(psd_relink_safety PRIVATE .. ../src ../src/gui)
target_compile_definitions(psd_relink_safety PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(psd_relink_safety PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Contract test for the animation graph's value model: ctrl::graph::Channel,
# ctrl::graph::Curve and ctrl::graph::Scaler (src/ctrl/graph). The sampler is
# compared against the real core::TimeKeyBlender over a real core::TimeLine and
# read out of the same core::TimeKeyExpans the renderer reads, so the graph can
# only pass by drawing what playback evaluates. Headless: no widgets, no GL, no
# .anie fixture, no extra source files (the one property that needs a GL
# context, the image-offset staircase, reports itself as a skip). See
# graph_curve.cpp.
add_executable(graph_curve graph_curve.cpp)
target_include_directories(graph_curve PRIVATE ..)
target_compile_definitions(graph_curve PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(graph_curve PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# The frame axis' zoom ladder: the two limits it used to have (1 and 16 px/frame) are gone, its
# inverse rounds to the sparsest rung that fits, and the tick stride the ruler and the panel
# iterate with never skips a drawn tick. Headless: the scaler and Qt6::Core only.
add_executable(time_scaler time_scaler.cpp)
target_include_directories(time_scaler PRIVATE ..)
target_compile_definitions(time_scaler PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
)
target_link_libraries(time_scaler PRIVATE
    Qt6::Core ctrl util
)

# Proportional keys: stable ids/state and the atomic multi-key batch move at the
# core::TimeLine layer. Headless QApplication only; no widgets, no GL.
add_executable(timekey_proportional timekey_proportional.cpp)
target_include_directories(timekey_proportional PRIVATE ..)
target_compile_definitions(timekey_proportional PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
)
target_link_libraries(timekey_proportional PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core gl img thr util pugixml
)

# Proportional keys: the stored-weight solver (bounds, rebase, drift-free
# projection) and the value reflow, over a real core::TimeLine. Headless.
add_executable(proportional_keys proportional_keys.cpp)
target_include_directories(proportional_keys PRIVATE ..)
target_compile_definitions(proportional_keys PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(proportional_keys PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# ---------------------------------------------------------------------------
# The overlay's units (the Dear ImGui renderer of the viewport panel). They exist
# because the pieces a screenshot cannot see need gates of their own: the font
# stack, the Qt->ImGuiIO translation, the accessibility bridge and the panel
# model. The first two link ae_imgui (declared in the root CMakeLists, behind
# AE_ENABLE_IMGUI); the last two deliberately do not - the accessibility bridge is
# pure QAccessible over a plain model, and the panel model is geometry. All four
# are headless: offscreen, no GL context, no window.
#
# scripts/run_suites.sh discovers targets from this file, so registering one here
# is all that puts it on the gate.
# ---------------------------------------------------------------------------
if(AE_ENABLE_IMGUI)
    # The font loader: ImGui rasterizes through Qt (QRawFont), which is what makes
    # CJK, .ttc, CFF and variable fonts work. Builds an atlas and reads glyph
    # pixels - no GL. See src/gui/ImGuiQtFontLoader.h.
    add_executable(imgui_qt_font_loader imgui_qt_font_loader.cpp
        ../src/gui/ImGuiQtFontLoader.cpp)
    target_include_directories(imgui_qt_font_loader PRIVATE .. ../src)
    target_compile_definitions(imgui_qt_font_loader PRIVATE
        AE_MAJOR_VERSION=1
        AE_MINOR_VERSION=7
        AE_MICRO_VERSION=1
        USE_GL_CORE_PROFILE
    )
    target_link_libraries(imgui_qt_font_loader PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets
        ae_imgui
    )

    # The input bridge: mouse/keys/wheel, IME (commit and pre-edit), tablet, touch,
    # DPR, and the WantCapture consumption policy the application's key routing has
    # to obey. No GL, no window.
    add_executable(imgui_input_bridge imgui_input_bridge.cpp
        ../src/gui/ImGuiInputBridge.cpp)
    target_include_directories(imgui_input_bridge PRIVATE .. ../src)
    target_compile_definitions(imgui_input_bridge PRIVATE
        AE_MAJOR_VERSION=1
        AE_MINOR_VERSION=7
        AE_MICRO_VERSION=1
        USE_GL_CORE_PROFILE
    )
    target_link_libraries(imgui_input_bridge PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets
        ae_imgui
    )

    # The panel's PIXEL contract, and the replacement for graph_render_pixels (which
    # pins the painter renderer the cutover deletes). It drives gui::drawPanel through
    # a real ImGui frame into its own offscreen framebuffer and asserts the pixels the
    # model's geometry promises - including the INVERSE assertion that a plot area
    # holding no geometry is left untouched, which is what "no background, no grid"
    # means in pixels. Needs a real GL context, so it runs under xvfb (the GL branch
    # in scripts/run_suites.sh).
    #
    # There is no `gui` library to link against - the application target compiles
    # these files itself - so the overlay's own units come along as sources.
    # ImGuiSpikeGL.cpp must stay the first ImGui translation unit: its loader shim
    # precedes the backend.
    add_executable(graph_panel_pixels graph_panel_pixels.cpp
        ../src/gui/ImGuiGLBackend.cpp
        ../src/gui/GraphPanelFrame.cpp
        ../src/gui/GraphPanelModel.cpp
        ../src/gui/InkImGui.cpp
        ../src/gui/InkQPainter.cpp
        ../src/gui/FrameRuler.cpp
        ../src/gui/ImGuiInputBridge.cpp
        ../src/gui/ImGuiA11y.cpp
        ../src/gui/ImGuiQtFontLoader.cpp
        ../src/gui/theme/Graph.cpp
        ../src/gui/theme/TimeLine.cpp)
    target_include_directories(graph_panel_pixels PRIVATE .. ../src)
    target_compile_definitions(graph_panel_pixels PRIVATE
        AE_MAJOR_VERSION=1
        AE_MINOR_VERSION=7
        AE_MICRO_VERSION=1
        USE_GL_CORE_PROFILE
        AE_ENABLE_IMGUI=1
    )
    target_link_libraries(graph_panel_pixels PRIVATE
        Qt6::Core Qt6::Core5Compat Qt6::Widgets
        Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
        Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
        cmnd core ctrl gl img thr util pugixml
        ae_imgui
    )
endif()

# The accessibility bridge for the overlay's framebuffer-drawn content. No ImGui
# dependency at all: it publishes a plain item model through QAccessible, and the
# live AT-SPI dump (scripts/gui/probe_graph_overlay.sh) is what finally checks it
# against a screen reader's view. See ImGuiA11y.h.
add_executable(imgui_a11y imgui_a11y.cpp
    ../src/gui/ImGuiA11y.cpp)
target_include_directories(imgui_a11y PRIVATE .. ../src)
target_compile_definitions(imgui_a11y PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(imgui_a11y PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets
)

# The timeline's pixel contract for the row tint, the key fill/ring, the lane
# label's ink and the pill's column. Drives the real ctrl::TimeLineEditor::render
# into an offscreen QImage - no GL, no window - and reads its geometry off the
# editor's own public mapping, so a change to the scale cannot silently move it.
# Compiles theme::TimeLine directly because ctrl's renderer names that class and
# leaves its symbols unresolved (the same reason graph_panel_model compiles
# theme::Graph). See timeline_visuals.cpp.

# THE FRAME RULER'S CONTRACT: the one model both rulers read (src/gui/FrameRuler.h).
# It drives the real ctrl::TimeLineEditor through the shared builder and painter into an
# offscreen QImage, and pins the tick tiers, the boundary/merged labels, the one-frame
# range publishing none, the playhead's column, the playback window's geometry and the
# two signed-off rules (clip-never-shift, and the project range as the formatter's
# basis). No GL, no widget. See frame_ruler.cpp.
add_executable(frame_ruler frame_ruler.cpp
    ../src/gui/FrameRuler.cpp
    ../src/gui/theme/TimeLine.cpp)
target_include_directories(frame_ruler PRIVATE .. ../src)
target_compile_definitions(frame_ruler PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(frame_ruler PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

add_executable(timeline_visuals timeline_visuals.cpp
    ../src/gui/theme/TimeLine.cpp)
target_include_directories(timeline_visuals PRIVATE .. ../src)
target_compile_definitions(timeline_visuals PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(timeline_visuals PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# The panel model: what the overlay draws, as engine-neutral geometry computed from
# ctrl::GraphEditor - and, crucially, asserted CONSISTENT with the editor's own
# panelRect/scale, because a model that computed its own rects would silently
# desync the pixels from the hit-test. Compiles theme::Graph for the same reason
# graph_drag does. See graph_panel_model.cpp.
add_executable(graph_panel_model graph_panel_model.cpp
    ../src/gui/GraphPanelModel.cpp
    ../src/gui/FrameRuler.cpp
    ../src/gui/theme/Graph.cpp
    # The model now reaches the timeline renderer's theme through the shared easing
    # rewrite path; theme::TimeLine is a leaf, so it is linked rather than stubbed.
    ../src/gui/theme/TimeLine.cpp
    # Section 17 of the test paints the APPLICATION'S scroll bar: gui::AppStyle's
    # CC_ScrollBar into a transparent QImage, so the pill the style draws is measured
    # against the same ctrl::graph::scrollBar::pill the panel's lanes draw from.
    # AppStyle.cpp names theme::iconDir() for the tree branch caret, hence Icons.cpp.
    ../src/gui/AppStyle.cpp
    ../src/gui/theme/Icons.cpp)
target_include_directories(graph_panel_model PRIVATE .. ../src/gui)
target_compile_definitions(graph_panel_model PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(graph_panel_model PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Contract test for the animation graph's EDIT path (ctrl::GraphEditor): one
# handle drag must be exactly ONE undo step, and it must edit the grabbed key's
# own util::Easing::Param. Drives the real editor with synthetic
# core::AbstractCursor frames and proves the command count by undo/redo rather
# than by inspection. Headless: offscreen, no GL, no .anie. Compiles the real
# theme::Graph because GraphEditor names it and the ctrl library leaves that
# symbol unresolved - the same reason the timeline tests compile
# theme/TimeLine.cpp. gui/CanvasMover.cpp is here for the wheel's DIRECTION:
# the panel's notch must move the same way as the canvas', and comparing against
# the real mover is the only way that stays true (see graph_drag's section 0b-ii;
# ../src is on the include path for the gui/ header, which `ctrl` does not export).
add_executable(graph_drag graph_drag.cpp
    ../src/gui/theme/Graph.cpp
    ../src/gui/CanvasMover.cpp)
target_include_directories(graph_drag PRIVATE .. ../src)
target_compile_definitions(graph_drag PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(graph_drag PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# The panel's pixels used to be pinned here, by driving the raster QPainter
# renderer (ctrl::graph::Renderer) into a QImage. That renderer is gone: the panel
# is drawn by gui::drawPanel (Dear ImGui), and its pixel contract lives in
# tests/graph_panel_pixels.cpp, which drives the real drawing through a GL frame and
# asserts the same primitives - plus the inverse assertion that an area with no
# geometry in it is untouched, which no test of the painter path could make.

# The image-offset channel's step-hold, in the one environment where it can be
# observed: constructing a core::ImageKey binds a GL buffer for its GridMesh, so
# tests/graph_curve.cpp (headless, offscreen) has to report that section as a
# skip. This test creates a context and asserts the staircase for real - the
# offset holds the last preceding key's value across the whole span, with no
# trace of the Custom easing the first key carries. Runs under xvfb.
add_executable(graph_image_step graph_image_step.cpp)
target_include_directories(graph_image_step PRIVATE ..)
target_compile_definitions(graph_image_step PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(graph_image_step PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# GUARD test for the paint engine that draws every viewport overlay
# (ctrl::PainterHandle -> GLCorePaintEngine in a USE_GL_CORE_PROFILE build). It
# drives the real ctrl::PainterHandle into its OWN offscreen framebuffer and
# counts pixels per probe, so the engine's ACTUAL primitive subset is pinned
# rather than assumed: the primitives the gizmos and the graph panel rely on must
# paint, and two silent traps must keep behaving as documented - a QPainterPath
# paints nothing (drawPath is `{ (void)aPath; }`), and a text draw with no brush
# scheduled paints nothing (pushDrawCommand drops usePen=false draws unless a
# brush is scheduled). Both cost real defects on 2026-10-12; see the measured
# notes in AGENTS.md. It reads its own FBO with glReadPixels, never
# QOpenGLWidget::grabFramebuffer() (which re-enters Qt's render and invalidates
# the buffer). Needs a real GL context, so it runs under xvfb (see the GL branch
# in scripts/run_suites.sh). assets.qrc is embedded for the engine's shaders.
add_executable(overlay_engine_primitives overlay_engine_primitives.cpp
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc)
target_include_directories(overlay_engine_primitives PRIVATE ..)
target_compile_definitions(overlay_engine_primitives PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(overlay_engine_primitives PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# Regression test for removing an asset that layers still use
# (core::ObjectTree::findNodesUsingResource, ctrl::tree::appendDeleteNodes,
# gui::res::ResourceUpdater::remove): the removal used to be refused outright when
# any layer rendered from the asset. It now deletes those layers in the same undo
# step, so the search must walk the asset's whole subtree, the resource must be
# released before its tree is removed, and ONE undo must restore both the asset and
# its layers IN ORDER. Builds the real object tree and asset tree and drives the
# real removal; needs a GL context for core::LayerNode's default ImageKey, so it
# runs under xvfb (cf. psd_layer_identity). See asset_remove_layers.cpp.
#
# It builds no tree VIEW at all any more: the layer deletions are DOCUMENT commands
# (ctrl/TreeActions.h), and the QML tree derives its rows from the document, so there is
# no view state to keep in step - which is why the host source is not in this list.
add_executable(asset_remove_layers asset_remove_layers.cpp
    ../src/gui/GUIResources.cpp
    ../src/gui/AppStyle.cpp
    ../src/gui/ProjectHook.cpp
    # ViaPoint's log dispatch goes through the concrete view (see psd_relink_safety).
    ../src/gui/CommandLogView.cpp
    ../src/gui/CommandLogView.h
    ../src/gui/ViaPoint.cpp
    ../src/gui/EasyDialog.cpp
    ../src/gui/ResourceDialog.cpp
    ../src/gui/ResourceTreeWidget.cpp
    ../src/gui/res/res_ResourceUpdater.cpp
    ../src/gui/res/res_Item.cpp
    ../src/gui/res/res_Notifier.cpp
    ../src/gui/theme/Theme.cpp
    ../src/gui/theme/Icons.cpp
    ../src/gui/theme/TimeLine.cpp
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc)
target_include_directories(asset_remove_layers PRIVATE .. ../src ../src/gui)
target_compile_definitions(asset_remove_layers PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(asset_remove_layers PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# THE CANVAS PAINTER RULER'S PARITY GATE: one model painted by QPainter and by Canvas
# Painter, compared pixel by pixel (text coverage is reported separately - two glyph
# rasterizers never agree). QA: only built when the Qt in use provides the module AND is
# 6.12+, which the default Fedora 6.11.2 toolchain is not. See frame_ruler_canvas.cpp.
add_executable(frame_ruler_canvas frame_ruler_canvas.cpp
    ../src/gui/FrameRuler.cpp
    ../src/gui/FrameRulerCanvas.cpp
    ../src/gui/theme/TimeLine.cpp)
target_include_directories(frame_ruler_canvas PRIVATE .. ../src)
target_compile_definitions(frame_ruler_canvas PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(frame_ruler_canvas PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    Qt6::CanvasPainter
    cmnd core ctrl gl img thr util pugixml
)

# THE CANVAS PAINTER BODY'S PARITY GATE (Qt >= 6.12 + AE_CANVAS_PAINTER only).
# NOT PASSING YET: 268 of 76,800 px differ in the "geometry" class - see AGENTS.md ("the body's
# Canvas port is SHORT"). Registered so the remaining gap has a number to close.
add_executable(time_body_canvas time_body_canvas.cpp
    ../src/gui/TimeBodyCanvas.cpp
    ../src/gui/InkCanvas.cpp
    ../src/gui/theme/TimeLine.cpp)
target_include_directories(time_body_canvas PRIVATE .. ../src)
target_compile_definitions(time_body_canvas PRIVATE
    AE_MAJOR_VERSION=1 AE_MINOR_VERSION=7 AE_MICRO_VERSION=1 USE_GL_CORE_PROFILE
    AE_CANVAS_PAINTER=1)
target_link_libraries(time_body_canvas PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia Qt6::CanvasPainter
    cmnd core ctrl gl img thr util pugixml)

# THE GRAPH PANEL'S BACKEND PARITY (Qt >= 6.12 + AE_CANVAS_PAINTER): the pixel gate above
# renders the SAME models through InkQPainter (Qt's own rasteriser) and InkCanvas (Canvas
# Painter) and compares them. It lives inside graph_panel_pixels because that target already
# carries the whole panel - the ink decisions, the fixtures and the ImGui reference.
target_sources(graph_panel_pixels PRIVATE
    ../src/gui/InkCanvas.cpp)
target_compile_definitions(graph_panel_pixels PRIVATE AE_CANVAS_PAINTER=1)
target_link_libraries(graph_panel_pixels PRIVATE Qt6::CanvasPainter)

# THE COMMAND LOG'S CONTRACT (src/gui/CommandLogView.{h,cpp}): badges instead of the
# old "Undone : X" strings, and - the load-bearing half - a selection that still
# crosses entries, so the clipboard gets the whole log as plain text. Compiles the
# real view and the real theme tokens; headless, no GL. See command_log.cpp.
add_executable(command_log command_log.cpp
    ../src/gui/CommandLogView.cpp
    ../src/gui/CommandLogView.h)
target_include_directories(command_log PRIVATE .. ../src)
target_link_libraries(command_log PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

# MIPMAP VERIFICATION GATE (tools/verify_mipmap): the layer texture's mip chain against an analytic
# CPU reference, and the production render path that consumes it. Four suites: M1 (the chain is the
# premultiplied box, with the straight chain as the positive control), M2 (the production ImageKey
# texture is the CONTENT's own size and its levels are the area average of the premultiplied content),
# M18 (every level, every size, odd sides included, plus the raw driver chain as the mutation control)
# and M3 (a minified layer renders source*coverage, and a 1-texel checkerboard at 1:1 comes back at
# full contrast, which is the texel-phase/UV check).
#
# It is a registered target rather than a hand-rolled script because tools/verify_blur rotted into not
# compiling at all when nothing kept building it. Needs a real GL context, so it runs under xvfb (the
# GL branch in scripts/run_suites.sh) or with QT_QPA_PLATFORM=offscreen; exit code 0 = pass, 1 = test
# failures, 2 = GL bootstrap failure. assets.qrc is embedded because core::LayerNode compiles its
# shaders from :/shaders.
add_executable(verify_mipmap
    ../tools/verify_mipmap/mip_probe.cpp
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc
)
target_include_directories(verify_mipmap PRIVATE ..)
target_compile_definitions(verify_mipmap PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(verify_mipmap PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# MIPMAP POLICY RESEARCH HARNESS (tools/mip_compare): how far each alternative to the reach-back
# rule in gl::Texture::mipSourceLevel drifts from the exact area average of the premultiplied
# content. It is registered here for the same reason verify_mipmap is - a tool nothing compiles
# rots - but it is NOT a gate: it needs a sample image and an output directory, and it writes a
# PNG per level per policy. scripts/run_suites.sh therefore SKIPs it; run it by hand:
#   ./build/tests/Debug/mip_compare art.png /tmp/mip-out
# No GL context is needed (the reference is CPU arithmetic), so it runs headless as-is.
add_executable(mip_compare
    ../tools/mip_compare/mip_compare.cpp
)
target_include_directories(mip_compare PRIVATE ..)
target_compile_definitions(mip_compare PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
)
# img (PSDReader/PSDUtil for the flattened PSD composite) is the only project library needed; it
# brings util, pugixml and the rest of its Qt interface along.
target_link_libraries(mip_compare PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets
    img util pugixml
)

# DEVELOPMENT RENDER HARNESS (tools/render_probe): renders selected frames of a real .anie
# offscreen through the viewport's own path (ctrl::Driver::renderGL plus the production
# ClippingFrame/FilterFrame/DestinationTexturizer triple) and writes one PNG per frame. It exists
# because live playback and AE_FRAMEBUFFER_DUMP cannot give a deterministic frame on this project:
# a software-GL run drops animation frames, so motion artifacts vanish.
#
# It is registered here for the same reason verify_mipmap and mip_compare are - a tool nothing
# compiles rots - but it is NOT a gate: it needs a caller-supplied project and an output directory,
# and with no arguments it only prints usage and exits 2. scripts/run_suites.sh therefore SKIPs it;
# run it by hand:
#   QT_QPA_PLATFORM=offscreen ./build/tests/Debug/render_probe \
#       --project /tmp/ae-mid.anie --out /tmp/render-probe --size 603x679 --frames 0..0 --metrics
# assets.qrc is embedded because core::LayerNode compiles its shaders from :/shaders.
add_executable(render_probe
    ../tools/render_probe/render_probe.cpp
    ../src/gui/theme/TimeLine.cpp   # ctrl's renderer links against theme::TimeLine
    ${CMAKE_SOURCE_DIR}/assets/assets.qrc
)
target_include_directories(render_probe PRIVATE ..)
target_compile_definitions(render_probe PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
    USE_GL_CORE_PROFILE
)
target_link_libraries(render_probe PRIVATE
    Qt6::Core Qt6::Core5Compat Qt6::Widgets
    Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
    Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
    cmnd core ctrl gl img thr util pugixml
)

# THE DOCK SLOT MODEL AND THE DROP RULE: one derivation of "what is an area" (DockSlots)
# and the rule the drop preview and the drop itself share (DockDropOverlay). A real
# QMainWindow with real DockPanels, offscreen: slots, hidden-vs-empty, a tab group as
# one slot, the empty-area place, and the target at a point. See dock_slots.cpp.
add_executable(dock_slots
    dock_slots.cpp
    ../src/gui/DockDropOverlay.cpp ../src/gui/DockDropOverlay.h
    ../src/gui/DockMetrics.h
    ../src/gui/DockPanel.cpp ../src/gui/DockPanel.h
    ../src/gui/DockSlots.cpp ../src/gui/DockSlots.h
    ../src/gui/DockTabBar.cpp ../src/gui/DockTabBar.h
)
target_include_directories(dock_slots PRIVATE .. ../src)
target_compile_definitions(dock_slots PRIVATE
    AE_MAJOR_VERSION=1
    AE_MINOR_VERSION=7
    AE_MICRO_VERSION=1
)
target_link_libraries(dock_slots PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets
)
