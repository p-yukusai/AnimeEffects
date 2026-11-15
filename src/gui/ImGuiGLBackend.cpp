// Compiles Dear ImGui's GL3 renderer backend (out-of-tree, fetched by CMake)
// inside THIS translation unit, after the custom GL loader header.
//
// WHY A .cpp INCLUDING A .cpp. imgui_impl_opengl3.cpp resolves its GL entry
// points from whatever header is visible when it is preprocessed. Up to 1.91 it
// did that itself - `#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM` plus
// `#include IMGUI_IMPL_OPENGL_LOADER_CUSTOM` - but 1.92 dropped the include and
// kept only the negative test (`#elif !defined(IMGUI_IMPL_OPENGL_LOADER_CUSTOM)`,
// i.e. "do not include our bundled gl3w"). So the custom header has to be
// included ahead of it, and this wrapper is the portable way to do that: no
// `-include`/`/FI` compiler flag, no edit of the fetched sources.
//
// The header it includes (gui/ImGuiGLBackend.h) maps every entry point the
// backend names onto gl::Global::functions() - the application's own
// QOpenGLFunctions_4_0_Core instance - so the overlay runs on exactly the
// function table the rest of the renderer uses, with no second loader in the
// process. This is the application's ImGui GL loader shim: it started life as
// the spike's, and the spike itself is gone.

#include "gui/ImGuiGLBackend.h"

#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM

#include "backends/imgui_impl_opengl3.cpp"
