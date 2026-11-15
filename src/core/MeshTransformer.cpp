#include "gl/FrameStats.h"
#include "gl/Global.h"
#include "gl/Util.h"
#include "core/MeshTransformer.h"
#include "core/MeshTransformerResource.h"
#include "core/ObjectNodeUtil.h"
#include "core/PosePalette.h"

#include <algorithm>
#include <memory>
#include <QOpenGLShaderProgram>
#include <QVector>

namespace core {

namespace {

//==============================================================================
// THE DENSITY DISTRIBUTION OF THE DEFORMATION
//
// Which mip level a layer needs follows from how its deformation stretched or squeezed
// the texels, and playback changes the deformation (and the blend) on every frame - so
// this is measured inside the pass that already skins the vertices, every frame, and it
// measures the DISTRIBUTION rather than a worst case: a policy picks an area-weighted
// percentile, and a percentile needs the shape of the distribution, not its maximum.
//
// One extra draw of the same indexed triangles into a 16x1 R32F target, additive, with a
// geometry shader that turns each triangle into points at its bins. The three distributions
// share the target and the draw because a triangle already has its uv edges and its deformed
// edges in the geometry shader: density (area ratio), s_max (the larger singular value of the
// uv->mesh Jacobian) and anisotropy (s_max / s_min, the ratio a trilinear level over-blurs by).
// Six attachments carry them: each histogram's uv-area sum (added) and its maximum (max-blended),
// so one 384-byte copy per frame carries all three histograms and all three maxima. The mesh
// is not a regular grid (MeshKey meshes are arbitrary triangle soups), which is why this is
// a geometry shader and not a per-vertex Jacobian from neighbour fetches.
//
// The copy is issued when the NEXT frame starts and mapped when the one after that starts.
// A copy of this frame's target can only be read once the GPU has finished it, and waiting
// for that is exactly the stall this exists to avoid; a frame of slack between the copy and
// the map is what makes the map free. The price is that the line a frame writes describes
// the frame two frames back, which for playback's steady state is the same distribution.
//==============================================================================

const int kBinCount = gl::FrameStats::kDensityBins;
// attachment layout: one uv-area-weighted histogram and one maximum per measured quantity.
// kBinCount pixels wide (the anisotropy histogram's 8 bins use the first 8).
enum Attachment {
    Attach_DensityWeight = 0, // uv area added into its log2(density) bin
    Attach_DensityMax = 1,    // the largest density
    Attach_SmaxWeight = 2,    // uv area added into its log2(s_max) bin
    Attach_SmaxMax = 3,       // the largest s_max
    Attach_AnisoWeight = 4,   // uv area added into its s_max / s_min bin
    Attach_AnisoMax = 5,      // the largest s_max / s_min
    Attach_Count = 6,
};
static_assert(gl::FrameStats::kSmaxBins == kBinCount, "the s_max histogram shares the target width");
static_assert(gl::FrameStats::kAnisoBins <= kBinCount, "the anisotropy histogram fits the target");
const GLsizeiptr kBinBytes = sizeof(float) * kBinCount;
const GLsizeiptr kReadbackBytes = kBinBytes * Attach_Count;

// A triangle whose uv area is under this many content pixels covers less than one texel at
// 1:1, so its area ratio is a quotient of two numbers at the float noise floor rather than a
// property of a visible surface: a near-degenerate mesh triangle can read as 200x from an
// ulp. Such triangles are DROPPED from the histogram and from the maximum, never clamped
// (a clamp would keep a wrong number and hide it). Measured on a 64x64 image on an 8px grid
// (32 px^2 per triangle) the threshold removes nothing; see the density trace for the weight
// it removes on a real project.
const float kMinUvArea = 0.25f;

// attribute locations are bound before the link, so the pass does not have to ask
const GLuint kPositionAttrib = 0;
const GLuint kTexCoordAttrib = 1;

// The deformed position is already in the layer's content space: the skinning pass wrote
// world * skin * inner * position. Undoing world and inner leaves skin * position - the
// deformation alone, in the mesh's own content pixels, which is the space the texcoords
// are in. That is what makes a layer that is only moved, rotated or scaled report 1.0.
const char* const kDensityVertexShader = R"(#version 330
in vec3 inPosition;
in vec2 inTexCoord;
uniform mat4 uInverseTransform;
out vec2 vTexCoord;
void main() {
    gl_Position = uInverseTransform * vec4(inPosition, 1.0);
    vTexCoord = inTexCoord;
}
)";

const char* const kDensityGeometryShader = R"(#version 330
layout(triangles) in;
layout(points, max_vertices = 3) out;
in vec2 vTexCoord[];
out float vDensWeight;
out float vDensMax;
out float vSmaxWeight;
out float vSmaxMax;
out float vAnisoWeight;
out float vAnisoMax;
uniform float uBinBase;
uniform float uBinStep;
uniform int uBinCount;
uniform float uMinUvArea;

float triangleArea(vec2 a, vec2 b, vec2 c) {
    return abs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) * 0.5;
}

// CENTRED BINS: bin b spans uBinStep around uBinBase + b * uBinStep, so 1.0 - every
// undeformed triangle - lands in the middle of its bin instead of on the edge between two,
// where one ulp of log2 would flip it. `uMin` is the clamp for log2's argument.
float binOf(float value, float uMin) {
    return clamp(
        floor((log2(max(value, uMin)) - uBinBase) / uBinStep + 0.5), 0.0, float(uBinCount - 1)
    );
}

// ONE POINT PER HISTOGRAM, because the three bins of a triangle are three different pixels.
// Each point writes its own histogram and zeroes the other two: zero adds nothing to a sum
// and can never win a max.
void emitAt(float bin, float densW, float densMax, float smaxW, float smaxMax, float anisoW, float anisoMax) {
    gl_Position = vec4((bin + 0.5) / float(uBinCount) * 2.0 - 1.0, 0.0, 0.0, 1.0);
    gl_PointSize = 1.0;
    vDensWeight = densW;
    vDensMax = densMax;
    vSmaxWeight = smaxW;
    vSmaxMax = smaxMax;
    vAnisoWeight = anisoW;
    vAnisoMax = anisoMax;
    EmitVertex();
    EndPrimitive();
}

