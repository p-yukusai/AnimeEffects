#ifndef GL_DEVICEINFO_H
#define GL_DEVICEINFO_H

#include <string>
#include <QtOpenGL/QtOpenGL>

namespace gl {

struct DeviceInfo {
    static void setInstance(const DeviceInfo* aInstance);
    static bool validInstanceExists();
    static const DeviceInfo& instance();

    DeviceInfo();
    void load();
    bool isValid() const;

    std::string vender;
    std::string renderer;
    std::string version;
    GLint maxTextureSize;
    GLint maxRenderBufferSize;

    /// The largest anisotropy the device will apply, from EXT_texture_filter_anisotropic (or its ARB
    /// promotion, which is where it lives in a 4.6 core context). 1 means the device cannot filter
    /// anisotropically at all, and gl::Texture::setMaxAnisotropy then leaves the sampler alone
    /// instead of setting an enum the driver may not know.
    GLfloat maxAnisotropy;

    /// Reads the anisotropy limit off the CURRENT context. Static because a harness that stubs the
    /// rest of the device still wants the real limit: a stubbed limit would hide a device that
    /// cannot do this at all, which is exactly what the policy has to cope with.
    static GLfloat queryMaxAnisotropy();
};

} // namespace gl

#endif // GL_DEVICEINFO_H
