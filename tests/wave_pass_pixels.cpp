// Pixel gate for the WAVE DEFORM PASS (core::FilterFrame::waveTexture,
// src/core/FilterFrame.cpp's kWaveFrag) on a real GL context.
//
// Why a pixel gate and not just the CPU-side checks in tests/wave_deform.cpp:
// the pass is a fragment shader whose contract lives in the field's maths and in
// its SAMPLING, and both are invisible to any test that only compares matrices.
// Every assertion below is read back from an offscreen framebuffer, so a sign
// error, a wrong uniform, a mode swap or a missing clamp fails here and nowhere
// else.
//
// The harness renders a known "composite" (a texture it uploads) through
// `waveTexture` and measures the result:
//
//   [1] IDENTITY: strength 0 copies the source exactly. The fullscreen quad
//       samples texel centres, so a plain copy must be bit-identical; anything
//       else means the pass's own coordinates (vDestCoord -> uv) are off by half
//       a pixel and every deformation below would be measured against a shifted
//       source.
//   [2] THE FIELD IS A SINE ACROSS THE TRAVEL DIRECTION (Sway): an opaque band
//       whose lower edge sits at a known row moves by `strength * sin(2*pi*x /
//       wavelength)`, measured per column at four phases (0, +1, 0, -1).
//   [3] THE GRADIENT AXIS (Ripple): the same y-only pattern is UNCHANGED by a
//       ripple travelling along x (its displacement is along the direction of
//       travel, i.e. horizontal), which is the property that separates the two
//       deform modes.
//   [4] THE CONTENT CLAMP: a sample may not leave the node's content rectangle.
//       With the clamp, no pixel that is OUTSIDE the rectangle in the source can
//       appear inside it (the silhouette cannot tear open and pull the
//       background in); with the clamp disabled the same wave does exactly that,
//       which is what proves the clamp is doing the work rather than the pattern.
//   [5] EVERY FIELD REACHES THE SHADER: changing one field at a time changes the
//       frame. A uniform that is set under a name the shader does not use (or not
//       set at all) is invisible otherwise - the pass would still "work".
//
// Run under xvfb-run (it creates a real core-profile context and its own FBO):
//   xvfb-run -a build/tests/Debug/wave_pass_pixels
#include <QApplication>
#include <QImage>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QOpenGLFunctions_4_0_Core>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLWidget>
#include <QSize>
#include <QSurfaceFormat>
#include <QVector2D>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "XCAssert.h"
#include "core/FilterFrame.h"
#include "core/FilterMask.h"
#include "core/WavePass.h"
#include "gl/DeviceInfo.h"
#include "gl/Global.h"
#include "gl/Texture.h"
#include "gl/VertexArrayObject.h"

// core::Project.cpp's audio sidecar loader; nothing here loads a project.
#include "gui/AudioPlaybackWidget.h"
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

