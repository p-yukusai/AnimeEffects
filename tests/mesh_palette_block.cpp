// The skinning palette's uniform block, exercised at a bone index the old
// default-block array could not address.
//
// `MeshTransformVert.glsl` reads the palette as `uBoneDualQuat[2 * boneIndex]`
// from `layout(std140) uniform BonePalette`. A plain uniform array would need
// 2 * 201 = 402 vec4 for this vertex - past the guaranteed 1024/4 = 256 vec4
// budget (128 bones) - so this is exactly the case the block exists for.
//
// The gate compiles the REAL shader with the variations MeshTransformerResource
// uses for a one-slot map (USE_SKINNING=1, USE_DUAL_QUATERNION=1,
// INFLUENCE_SLOTS=1, MAX_BONES=256), binds a 201-bone palette in a uniform
// block, and skins two points through transform feedback:
//   * index 200 carries a 90-degree Z rotation: (1,0,0) must become (0,1,0);
//   * index 1 is identity: (2,0,0) must stay (2,0,0).
// Both values are computed independently of the shader (a pure rotation has a
// zero dual part), so this pins the block binding, the std140 layout, the
// high-index fetch and the sparse one-slot path together.
//
// Needs a live GL context (QOpenGLWidget), like the other GL gates; run it with
// xvfb-run. No project, no document.
#include <QApplication>
#include <QFile>
#include <QMatrix4x4>
#include <QOpenGLShaderProgram>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLWidget>
#include <QSurfaceFormat>
#include <cmath>
#include <cstdio>
#include <vector>

#include "XC.h"
#include "gl/ExtendShader.h"
#include "gl/Global.h"
#include "core/PosePalette.h"

namespace {

class HarnessAssertHandler: public XCAssertHandler {
public:
    void failure() const override {
        std::fprintf(stderr, "XC_ASSERT FAILURE\n");
        std::fflush(stderr);
        std::abort();
    }
};
class HarnessErrorHandler: public XCErrorHandler {
public:
    void critical(const QString& aText, const QString&, const QString& aDetail) const override {
        std::fprintf(stderr, "XC_FATAL %s | %s\n", qPrintable(aText), qPrintable(aDetail));
        std::fflush(stderr);
        std::abort();
    }
};
HarnessAssertHandler gAssertHandler;
HarnessErrorHandler gErrorHandler;
int failures = 0;

void check(bool aCondition, const char* aWhat) {
    if (aCondition) {
        std::printf("PASS  %s\n", aWhat);
    } else {
        std::printf("FAIL  %s\n", aWhat);
        ++failures;
    }
}

bool near(float aLhs, float aRhs) { return std::fabs(aLhs - aRhs) <= 1e-4f; }

struct Dq {
    float real[4]; // shader layout: x = w, y = x, z = y, w = z
    float dual[4];
};

Dq identityDq() {
    Dq dq = {};
    dq.real[0] = 1.0f;
    return dq;
}

// A rotation about Z, no translation: the dual part is exactly zero.
Dq rotateZDq(float aDegrees) {
    const float half = aDegrees * 3.14159265358979323846f / 360.0f;
    Dq dq = {};
    dq.real[0] = std::cos(half);
    dq.real[3] = std::sin(half);
    return dq;
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // ---- a live core-profile context ----
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setVersion(gl::Global::kVersion.first, gl::Global::kVersion.second);
    QSurfaceFormat::setDefaultFormat(format);
    QOpenGLWidget widget;
    widget.resize(4, 4);
    widget.show();
    widget.makeCurrent();
    if (!widget.context()) {
        std::printf("FAIL  no GL context\n");
        return 1;
    }
    auto* funcs = QOpenGLVersionFunctionsFactory::get<gl::Global::Functions>(widget.context());
    if (!funcs || !funcs->initializeOpenGLFunctions()) {
        std::printf("FAIL  GL functions init\n");
        return 1;
    }
    gl::Global::setContext(widget);
    gl::Global::setFunctions(*funcs);
    gl::Global::Functions& ggl = gl::Global::functions();

    // ---- the real skinning shader, with the production variations ----
    gl::ExtendShader source;
    if (!source.openFromFileVert(QStringLiteral(":/shaders/MeshTransformVert.glsl"))) {
        std::printf("FAIL  cannot read the shader: %s\n", qPrintable(source.log()));
        return 1;
    }
    source.setVariationValue("USE_SKINNING", QStringLiteral("1"));
    source.setVariationValue("USE_DUAL_QUATERNION", QStringLiteral("1"));
    source.setVariationValue("INFLUENCE_SLOTS", QStringLiteral("1"));
    source.setVariationValue("MAX_BONES", QString::number(core::PosePalette::kMaxCount));
    if (!source.resolveVariation()) {
        std::printf("FAIL  shader variation: %s\n", qPrintable(source.log()));
        return 1;
    }

    QOpenGLShaderProgram program;
    if (!program.addShaderFromSourceCode(QOpenGLShader::Vertex, source.vertexCode())) {
        std::printf("FAIL  vertex compile: %s\n", qPrintable(program.log()));
        return 1;
    }
    program.bindAttributeLocation("inPosition", 0);
    program.bindAttributeLocation("inBoneIndex0", 1);
    // transform feedback varyings must be declared before the link
    const GLchar* varyings[] = {"outPosition"};
    ggl.glTransformFeedbackVaryings(program.programId(), 1, varyings, GL_SEPARATE_ATTRIBS);
    if (!program.link()) {
        std::printf("FAIL  link: %s\n", qPrintable(program.log()));
        return 1;
    }

    const GLuint block = ggl.glGetUniformBlockIndex(program.programId(), "BonePalette");
    check(block != GL_INVALID_INDEX, "the shader declares the BonePalette uniform block");
    const int paletteBones = 201;
    check(
        paletteBones > 128,
        "the gate's palette is above the old 128-bone default-block ceiling"
    );
    if (block != GL_INVALID_INDEX)
        ggl.glUniformBlockBinding(program.programId(), block, 0);

    // ---- the palette: identity everywhere except bone 200 ----
    std::vector<Dq> palette(size_t(core::PosePalette::kMaxCount), identityDq());
    palette[200] = rotateZDq(90.0f);

    GLuint ubo = 0;
    ggl.glGenBuffers(1, &ubo);
    ggl.glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    const GLsizeiptr paletteBytes = GLsizeiptr(core::PosePalette::kMaxCount) * GLsizeiptr(sizeof(Dq));
    ggl.glBufferData(GL_UNIFORM_BUFFER, paletteBytes, nullptr, GL_DYNAMIC_DRAW);
    ggl.glBufferSubData(GL_UNIFORM_BUFFER, 0, GLsizeiptr(paletteBones) * GLsizeiptr(sizeof(Dq)), palette.data());
    ggl.glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);
    ggl.glBindBuffer(GL_UNIFORM_BUFFER, 0);

