#include "core/MaskPyramid.h"
#include <QtCore/qfloat16.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "gl/Global.h"
#include "gl/PagePool.h"

namespace core {

namespace {
    int trailingZeroBits(int aValue) {
        int count = 0;
        while (aValue > 1 && (aValue & 1) == 0) {
            aValue >>= 1;
            ++count;
        }
        return count;
    }
} // namespace

MaskPyramid::MaskPyramid(): mSizes(), mLevels() {}

void MaskPyramid::clear() {
    mSizes.clear();
    mLevels.clear();
    mBlendA = nullptr;
    mBlendB = nullptr;
    mBlendFieldA = QImage();
    mBlendFieldB = QImage();
    mBlendWeight = 0;
    mBlendScratch.clear();
}

QSize MaskPyramid::levelSizeOf(const QSize& aBase, int aLevel) {
    return QSize(std::max(1, aBase.width() >> aLevel), std::max(1, aBase.height() >> aLevel));
}

QSize MaskPyramid::levelSize(int aLevel) const {
    if (aLevel < 0 || aLevel >= (int)mSizes.size())
        return QSize();
    return mSizes[(size_t)aLevel];
}

const std::vector<uint8>& MaskPyramid::levelBytes(int aLevel) const {
    XC_ASSERT(0 <= aLevel && aLevel < (int)mLevels.size());
    return mLevels[(size_t)aLevel];
}

qint64 MaskPyramid::bytes() const {
    qint64 total = (qint64)mBlendScratch.size();
    for (const std::vector<uint8>& level : mLevels)
        total += (qint64)level.size();
    return total;
}

void MaskPyramid::lerpInto(uint8* aDst, const uint8* aOther, size_t aCount) const {
    const int w = std::max(0, std::min(256, mBlendWeight));
    const int inv = 256 - w;
    for (size_t i = 0; i < aCount; ++i)
        aDst[i] = uint8((int(aDst[i]) * inv + int(aOther[i]) * w) >> 8);
}

bool MaskPyramid::setBlend(
    const MaskPyramid* aA, const QImage& aFieldA, const MaskPyramid* aB, const QImage& aFieldB,
    int aWeight
) {
    clear();
    if (!aA || !aB || aA == aB || aA->isBlend() || aB->isBlend())
        return false;
    if (!aA->isValid() || !aB->isValid() || aA->size() != aB->size()
        || aA->levelCount() != aB->levelCount())
        return false;
    if (aFieldA.isNull() || aFieldB.isNull() || aFieldA.size() != aA->size()
        || aFieldB.size() != aB->size())
        return false;
    mSizes = aA->mSizes;
    mBlendA = aA;
    mBlendB = aB;
    mBlendFieldA = aFieldA;
    mBlendFieldB = aFieldB;
    mBlendWeight = std::max(0, std::min(256, aWeight));
    return true;
}

void MaskPyramid::setBlendWeight(int aWeight) {
    if (isBlend())
        mBlendWeight = std::max(0, std::min(256, aWeight));
}

QImage MaskPyramid::displayField() const {
    if (!isBlend() || mBlendFieldA.isNull() || mBlendFieldB.isNull()
        || mBlendFieldA.size() != mBlendFieldB.size())
        return QImage();
    QImage out(mBlendFieldA.size(), QImage::Format_Grayscale8);
    const int w = out.width();
    for (int y = 0; y < out.height(); ++y) {
        const uint8* a = mBlendFieldA.constScanLine(y);
        const uint8* b = mBlendFieldB.constScanLine(y);
        uint8* dst = out.scanLine(y);
        std::memcpy(dst, a, (size_t)w);
        lerpInto(dst, b, (size_t)w);
    }
    return out;
}

bool MaskPyramid::capture(const gl::Texture& aChain) {
    clear();
    if (aChain.id() == 0 || aChain.size().isEmpty())
        return false;

    const int levels = std::max(1, aChain.mipmapLevels());
    const QSize base = aChain.size();
    auto& ggl = gl::Global::functions();
    ggl.glBindTexture(GL_TEXTURE_2D, aChain.id());
    ggl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
    // LEVEL 0 IS THE FIELD, not a copy: the caller keeps it, and the page fill reads it there.
    mSizes.reserve(levels);
    mLevels.resize((size_t)levels);
    for (int l = 0; l < levels; ++l) {
        const QSize size = levelSizeOf(base, l);
        mSizes.push_back(size);
        if (l == 0)
            continue;
        std::vector<uint8> bytes((size_t)size.width() * size.height(), 0);
        ggl.glGetTexImage(GL_TEXTURE_2D, l, GL_RED, GL_UNSIGNED_BYTE, bytes.data());
        mLevels[(size_t)l] = std::move(bytes);
    }
    ggl.glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

// The box on aDstSize's grid that the damage in aSrcBox reaches: every destination texel whose
// footprint touches the source range, rounded OUTWARDS at both ends, clamped into the grid. The
// same integer rule as MaskMipChain's, so a stroke paints the same texels here and there.
QRect MaskPyramid::coveredBox(const QRect& aSrcBox, const QSize& aSrcSize, const QSize& aDstSize) {
    if (aSrcBox.isEmpty() || aSrcSize.isEmpty() || aDstSize.isEmpty())
        return QRect();
    const auto span = [](int aLo, int aHi, int aSrc, int aDst, int& aFirst, int& aLast) {
        const qint64 src = aSrc;
        const qint64 dst = aDst;
        const int lo = int((qint64(aLo) * dst) / src);
        const int hi = int(((qint64(aHi) * dst) + src - 1) / src) - 1;
        aFirst = std::max(0, std::min(lo, aDst - 1));
        aLast = std::max(0, std::min(hi, aDst - 1));
    };
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    span(aSrcBox.left(), aSrcBox.right() + 1, aSrcSize.width(), aDstSize.width(), x0, x1);
    span(aSrcBox.top(), aSrcBox.bottom() + 1, aSrcSize.height(), aDstSize.height(), y0, y1);
    if (x1 < x0 || y1 < y0)
        return QRect();
    return QRect(QPoint(x0, y0), QPoint(x1, y1));
}

// One box of one level, the GPU's two-pass rule mirrored: the x pass averages one source row at
// a time and stores the NORMALIZED average in half precision (the chain's R16F scratch), the y
// pass averages those halves and rounds once into R8. Reproducing the half store is what keeps
// the CPU within 1-2 LSB of the GPU chain instead of the 3 a single 2D average gives.
void MaskPyramid::resampleBox(
    const uint8* aSrc, int aSrcStride, const QSize& aSrcSize, std::vector<uint8>& aDst,
    const QSize& aDstSize, const QRect& aDestBox
) const {
    if (aDestBox.isEmpty() || !aSrc || aSrcStride <= 0 || aDst.empty())
        return;
    const QRect srcBox = coveredBox(aDestBox, aDstSize, aSrcSize);
    if (srcBox.isEmpty())
        return;

    const int xw = aDestBox.width();
    const int sh = srcBox.height();
    std::vector<float> xpass((size_t)xw * (size_t)sh, 0.0f);
    const int xTaps = int(std::ceil(double(aSrcSize.width()) / double(aDstSize.width()))) + 1;
    for (int i = 0; i < xw; ++i) {
        const int g = aDestBox.left() + i;
        const float lo = float(g) * float(aSrcSize.width()) / float(aDstSize.width());
        const float hi = float(g + 1) * float(aSrcSize.width()) / float(aDstSize.width());
        const int first = int(std::floor(lo));
        for (int sy = 0; sy < sh; ++sy) {
            const int row = srcBox.top() + sy;
            float acc = 0.0f;
            float wsum = 0.0f;
            for (int t = 0; t < xTaps; ++t) {
                const int k = first + t;
                if (k < 0 || k >= aSrcSize.width())
                    continue;
                const float w = std::min(float(k + 1), hi) - std::max(float(k), lo);
                if (w <= 0.0f)
                    continue;
                acc += (aSrc[(size_t)row * aSrcStride + k] / 255.0f) * w;
                wsum += w;
            }
            xpass[(size_t)sy * xw + i] = wsum > 0.0f ? float(qfloat16(acc / wsum)) : 0.0f;
        }
    }

    const int yTaps = int(std::ceil(double(aSrcSize.height()) / double(aDstSize.height()))) + 1;
    for (int j = 0; j < aDestBox.height(); ++j) {
        const int g = aDestBox.top() + j;
        const float lo = float(g) * float(aSrcSize.height()) / float(aDstSize.height());
        const float hi = float(g + 1) * float(aSrcSize.height()) / float(aDstSize.height());
        const int first = int(std::floor(lo));
        for (int i = 0; i < xw; ++i) {
            float acc = 0.0f;
            float wsum = 0.0f;
            for (int t = 0; t < yTaps; ++t) {
                const int k = first + t;
                if (k < srcBox.top() || k > srcBox.bottom())
                    continue;
                const float w = std::min(float(k + 1), hi) - std::max(float(k), lo);
                if (w <= 0.0f)
                    continue;
                acc += xpass[(size_t)(k - srcBox.top()) * xw + i] * w;
                wsum += w;
            }
            const float v = wsum > 0.0f ? acc / wsum : 0.0f;
            aDst[(size_t)g * aDstSize.width() + (aDestBox.left() + i)] =
                (uint8)std::lround(std::max(0.0f, std::min(1.0f, v)) * 255.0f);
        }
    }
}

void MaskPyramid::updateBox(const QImage& aField, const QRect& aTexelBox) {
    if (isBlend() || !isValid() || aField.isNull() || aField.format() != QImage::Format_Grayscale8)
        return;
    if (aField.size() != size())
        return;
    const QRect box = aTexelBox.intersected(QRect(QPoint(0, 0), size()));
    if (box.isEmpty())
        return;

    // LEVEL 0 IS THE FIELD. Level 1 reads it in place (the QImage's own row stride, so an
    // aligned Grayscale8 field costs no copy); each level above reads the one just rebuilt.
    QRect prevBox = box;
    QSize prevSize = mSizes[0];
    const uint8* prevData = aField.constBits();
    int prevStride = aField.bytesPerLine();
    for (int l = 1; l < (int)mLevels.size(); ++l) {
        const QSize size = mSizes[(size_t)l];
        const QRect destBox = coveredBox(prevBox, prevSize, size);
        if (destBox.isEmpty())
            break;
        resampleBox(prevData, prevStride, prevSize, mLevels[(size_t)l], size, destBox);
        prevBox = destBox;
        prevSize = size;
        prevData = mLevels[(size_t)l].data();
        prevStride = size.width();
    }
}

void MaskPyramid::fillPage(int aLevel, int aPageX, int aPageY, const QImage& aField, uint8* aDst) const {
    XC_PTR_ASSERT(aDst);
    // THE VIEW: fill both sources and lerp the pages. The lerp commutes with the fill's
    // edge-clamp (which selects texels by INDEX, not by value), so this is exactly the page of
    // the blended field.
    if (isBlend()) {
        const int pitch = gl::PagePool::kPitch;
        mBlendA->fillPage(aLevel, aPageX, aPageY, mBlendFieldA, aDst);
        mBlendScratch.resize((size_t)pitch * pitch);
        mBlendB->fillPage(aLevel, aPageX, aPageY, mBlendFieldB, mBlendScratch.data());
        lerpInto(aDst, mBlendScratch.data(), (size_t)pitch * pitch);
        return;
    }
    if (aLevel < 0 || aLevel >= (int)mLevels.size())
        return;
    const int pitch = gl::PagePool::kPitch;
    const int gutter = gl::PagePool::kGutter;
    const QSize size = mSizes[(size_t)aLevel];
    const uint8* level = nullptr;
    int stride = 0;
    if (aLevel == 0) {
        // the field itself; no copy of it is kept (see the class note). A field that is not the
        // captured grid cannot be filled from, and an empty page is a hole but not garbage: zero
        // it so a caller's mistake is a transparent matte rather than the previous page's texels.
        if (aField.isNull() || aField.format() != QImage::Format_Grayscale8
            || aField.size() != size) {
            std::memset(aDst, 0, (size_t)pitch * pitch);
            return;
        }
        level = aField.constBits();
        stride = aField.bytesPerLine();
    } else {
        level = mLevels[(size_t)aLevel].data();
        stride = size.width();
    }
    const int originX = aPageX * (int)gl::PagePool::kPageSize - gutter;
    const int originY = aPageY * (int)gl::PagePool::kPageSize - gutter;

    for (int y = 0; y < pitch; ++y) {
        // GL_CLAMP_TO_EDGE: outside the level, the nearest edge texel - see the header. The level
        // itself is copied, so a page's interior is exact and only the border repeats.
        const int gy = std::min(std::max(originY + y, 0), size.height() - 1);
        const uint8* src = level + (size_t)gy * stride;
        uint8* row = aDst + (size_t)y * pitch;
        // clamped to the page: a page wholly left of the level has no run, and the edge-repeat
        // loops below are bounded by the page (a page outside the grid is never demanded, but the
        // fill must not write outside its buffer - ImagePyramid::fillPage had exactly that bug)
        const int x0 = std::min(pitch, std::max(0, -originX));
        const int x1 = std::min(pitch, size.width() - originX);
        for (int x = 0; x < x0; ++x)
            row[x] = src[0];
        if (x1 > x0)
            std::memcpy(row + x0, src + (originX + x0), (size_t)(x1 - x0));
        for (int x = std::max(x0, x1); x < pitch; ++x)
            row[x] = src[size.width() - 1];
    }
}

void MaskPyramid::copyLevel(int aLevel, uint8* aDst) const {
    XC_PTR_ASSERT(aDst);
    // level 0 is the field, not stored (the coarse chain starts at a level >= 1, so no caller
    // needs it here)
    XC_ASSERT(aLevel >= 1);
    if (isBlend()) {
        const QSize levelSize = this->levelSize(aLevel);
        if (levelSize.isEmpty())
            return;
        mBlendA->copyLevel(aLevel, aDst);
        const size_t count = (size_t)levelSize.width() * levelSize.height();
        mBlendScratch.resize(count);
        mBlendB->copyLevel(aLevel, mBlendScratch.data());
        lerpInto(aDst, mBlendScratch.data(), count);
        return;
    }
    if (aLevel < 1 || aLevel >= (int)mLevels.size())
        return;
    const std::vector<uint8>& level = mLevels[(size_t)aLevel];
    std::memcpy(aDst, level.data(), level.size());
}

QRect MaskPyramid::boxAtLevel(int aLevel, const QRect& aBaseBox) const {
    if (aLevel < 0 || aLevel >= (int)mLevels.size())
        return QRect();
    QRect box = aBaseBox;
    QSize aboveSize = mSizes[0];
    for (int l = 1; l <= aLevel; ++l) {
        const QSize size = mSizes[(size_t)l];
        box = coveredBox(box, aboveSize, size);
        if (box.isEmpty())
            return QRect();
        aboveSize = size;
    }
    return box;
}

} // namespace core
