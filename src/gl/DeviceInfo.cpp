#include <cstring>
#include "XC.h"
#include "gl/DeviceInfo.h"
#include "gl/Global.h"

namespace {
static const gl::DeviceInfo* sDeviceInfoPtr;

// EXT_texture_filter_anisotropic's own enums. Qt includes the system GL headers with
// GL_GLEXT_LEGACY (so GL/gl.h does not pull in GL/glext.h) and supplies its own subset through
// qopenglext.h, which defines these today - but the set differs between Qt builds and platforms,
// so they are guarded rather than assumed.
#ifndef GL_NUM_EXTENSIONS
#define GL_NUM_EXTENSIONS 0x821D
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

// Is a named extension in the current context's list? glGetString(GL_EXTENSIONS) is not valid in a
// core profile, so the indexed form is the only way to ask.
bool hasExtension(const char* aName) {
    auto& ggl = gl::Global::functions();
    GLint count = 0;
    ggl.glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
        const char* name = (const char*)ggl.glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if (name && std::strcmp(name, aName) == 0)
            return true;
    }
    return false;
}
} // namespace

namespace gl {

const DeviceInfo& DeviceInfo::instance() {
    XC_PTR_ASSERT(sDeviceInfoPtr);
    return *sDeviceInfoPtr;
}

void DeviceInfo::setInstance(const DeviceInfo* aInstance) { sDeviceInfoPtr = aInstance; }

bool DeviceInfo::validInstanceExists() { return sDeviceInfoPtr && sDeviceInfoPtr->isValid(); }

DeviceInfo::DeviceInfo():
    vender(), renderer(), version(), maxTextureSize(0), maxRenderBufferSize(0), maxAnisotropy(1.0f) {}

GLfloat DeviceInfo::queryMaxAnisotropy() {
    // Both names: a 4.6 core context promotes it to ARB and drivers differ in which of the two they
    // list. Without the extension the limit query would set GL_INVALID_ENUM, and this app treats a GL
    // error as a hard failure, so it is not attempted.
    if (!hasExtension("GL_EXT_texture_filter_anisotropic") &&
        !hasExtension("GL_ARB_texture_filter_anisotropic")) {
        return 1.0f;
    }

    auto& ggl = Global::functions();
    GLfloat value = 1.0f;
    ggl.glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &value);
    if (ggl.glGetError() != GL_NO_ERROR)   // consumed here rather than left for GL_CHECK_ERROR
        return 1.0f;

    return (value > 1.0f) ? value : 1.0f;
}

void DeviceInfo::load() {
    Global::makeCurrent();
    auto& ggl = Global::functions();

    vender = std::string((const char*)ggl.glGetString(GL_VENDOR));
    renderer = std::string((const char*)ggl.glGetString(GL_RENDERER));
    version = std::string((const char*)ggl.glGetString(GL_VERSION));

    ggl.glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    ggl.glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxRenderBufferSize);
    maxAnisotropy = queryMaxAnisotropy();

#if 0
    GLint val = 0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &val);
    qDebug() << "max vertex attribs" << val;
    glGetIntegerv(GL_MAX_VARYING_VECTORS, &val);
    qDebug() << "max varying vectors" << val;
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &val);
    qDebug() << "max vertex uniform vectors" << val;
    glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &val);
    qDebug() << "max fragment uniform vectors" << val;
    glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &val);
    qDebug() << "max vertex texture image units" << val;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &val);
    qDebug() << "max texture image units" << val;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &val);
    qDebug() << "max texture size" << val;
    glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &val);
    qDebug() << "max cube map texture size" << val;
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &val);
    qDebug() << "max renderbuffer size" << val;
    GLint dims[2] = {};
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, dims);
    qDebug() << "max viewport dims" << dims[0] << dims[1];

    //glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &maxAttach);
    //glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxAttach);
#endif
}

bool DeviceInfo::isValid() const { return maxTextureSize > 0 && maxRenderBufferSize > 0; }

} // namespace gl
