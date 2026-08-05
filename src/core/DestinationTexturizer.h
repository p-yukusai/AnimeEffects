#ifndef CORE_DESTINATIONTEXTURIZER_H
#define CORE_DESTINATIONTEXTURIZER_H

#include "gl/Framebuffer.h"
#include "gl/Texture.h"
#include "gl/EasyShaderProgram.h"
#include "core/RenderInfo.h"
#include "core/LayerMesh.h"

namespace core {

class DestinationTexturizer {
public:
    DestinationTexturizer();

    void resize(const QSize& aSize);

    void clearTexture();

    void update(
        GLuint aFramebuffer,
        GLuint aFrameTexture,
        const QMatrix4x4& aViewMatrix,
        LayerMesh& aMesh,
        gl::BufferObject& aPositions
    );

    // copies the whole bound framebuffer (screen-aligned quad, identity view) instead of a
    // mesh region; used as the destination source for composite presentation passes whose
    // blend must sample the scene behind a full-screen composite quad
    void updateAll(GLuint aFramebuffer, GLuint aFrameTexture);

    gl::Texture& texture() { return *mTexture; }
    const gl::Texture& texture() const { return *mTexture; }

private:
    void createShader();

    QScopedPointer<gl::Framebuffer> mFramebuffer;
    QScopedPointer<gl::Texture> mTexture;
    gl::EasyShaderProgram mShader;
    gl::BufferObject mIndices;
};

} // namespace core

#endif // CORE_DESTINATIONTEXTURIZER_H
