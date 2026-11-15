#include "gl/Global.h"
#include "gl/Util.h"
#include "gl/BufferObject.h"
#include "gl/Vector4.h"
#include "core/DestinationTexturizer.h"

namespace {
static const int kAttachmentId = 0;

static const GLuint kFullscreenIndices[4] = {0, 1, 3, 2};
} // namespace

namespace core {

DestinationTexturizer::DestinationTexturizer():
    mFramebuffer(), mTexture(), mShader(), mIndices(GL_ELEMENT_ARRAY_BUFFER) {
    mFramebuffer.reset(new gl::Framebuffer());
    mTexture.reset(new gl::Texture());

    createShader();
}

void DestinationTexturizer::resize(const QSize& aSize) {
    mTexture->destroy();
    mFramebuffer.reset();

    // create framebuffer
    mFramebuffer.reset(new gl::Framebuffer());

    // create texture
    mTexture->create(aSize);
    mTexture->setFilter(GL_NEAREST);
    mTexture->setWrap(GL_CLAMP_TO_EDGE);

    // attach textures
    mFramebuffer->setColorAttachment(kAttachmentId, mTexture->id());
    XC_ASSERT(mFramebuffer->isComplete());
}

void DestinationTexturizer::clearTexture() {
    XC_ASSERT(mTexture->size().isValid());

    auto& ggl = gl::Global::functions();

    mFramebuffer->bind();

    // setup drawbuffers
    const GLenum attachments[] = {GL_COLOR_ATTACHMENT0};
    ggl.glDrawBuffers(1, attachments);

    gl::Util::resetRenderState();
    gl::Util::setViewportAsActualPixels(mTexture->size());
    gl::Util::clearColorBuffer(0.0f, 0.0f, 0.0f, 0.0f);

    mFramebuffer->release();

    ggl.glFlush();
    GL_CHECK_ERROR();
}

void DestinationTexturizer::updateAll(GLuint aFramebuffer, GLuint aFrameTexture) {
    XC_ASSERT(mTexture->size().isValid());

    auto& ggl = gl::Global::functions();

    // A COPY, NOT A BLEND. These draws replace the texturizer's texels with the framebuffer's;
    // they inherit whatever draw state the caller's last layer left, and a layer draw leaves
    // GL_BLEND enabled with (SRC_ALPHA, ONE_MINUS_SRC_ALPHA). The copy shader writes the scene
    // premultiplied with its alpha unchanged, so blending makes it `scene over <the texturizer's
    // previous content>` in every semi-transparent pixel - and every folder runs this pass, so
    // the residue compounds through the tree. A leftover scissor would clip the copy to the last
    // draw's band for the same reason. Measured: with this fixed the panned-camera defect in
    // docs/design/renderer/texture-residency.md disappears (see the doc's Measured section).
    const GLboolean prevBlend = ggl.glIsEnabled(GL_BLEND);
    const GLboolean prevScissor = ggl.glIsEnabled(GL_SCISSOR_TEST);
    ggl.glDisable(GL_BLEND);
    ggl.glDisable(GL_SCISSOR_TEST);

    // bind framebuffer
    mFramebuffer->bind();

    // setup drawbuffers
    const GLenum attachments[] = {GL_COLOR_ATTACHMENT0};
    ggl.glDrawBuffers(1, attachments);

    // bind the source scene texture
    ggl.glActiveTexture(GL_TEXTURE0);
    ggl.glBindTexture(GL_TEXTURE_2D, aFrameTexture);

    {
        mIndices.resetData(4, GL_STATIC_DRAW, kFullscreenIndices);

        static const gl::Vector4 kScreenQuad[4] = {
            {-1.0f, -1.0f, 0.0f, 1.0f},
            {-1.0f,  1.0f, 0.0f, 1.0f},
            { 1.0f,  1.0f, 0.0f, 1.0f},
            { 1.0f, -1.0f, 0.0f, 1.0f}
        };

        mShader.bind();
        mShader.setAttributeArray("inPosition", kScreenQuad, 4);

        QMatrix4x4 ident;
        mShader.setUniformValue("uViewMatrix", ident);
        mShader.setUniformValue("uScreenSize", QSizeF(mTexture->size()));
        mShader.setUniformValue("uDestTexture", 0);

        gl::Util::drawElements(GL_TRIANGLE_STRIP, GL_UNSIGNED_INT, mIndices);

        mShader.release();
    }
    // unbind texture
    ggl.glBindTexture(GL_TEXTURE_2D, 0);

    if (prevBlend)
        ggl.glEnable(GL_BLEND);
    if (prevScissor)
        ggl.glEnable(GL_SCISSOR_TEST);

    // release framebuffer
    mFramebuffer->release();

    // bind default framebuffer
    ggl.glBindFramebuffer(GL_FRAMEBUFFER, aFramebuffer);

    ggl.glFlush();
}

void DestinationTexturizer::update(
    GLuint aFramebuffer,
    GLuint aFrameTexture,
    const QMatrix4x4& aViewMatrix,
    LayerMesh& aMesh,
    gl::BufferObject& aPositions
) {
    XC_ASSERT(mTexture->size().isValid());

    auto& ggl = gl::Global::functions();

    // A COPY, NOT A BLEND. These draws replace the texturizer's texels with the framebuffer's;
    // they inherit whatever draw state the caller's last layer left, and a layer draw leaves
    // GL_BLEND enabled with (SRC_ALPHA, ONE_MINUS_SRC_ALPHA). The copy shader writes the scene
    // premultiplied with its alpha unchanged, so blending makes it `scene over <the texturizer's
    // previous content>` in every semi-transparent pixel - and every folder runs this pass, so
    // the residue compounds through the tree. A leftover scissor would clip the copy to the last
    // draw's band for the same reason. Measured: with this fixed the panned-camera defect in
    // docs/design/renderer/texture-residency.md disappears (see the doc's Measured section).
    const GLboolean prevBlend = ggl.glIsEnabled(GL_BLEND);
    const GLboolean prevScissor = ggl.glIsEnabled(GL_SCISSOR_TEST);
    ggl.glDisable(GL_BLEND);
    ggl.glDisable(GL_SCISSOR_TEST);

    // bind framebuffer
    mFramebuffer->bind();

    // setup drawbuffers
    const GLenum attachments[] = {GL_COLOR_ATTACHMENT0};
    ggl.glDrawBuffers(1, attachments);

    // bind textures
    ggl.glActiveTexture(GL_TEXTURE0);
    ggl.glBindTexture(GL_TEXTURE_2D, aFrameTexture);

    {
        mShader.bind();

        mShader.setAttributeBuffer("inPosition", aPositions, GL_FLOAT, 3);

        mShader.setUniformValue("uViewMatrix", aViewMatrix);
        mShader.setUniformValue("uScreenSize", QSizeF(mTexture->size()));
        mShader.setUniformValue("uDestTexture", 0);

        gl::Util::drawElements(aMesh.primitiveMode(), GL_UNSIGNED_INT, aMesh.getIndexBuffer());

        mShader.release();
    }
    // unbind texture
    ggl.glBindTexture(GL_TEXTURE_2D, 0);

    if (prevBlend)
        ggl.glEnable(GL_BLEND);
    if (prevScissor)
        ggl.glEnable(GL_SCISSOR_TEST);

    // release framebuffer
    mFramebuffer->release();

    // bind default framebuffer
    ggl.glBindFramebuffer(GL_FRAMEBUFFER, aFramebuffer);

    ggl.glFlush();
}

void DestinationTexturizer::createShader() {
    auto shader = &mShader;

    gl::ExtendShader source;
    if (!source.openFromFileVert(":/shaders/PartialScreenCopyingVert.glsl")) {
        XC_FATAL_ERROR("FileIO Error", "Current location: " + QDir::currentPath() +
                           "\nFailed to open vertex shader file.\n", source.log());
    }
    if (!source.openFromFileFrag(":/shaders/PartialScreenCopyingFrag.glsl")) {
        XC_FATAL_ERROR("FileIO Error", "Current location: " + QDir::currentPath() +
                           "\nFailed to open fragment shader file.\n", source.log());
    }

    if (!source.resolveVariation()) {
        XC_FATAL_ERROR("OpenGL Error", "Failed to resolve shader variation.", source.log());
    }

    if (!shader->setAllSource(source)) {
        XC_FATAL_ERROR("OpenGL Error", "Failed to compile shader.", shader->log());
    }

    if (!shader->link()) {
        XC_FATAL_ERROR("OpenGL Error", "Failed to link shader.", shader->log());
    }
}

} // namespace core
