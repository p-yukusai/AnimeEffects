#ifndef CTRL_FFD_TASKRESOURCE_H
#define CTRL_FFD_TASKRESOURCE_H

#include "gl/EasyShaderProgram.h"

namespace ctrl {
namespace ffd {

    class TaskResource {
    public:
        // One program per brush target. Hardness used to be a compile-time
        // variation (three powers of two); it is a uniform now, so the mesh brush
        // has the same continuous hardness the raster brush has.
        enum { kTypeDeformer = 0, kTypeEraser = 1, kTypeFocuser = 2, kVariation = 3 };

        TaskResource();

        void setup(
            const QString& aBrushPath, const QString& aEraserPath, const QString& aFocuserPath, const QString& aBlurPath
        );

        gl::EasyShaderProgram& program(int aType);
        const gl::EasyShaderProgram& program(int aType) const;

        gl::EasyShaderProgram& blurProgram() { return mBlurProgram; }
        const gl::EasyShaderProgram& blurProgram() const { return mBlurProgram; }

    private:
        void loadFile(const QString& aPath, QString& aDstCode);
        void buildShader(gl::EasyShaderProgram& aProgram, QString aCode, int aType);
        void buildBlurShader(gl::EasyShaderProgram& aProgram, const QString& aCode);

        gl::EasyShaderProgram mProgram[kVariation];
        gl::EasyShaderProgram mBlurProgram;
    };

} // namespace ffd
} // namespace ctrl

#endif // CTRL_FFD_TASKRESOURCE_H
