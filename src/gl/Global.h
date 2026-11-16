#ifndef GL_GLOBAL_H
#define GL_GLOBAL_H

#include <QOpenGLFunctions_4_0_Core>
#include <QOpenGLContext>
#include <QtOpenGLWidgets/QtOpenGLWidgets>

namespace gl {

class Global {
public:
    typedef QOpenGLFunctions_4_0_Core Functions;
    static const QPair<int, int> kVersion;

    static void setFunctions(Functions& aFunctions);
    static void clearFunctions();
    static Functions& functions();

    /// The context to make current, in either of two shapes: a WIDGET (the app's viewport) or a
    /// CONTEXT plus a SURFACE (an offscreen surface in a tool or a test - no window, no display
    /// server). Exactly one of them is registered.
    static void setContext(QOpenGLContext& aContext, QSurface& aSurface);
    static void setContext(QOpenGLWidget& aWidget);
    static void clearContext();
    static void makeCurrent();
    static void doneCurrent();
    // makes the registered context current when it is usable - the widget's when a widget is
    // registered (and it is visible), the offscreen context's otherwise - and returns false when
    // none is, so callers can harden non-paint GL work.
    static bool makeCurrentIfReady();

private:
    Global() {}
};

} // namespace gl


#include "XCAssert.h"

#define GL_CHECK_ERROR() \
    do { \
        GLuint e = gl::Global::functions().glGetError(); \
        if (e != GL_NO_ERROR) { \
            XC_DEBUG_REPORT("OpenGL Error (code:%u)", e); \
            Q_ASSERT(0); \
        } \
    } while (0)


#endif // GL_GLOBAL_H