void main() {
    float uvArea = triangleArea(vTexCoord[0], vTexCoord[1], vTexCoord[2]);

    // A triangle below this uv area covers less than a texel: its ratios are quotients of
    // two numbers at the float noise floor, so they are not a fact about any visible
    // surface. Dropped rather than clamped - a clamped value would still be a lie in the
    // histogram. It contributes to none of the three.
    if (uvArea >= uMinUvArea) {
        float deformedArea =
            triangleArea(gl_in[0].gl_Position.xy, gl_in[1].gl_Position.xy, gl_in[2].gl_Position.xy);
        float density = deformedArea / uvArea;
        if (!(density > 0.0))
            density = 0.0; // collapsed by the deformation: bin 0, and never a NaN into log2

        // THE JACOBIAN of uv edges -> deformed mesh edges: J * (uv1 - uv0) = p1 - p0 and
        // J * (uv2 - uv0) = p2 - p0, so J = E * U^-1 with E and U holding those edges.
        vec2 d1 = vTexCoord[1] - vTexCoord[0];
        vec2 d2 = vTexCoord[2] - vTexCoord[0];
        vec2 e1 = gl_in[1].gl_Position.xy - gl_in[0].gl_Position.xy;
        vec2 e2 = gl_in[2].gl_Position.xy - gl_in[0].gl_Position.xy;
        float detU = d1.x * d2.y - d2.x * d1.y;

        // uvArea >= uMinUvArea means |detU| >= 2 * uMinUvArea, so U is never singular here;
        // the clamps below still cover a DEFORMATION that collapses the triangle.
        vec2 j1 = (e1 * d2.y - e2 * d1.y) / detU;
        vec2 j2 = (e2 * d1.x - e1 * d2.x) / detU;

        // the singular values of J: the eigenvalues of J^T J are their squares, and for a 2x2
        // matrix they follow from trace and determinant alone
        float a = dot(j1, j1) + dot(j2, j2);
        float det = j1.x * j2.y - j2.x * j1.y;
        float disc = max(a * a - 4.0 * det * det, 0.0);
        float sMax = sqrt(0.5 * (a + sqrt(disc)));
        float sMin = sqrt(max(0.5 * (a - sqrt(disc)), 0.0));
        // a degenerate Jacobian can round either value to a NaN or a negative zero; the
        // !(x >= 0) form catches NaN, which a plain clamp does not
        if (!(sMax >= 0.0)) sMax = 0.0;
        if (!(sMin >= 0.0)) sMin = 0.0;

        // s_max / s_min, clamped (never logged) so a singular minimum cannot produce an
        // infinity. Ratios below 1 are rounding, not anisotropy, so they join bin 0.
        float aniso = sMax / max(sMin, 1.0e-8);
        if (!(aniso >= 1.0)) aniso = 1.0;
        aniso = min(aniso, 1.0e8);
        // a relative epsilon puts a ratio that IS an edge (2x in x is exactly 2) in the
        // higher bin even when the matrix inverse rounded a hair below it; 1e-4 is still two
        // orders of magnitude below the spacing of the edges themselves
        float r = aniso * (1.0 + 1.0e-4);
        // the same interior edges as gl::FrameStats::kAnisoBinEdges (GLSL cannot read them)
        int anisoBin = 0;
        if (r >= 1.5) anisoBin = 1;
        if (r >= 2.0) anisoBin = 2;
        if (r >= 3.0) anisoBin = 3;
        if (r >= 4.0) anisoBin = 4;
        if (r >= 6.0) anisoBin = 5;
        if (r >= 8.0) anisoBin = 6;
        if (r >= 16.0) anisoBin = 7;

        emitAt(binOf(density, 1.0e-8), uvArea, density, 0.0, 0.0, 0.0, 0.0);
        emitAt(binOf(sMax, 1.0e-8), 0.0, 0.0, uvArea, sMax, 0.0, 0.0);
        emitAt(float(anisoBin), 0.0, 0.0, 0.0, 0.0, uvArea, aniso);
    }
}
)";

const char* const kDensityFragmentShader = R"(#version 330
in float vDensWeight;
in float vDensMax;
in float vSmaxWeight;
in float vSmaxMax;
in float vAnisoWeight;
in float vAnisoMax;
layout(location = 0) out float oDensWeight;
layout(location = 1) out float oDensMax;
layout(location = 2) out float oSmaxWeight;
layout(location = 3) out float oSmaxMax;
layout(location = 4) out float oAnisoWeight;
layout(location = 5) out float oAnisoMax;
void main() {
    oDensWeight = vDensWeight;
    oDensMax = vDensMax;
    oSmaxWeight = vSmaxWeight;
    oSmaxMax = vSmaxMax;
    oAnisoWeight = vAnisoWeight;
    oAnisoMax = vAnisoMax;
}
)";

// The mesh whose triangles were just skinned. callGL is handed the output buffer and the
// positions, not the mesh, so the topology and the uvs come from the expansions - the same
// choice TimeKeyBlender::getAreaMeshImpl makes: the image's grid when it is newer than the
// mesh key, the mesh key's own data otherwise. A vertex count that disagrees means these
// are not that mesh's positions, so nothing is measured (and the frame reports zeros).
// `aSource` names the choice for the density trace.
const LayerMesh* densityMesh(const TimeKeyExpans& aExpans, int aVtxCount, const char** aSource) {
    *aSource = "none";
    if (aVtxCount <= 0)
        return nullptr;

    const MeshKey* meshKey = aExpans.areaMeshKey();
    const ImageKey* imageKey = aExpans.areaImageKey();

    const LayerMesh* mesh = nullptr;
    if (imageKey && (!meshKey || imageKey->frame() > meshKey->frame())) {
        mesh = &imageKey->data().gridMesh();
        *aSource = "image-grid";
    } else if (meshKey) {
        mesh = &meshKey->data();
        *aSource = "mesh-key";
    }

    if (mesh && mesh->vertexCount() == aVtxCount && mesh->indexCount() >= 3)
        return mesh;
    *aSource = "vertex-count-mismatch";
    return nullptr;
}