namespace {

class HarnessAssertHandler: public XCAssertHandler {
public:
    // A GL error or an engine invariant firing means the readback no longer
    // describes the pass; stop rather than print a measurement of something else.
    void failure() const override { std::abort(); }
};
class HarnessErrorHandler: public XCErrorHandler {
public:
    void critical(const QString& aText, const QString& aInfo, const QString& aDetail) const override {
        std::fprintf(stderr, "XC_FATAL %s %s %s\n", qPrintable(aText), qPrintable(aInfo), qPrintable(aDetail));
        std::abort();
    }
};
HarnessAssertHandler gAssertHandler;
HarnessErrorHandler gErrorHandler;

int failures = 0;
void check(bool aOk, const char* aWhat) {
    if (!aOk) {
        std::printf("FAIL  %s\n", aWhat);
        ++failures;
    }
}

constexpr int kWidth = 300;
constexpr int kHeight = 300;
// The layer's content rectangle inside the composite: a 200x100 layer placed at
// (50,100). Its content pixels are the rectangle's LOCAL coordinates, so the
// pass's mapping is a translation and every expected pixel is exact.
constexpr int kLayerX = 50;
constexpr int kLayerY = 100;
constexpr int kLayerW = 200;
constexpr int kLayerH = 100;
// The opaque band inside the content, in CONTENT pixels: rows 25..74 (so a
// +-10px displacement stays inside the rectangle and the clamp is not what the
// sine measurement sees).
constexpr int kBandTop = 25;
constexpr int kBandHeight = 50;

// Colours (premultiplied, as the pass requires of its input).
constexpr int kBandRed = 255;
constexpr int kBackgroundGreen = 255;

// Builds the "composite": opaque green EVERYWHERE outside the layer's content
// rectangle (the stand-in for everything the layer is not - what a sample that
// left the content would pull in), a fully transparent rectangle inside it, and
// the opaque red band inside that. Content and background are therefore
// distinguishable colours, which is what makes the clamp assertion measurable.
QImage makeSource() {
    QImage image(kWidth, kHeight, QImage::Format_RGBA8888);
    image.fill(QColor(0, kBackgroundGreen, 0, 255));
    for (int y = kLayerY; y < kLayerY + kLayerH; ++y) {
        uchar* line = image.scanLine(y); // texture row 0 is t = 0, i.e. composite y = 0
        for (int x = kLayerX; x < kLayerX + kLayerW; ++x) {
            line[4 * x + 0] = 0;
            line[4 * x + 1] = 0;
            line[4 * x + 2] = 0;
            line[4 * x + 3] = 0;
        }
    }
    for (int y = kLayerY + kBandTop; y < kLayerY + kBandTop + kBandHeight; ++y) {
        uchar* line = image.scanLine(y);
        for (int x = kLayerX; x < kLayerX + kLayerW; ++x) {
            line[4 * x + 0] = uchar(kBandRed);
            line[4 * x + 1] = 0;
            line[4 * x + 2] = 0;
            line[4 * x + 3] = 255;
        }
    }
    return image;
}

// The pass's default parameters: content pixels ARE local pixels, translated into
// the composite by (kLayerX, kLayerY), with the content rectangle published as the
// sample clamp.
core::WavePassParams baseParams() {
    core::WavePassParams params;
    QMatrix4x4 toScreen;
    toScreen.translate(float(kLayerX), float(kLayerY));
    params.layerToScreen = toScreen;
    bool invertible = false;
    params.screenToLayer = toScreen.inverted(&invertible);
    XC_ASSERT(invertible);
    // The texel-centre range of the content (see core::WavePassParams): a sample
    // clamped to the outline would land on the boundary between the last content
    // texel and the background.
    params.clampMin = QVector2D(0.5f, 0.5f);
    params.clampMax = QVector2D(float(kLayerW) - 0.5f, float(kLayerH) - 0.5f);

    params.strength = 0.0f;
    params.wavelength = 200.0f;
    params.angleDeg = 0.0f; // travels +x, so it displaces across y
    params.phase = 0.0f;
    params.mix = 0.0f;
    params.speed = 0.0f;
    params.seconds = 0.0f;
    params.crest = 1.0f;
    params.dualAngleDeg = 60.0f;
    params.dualScale = 0.6f;
    params.deform = core::WaveKey::Deform_Sway;
    return params;
}

// One FilterFrame + its source, bound to whatever context is current.
class Scene {
public:
    explicit Scene(const QImage& aSource): mFrame(), mSource(), mSlot(-1), mBlurSlot(-1), mMixSlot(-1) {
        mFrame.resize(QSize(kWidth, kHeight));
        mSource.create(QSize(kWidth, kHeight), aSource.constBits());
        mSource.setFilter(GL_LINEAR);
        mSource.setWrap(GL_CLAMP_TO_EDGE);
        mSlot = mFrame.acquire();
        mBlurSlot = mFrame.acquire();
        mMixSlot = mFrame.acquire();
    }
    ~Scene() {
        mFrame.release(mMixSlot);
        mFrame.release(mBlurSlot);
        mFrame.release(mSlot);
    }

    GLuint slotTexture(int aSlot) { return mFrame.texture(aSlot); }

    // Runs one pass and returns the composite in GL ROW ORDER (row 0 is the
    // bottom, i.e. composite y = 0) - the order the pass itself works in, so an
    // expectation is written in the same coordinates the shader uses.
    //
    // renderHsv is the HSV pass with an optional amount mask (identity content
    // mapping: this harness's "content pixels" ARE composite pixels).
    QImage renderHsv(const QList<int>& aHsv, const core::MaskInput& aMask, const QMatrix4x4& aScreenToLayer) {
        mFrame.hsvTexture(mSlot, mSource.id(), aHsv, aMask, aScreenToLayer);
        QImage image(kWidth, kHeight, QImage::Format_RGBA8888);
        mFrame.bind(mSlot);
        gl::Global::functions().glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
        return image;
    }

