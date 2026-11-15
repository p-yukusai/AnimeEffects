#ifndef GUI_IMGUIGLBACKEND_H
#define GUI_IMGUIGLBACKEND_H

// The custom OpenGL loader for Dear ImGui's GL3 backend (the application's own
// ImGui GL shim, not the throwaway spike it started as).
//
// Passed to that backend as IMGUI_IMPL_OPENGL_LOADER_CUSTOM, which makes it
// include this file instead of its bundled loader (backends/imgui_impl_opengl3.cpp,
// the `#elif !defined(IMGUI_IMPL_OPENGL_LOADER_CUSTOM)` branch). The bundled
// loader is a stripped gl3w: it carries its own dlopen/dlsym table and would be
// a SECOND function loader in a process whose GL entry points are already
// resolved by Qt's QOpenGLFunctions_4_0_Core - two independent tables that can
// disagree, for no benefit.
//
// So every entry point the backend names is mapped onto the application's own
// function table, `gl::Global::functions()` (that is the QOpenGLFunctions_4_0_Core
// instance gui::MainDisplayWidget installs in initializeGL). The backend then
// calls the same pointers the rest of the renderer calls.
//
// This file must be included BEFORE any other GL header in the backend's
// translation unit (it is: the backend includes it where it would include the
// bundled one), and only in that translation unit: it renames global GL
// identifiers, so a header included afterwards that mentions them would see the
// macro. imgui_impl_opengl3.cpp includes nothing but standard C headers after
// it.

#include "gl/Global.h"

// Object-like macros, one per name the backend actually calls (verified against
// imgui_impl_opengl3.cpp; the ES-only *OES names are never reached on desktop
// GL, and glClipControl appears in comments only).
#define glActiveTexture ::gl::Global::functions().glActiveTexture
#define glAttachShader ::gl::Global::functions().glAttachShader
#define glBindBuffer ::gl::Global::functions().glBindBuffer
#define glBindSampler ::gl::Global::functions().glBindSampler
#define glBindTexture ::gl::Global::functions().glBindTexture
#define glBindVertexArray ::gl::Global::functions().glBindVertexArray
#define glBlendEquation ::gl::Global::functions().glBlendEquation
#define glBlendEquationSeparate ::gl::Global::functions().glBlendEquationSeparate
#define glBlendFuncSeparate ::gl::Global::functions().glBlendFuncSeparate
#define glBufferData ::gl::Global::functions().glBufferData
#define glBufferSubData ::gl::Global::functions().glBufferSubData
#define glCompileShader ::gl::Global::functions().glCompileShader
#define glCreateProgram ::gl::Global::functions().glCreateProgram
#define glCreateShader ::gl::Global::functions().glCreateShader
#define glDeleteBuffers ::gl::Global::functions().glDeleteBuffers
#define glDeleteProgram ::gl::Global::functions().glDeleteProgram
#define glDeleteShader ::gl::Global::functions().glDeleteShader
#define glDeleteTextures ::gl::Global::functions().glDeleteTextures
#define glDeleteVertexArrays ::gl::Global::functions().glDeleteVertexArrays
#define glDetachShader ::gl::Global::functions().glDetachShader
#define glDisable ::gl::Global::functions().glDisable
#define glDisableVertexAttribArray ::gl::Global::functions().glDisableVertexAttribArray
#define glDrawElements ::gl::Global::functions().glDrawElements
#define glDrawElementsBaseVertex ::gl::Global::functions().glDrawElementsBaseVertex
#define glEnable ::gl::Global::functions().glEnable
#define glEnableVertexAttribArray ::gl::Global::functions().glEnableVertexAttribArray
#define glGenBuffers ::gl::Global::functions().glGenBuffers
#define glGenTextures ::gl::Global::functions().glGenTextures
#define glGenVertexArrays ::gl::Global::functions().glGenVertexArrays
#define glGetAttribLocation ::gl::Global::functions().glGetAttribLocation
#define glGetError ::gl::Global::functions().glGetError
#define glGetIntegerv ::gl::Global::functions().glGetIntegerv
#define glGetProgramInfoLog ::gl::Global::functions().glGetProgramInfoLog
#define glGetProgramiv ::gl::Global::functions().glGetProgramiv
#define glGetShaderInfoLog ::gl::Global::functions().glGetShaderInfoLog
#define glGetShaderiv ::gl::Global::functions().glGetShaderiv
#define glGetString ::gl::Global::functions().glGetString
#define glGetStringi ::gl::Global::functions().glGetStringi
#define glGetUniformLocation ::gl::Global::functions().glGetUniformLocation
#define glGetVertexAttribiv ::gl::Global::functions().glGetVertexAttribiv
#define glGetVertexAttribPointerv ::gl::Global::functions().glGetVertexAttribPointerv
#define glIsEnabled ::gl::Global::functions().glIsEnabled
#define glIsProgram ::gl::Global::functions().glIsProgram
#define glLinkProgram ::gl::Global::functions().glLinkProgram
#define glPixelStorei ::gl::Global::functions().glPixelStorei
#define glPolygonMode ::gl::Global::functions().glPolygonMode
#define glScissor ::gl::Global::functions().glScissor
#define glShaderSource ::gl::Global::functions().glShaderSource
#define glTexImage2D ::gl::Global::functions().glTexImage2D
#define glTexParameteri ::gl::Global::functions().glTexParameteri
#define glTexSubImage2D ::gl::Global::functions().glTexSubImage2D
#define glUniform1i ::gl::Global::functions().glUniform1i
#define glUniformMatrix4fv ::gl::Global::functions().glUniformMatrix4fv
#define glUseProgram ::gl::Global::functions().glUseProgram
#define glVertexAttribPointer ::gl::Global::functions().glVertexAttribPointer
#define glViewport ::gl::Global::functions().glViewport

#endif // GUI_IMGUIGLBACKEND_H