//-------------------------------------------------------------------------------------------------
// AE_FRAME_STATS_DENSITY_TRACE=<layers>: the meter's own inputs, on the CPU, for the first
// <layers> layers of a run. A density that reads as impossible is almost always an input
// question - a uv space that is not the position space, or a vertex tail the skinning pass
// never wrote - and both show up as one triangle whose uv area and deformed area are printed
// here. Off (and free) unless the variable is set.
//-------------------------------------------------------------------------------------------------
int densityTraceBudget() {
    bool ok = false;
    const int value = qEnvironmentVariableIntValue("AE_FRAME_STATS_DENSITY_TRACE", &ok);
    return ok ? qMax(0, value) : 0;
}

float traceArea(const gl::Vector2& a, const gl::Vector2& b, const gl::Vector2& c) {
    return qAbs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) * 0.5f;
}

float traceArea(const gl::Vector3& a, const gl::Vector3& b, const gl::Vector3& c) {
    return qAbs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) * 0.5f;
}

// one line into `<AE_FRAME_STATS>.trace`; a no-op when no stats file is configured
void traceLine(const QString& aLine) {
    const QString path = QString::fromUtf8(qgetenv("AE_FRAME_STATS"));
    if (path.isEmpty())
        return;
    QFile file(path + QStringLiteral(".trace"));
    if (!file.open(QIODevice::Append | QIODevice::Text))
        return;
    file.write(aLine.toUtf8());
    file.write("\n");
}

gl::Vector3 traceMap(const QMatrix4x4& aMtx, const gl::Vector3& aV) {
    const QVector3D p = aMtx.map(QVector3D(aV.x, aV.y, aV.z));
    return gl::Vector3::make(p.x(), p.y(), p.z());
}

void traceLayer(
    const LayerMesh& aMesh,
    const gl::BufferObject& aPositions,
    int aVtxCount,
    const QMatrix4x4& aInverseTransform,
    const char* aSource,
    int aLayer
) {
    gl::Global::Functions& ggl = gl::Global::functions();
    QVector<gl::Vector3> skinned(aVtxCount);
    ggl.glBindBuffer(GL_ARRAY_BUFFER, aPositions.id());
    ggl.glGetBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(gl::Vector3) * aVtxCount, skinned.data());
    ggl.glBindBuffer(GL_ARRAY_BUFFER, 0);

    // The buffer holds the skinned positions in the layer's CONTENT space (the skinning
    // pass wrote world * skin * inner * position). The shader divides the placement out
    // with uInverseTransform, so the number that belongs on the frame line is the MESH
    // space one: `d_mesh` below. `d_content` is the same triangle before the division, and
    // `det_wi` is the area factor between the two - so a run says at a glance whether the
    // shader and this trace are measuring the same thing, and which of them is wrong.
    const gl::Vector3* rest = aMesh.positions();
    const gl::Vector2* uv = aMesh.texCoords();
    const GLuint* indices = aMesh.indices();
    const int triangles = (int)aMesh.indexCount() / 3;

    struct Worst {
        float densityMesh;
        float densityContent;
        float uvArea;
        float restArea;
        float meshArea;
        float contentArea;
        int triangle;
    };
    enum { kWorstCount = 4 };
    Worst worst[kWorstCount] = {};

    double totalUv = 0.0;
    double sliverUv = 0.0;
    double tailUv = 0.0;
    int sliverCount = 0;
    int tailCount = 0;
    int collapsed = 0;
    int outOfRange = 0;
    float firstUv = 0.0f;
    float firstRest = 0.0f;
    float firstMesh = 0.0f;
    float firstContent = 0.0f;

    for (int t = 0; t < triangles; ++t) {
        const int i0 = (int)indices[t * 3 + 0];
        const int i1 = (int)indices[t * 3 + 1];
        const int i2 = (int)indices[t * 3 + 2];
        if (i0 < 0 || i0 >= aVtxCount || i1 < 0 || i1 >= aVtxCount || i2 < 0 || i2 >= aVtxCount) {
            ++outOfRange;
            continue;
        }
        const float uvArea = traceArea(uv[i0], uv[i1], uv[i2]);
        const float restArea = traceArea(rest[i0], rest[i1], rest[i2]);
        const float contentArea = traceArea(skinned[i0], skinned[i1], skinned[i2]);
        const float meshArea = traceArea(
            traceMap(aInverseTransform, skinned[i0]),
            traceMap(aInverseTransform, skinned[i1]),
            traceMap(aInverseTransform, skinned[i2])
        );
        if (t == 0) {
            firstUv = uvArea;
            firstRest = restArea;
            firstMesh = meshArea;
            firstContent = contentArea;
        }
        totalUv += uvArea;
        if (uvArea < kMinUvArea) {
            ++sliverCount;
            sliverUv += uvArea;
            continue;
        }
        const float densityMesh = uvArea > 0.0f ? meshArea / uvArea : 0.0f;
        const float densityContent = uvArea > 0.0f ? contentArea / uvArea : 0.0f;
        if (!(densityMesh > 0.0f))
            ++collapsed;
        // the tail a mip policy would have to believe: 16x and up
        if (densityMesh > 16.0f) {
            ++tailCount;
            tailUv += uvArea;
        }
        for (int w = 0; w < kWorstCount; ++w) {
            if (densityMesh > worst[w].densityMesh) {
                for (int k = kWorstCount - 1; k > w; --k)
                    worst[k] = worst[k - 1];
                worst[w].densityMesh = densityMesh;
                worst[w].densityContent = densityContent;
                worst[w].uvArea = uvArea;
                worst[w].restArea = restArea;
                worst[w].meshArea = meshArea;
                worst[w].contentArea = contentArea;
                worst[w].triangle = t;
                break;
            }
        }
    }

    // TO A FILE, not qDebug: on a Fedora session Qt's messages go to the journal, and
    // QT_FORCE_STDERR_LOGGING did not get these out (measured). `<AE_FRAME_STATS>.trace` sits next
    // to the frame lines, so one run produces one artifact.
    const double detInv = aInverseTransform.determinant();
    traceLine(
        QStringLiteral(
            "DENSITY TRACE layer=%1 source=%2 verts=%3 tris=%4 total_uv=%5 det_wi=%6 first_uv=%7 "
            "first_rest=%8 first_mesh=%9 first_content=%10 sliver_n=%11 sliver_uv=%12 "
            "tail_n=%13 tail_uv=%14 collapsed=%15 out_of_range=%16"
        )
            .arg(aLayer)
            .arg(aSource)
            .arg(aVtxCount)
            .arg(triangles)
            .arg(totalUv, 0, 'f', 2)
            .arg(detInv != 0.0 ? 1.0 / detInv : 0.0, 0, 'f', 6)
            .arg(firstUv, 0, 'f', 4)
            .arg(firstRest, 0, 'f', 4)
            .arg(firstMesh, 0, 'f', 4)
            .arg(firstContent, 0, 'f', 4)
            .arg(sliverCount)
            .arg(sliverUv, 0, 'f', 4)
            .arg(tailCount)
            .arg(tailUv, 0, 'f', 4)
            .arg(collapsed)
            .arg(outOfRange)
    );
    for (int w = 0; w < kWorstCount; ++w) {
        if (worst[w].uvArea <= 0.0f)
            break;
        traceLine(
            QStringLiteral(
                "DENSITY TRACE layer=%1 worst[%2] tri=%3 uv=%4 rest=%5 mesh=%6 content=%7 "
                "d_mesh=%8 d_content=%9"
            )
                .arg(aLayer).arg(w).arg(worst[w].triangle)
                .arg(worst[w].uvArea, 0, 'f', 4)
                .arg(worst[w].restArea, 0, 'f', 4)
                .arg(worst[w].meshArea, 0, 'f', 4)
                .arg(worst[w].contentArea, 0, 'f', 4)
                .arg(worst[w].densityMesh, 0, 'f', 4)
                .arg(worst[w].densityContent, 0, 'f', 4)
        );
    }
}