    QImage render(const core::WavePassParams& aParams) {
        mFrame.waveTexture(mSlot, mSource.id(), aParams);
        QImage image(kWidth, kHeight, QImage::Format_RGBA8888);
        mFrame.bind(mSlot);
        gl::Global::functions().glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
        return image;
    }

    // what a node does for a MASKED blur: blur into a second slot (so the sharp
    // content survives), then mix the two by the field
    QImage renderMaskedBlur(float aRadius, const core::MaskInput& aMask) {
        mFrame.blurTexture(
            mBlurSlot, mFrame.texture(mSlot), QVector2D(1.0f, 0.0f), aRadius, QVector2D(0.0f, 1.0f), aRadius
        );
        // diagnostic: does the blur itself change anything?
        {
            QImage a(kWidth, kHeight, QImage::Format_RGBA8888);
            mFrame.bind(mBlurSlot);
            gl::Global::functions().glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, a.bits());
            QImage b(kWidth, kHeight, QImage::Format_RGBA8888);
            mFrame.bind(mSlot);
            gl::Global::functions().glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, b.bits());
            int diff = 0;
            for (int y = 0; y < kHeight; ++y)
                for (int x = 0; x < kWidth; ++x)
                    if (a.pixelColor(x, y) != b.pixelColor(x, y))
                        ++diff;
            std::printf("MEASURED blurSelfDiff=%d\n", diff);
        }
        mFrame.mixTexture(mMixSlot, mFrame.texture(mSlot), mFrame.texture(mBlurSlot), aMask, QMatrix4x4());
        QImage image(kWidth, kHeight, QImage::Format_RGBA8888);
        mFrame.bind(mMixSlot);
        gl::Global::functions().glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
        return image;
    }

private:
    core::FilterFrame mFrame;
    gl::Texture mSource;
    int mSlot;
    int mBlurSlot;
    int mMixSlot;
};

// QImage's RGBA8888 bytes are NOT the memory layout of QRgb (0xAARRGGBB), so the
// pixels are read through the image's own format rather than reinterpreted.
QColor pixel(const QImage& aImage, int aX, int aY) { return aImage.pixelColor(aX, aY); }

int differingPixels(const QImage& a, const QImage& b, const QRect& aRect) {
    int count = 0;
    for (int y = aRect.top(); y <= aRect.bottom(); ++y) {
        for (int x = aRect.left(); x <= aRect.right(); ++x) {
            if (pixel(a, x, y) != pixel(b, x, y))
                ++count;
        }
    }
    return count;
}

bool isBand(const QColor& aColor) { return aColor.green() < 32 && aColor.red() > 200; }
bool isBackground(const QColor& aColor) { return aColor.green() > 200 && aColor.red() < 32; }