    // ---- two points: (1,0,0) on bone 200, (2,0,0) on bone 1 ----
    GLuint vao = 0;
    ggl.glGenVertexArrays(1, &vao);
    ggl.glBindVertexArray(vao);

    const float positions[2][4] = {{1.0f, 0.0f, 0.0f, 1.0f}, {2.0f, 0.0f, 0.0f, 1.0f}};
    const GLubyte indices[2] = {200, 1};
    GLuint positionBuffer = 0;
    GLuint indexBuffer = 0;
    ggl.glGenBuffers(1, &positionBuffer);
    ggl.glBindBuffer(GL_ARRAY_BUFFER, positionBuffer);
    ggl.glBufferData(GL_ARRAY_BUFFER, sizeof(positions), positions, GL_STATIC_DRAW);
    ggl.glEnableVertexAttribArray(0);
    ggl.glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    ggl.glGenBuffers(1, &indexBuffer);
    ggl.glBindBuffer(GL_ARRAY_BUFFER, indexBuffer);
    ggl.glBufferData(GL_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    ggl.glEnableVertexAttribArray(1);
    ggl.glVertexAttribIPointer(1, 1, GL_UNSIGNED_BYTE, 0, nullptr);
    ggl.glBindBuffer(GL_ARRAY_BUFFER, 0);

    GLuint feedback = 0;
    ggl.glGenBuffers(1, &feedback);
    ggl.glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, feedback);
    ggl.glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, sizeof(float) * 3 * 2, nullptr, GL_STREAM_READ);
    ggl.glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, feedback);

    // ---- draw ----
    program.bind();
    program.setUniformValue("uInnerMatrix", QMatrix4x4());
    program.setUniformValue("uWorldMatrix", QMatrix4x4());
    ggl.glEnable(GL_RASTERIZER_DISCARD);
    ggl.glBeginTransformFeedback(GL_POINTS);
    ggl.glDrawArrays(GL_POINTS, 0, 2);
    ggl.glEndTransformFeedback();
    ggl.glDisable(GL_RASTERIZER_DISCARD);
    program.release();

    float out[6] = {};
    ggl.glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, feedback);
    ggl.glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER, 0, sizeof(out), out);
    ggl.glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, 0);
    const GLenum error = ggl.glGetError();
    check(error == GL_NO_ERROR, "the block draw leaves no GL error");

    // independent expectation: R_z(90) * (1,0,0) = (0,1,0); identity keeps (2,0,0)
    check(
        near(out[0], 0.0f) && near(out[1], 1.0f) && near(out[2], 0.0f),
        "bone index 200 (the block's high range) skins (1,0,0) to (0,1,0)"
    );
    check(
        near(out[3], 2.0f) && near(out[4], 0.0f) && near(out[5], 0.0f),
        "bone index 1 in the same block stays identity"
    );

    ggl.glDeleteBuffers(1, &feedback);
    ggl.glDeleteBuffers(1, &indexBuffer);
    ggl.glDeleteBuffers(1, &positionBuffer);
    ggl.glDeleteBuffers(1, &ubo);
    ggl.glDeleteVertexArrays(1, &vao);

    if (failures == 0) {
        std::printf("PASS: skinning palette uniform block\n");
    } else {
        std::printf("FAILED %d check(s)\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