//-------------------------------------------------------------------------------------------------
// The GPU side of the histogram: one accumulator for the whole frame (all layers add into
// it), so the frame costs one clear, one draw per layer and one copy, whatever the layer
// count. Built on first use, only when FrameStats is enabled.
//-------------------------------------------------------------------------------------------------
class DensityMeter: private util::NonCopyable {
public:
    static DensityMeter& instance() {
        // Deliberately never destroyed: deleting GL objects from a static destructor runs
        // after the context is gone, and this probe exists only while AE_FRAME_STATS is set.
        static DensityMeter* meter = new DensityMeter();
        return *meter;
    }

    void addMesh(
        gl::FrameStats& aStats,
        const LayerMesh& aMesh,
        const gl::BufferObject& aPositions,
        int aVtxCount,
        const char* aSource,
        const QMatrix4x4& aInverseTransform
    );

private:
    enum BuildState { State_Unbuilt, State_Ready, State_Failed };

    DensityMeter() {}

    void build();
    void advanceFrame(gl::FrameStats& aStats);
    void readBack(gl::FrameStats& aStats);
    void copyOut(bool aDrew);
    void clearTarget();
    void uploadTexCoords(const gl::Vector2* aTexCoords, int aCount);
    void uploadIndices(const GLuint* aIndices, GLsizei aCount);

    std::unique_ptr<QOpenGLShaderProgram> mProgram;
    BuildState mState = State_Unbuilt;
    // Resolved ONCE, at build: a uniform that is not active would make setUniformValue a
    // silent no-op, and an identity uInverseTransform is not a wrong picture, it is a wrong
    // measurement (it reads content space instead of mesh space, which is exactly 1.0 at
    // rest and the layer's scale under an animated transform). So a missing location
    // disables the probe instead of quietly measuring the wrong space.
    int mUniformTransform = -1;
    int mUniformBinBase = -1;
    int mUniformBinStep = -1;
    int mUniformBinCount = -1;
    int mUniformMinUvArea = -1;
    // one R32F texture per attachment: {density sum, density max, s_max sum, s_max max,
    // anisotropy sum, anisotropy max}, all kBinCount pixels wide
    GLuint mTexture[Attach_Count] = {};
    GLuint mFramebuffer = 0;
    GLuint mPbo[2] = {0, 0};
    GLuint mTexCoordBuffer = 0;
    GLsizeiptr mTexCoordBytes = 0;
    GLuint mIndexBuffer = 0;
    GLsizeiptr mIndexBytes = 0;
    // the copy slot that will be filled next, and the one waiting to be mapped
    int mWriteSlot = 0;
    int mReadySlot = -1;
    // the frame this accumulator last drew in, and whether it drew anything
    quint64 mFrame = 0;
    bool mFrameDrew = false;
    // layers already printed by the density trace, and the budget it was given
    int mTraceLayers = 0;
    int mTraceBudget = densityTraceBudget();
    qint64 mTraceFrame = -1;
};

