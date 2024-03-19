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

    // static void setContext(QOpenGLContext& aContext, QSurface& aSurface);
    static void setContext(QOpenGLWidget& aWidget);
    static void clearContext();
    static void makeCurrent();
    static void doneCurrent();

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
