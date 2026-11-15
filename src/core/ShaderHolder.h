#ifndef CORE_SHADERHOLDER_H
#define CORE_SHADERHOLDER_H

#include <QVector>
#include <QScopedPointer>
#include "gl/EasyShaderProgram.h"
#include "img/BlendMode.h"

namespace core {

class ShaderHolder {
public:
    ShaderHolder();
    ~ShaderHolder();

    // aUseLayerMask compiles the node's own matte into the draw (LAYER_MASK): a
    // node whose mask has a field needs one more variant than a node without one.
    gl::EasyShaderProgram& reserveShader(
        img::BlendMode aBlendMode, bool aIsClippee, bool aUseHSV, bool aUseLayerMask
    );
    void reserveShaders(img::BlendMode aBlendMode);
    gl::EasyShaderProgram& shader(
        img::BlendMode aBlendMode, bool aIsClippee, bool aUseHSV, bool aUseLayerMask
    );
    const gl::EasyShaderProgram& shader(
        img::BlendMode aBlendMode, bool aIsClippee, bool aUseHSV, bool aUseLayerMask
    ) const;

    gl::EasyShaderProgram& reserveGridShader();
    gl::EasyShaderProgram& gridShader();
    const gl::EasyShaderProgram& gridShader() const;

    gl::EasyShaderProgram& reserveClipperShader(bool aIsClippee);
    void reserveClipperShaders();
    gl::EasyShaderProgram& clipperShader(bool aIsClippee);
    const gl::EasyShaderProgram& clipperShader(bool aIsClippee) const;

private:
    QVector<gl::EasyShaderProgram*> mShaders;
    QVector<gl::EasyShaderProgram*> mGridShaders;
    QVector<gl::EasyShaderProgram*> mClipperShaders;
};

} // namespace core

#endif // CORE_SHADERHOLDER_H