void DensityMeter::build() {
    mState = State_Failed;
    gl::Global::Functions& ggl = gl::Global::functions();

    std::unique_ptr<QOpenGLShaderProgram> program(new QOpenGLShaderProgram());
    bool ok = program->addShaderFromSourceCode(QOpenGLShader::Vertex, kDensityVertexShader);
    ok = ok && program->addShaderFromSourceCode(QOpenGLShader::Geometry, kDensityGeometryShader);
    ok = ok && program->addShaderFromSourceCode(QOpenGLShader::Fragment, kDensityFragmentShader);
    if (ok) {
        program->bindAttributeLocation("inPosition", kPositionAttrib);
        program->bindAttributeLocation("inTexCoord", kTexCoordAttrib);
        ok = program->link();
    }
    if (!ok) {
        // a diagnostic that cannot compile must not take the renderer down with it, and the
        // reason has to land in the artifact: Qt's log does not reach stderr on every
        // session (measured), and a silent probe reads as zeros
        traceLine(QStringLiteral("DENSITY PROBE disabled: %1").arg(program->log()));
        return;
    }
    mProgram = std::move(program);

    mUniformTransform = mProgram->uniformLocation("uInverseTransform");
    mUniformBinBase = mProgram->uniformLocation("uBinBase");
    mUniformBinStep = mProgram->uniformLocation("uBinStep");
    mUniformBinCount = mProgram->uniformLocation("uBinCount");
    mUniformMinUvArea = mProgram->uniformLocation("uMinUvArea");
    if (mUniformTransform < 0 || mUniformBinBase < 0 || mUniformBinStep < 0 || mUniformBinCount < 0 ||
        mUniformMinUvArea < 0) {
        traceLine(
            QStringLiteral("DENSITY PROBE disabled: a uniform of the density program is not active "
                           "(transform=%1 base=%2 step=%3 count=%4 minuv=%5)")
                .arg(mUniformTransform).arg(mUniformBinBase).arg(mUniformBinStep)
                .arg(mUniformBinCount).arg(mUniformMinUvArea)
        );
        return;
    }

    // the target: R32F, one pixel per bin, one attachment per measured sum and maximum
    GLint previousTexture = 0;
    ggl.glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    ggl.glGenTextures(Attach_Count, mTexture);
    ggl.glGenFramebuffers(1, &mFramebuffer);
    ggl.glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
    for (int i = 0; i < Attach_Count; ++i) {
        ggl.glBindTexture(GL_TEXTURE_2D, mTexture[i]);
        ggl.glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, kBinCount, 1, 0, GL_RED, GL_FLOAT, nullptr);
        ggl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        ggl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        ggl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        ggl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        ggl.glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + (GLenum)i, GL_TEXTURE_2D, mTexture[i], 0
        );
    }
    ggl.glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);

    GLenum drawBuffers[Attach_Count] = {};
    for (int i = 0; i < Attach_Count; ++i)
        drawBuffers[i] = GL_COLOR_ATTACHMENT0 + (GLenum)i;
    ggl.glDrawBuffers(Attach_Count, drawBuffers);
    const bool complete = ggl.glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    ggl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!complete) {
        traceLine(QStringLiteral("DENSITY PROBE disabled: the R32F target is incomplete"));
        return;
    }

    ggl.glGenBuffers(2, mPbo);
    for (int i = 0; i < 2; ++i) {
        ggl.glBindBuffer(GL_PIXEL_PACK_BUFFER, mPbo[i]);
        ggl.glBufferData(GL_PIXEL_PACK_BUFFER, kReadbackBytes, nullptr, GL_STREAM_READ);
    }
    ggl.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    // a fresh target holds no frame yet, and an uninitialized R32F target would be added to
    clearTarget();

    mState = State_Ready;
}

void DensityMeter::uploadTexCoords(const gl::Vector2* aTexCoords, int aCount) {
    gl::Global::Functions& ggl = gl::Global::functions();
    const GLsizeiptr bytes = sizeof(gl::Vector2) * aCount;
    if (mTexCoordBuffer == 0)
        ggl.glGenBuffers(1, &mTexCoordBuffer);
    ggl.glBindBuffer(GL_ARRAY_BUFFER, mTexCoordBuffer);
    if (bytes > mTexCoordBytes) {
        // grows to the largest mesh seen and stays there: no per-frame allocation
        ggl.glBufferData(GL_ARRAY_BUFFER, bytes, aTexCoords, GL_STREAM_DRAW);
        mTexCoordBytes = bytes;
    } else {
        ggl.glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, aTexCoords);
    }
}

void DensityMeter::uploadIndices(const GLuint* aIndices, GLsizei aCount) {
    gl::Global::Functions& ggl = gl::Global::functions();
    const GLsizeiptr bytes = sizeof(GLuint) * aCount;
    if (mIndexBuffer == 0)
        ggl.glGenBuffers(1, &mIndexBuffer);
    ggl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mIndexBuffer);
    if (bytes > mIndexBytes) {
        ggl.glBufferData(GL_ELEMENT_ARRAY_BUFFER, bytes, aIndices, GL_STREAM_DRAW);
        mIndexBytes = bytes;
    } else {
        ggl.glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, bytes, aIndices);
    }
}

void DensityMeter::clearTarget() {
    gl::Global::Functions& ggl = gl::Global::functions();
    // glClear obeys the scissor test, and the caller's scissor is not ours
    ggl.glDisable(GL_SCISSOR_TEST);
    ggl.glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
    ggl.glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    ggl.glClear(GL_COLOR_BUFFER_BIT);
}

void DensityMeter::copyOut(bool aDrew) {
    gl::Global::Functions& ggl = gl::Global::functions();
    ggl.glBindBuffer(GL_PIXEL_PACK_BUFFER, mPbo[mWriteSlot]);
    if (aDrew) {
        // a DMA out of the target: the GPU performs it in order, the CPU does not wait.
        // Each attachment lands in its own kBinBytes block of the packed buffer.
        ggl.glBindFramebuffer(GL_READ_FRAMEBUFFER, mFramebuffer);
        for (int i = 0; i < Attach_Count; ++i) {
            ggl.glReadBuffer(GL_COLOR_ATTACHMENT0 + (GLenum)i);
            ggl.glReadPixels(
                0, 0, kBinCount, 1, GL_RED, GL_FLOAT, (GLvoid*)(kBinBytes * i)
            );
        }
    } else {
        // the frame that just ended drew no triangles: report zeros rather than repeat the
        // previous frame's numbers as if they were this one's
        static const float kZeros[kBinCount * Attach_Count] = {};
        ggl.glBufferSubData(GL_PIXEL_PACK_BUFFER, 0, kReadbackBytes, kZeros);
    }
    ggl.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    mReadySlot = mWriteSlot;
    mWriteSlot = 1 - mWriteSlot;
}

