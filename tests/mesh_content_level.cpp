// The per-layer mesh content level (img::MeshContentLevel): the mapping, its no-measurement
// fallback, and the creator's content threshold. Pure pixel math, no GL and no project.
#include <QSize>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "XCAssert.h"
#include "img/GridMeshCreator.h"
#include "img/MeshContentLevel.h"

class AbortAssertHandler: public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};
AbortAssertHandler gAssertHandler;
XCAssertHandler* gXCAssertHandler = &gAssertHandler;

namespace {

int failures = 0;
int checks = 0;

void expect(bool aValue, const char* aWhat) {
    ++checks;
    if (!aValue) {
        std::printf("FAIL  %s\n", aWhat);
        ++failures;
    }
}

void expectLevel(double aMean, int aWant, const char* aWhat) {
    const int got = img::MeshContentLevel::fromMean(aMean);
    ++checks;
    if (got != aWant) {
        std::printf("FAIL  %s: mean %.17g -> %d, want %d\n", aWhat, aMean, got, aWant);
        ++failures;
    }
}

int derive(int aWidth, int aHeight, std::uint8_t aAlpha) {
    std::vector<std::uint8_t> pixels((size_t)aWidth * aHeight * 4, 0);
    for (size_t i = 3; i < pixels.size(); i += 4) {
        pixels[i] = aAlpha;
    }
    return img::MeshContentLevel::derive(pixels.data(), QSize(aWidth, aHeight));
}

int triangles(const std::vector<std::uint8_t>& aPixels, int aWidth, int aHeight, int aThreshold) {
    img::GridMeshCreator creator(aPixels.data(), QSize(aWidth, aHeight), 16, aThreshold);
    return creator.indexCount() / 3;
}

std::vector<std::uint8_t> block(int aWidth, int aHeight, int aX0, int aY0, int aX1, int aY1, std::uint8_t aAlpha) {
    std::vector<std::uint8_t> pixels((size_t)aWidth * aHeight * 4, 0);
    for (int y = aY0; y <= aY1; ++y) {
        for (int x = aX0; x <= aX1; ++x) {
            pixels[((size_t)y * aWidth + x) * 4 + 3] = aAlpha;
        }
    }
    return pixels;
}

} // namespace

int main() {
    // the mapping: clamp(round(0.05 * mean), 1, 10), with 10 for no measurement
    expectLevel(255.0, 10, "an opaque layer gets the strict level");
    expectLevel(100.0, 5, "mean 100 gives level 5");
    expectLevel(40.0, 2, "mean 40 gives level 2");
    expectLevel(20.0, 1, "a faint layer is clamped to level 1");
    expectLevel(30.0, 2, "mean 30 rounds up to level 2");
    expectLevel(10.0, 1, "mean 10 gives level 1");
    expectLevel(3.0, 1, "a tiny mean clamps up to level 1");
    expectLevel(0.0, 10, "no measurement keeps the strict level");
    expectLevel(-5.0, 10, "a negative mean keeps the strict level");

    // the derivation over a whole layer
    expect(derive(8, 8, 255) == 10, "all-opaque 8x8 derives 10");
    expect(derive(8, 8, 100) == 5, "all-alpha-100 8x8 derives 5");
    expect(derive(8, 8, 40) == 2, "all-alpha-40 8x8 derives 2");
    expect(derive(8, 8, 20) == 1, "all-alpha-20 8x8 derives 1");
    expect(derive(8, 8, 6) == 1, "all-alpha-6 8x8 derives 1");
    expect(derive(8, 8, 3) == 1, "all-alpha-3 8x8 derives 1");
    expect(derive(8, 8, 2) == 10, "nothing above alpha 2 keeps 10");

    {
        // The mean is over pixels above 2, not over the whole bitmap: one opaque pixel in an
        // otherwise empty image measures 255.
        std::vector<std::uint8_t> pixels((size_t)100 * 100 * 4, 0);
        pixels[3] = 255;
        expect(
            img::MeshContentLevel::derive(pixels.data(), QSize(100, 100)) == 10,
            "the mean ignores pixels at or below alpha 2"
        );
    }
    {
        std::vector<std::uint8_t> pixels = block(100, 10, 0, 0, 49, 9, 100);
        expect(
            img::MeshContentLevel::derive(pixels.data(), QSize(100, 10)) == 5, "half a layer at alpha 100 derives 5"
        );
    }
    expect(img::MeshContentLevel::derive(nullptr, QSize(8, 8)) == 10, "null pixels keep 10");
    {
        const std::vector<std::uint8_t> pixels(4, 0);
        expect(img::MeshContentLevel::derive(pixels.data(), QSize()) == 10, "an empty size keeps 10");
    }

    // The creator tests strictly above its level: alpha 6 is content at 2 and not at 10.
    {
        const std::vector<std::uint8_t> faint = block(96, 32, 40, 8, 55, 23, 6);
        expect(triangles(faint, 96, 32, 0) > 0, "alpha 6 is content at level 0");
        expect(triangles(faint, 96, 32, 2) > 0, "alpha 6 is content at level 2");
        expect(triangles(faint, 96, 32, 10) == 0, "alpha 6 is not content at level 10");
        expect(triangles(faint, 96, 32, 6) == 0, "the test is strictly above the level");
    }
    {
        const std::vector<std::uint8_t> painted = block(96, 32, 40, 8, 55, 23, 20);
        expect(triangles(painted, 96, 32, 10) > 0, "alpha 20 is content at level 10");
        expect(triangles(painted, 96, 32, 19) > 0, "alpha 20 is content at level 19");
        expect(triangles(painted, 96, 32, 20) == 0, "alpha 20 is not content at level 20");
        expect(triangles(painted, 96, 32, 30) == 0, "alpha 20 is not content at level 30");
    }
    {
        // the three-argument constructor uses the constant, which is the strict old level
        const std::vector<std::uint8_t> faint = block(96, 32, 40, 8, 55, 23, 6);
        img::GridMeshCreator byDefault(faint.data(), QSize(96, 32), 16);
        expect(byDefault.indexCount() / 3 == 0, "the creator's default level is 10");
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
