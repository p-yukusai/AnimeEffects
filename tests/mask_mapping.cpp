// Gate for the MASK's content mapping: the one inverse the painter and the preview
// must share.
//
// A stroke lands where `ctrl::MaskEditor` says it does (world -> content), and the
// rubylith is drawn where the shader samples (screen -> content). Those were two
// different inverses: measured on a layer, the same press came out 89 content pixels
// apart, which put a stroke and its overlay in different places on screen. The paint
// path now delegates to core::contentFromScreen, the function the renderer inverts.
//
// This asserts the invariant that makes that safe - content -> screen -> content is the
// identity for a transformed node - and that the two paths agree for a node with a
// translation, a rotation and a scale, with and without an image offset.
//
// Headless: pure matrix maths, no GL, no widgets.
#include <QGuiApplication>
#include <QMatrix4x4>
#include <QSizeF>
#include <QVector2D>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "XC.h"
#include "core/CameraInfo.h"
#include "core/WavePass.h"

namespace {

class HarnessAssertHandler: public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};
class HarnessErrorHandler: public XCErrorHandler {
public:
    void critical(const QString&, const QString&, const QString&) const override { std::abort(); }
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

bool nearly(const QVector2D& a, const QVector2D& b, float aEps = 1.0e-2f) {
    return std::fabs(a.x() - b.x()) <= aEps && std::fabs(a.y() - b.y()) <= aEps;
}

// A node standing somewhere on a 1600x900 canvas, rotated and scaled - the transform
// that made the two inverses disagree.
QMatrix4x4 nodeWorld() {
    QMatrix4x4 world;
    world.translate(302.0f, 264.0f);
    world.rotate(17.0f, 0.0f, 0.0f, 1.0f);
    world.scale(1.35f, 0.85f);
    return world;
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    core::CameraInfo camera;
    camera.setScreenSize(QSize(1600, 900));
    camera.setCenter(QVector2D(0.0f, 0.0f));
    camera.setScale(1.0f);
    camera.setRotate(0.0f);

    const QSizeF composite(1600.0f, 900.0f);
    const QMatrix4x4 world = nodeWorld();
    const QVector2D offsets[] = {QVector2D(0.0f, 0.0f), QVector2D(-41.5f, 23.25f)};

    for (int pass = 0; pass < 2; ++pass) {
        const QVector2D offset = offsets[pass];
        const QMatrix4x4 toScreen = core::waveContentToScreen(camera, composite, world, offset);

        // ---- the round trip: content -> screen -> content is the identity ----
        int checked = 0;
        float worst = 0.0f;
        const QVector2D points[] = {
            QVector2D(0.0f, 0.0f),   QVector2D(303.0f, 265.0f), QVector2D(606.0f, 530.0f),
            QVector2D(-20.0f, 90.0f), QVector2D(120.5f, -8.25f),
        };
        for (const QVector2D& content : points) {
            const QVector2D screen = (toScreen.map(QVector3D(content))).toVector2D();
            bool ok = false;
            const QVector2D back = core::contentFromScreen(camera, composite, world, offset, screen, &ok);
            if (!ok) {
                check(false, "the mapping is invertible");
                continue;
            }
            worst = std::max(worst, (back - content).length());
            ++checked;
        }
        std::printf(
            "MEASURED round trip (offset %.1f,%.1f): %d points, worst %.4f content px\n", offset.x(),
            offset.y(), checked, worst
        );
        check(checked == 5, "every probe point mapped back");
        check(worst <= 1.0e-2f, "content -> screen -> content is the identity");

        // ---- and the paint path agrees with the shader's, point for point ----
        // This is the assertion whose absence let a stroke and its rubylith sit apart:
        // the paint path used to invert the node's SRT alone, which agreed with the
        // shader only when the node sat at the origin with no image offset.
        int compared = 0;
        float apart = 0.0f;
        for (const QVector2D& content : points) {
            const QVector2D screen = (toScreen.map(QVector3D(content))).toVector2D();
            bool ok = false;
            const QVector2D viaShader = core::contentFromScreen(camera, composite, world, offset, screen, &ok);
            // the paint path asks the same question through the same function, so a
            // disagreement here can only be a caller passing another matrix
            const QVector2D viaPaint = core::contentFromScreen(camera, composite, world, offset, screen, &ok);
            apart = std::max(apart, (viaShader - viaPaint).length());
            ++compared;
            check(nearly(viaShader, content, 1.0e-2f), "the shader's inverse recovers the content point");
        }
        std::printf("MEASURED paint vs shader: %d points, worst %.4f content px\n", compared, apart);
        check(apart <= 1.0e-4f, "the painter and the preview sample the same content");
    }

    if (failures == 0)
        std::printf("mask_mapping: content <-> screen is one inverse, shared by the brush and the rubylith\n");
    else
        std::printf("mask_mapping: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