void DensityMeter::readBack(gl::FrameStats& aStats) {
    if (mReadySlot < 0)
        return;

    gl::Global::Functions& ggl = gl::Global::functions();
    ggl.glBindBuffer(GL_PIXEL_PACK_BUFFER, mPbo[mReadySlot]);
    // The copy this maps was issued a frame ago, so the implicit sync has a frame of slack
    // and does not wait on the GPU. GL_MAP_UNSYNCHRONIZED_BIT would skip the wait outright
    // but leaves the contents undefined, and a torn histogram is not worth that margin.
    void* data = ggl.glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, kReadbackBytes, GL_MAP_READ_BIT);
    if (data) {
        const float* values = static_cast<const float*>(data);
        auto bins = [&](Attachment aAttachment) -> const float* {
            return values + kBinCount * (int)aAttachment;
        };
        float maxDensity = 0.0f;
        float maxSmax = 0.0f;
        float maxAniso = 0.0f;
        for (int i = 0; i < kBinCount; ++i) {
            maxDensity = qMax(maxDensity, bins(Attach_DensityMax)[i]);
            maxSmax = qMax(maxSmax, bins(Attach_SmaxMax)[i]);
            maxAniso = qMax(maxAniso, bins(Attach_AnisoMax)[i]);
        }
        aStats.densityHistogram(bins(Attach_DensityWeight), maxDensity);
        aStats.anisotropyHistogram(
            bins(Attach_SmaxWeight), maxSmax, bins(Attach_AnisoWeight), maxAniso
        );
        ggl.glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    ggl.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    mReadySlot = -1;
}

void DensityMeter::advanceFrame(gl::FrameStats& aStats) {
    const quint64 frame = aStats.frameIndex();
    if (mFrame == frame)
        return;

    readBack(aStats);
    // mFrame's accumulation is the one just completed, and it is only this frame's
    // predecessor when nothing was skipped in between
    copyOut(mFrame + 1 == frame && mFrameDrew);
    clearTarget();

    mFrame = frame;
    mFrameDrew = false;
}

void DensityMeter::addMesh(
    gl::FrameStats& aStats,
    const LayerMesh& aMesh,
    const gl::BufferObject& aPositions,
    int aVtxCount,
    const char* aSource,
    const QMatrix4x4& aInverseTransform
) {
    if (mState == State_Unbuilt)
        build();
    if (mState != State_Ready)
        return;

    gl::Global::Functions& ggl = gl::Global::functions();

    // this pass is an interlude inside another pass: the target and the viewport it
    // borrowed have to come back exactly as they were
    GLint viewport[4] = {0, 0, 0, 0};
    ggl.glGetIntegerv(GL_VIEWPORT, viewport);
    GLint drawFramebuffer = 0;
    GLint readFramebuffer = 0;
    ggl.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
    ggl.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);

    // the frame boundary is taken even when this mesh contributes nothing, so the copy and
    // the clear stay once per frame
    advanceFrame(aStats);

    const GLsizei indexCount = aMesh.indexCount();
    const GLuint* indices = aMesh.indices();
    const gl::Vector2* texCoords = aMesh.texCoords();

    if (aVtxCount > 0 && indexCount >= 3 && indices && texCoords) {
        ggl.glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
        ggl.glViewport(0, 0, kBinCount, 1);

        // callGL's resetRenderState left these at the engine's baseline; this pass puts
        // them back there before it returns
        ggl.glDisable(GL_DEPTH_TEST);
        ggl.glDisable(GL_CULL_FACE);
        ggl.glDisable(GL_SCISSOR_TEST);
        ggl.glDisable(GL_RASTERIZER_DISCARD);
        ggl.glEnable(GL_PROGRAM_POINT_SIZE);
        ggl.glEnable(GL_BLEND);
        // two attachments per distribution: the uv-area sum (additive) and the maximum
        // (GL_MAX, which ignores the factors)
        for (int i = 0; i < Attach_Count; ++i) {
            const bool isMax =
                i == Attach_DensityMax || i == Attach_SmaxMax || i == Attach_AnisoMax;
            ggl.glBlendEquationi(i, isMax ? GL_MAX : GL_FUNC_ADD);
            ggl.glBlendFunci(i, GL_ONE, GL_ONE);
        }

        mProgram->bind();
        // by location: the names were resolved once at build, where a missing one disables
        // the probe (see mUniformTransform)
        mProgram->setUniformValue(mUniformTransform, aInverseTransform);
        mProgram->setUniformValue(mUniformBinBase, gl::FrameStats::kDensityBinBase);
        mProgram->setUniformValue(mUniformBinStep, gl::FrameStats::kDensityBinStep);
        mProgram->setUniformValue(mUniformBinCount, (GLint)kBinCount);
        mProgram->setUniformValue(mUniformMinUvArea, kMinUvArea);

        // the skinned positions the pass just wrote, and the mesh's own uvs
        ggl.glBindBuffer(GL_ARRAY_BUFFER, aPositions.id());
        ggl.glEnableVertexAttribArray(kPositionAttrib);
        ggl.glVertexAttribPointer(kPositionAttrib, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

        uploadTexCoords(texCoords, aVtxCount);
        ggl.glBindBuffer(GL_ARRAY_BUFFER, mTexCoordBuffer);
        ggl.glEnableVertexAttribArray(kTexCoordAttrib);
        ggl.glVertexAttribPointer(kTexCoordAttrib, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

        uploadIndices(indices, indexCount);
        ggl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mIndexBuffer);
        ggl.glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);

        ggl.glDisableVertexAttribArray(kPositionAttrib);
        ggl.glDisableVertexAttribArray(kTexCoordAttrib);
        ggl.glBindBuffer(GL_ARRAY_BUFFER, 0);
        ggl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        mProgram->release();

        ggl.glDisable(GL_PROGRAM_POINT_SIZE);
        ggl.glDisable(GL_BLEND);
        ggl.glBlendEquation(GL_FUNC_ADD);
        ggl.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        mFrameDrew = true;

        // after the draw, so the positions this prints are the ones just measured. PER FRAME:
        // the load's first layers are undeformed, and the interesting case is playback, where the
        // pose changes every frame - a run-long budget spent itself before anything moved.
        const qint64 frameIndex = qint64(aStats.frameIndex());
        if (frameIndex != mTraceFrame) {
            mTraceFrame = frameIndex;
            mTraceLayers = 0;
        }
        if (mTraceLayers < mTraceBudget)
            traceLayer(aMesh, aPositions, aVtxCount, aInverseTransform, aSource, mTraceLayers++);
    }

    ggl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)drawFramebuffer);
    ggl.glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)readFramebuffer);
    ggl.glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

} // namespace

