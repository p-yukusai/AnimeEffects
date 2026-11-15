#include "gl/ExtendShader.h"
#include "gl/Global.h"
#include "core/ShaderHolder.h"
#include "core/MeshTransformerResource.h"

namespace core {

ShaderHolder::ShaderHolder(): mShaders(), mGridShaders(), mClipperShaders() {
    mShaders.resize(img::BlendMode_TERM * 2 * 2 * 2);
    mGridShaders.resize(1);
    mClipperShaders.resize(2);
}

ShaderHolder::~ShaderHolder() {
    for (auto p : mShaders) {
        if (p)
            delete p;
    }
    for (auto p : mGridShaders) {
        if (p)
            delete p;
    }
    for (auto p : mClipperShaders) {
        if (p)
            delete p;
    }
}

MeshTransformerResource& ShaderHolder::meshTransformerResource() {
    if (!mMeshTransformer) {
        mMeshTransformer = std::make_unique<MeshTransformerResource>();
        mMeshTransformer->setup(QStringLiteral(":/shaders/MeshTransformVert.glsl"));
    }
    return *mMeshTransformer;
}

gl::EasyShaderProgram& ShaderHolder::reserveShader(
    img::BlendMode aBlendMode, bool aIsClippee, bool aUseHSV, bool aUseLayerMask
) {
    const int index = aBlendMode + img::BlendMode_TERM * (int)aIsClippee + img::BlendMode_TERM * 2 * (int)aUseHSV
        + img::BlendMode_TERM * 4 * (int)aUseLayerMask;
    if (!mShaders[index]) {
        mShaders[index] = new gl::EasyShaderProgram();
        auto shader = mShaders[index];

        gl::ExtendShader source;
        if (!source.openFromFileVert(":/shaders/LayerDrawingVert.glsl")) {
            XC_FATAL_ERROR("FileIO Error", "Current location: " + QDir::currentPath() +
                               "Failed to open vertex shader file.\n", source.log());
        }
        if (!source.openFromFileFrag(":/shaders/LayerDrawingFrag.glsl")) {
            XC_FATAL_ERROR("FileIO Error", "Current location: " + QDir::currentPath() +
                               "Failed to open fragment shader file.\n", source.log());
        }

        auto blendFunc = QString("Blend") + img::getBlendFuncNameFromBlendMode(aBlendMode);
        source.setVariationValue("BLEND_FUNC", blendFunc);
        source.setVariationValue("BLEND_NONSEPARABLE", img::isNonSeparableBlendMode(aBlendMode) ? "1" : "0");
        source.setVariationValue(
            "BLEND_PREMULTIPLIED_SRC", img::isPremultipliedSrcBlendMode(aBlendMode) ? "1" : "0"
        );

        source.setVariationValue("IS_CLIPPEE", aIsClippee ? "1" : "0");

        source.setVariationValue("USE_HSV", aUseHSV ? "1" : "0");
        source.setVariationValue("LAYER_MASK", aUseLayerMask ? "1" : "0");

        // The layer texture (LayerNode's uTexture) is stored alpha-premultiplied: it is the
        // mipmapped artwork texture built in ImageKey (see img::Util::makeTextureUpload), and
        // correct mipmap averaging requires premultiplied color. This variation divides the
        // coverage back out of the sampled texel, so the blend functions and the HSV adjust below
        // keep operating on straight color. It is the only texture this shader samples with a
        // filtered lookup - the destination is read with texelFetch and must stay premultiplied
        // (see the scenebuffer/blend math).
        //
        // The pair has to flip together: before this variation was set the upload was straight and
        // the shader's default (assets/shaders/LayerDrawingFrag.glsl declares PREMULTIPLIED_INPUT 0)
        // matched it. A premultiplied upload with the variation left at 0 darkens every layer by
        // its own alpha.
        source.setVariationValue("PREMULTIPLIED_INPUT", "1");

        if (!source.resolveVariation()) {
            XC_FATAL_ERROR("OpenGL Error", "Failed to resolve shader variation.\n", source.log());
        }

        if (!shader->setAllSource(source)) {
            XC_FATAL_ERROR("OpenGL Error", "Failed to compile shader.\n", shader->log());
        }

        if (!shader->link()) {
            XC_FATAL_ERROR("OpenGL Error", "Failed to link shader.\n", shader->log());
        }
    }
    return *mShaders[index];
}

void ShaderHolder::reserveShaders(img::BlendMode aBlendMode) {
    for (int mask = 0; mask < 2; ++mask) {
        reserveShader(aBlendMode, true, true, mask != 0);
        reserveShader(aBlendMode, true, false, mask != 0);
        reserveShader(aBlendMode, false, true, mask != 0);
        reserveShader(aBlendMode, false, false, mask != 0);
    }
}

gl::EasyShaderProgram& ShaderHolder::shader(
    img::BlendMode aBlendMode, bool aIsClippee, bool aUseHSV, bool aUseLayerMask
) {
    const int index = aBlendMode + img::BlendMode_TERM * (int)aIsClippee + img::BlendMode_TERM * 2 * (int)aUseHSV
        + img::BlendMode_TERM * 4 * (int)aUseLayerMask;
    XC_PTR_ASSERT(mShaders.at(index));
    return *(mShaders.at(index));
}

const gl::EasyShaderProgram& ShaderHolder::shader(
    img::BlendMode aBlendMode, bool aIsClippee, bool aUseHSV, bool aUseLayerMask
) const {
    const int index = aBlendMode + img::BlendMode_TERM * (int)aIsClippee + img::BlendMode_TERM * 2 * (int)aUseHSV
        + img::BlendMode_TERM * 4 * (int)aUseLayerMask;
    XC_PTR_ASSERT(mShaders.at(index));
    return *(mShaders.at(index));
}

gl::EasyShaderProgram& ShaderHolder::reserveGridShader() {
    if (!mGridShaders[0]) {
        mGridShaders[0] = new gl::EasyShaderProgram();
        auto shader = mGridShaders[0];

        gl::ExtendShader source;
        if (!source.openFromFileVert(":/shaders/GridDrawingVert.glsl")) {
            XC_FATAL_ERROR("FileIO Error", "Current location: " + QDir::currentPath() +
                               "Failed to open vertex shader file.", source.log());
        }
        if (!source.openFromFileFrag(":/shaders/GridDrawingFrag.glsl")) {
            XC_FATAL_ERROR("FileIO Error", "Current location: " + QDir::currentPath() +
                                "Failed to open fragment shader file.", source.log());
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
    return *mGridShaders[0];
}

gl::EasyShaderProgram& ShaderHolder::gridShader() {
    XC_PTR_ASSERT(mGridShaders.at(0));
    return *(mGridShaders.at(0));
}

const gl::EasyShaderProgram& ShaderHolder::gridShader() const {
    XC_PTR_ASSERT(mGridShaders.at(0));
    return *(mGridShaders.at(0));
}

gl::EasyShaderProgram& ShaderHolder::reserveClipperShader(bool aIsClippee) {
    if (!mClipperShaders.at(aIsClippee)) {
        mClipperShaders[aIsClippee] = new gl::EasyShaderProgram();
        auto shader = mClipperShaders[aIsClippee];

        gl::ExtendShader source;
        if (!source.openFromFileVert(":/shaders/ClipperWritingVert.glsl")) {
            XC_FATAL_ERROR("FileIO Error", "Current location: " + QDir::currentPath() +
                               "\nFailed to open vertex shader file.", source.log());
        }
        if (!source.openFromFileFrag(":/shaders/ClipperWritingFrag.glsl")) {
            XC_FATAL_ERROR("FileIO Error", "Current location: " + QDir::currentPath() +
                               "\nFailed to open fragment shader file.", source.log());
        }

        source.setVariationValue("IS_CLIPPEE", aIsClippee ? "1" : "0");

        if (!source.resolveVariation()) {
            XC_FATAL_ERROR("OpenGL Error", "Failed to resolve shader variation.", source.log());
        }

        if (!shader->setAllSource(source)) {
            XC_FATAL_ERROR("OpenGL Error", "Failed to compile shader.", shader->log());
        }

        gl::Global::functions().glBindFragDataLocation(shader->id(), 0, "oClip");

        if (!shader->link()) {
            XC_FATAL_ERROR("OpenGL Error", "Failed to link shader.", shader->log());
        }
    }
    return *mClipperShaders[aIsClippee];
}

void ShaderHolder::reserveClipperShaders() {
    reserveClipperShader(true);
    reserveClipperShader(false);
}

gl::EasyShaderProgram& ShaderHolder::clipperShader(bool aIsClippee) {
    XC_PTR_ASSERT(mClipperShaders.at(aIsClippee));
    return *(mClipperShaders.at(aIsClippee));
}

const gl::EasyShaderProgram& ShaderHolder::clipperShader(bool aIsClippee) const {
    XC_PTR_ASSERT(mClipperShaders.at(aIsClippee));
    return *(mClipperShaders.at(aIsClippee));
}

} // namespace core
