#include <QFile>
#include "gl/Global.h"
#include "gl/ExtendShader.h"
#include "ctrl/ffd/ffd_TaskResource.h"

namespace ctrl {
namespace ffd {

    // THE ONE GLSL COPY of ctrl::brushFalloff (src/ctrl/Brush.h): a flat core of
    // radius*hardness and a smoothstep band to the rim. Prepended to every brush
    // shader so the deformer, the eraser and the raster targets share one edge;
    // the C++ definition is the reference and tests/brush_stroke.cpp pins it.
    static const char* kBrushGlsl =
        "uniform float uBrushHardness;\n"
        "uniform float uBrushAmount;\n"
        "float brushFalloff(float aDistance, float aRadius, float aHardness) {\n"
        "  if (aRadius <= 0.0 || aDistance >= aRadius) return 0.0;\n"
        "  float h = clamp(aHardness, 0.0, 1.0);\n"
        "  float core = aRadius * h;\n"
        "  if (aDistance <= core) return 1.0;\n"
        "  float t = (aRadius - aDistance) / (aRadius - core);\n"
        "  return t * t * (3.0 - 2.0 * t);\n"
        "}\n";

    // Inserts the shared snippet AFTER the #version directive, which must stay the
    // first non-comment text of a shader.
    static QString withBrushGlsl(QString aCode) {
        const int lineEnd = aCode.indexOf(QLatin1Char('\n'));
        if (lineEnd < 0)
            return QString::fromLatin1(kBrushGlsl) + aCode;
        return aCode.left(lineEnd + 1) + QString::fromLatin1(kBrushGlsl) + aCode.mid(lineEnd + 1);
    }

    //-------------------------------------------------------------------------------------------------
    TaskResource::TaskResource() {}

    void TaskResource::setup(
        const QString& aBrushPath, const QString& aEraserPath, const QString& aFocuserPath, const QString& aBlurPath
    ) {
        // load brush shader
        {
            QString code;
            loadFile(aBrushPath, code);
            buildShader(mProgram[kTypeDeformer], withBrushGlsl(code), kTypeDeformer);
        }

        // load eraser shader
        {
            QString code;
            loadFile(aEraserPath, code);
            buildShader(mProgram[kTypeEraser], withBrushGlsl(code), kTypeEraser);
        }

        // load focuser shader
        {
            QString code;
            loadFile(aFocuserPath, code);

            buildShader(mProgram[kTypeFocuser], code, kTypeFocuser);
        }

        // load blur path
        {
            QString code;
            loadFile(aBlurPath, code);
            buildBlurShader(mBlurProgram, code);
        }
    }

    gl::EasyShaderProgram& TaskResource::program(int aType) {
        switch (aType) {
        case kTypeDeformer:
            return mProgram[kTypeDeformer];
        case kTypeEraser:
            return mProgram[kTypeEraser];
        case kTypeFocuser:
            return mProgram[kTypeFocuser];
        default:
            XC_ASSERT(0);
            return mProgram[0];
        }
    }

    const gl::EasyShaderProgram& TaskResource::program(int aType) const {
        switch (aType) {
        case kTypeDeformer:
            return mProgram[kTypeDeformer];
        case kTypeEraser:
            return mProgram[kTypeEraser];
        case kTypeFocuser:
            return mProgram[kTypeFocuser];
        default:
            XC_ASSERT(0);
            return mProgram[0];
        }
    }

    void TaskResource::loadFile(const QString& aPath, QString& aDstCode) {
        QFile file(aPath);

        if (!file.open(QIODevice::ReadOnly)) {
            XC_FATAL_ERROR("FileIO Error", file.errorString(), aPath);
        }
        QTextStream in(&file);
        aDstCode = in.readAll();
    }

    void TaskResource::buildShader(gl::EasyShaderProgram& aProgram, QString aCode, int aType) {
        gl::Global::Functions& ggl = gl::Global::functions();

        gl::ExtendShader source;

        // parse shader source
        source.openFromTextVert(aCode);

        // resolve variation
        if (!source.resolveVariation()) {
            XC_FATAL_ERROR("OpenGL Error", "Failed to resolve shader variation.", source.log());
        }

        // set shader source
        if (!aProgram.setVertexSource(source)) {
            XC_FATAL_ERROR("OpenGL Error", "Failed to compile vertex shader.", aProgram.log());
        }

        // feedback
        if (aType == kTypeDeformer) {
            static const GLchar* kVaryings[] = {"outPosition", "outWeight"};
            ggl.glTransformFeedbackVaryings(aProgram.id(), 2, kVaryings, GL_SEPARATE_ATTRIBS);
        } else if (aType == kTypeEraser) {
            static const GLchar* kVaryings[] = {"outPosition"};
            ggl.glTransformFeedbackVaryings(aProgram.id(), 1, kVaryings, GL_SEPARATE_ATTRIBS);
        } else if (aType == kTypeFocuser) {
            static const GLchar* kVaryings[] = {"outPosition", "outWeight"};
            ggl.glTransformFeedbackVaryings(aProgram.id(), 2, kVaryings, GL_SEPARATE_ATTRIBS);
        }

        // link shader
        if (!aProgram.link()) {
            XC_FATAL_ERROR("OpenGL Error", "Failed to link shader.", aProgram.log());
        }
        XC_ASSERT(ggl.glGetError() == GL_NO_ERROR);
    }

    void TaskResource::buildBlurShader(gl::EasyShaderProgram& aProgram, const QString& aCode) {
        gl::Global::Functions& ggl = gl::Global::functions();

        gl::ExtendShader source;

        // parse shader source
        source.openFromTextVert(aCode);

        // resolve variation
        if (!source.resolveVariation()) {
            XC_FATAL_ERROR("OpenGL Error", "Failed to resolve shader variation.", source.log());
        }

        // set shader source
        if (!aProgram.setVertexSource(source)) {
            XC_FATAL_ERROR("OpenGL Error", "Failed to compile vertex shader.", aProgram.log());
        }

        // feedback
        static const GLchar* kVaryings[] = {"outPosition"};
        ggl.glTransformFeedbackVaryings(aProgram.id(), 1, kVaryings, GL_SEPARATE_ATTRIBS);

        // link shader
        if (!aProgram.link()) {
            XC_FATAL_ERROR("OpenGL Error", "Failed to link shader.", aProgram.log());
        }
        XC_ASSERT(ggl.glGetError() == GL_NO_ERROR);
    }

} // namespace ffd
} // namespace ctrl