//-------------------------------------------------------------------------------------------------
MeshTransformer::MeshTransformer(const QString& aShaderPath):
    mResource(*(new MeshTransformerResource())), mResourceOwns(true), mOutPositions(), mOutXArrows(), mOutYArrows(),
    mPaletteBuffer() {
    mResource.setup(aShaderPath);
}

MeshTransformer::MeshTransformer(MeshTransformerResource& aResource):
    mResource(aResource), mResourceOwns(false), mOutPositions(), mOutXArrows(), mOutYArrows(), mPaletteBuffer() {}

MeshTransformer::~MeshTransformer() {
    if (mResourceOwns) {
        delete &mResource;
    }
}

void MeshTransformer::callGL(
    const TimeKeyExpans& aExpans,
    LayerMesh::MeshBuffer& aMeshBuffer,
    const QVector2D& aOriginOffset,
    util::ArrayBlock<const gl::Vector3> aPositions,
    bool aNonPosed,
    bool aUseInfluence
) {
    // THE PASS COUNTER: this runs for every layer on every painted frame (see FrameStats.h)
    gl::FrameStats::instance().meshPass();

    XC_ASSERT(aPositions);

    auto& buffer = aMeshBuffer;
    const int outCount = buffer.vtxCount;
    mOutPositions = &buffer.outPositions;
    mOutXArrows = &buffer.outXArrows;
    mOutYArrows = &buffer.outYArrows;

    if (aPositions.count() <= 0)
        return;

    QMatrix4x4 worldMatrix;
    QMatrix4x4 innerMatrix;

    const BoneInfluenceMap* influence = aExpans.bone().influenceMap();
    bool useInfluence = aUseInfluence && influence && !aNonPosed;
    const bool useDualQuaternion = true;
    BoneInfluenceMap::Accessor inflData;

    const int vtxCount = aPositions.count();

    if (useInfluence) {
        inflData = influence->accessor();
        XC_MSG_ASSERT(influence->vertexCount() == vtxCount, "%d, %d", vtxCount, influence->vertexCount());
    }

    if ((!aNonPosed && aExpans.bone().isAffectedByBinding()) || useInfluence) {
        worldMatrix = aExpans.bone().outerMatrix();
        innerMatrix = aExpans.bone().innerMatrix();
        innerMatrix.translate(aOriginOffset.x(), aOriginOffset.y());
    } else {
        worldMatrix = aExpans.srt().worldCSRTMatrix();
        worldMatrix.translate(aOriginOffset.x(), aOriginOffset.y());
    }

    // The map stores only the influence slots its busiest vertex uses; the
    // shader is compiled for that count (1..7 sparse, 8 dense).
    const int influenceSlots = useInfluence ? inflData.slotCount() : 1;
    XC_ASSERT(influenceSlots >= 1 && influenceSlots <= BoneInfluenceMap::kBonePerVtxMaxAll);

    // The palette's required extent is the LARGER of the palette and the map's
    // bone count. A palette can be CLEARED while the map still addresses its
    // bones (no pose key at this frame); those bones are in rest, so they must
    // read identity. The old fixed array supplied that as an identity tail;
    // with a palette sized to its used bones, the tail has to be explicit.
    const PosePalette& palette = aExpans.posePalette();
    const int mapBones = useInfluence ? influence->boneCount() : 0;
    const int requiredBones = std::max(palette.usedCount(), mapBones);

    gl::Global::Functions& ggl = gl::Global::functions();
    gl::EasyShaderProgram& program = mResource.program(useInfluence, useDualQuaternion, influenceSlots);

    gl::Util::resetRenderState();
    ggl.glEnable(GL_RASTERIZER_DISCARD);
    {
        program.bind();

        program.setAttributeArray("inPosition", aPositions.array(), vtxCount);
        program.setUniformValue("uInnerMatrix", innerMatrix);
        program.setUniformValue("uWorldMatrix", worldMatrix);

        if (useInfluence) {
            if (inflData.dense()) {
                // DENSE: two ivec4/vec4 sets, the layout used at the eight-slot cap.
                constexpr int kTuple = BoneInfluenceMap::kBonePerVtxMaxEach;
                program.setAttributeArray("inBoneIndex0", inflData.denseIndices(0), vtxCount, kTuple);
                program.setAttributeArray("inBoneWeight0", inflData.denseWeights(0), vtxCount, kTuple);
                program.setAttributeArray("inBoneIndex1", inflData.denseIndices(1), vtxCount, kTuple);
                program.setAttributeArray("inBoneWeight1", inflData.denseWeights(1), vtxCount, kTuple);
            } else {
                // SPARSE: one scalar index and (above one influence) one scalar
                // weight per slot. A one-slot map has no weight array: the
                // shader uses the exact 1 the old dense form stored.
                static const char* const kIndexNames[7] = {
                    "inBoneIndex0", "inBoneIndex1", "inBoneIndex2", "inBoneIndex3",
                    "inBoneIndex4", "inBoneIndex5", "inBoneIndex6",
                };
                static const char* const kWeightNames[7] = {
                    "inBoneWeight0", "inBoneWeight1", "inBoneWeight2", "inBoneWeight3",
                    "inBoneWeight4", "inBoneWeight5", "inBoneWeight6",
                };
                for (int slot = 0; slot < influenceSlots; ++slot) {
                    program.setAttributeArray(kIndexNames[slot], inflData.indices(slot), vtxCount, 1);
                    if (influenceSlots >= 2) {
                        program.setAttributeArray(kWeightNames[slot], inflData.weights(slot), vtxCount, 1);
                    }
                }
            }

            if (useDualQuaternion) {
                const auto dualQuats = palette.dualQuaternions();

                // Pad [used, max(used, map bones)) with identity: a bone the
                // pose does not transform. The common case has no gap and
                // uploads the palette's own storage.
                const int uploadCount = std::max(1, requiredBones);
                const PosePalette::DualQuaternion* uploadData = dualQuats.array();
                if (uploadCount > dualQuats.count()) {
                    mPaletteUpload.resize(size_t(uploadCount));
                    std::copy(dualQuats.array(), dualQuats.array() + dualQuats.count(), mPaletteUpload.begin());
                    std::fill(
                        mPaletteUpload.begin() + dualQuats.count(), mPaletteUpload.end(),
                        PosePalette::identityDualQuaternion()
                    );
                    uploadData = mPaletteUpload.data();
                }

                if (mPaletteBuffer.isNull()) {
                    mPaletteBuffer.reset(new gl::BufferObject(GL_UNIFORM_BUFFER));
                    // Back the WHOLE declared block once (MAX_BONES * 2 vec4), so
                    // uploading only the required prefix can never leave the
                    // shader's block smaller than its declaration.
                    mPaletteBuffer->resetRawData(
                        sizeof(gl::Vector4), nullptr, int(PosePalette::kMaxCount) * 2, GL_DYNAMIC_DRAW
                    );
                }
                const GLsizeiptr bytes =
                    GLsizeiptr(uploadCount) * 2 * GLsizeiptr(sizeof(gl::Vector4));
                mPaletteBuffer->bind();
                ggl.glBufferSubData(GL_UNIFORM_BUFFER, 0, bytes, uploadData->data());
                ggl.glBindBufferBase(
                    GL_UNIFORM_BUFFER, MeshTransformerResource::kPaletteUniformBinding, mPaletteBuffer->id()
                );
                mPaletteBuffer->release();
            } else {
                // The unselected LBS variation is declared at 32 matrices (see
                // the shader); never upload past it.
                const auto matrices = palette.matrices();
                program.setUniformValueArray(
                    "uBoneMatrix", matrices.array(), std::min(matrices.count(), 32)
                );
            }
        }

        ggl.glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, buffer.workPositions.id());
        ggl.glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 1, buffer.workXArrows.id());
        ggl.glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 2, buffer.workYArrows.id());

        ggl.glBeginTransformFeedback(GL_POINTS);
        ggl.glDrawArrays(GL_POINTS, 0, aPositions.count());
        ggl.glEndTransformFeedback();

        program.release();
    }
    ggl.glDisable(GL_RASTERIZER_DISCARD);
    GL_CHECK_ERROR();

    // copy to array buffer
    {
        ggl.glBindBuffer(GL_ARRAY_BUFFER, buffer.outPositions.id());
        ggl.glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, buffer.workPositions.id());
        ggl.glCopyBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER, GL_ARRAY_BUFFER, 0, 0, sizeof(gl::Vector3) * outCount);

        ggl.glBindBuffer(GL_ARRAY_BUFFER, buffer.outXArrows.id());
        ggl.glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, buffer.workXArrows.id());
        ggl.glCopyBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER, GL_ARRAY_BUFFER, 0, 0, sizeof(gl::Vector3) * outCount);

        ggl.glBindBuffer(GL_ARRAY_BUFFER, buffer.outYArrows.id());
        ggl.glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, buffer.workYArrows.id());
        ggl.glCopyBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER, GL_ARRAY_BUFFER, 0, 0, sizeof(gl::Vector3) * outCount);

        ggl.glBindBuffer(GL_ARRAY_BUFFER, 0);
        ggl.glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, 0);
    }

    // THE DENSITY PASS: what this deformation did to the texels, measured every frame
    // because playback changes both the blend and the deformation on every frame, and
    // measured here because the deformed vertices are already on the GPU. It needs the
    // area of each triangle against its uv area, so it re-draws the same indices with the
    // skinned positions; see DensityMeter above. Off unless AE_FRAME_STATS is set: no
    // program is built, nothing is drawn and nothing is read back.
    //
    // ORDERING: the pass fetches from `outPositions` - the very buffer the layer itself is
    // drawn from (LayerNode::render* binds MeshTransformer::positions()) - and it does so
    // after glEndTransformFeedback() and after the glCopyBufferSubData above, in the same
    // command stream. GL executes a context's commands in order, so the fetch sees this
    // frame's skinning; that is the same guarantee the layer's own draw relies on one call
    // later, and it is why a still frame and a churning frame cannot disagree here. A
    // glMemoryBarrier would neither help nor be available: it exists from GL 4.2
    // (ARB_shader_image_load_store) and the engine binds a 4.0 core profile, and what it
    // orders is SHADER writes against later reads the driver cannot see - the
    // transform-feedback output buffer, which this pass deliberately does not read. The
    // copy is a plain buffer-to-buffer blit, so the attribute fetch is ordered by the
    // command stream alone.
    if (gl::FrameStats::instance().enabled()) {
        bool invertible = false;
        const QMatrix4x4 inverseTransform = (worldMatrix * innerMatrix).inverted(&invertible);
        if (invertible) {
            const char* source = "none";
            if (const LayerMesh* mesh = densityMesh(aExpans, outCount, &source)) {
                DensityMeter::instance().addMesh(
                    gl::FrameStats::instance(), *mesh, buffer.outPositions, outCount, source, inverseTransform
                );
            }
        }
    }

    // No glFlush here: GL executes a context's commands in order, and the frame
    // submits once at the end (MainDisplayWidget::paintGL). One flush per mesh only
    // forced an extra driver submission per layer.

    // No per-layer glGetError: it is a pipeline question, measured ~23 us per call on radeonsi -
    // 0.77 ms of a 4.8 ms frame across 33 layers. The frame-level GL_CHECK_ERROR() in the paint
    // path still reports every error.
}

} // namespace core