// The lowest row of the opaque band in one column. The image is in GL row order,
// so the row index IS the composite's y.
int lowestBandRow(const QImage& aImage, int aX) {
    for (int y = 0; y < kHeight; ++y) {
        if (isBand(pixel(aImage, aX, y)))
            return y;
    }
    return -1;
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setVersion(gl::Global::kVersion.first, gl::Global::kVersion.second);
    QSurfaceFormat::setDefaultFormat(fmt);

    QOpenGLWidget glWidget;
    glWidget.resize(kWidth, kHeight);
    glWidget.show();
    glWidget.makeCurrent();
    if (!glWidget.context()) {
        std::fprintf(stderr, "FAIL: no GL context could be created\n");
        return 2;
    }
    auto funcs = QOpenGLVersionFunctionsFactory::get<gl::Global::Functions>(glWidget.context());
    if (!funcs || !funcs->initializeOpenGLFunctions()) {
        std::fprintf(stderr, "FAIL: no GL function table for the widget's context\n");
        return 2;
    }
    gl::Global::setContext(glWidget);
    gl::Global::setFunctions(*funcs);

    gl::DeviceInfo dev;
    dev.vender = "harness";
    dev.renderer = "harness";
    dev.version = "5";
    dev.maxTextureSize = 16384;
    dev.maxRenderBufferSize = 16384;
    gl::DeviceInfo::setInstance(&dev);

    std::printf(
        "INFO GL renderer=%s version=%s\n",
        reinterpret_cast<const char*>(funcs->glGetString(GL_RENDERER)),
        reinterpret_cast<const char*>(funcs->glGetString(GL_VERSION))
    );

    // A pending error would be turned into an abort by GL_CHECK_ERROR inside the
    // first draw, and would read as that draw's fault.
    {
        GLuint e = 0;
        while ((e = funcs->glGetError()) != GL_NO_ERROR) {
        }
    }

    // A core-profile draw needs a bound vertex-array object (the application keeps
    // one bound for the life of its viewport; see MainDisplayWidget::initializeGL).
    gl::VertexArrayObject vao;
    vao.bind();

    const QImage source = makeSource();
    Scene scene(source);

    // The content rectangle, in the same GL row order as the readbacks.
    const QRect contentRect(kLayerX, kLayerY, kLayerW, kLayerH);

    // ------------------------------------------------------------- [1] identity
    {
        const QImage out = scene.render(baseParams());
        const int diff = differingPixels(out, source, QRect(0, 0, kWidth, kHeight));
        std::printf("MEASURED identity differing=%d\n", diff);
        check(diff == 0, "strength 0 copies the source exactly (the pass's own coordinates are exact)");
        if (failures == 0)
            std::printf("[1] identity\n");
    }

    // ------------------------------------------------------- [2] the sway sine
    {
        core::WavePassParams params = baseParams();
        params.strength = 10.0f;
        const QImage out = scene.render(params);

        // opaque iff bandTop <= contentY + strength*sin(...) < bandTop+height, so the
        // band's LOWEST row is kLayerY + kBandTop - d. Measured four phases.
        struct Probe {
            int contentX;
            float sine; // sin(2*pi*x/200)
        };
        const Probe probes[] = {{0, 0.0f}, {50, 1.0f}, {100, 0.0f}, {150, -1.0f}};
        for (const Probe& probe : probes) {
            const int expected = kLayerY + kBandTop - int(std::lround(probe.sine * 10.0f));
            const int measured = lowestBandRow(out, kLayerX + probe.contentX);
            std::printf(
                "MEASURED sway x=%d sine=%+.2f lowestRow=%d expected=%d\n", probe.contentX, probe.sine,
                measured, expected
            );
            check(std::abs(measured - expected) <= 2, "the band's edge follows strength*sin(2*pi*x/wavelength)");
        }
        if (failures == 0)
            std::printf("[2] the field is a sine across the travel direction\n");
    }

    // ---------------------------------------------------- [3] the gradient axis
    {
        // Ripple displaces ALONG the direction of travel. The source has structure
        // in y only and is constant in x inside the rectangle, and a ripple along
        // +x can only sample other columns of that same uniform band - so the
        // frame must not change at all.
        core::WavePassParams params = baseParams();
        params.deform = core::WaveKey::Deform_Ripple;
        params.strength = 10.0f;
        const QImage out = scene.render(params);
        const QImage identity = scene.render(baseParams());
        const int diff = differingPixels(out, identity, contentRect);
        std::printf("MEASURED rippleAlongX differingInContent=%d\n", diff);
        check(diff == 0, "a ripple travelling along x does not move a y-only pattern (its offset is along x)");

        // And the two modes are different deformations: a y-only pattern IS moved
        // by Sway, so the pair above is discriminating rather than vacuous.
        core::WavePassParams sway = baseParams();
        sway.strength = 10.0f;
        const int swayDiff = differingPixels(scene.render(sway), identity, contentRect);
        std::printf("MEASURED swayOnSamePattern differingInContent=%d\n", swayDiff);
        check(swayDiff > 0, "the same pattern IS moved by Sway (the two modes differ)");
        if (failures == 0)
            std::printf("[3] the gradient axis is the travel direction\n");
    }

    // ------------------------------------------------------- [4] content clamp
    {
        // A displacement larger than the distance to the rectangle's edge: without
        // the clamp the sample walks out of the content and pulls the green field
        // in; with it the content's own edge texels are stretched instead.
        core::WavePassParams params = baseParams();
        params.strength = 90.0f;
        const QImage clamped = scene.render(params);

        int leakedWithClamp = 0;
        for (int y = contentRect.top(); y <= contentRect.bottom(); ++y) {
            for (int x = contentRect.left(); x <= contentRect.right(); ++x) {
                if (isBackground(pixel(clamped, x, y)))
                    ++leakedWithClamp;
            }
        }

        core::WavePassParams unclamped = params;
        unclamped.clampMin = QVector2D();
        unclamped.clampMax = QVector2D(); // a degenerate box disables the clamp
        const QImage open = scene.render(unclamped);

        int leakedWithout = 0;
        for (int y = contentRect.top(); y <= contentRect.bottom(); ++y) {
            for (int x = contentRect.left(); x <= contentRect.right(); ++x) {
                if (isBackground(pixel(open, x, y)))
                    ++leakedWithout;
            }
        }

        std::printf(
            "MEASURED clamp leakedWithClamp=%d leakedWithoutClamp=%d\n", leakedWithClamp, leakedWithout
        );
        check(leakedWithClamp == 0, "with the clamp no pixel outside the content rectangle enters it");
        check(leakedWithout > 0, "without the clamp the same wave pulls the background in (the clamp is load-bearing)");
        if (failures == 0)
            std::printf("[4] the sample is clamped into the content rectangle\n");
    }

    // ----------------------------------------------------------- [5] every field
    {
        core::WavePassParams base = baseParams();
        base.strength = 12.0f;
        base.mix = 0.5f;
        base.seconds = 1.0f;
        const QImage reference = scene.render(base);

        struct Field {
            const char* name;
            void (*mutate)(core::WavePassParams&);
        };
        const Field fields[] = {
            {"strength", [](core::WavePassParams& p) { p.strength = 6.0f; }},
            {"wavelength", [](core::WavePassParams& p) { p.wavelength = 120.0f; }},
            {"angle", [](core::WavePassParams& p) { p.angleDeg = 20.0f; }},
            {"phase", [](core::WavePassParams& p) { p.phase = 0.25f; }},
            {"mix", [](core::WavePassParams& p) { p.mix = 0.9f; }},
            {"speed", [](core::WavePassParams& p) { p.speed = 1.5f; }},
            {"crest", [](core::WavePassParams& p) { p.crest = 2.5f; }},
            {"dualAngle", [](core::WavePassParams& p) { p.dualAngleDeg = 10.0f; }},
            {"dualScale", [](core::WavePassParams& p) { p.dualScale = 1.4f; }},
            {"deform", [](core::WavePassParams& p) { p.deform = core::WaveKey::Deform_Ripple; }},
        };
        for (const Field& field : fields) {
            core::WavePassParams params = base;
            field.mutate(params);
            const int diff = differingPixels(scene.render(params), reference, QRect(0, 0, kWidth, kHeight));
            std::printf("MEASURED field=%-10s differing=%d\n", field.name, diff);
            check(diff > 0, "changing a field changes the frame (its uniform reaches the shader)");
        }
        if (failures == 0)
            std::printf("[5] every field reaches the shader\n");
    }

    // ------------------------------------------------------ [6] painted masks
    {
        // An AMOUNT mask scales the displacement per texel, and it is sampled in
        // CONTENT space: paint the left half white and only that half may move. A
        // mask that is ignored (or sampled in screen space) would move both halves
        // or neither.
        core::FilterMask amount;
        amount.reset(QRectF(0.0, 0.0, float(kLayerW), float(kLayerH)), core::filterMaskNeutral(core::FilterMaskChannel_Amount));
        // The field is NEUTRAL (white = the effect applies) and the LEFT half is
        // painted to 0, which is how an artist restricts an effect: the painted half
        // is where it stops applying.
        {
            QImage& field = amount.image();
            check(field.constScanLine(0)[0] == 255, "the amount mask starts neutral");
            for (int y = 0; y < field.height(); ++y) {
                uchar* line = field.scanLine(y);
                for (int x = 0; x < field.width() / 2; ++x)
                    line[x] = 0;
            }
            amount.markDirtyAll();
        }

        core::WavePassParams params = baseParams();
        params.strength = 10.0f;
        params.amountMask = amount.texture();
        params.amountMaskRect = amount.contentRect();
        check(params.amountMask != 0, "the painted mask uploads a texture");

        const QImage masked = scene.render(params);
        const QImage identity = scene.render(baseParams());

        int moved = 0;
        int movedWhereMasked = 0;
        int movedWhereClear = 0;
        const QRect content(kLayerX, kLayerY, kLayerW, kLayerH);
        for (int y = content.top(); y <= content.bottom(); ++y) {
            for (int x = content.left(); x <= content.right(); ++x) {
                if (pixel(masked, x, y) == pixel(identity, x, y))
                    continue;
                ++moved;
                if (x - kLayerX < kLayerW / 2)
                    ++movedWhereMasked;
                else
                    ++movedWhereClear;
            }
        }
        std::printf(
            "MEASURED amountMask moved=%d maskedHalf=%d clearHalf=%d\n", moved, movedWhereMasked, movedWhereClear
        );
        check(movedWhereMasked == 0, "the half painted to 0 stops moving");
        check(movedWhereClear > 0, "the half left neutral still moves");

        // A TIME OFFSET mask delays the phase where it is painted, so it must
        // change the frame relative to no mask at all - and it is a different
        // deformation from the amount mask, not the same one renamed.
        core::FilterMask delay;
        delay.reset(QRectF(0.0, 0.0, float(kLayerW), float(kLayerH)), core::filterMaskNeutral(core::FilterMaskChannel_TimeOffset));
        {
            QImage& field = delay.image();
            for (int y = 0; y < field.height(); ++y) {
                uchar* line = field.scanLine(y);
                for (int x = 0; x < field.width(); ++x)
                    line[x] = (x < field.width() / 2) ? 0 : 255;
            }
            delay.markDirtyAll();
        }
        core::WavePassParams delayed = baseParams();
        delayed.strength = 10.0f;
        delayed.timeMask = delay.texture();
        delayed.timeMaskRect = delay.contentRect();
        delayed.timeMaskCycles = 0.5f;
        const QImage shifted = scene.render(delayed);
        const int delayDiff = differingPixels(shifted, identity, content);
        std::printf("MEASURED timeMask differing=%d\n", delayDiff);
        check(delayDiff > 0, "a painted time offset moves the wave in the painted half");
        check(
            differingPixels(shifted, masked, content) > 0,
            "the time offset is a different deformation from the amount mask"
        );

        if (failures == 0)
            std::printf("[6] painted masks (amount scales, time offset delays)\n");
    }

    // -------------------------------------------------- [7] an HSV amount mask
    {
        // A hue shift of 180 degrees on this source is plainly visible, so "the
        // masked half keeps the original colour" is measurable. The mapping here is
        // the identity (content px are composite px).
        core::MaskInput mask;
        core::FilterMask field;
        field.reset(QRectF(0.0, 0.0, float(kWidth), float(kHeight)), 255);
        {
            QImage& image = field.image();
            for (int y = 0; y < image.height(); ++y) {
                uchar* line = image.scanLine(y);
                for (int x = 0; x < image.width() / 2; ++x)
                    line[x] = 0; // the left half: no adjustment
            }
            field.markDirtyAll();
        }
        mask.texture = field.texture();
        mask.rect = field.contentRect();

        const QList<int> hsv{180, 100, 100, 0};
        const QMatrix4x4 identity;
        const QImage adjusted = scene.renderHsv(hsv, core::MaskInput(), identity);
        const QImage masked = scene.renderHsv(hsv, mask, identity);
        const QImage plain = scene.renderHsv(QList<int>{0, 100, 100, 0}, core::MaskInput(), identity);

        int adjustedLeft = 0;
        int adjustedRight = 0;
        int plainLeft = 0;
        int plainRight = 0;
        for (int y = 0; y < kHeight; ++y) {
            for (int x = 0; x < kWidth; ++x) {
                const bool left = x < kWidth / 2;
                if (pixel(masked, x, y) == pixel(adjusted, x, y))
                    left ? ++adjustedLeft : ++adjustedRight;
                if (pixel(masked, x, y) == pixel(plain, x, y))
                    left ? ++plainLeft : ++plainRight;
            }
        }
        std::printf(
            "MEASURED hsvMask maskedHalfKeptOriginal=%d unmaskedHalfAdjusted=%d\n", plainLeft, adjustedRight
        );
        // the discriminating count: pixels the adjustment WOULD have changed, and
        // how many of those the mask kept at the original colour
        int wouldChangeLeft = 0;
        int keptLeft = 0;
        for (int y = 0; y < kHeight; ++y) {
            for (int x = 0; x < kWidth / 2; ++x) {
                if (pixel(adjusted, x, y) == pixel(plain, x, y))
                    continue;
                ++wouldChangeLeft;
                if (pixel(masked, x, y) == pixel(plain, x, y))
                    ++keptLeft;
            }
        }
        std::printf(
            "MEASURED hsvMask wouldChangeLeft=%d keptAtOriginal=%d\n", wouldChangeLeft, keptLeft
        );
        check(adjustedRight > 0, "the unmasked half carries the whole adjustment");
        check(plainLeft > 0, "the masked half keeps the unadjusted colour");
        check(wouldChangeLeft > 0, "the adjustment would have changed the masked half");
        check(keptLeft == wouldChangeLeft, "every pixel the mask covers kept the original colour");
        (void)adjustedLeft;

        if (failures == 0)
            std::printf("[7] an HSV amount mask applies the adjustment per pixel\n");
    }

    // ------------------------------------------------- [8] a masked BLUR
    {
        // The masked-blur rule: the blurred and the sharp results are rendered, and
        // the field mixes them - 0 keeps the sharp pixels, 1 takes the blurred ones.
        core::FilterMask field;
        field.reset(QRectF(0.0, 0.0, float(kWidth), float(kHeight)), 255);
        {
            QImage& image = field.image();
            for (int y = 0; y < image.height(); ++y) {
                uchar* line = image.scanLine(y);
                for (int x = 0; x < image.width() / 2; ++x)
                    line[x] = 0; // the left half stays SHARP
            }
            field.markDirtyAll();
        }
        core::MaskInput mask;
        mask.texture = field.texture();
        mask.rect = field.contentRect();

        // what the GPU actually holds for the field (a mask that uploaded wrong would
        // make the mix's "no blur anywhere" look like a shader bug)
        {
            QImage check(field.image().size(), QImage::Format_RGBA8888);
            gl::Global::functions().glBindTexture(GL_TEXTURE_2D, mask.texture);
            gl::Global::functions().glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, check.bits());
            gl::Global::functions().glBindTexture(GL_TEXTURE_2D, 0);
            std::printf(
                "MEASURED blurMask texelLeft=%d texelRight=%d\n", int(check.constScanLine(150)[40 * 4]),
                int(check.constScanLine(150)[255 * 4])
            );
        }

        const QImage sharp = scene.render(baseParams()); // strength 0 = a copy
        const QImage mixed = scene.renderMaskedBlur(8.0f, mask);

        int sharpKeptLeft = 0;
        int blurredRight = 0;
        int changedRight = 0;
        for (int y = 0; y < kHeight; ++y) {
            for (int x = 0; x < kWidth; ++x) {
                const bool left = x < kWidth / 2;
                if (left && pixel(mixed, x, y) == pixel(sharp, x, y))
                    ++sharpKeptLeft;
                if (!left) {
                    if (pixel(mixed, x, y) == pixel(sharp, x, y))
                        ++changedRight;
                    else
                        ++blurredRight;
                }
            }
        }
        std::printf(
            "MEASURED blurMask sharpKeptLeft=%d blurredRight=%d unchangedRight=%d\n", sharpKeptLeft,
            blurredRight, changedRight
        );
        int changedLeft = 0;
        for (int y = 0; y < kHeight; ++y) {
            for (int x = 0; x < kWidth / 2; ++x) {
                if (pixel(mixed, x, y) != pixel(sharp, x, y))
                    ++changedLeft;
            }
        }
        check(sharpKeptLeft > 0, "the half the mask leaves at 0 keeps the SHARP pixels");
        check(blurredRight > 0, "the half at 1 takes the BLURRED pixels");
        check(changedLeft == 0, "the SHARP half is not blurred at all (the mask decides, not the blur)");

        if (failures == 0)
            std::printf("[8] a masked blur mixes sharp and blurred per pixel\n");
    }

    if (failures == 0)
        std::printf("wave_pass_pixels: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
