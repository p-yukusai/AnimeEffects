#ifndef CORE_SHADERHOLDER_H
#define CORE_SHADERHOLDER_H

#include <QVector>
#include <memory>
#include <QScopedPointer>
#include "gl/EasyShaderProgram.h"
#include "img/BlendMode.h"

namespace core {

class MeshTransformerResource;

class ShaderHolder {
public:
    ShaderHolder();
    ~ShaderHolder();

    // aUseLayerMask compiles the node's own matte into the draw (LAYER_MASK): a
    // node whose mask has a field needs one more variant than a node without one.
    // aVirtualTexture selects the VIRTUAL_TEXTURE variation: the layer's texels are paged and
    // the sampler resolves them through a page table and the atlases (see
    // core::VirtualTexture). Every other input is independent of it, so a pageable layer costs
    // one more program per (blend, clippee, hsv, mask) combination.
    // aMaskVirtualTexture is the matte's OWN page stack (MASK_VIRTUAL_TEXTURE). It is only
    // meaningful together with aUseLayerMask && aVirtualTexture, and the combination is built on
    // demand like every other unseen one; reserveShaders warms the four it can create.
    gl::EasyShaderProgram& reserveShader(
        img::BlendMode aBlendMode,
        bool aIsClippee,
        bool aUseHSV,
        bool aUseLayerMask,
        bool aVirtualTexture,
        bool aMaskVirtualTexture = false
    );
    void reserveShaders(img::BlendMode aBlendMode, bool aVirtualTexture);
    gl::EasyShaderProgram& shader(
        img::BlendMode aBlendMode,
        bool aIsClippee,
        bool aUseHSV,
        bool aUseLayerMask,
        bool aVirtualTexture,
        bool aMaskVirtualTexture = false
    );
    const gl::EasyShaderProgram& shader(
        img::BlendMode aBlendMode,
        bool aIsClippee,
        bool aUseHSV,
        bool aUseLayerMask,
        bool aVirtualTexture,
        bool aMaskVirtualTexture = false
    ) const;

    /// The three mesh-transform programs (plain, skinning, dual quaternion), built ONCE per holder
    /// and shared by every LayerNode that renders the same mesh. They were per LayerNode before,
    /// and identical in all of them: a 54 MB project measured 8.4 s of a 16.7 s load compiling 344
    /// x 3 copies of the same program (2026-11-01, llvmpipe, so a real driver is faster but the
    /// work is the same and just as pointless).
    MeshTransformerResource& meshTransformerResource();

    gl::EasyShaderProgram& reserveGridShader();
    gl::EasyShaderProgram& gridShader();
    const gl::EasyShaderProgram& gridShader() const;

    /// The clipper's singulation program: IS_CLIPPEE (this clipper is itself clipped) and
    /// VIRTUAL_TEXTURE (its layer is paged) are independent inputs, so four programs at most.
    gl::EasyShaderProgram& reserveClipperShader(bool aIsClippee, bool aVirtualTexture);
    void reserveClipperShaders();
    gl::EasyShaderProgram& clipperShader(bool aIsClippee, bool aVirtualTexture);
    const gl::EasyShaderProgram& clipperShader(bool aIsClippee, bool aVirtualTexture) const;

private:
    QVector<gl::EasyShaderProgram*> mShaders;
    QVector<gl::EasyShaderProgram*> mGridShaders;
    QVector<gl::EasyShaderProgram*> mClipperShaders;
    std::unique_ptr<MeshTransformerResource> mMeshTransformer;
};

} // namespace core

#endif // CORE_SHADERHOLDER_H
