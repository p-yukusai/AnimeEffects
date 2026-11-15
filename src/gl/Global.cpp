#include "gl/Global.h"
#include "XC.h"

namespace {
gl::Global::Functions* gGLGlobalFunctions = nullptr;
QOpenGLContext* gGLGlobalContext = nullptr;
QSurface* gGLGlobalSurface = nullptr;
QOpenGLWidget* gGLGlobalWidget = nullptr;
} // namespace

namespace gl {

void Global::setFunctions(Functions& aFunctions) {
    XC_ASSERT(!gGLGlobalFunctions);
    gGLGlobalFunctions = &aFunctions;
}

void Global::clearFunctions() { gGLGlobalFunctions = nullptr; }

Global::Functions& Global::functions() {
    XC_PTR_ASSERT(gGLGlobalFunctions);
    return *gGLGlobalFunctions;
}

void Global::setContext(QOpenGLContext& aContext, QSurface& aSurface) {
    XC_ASSERT(!gGLGlobalContext && !gGLGlobalWidget);
    gGLGlobalContext = &aContext;
    gGLGlobalSurface = &aSurface;
}

void Global::setContext(QOpenGLWidget& aWidget) {
    XC_ASSERT(!gGLGlobalWidget);
    gGLGlobalWidget = &aWidget;
}

void Global::clearContext() {
    gGLGlobalContext = nullptr;
    gGLGlobalSurface = nullptr;
    gGLGlobalWidget = nullptr;
}

void Global::makeCurrent() {
    if (gGLGlobalContext) {
        XC_PTR_ASSERT(gGLGlobalSurface);
        gGLGlobalContext->makeCurrent(gGLGlobalSurface);
        return;
    }
    XC_PTR_ASSERT(gGLGlobalWidget);
    gGLGlobalWidget->makeCurrent();
}

bool Global::makeCurrentIfReady() {
    if (gGLGlobalContext)
        return true; // an offscreen context is always ready
    if (!gGLGlobalWidget || !gGLGlobalWidget->isVisible())
        return false;
    gGLGlobalWidget->makeCurrent();
    return true;
}

void Global::doneCurrent() {
    if (gGLGlobalContext) {
        gGLGlobalContext->doneCurrent();
        return;
    }
    XC_PTR_ASSERT(gGLGlobalWidget);
    gGLGlobalWidget->doneCurrent();
}

} // namespace gl

const QPair<int, int> gl::Global::kVersion{4, 0};
