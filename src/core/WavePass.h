#ifndef CORE_WAVEPASS_H
#define CORE_WAVEPASS_H

#include <QMatrix3x3>
#include <QRectF>
#include <QMatrix4x4>
#include <QSizeF>
#include <QVector2D>

#include "core/CameraInfo.h"
#include "core/TimeInfo.h"
#include "core/FilterMask.h"
#include "core/WaveKey.h"

namespace core {

// One painted field as a pass sees it: the texture plus the content rectangle it
// covers (see core/FilterMask.h). Absent = a zero texture and an empty rect.
struct MaskInput {
    GLuint texture = 0;
    QRectF rect;
    // the value "the filter is unaffected" at, 0..1 (core::filterMaskNeutral). The
    // preview tints where the field DEVIATES from it; the filters themselves do not
    // need it (they are told what their neutral means by their own logic).
    float neutral = 1.0f;
    bool isValid() const { return texture != 0 && !rect.isEmpty(); }
};

// Everything FilterFrame::waveTexture needs to deform one isolated composite.
//
// The field lives in CONTENT space (a layer's image pixels, a folder's own local
// frame) and the pass only knows how to get there: `layerToScreen` maps a
// content pixel into the composite's pixels and `screenToLayer` is its exact
// inverse, so the shader evaluates the wave where it belongs on the artwork and
// then samples where that displacement lands on screen. Both matrices come from
// the SAME matrices the layer shader draws with (the camera's view matrix times
// the node's world matrix), which is what keeps the deformation glued to the
// pixels the layer actually painted.
struct WavePassParams {
    QMatrix4x4 layerToScreen;
    QMatrix4x4 screenToLayer;
    // The content rectangle the SAMPLED coordinate is clamped into, in content
    // pixels. This is the texture-space clamp an image effect has in Wallpaper
    // Engine: a displaced sample may not leave the image, so the edge texels are
    // stretched instead of showing a transparent tear. It is expressed as the
    // rectangle's TEXEL-CENTRE range (0.5 .. size-0.5), not its outline: clamping
    // to the outline (0 .. size) puts a saturated sample exactly on the boundary
    // between the last content texel and whatever is outside it, and a bilinear
    // sample there is half of each. A degenerate box (max <= min on either axis)
    // disables the clamp - a folder has no image rectangle, and its content is
    // bounded by the composite itself.
    QVector2D clampMin;
    QVector2D clampMax;
    // the five graph channels
    float strength = 0.0f;
    float wavelength = WaveKey::Preset::wavelength;
    float angleDeg = WaveKey::Preset::angleDeg;
    float phase = WaveKey::Preset::phase;
    float mix = WaveKey::Preset::mix;
    // the step-held shape
    float speed = WaveKey::Preset::speed;
    float crest = WaveKey::Preset::crest;
    float dualAngleDeg = WaveKey::Preset::dualAngleDeg;
    float dualScale = WaveKey::Preset::dualScale;
    WaveKey::Deform deform = WaveKey::Deform_Ripple;
    // The document time the phase advances with, in SECONDS. Derived from the
    // frame (frame / fps) so playback, scrubbing and an export agree.
    float seconds = 0.0f;

    // PAINTED MASKS (core/FilterMask.h), both sampled in the same content space as
    // the field itself:
    //   * an AMOUNT mask scales the displacement per pixel (paint a heat haze that
    //     fades out, a wave only on the skirt);
    //   * a TIME OFFSET mask delays the phase per pixel (crests arrive at different
    //     times, which is what stops several objects moving in lockstep).
    // 0 means "no mask": the shader then does not sample at all.
    GLuint amountMask = 0;
    QRectF amountMaskRect;   // content px
    GLuint timeMask = 0;
    QRectF timeMaskRect;     // content px
    float timeMaskCycles = 0.0f; // how much a fully white texel delays the phase

    bool hasClamp() const { return clampMax.x() > clampMin.x() && clampMax.y() > clampMin.y(); }
};

// Maps content pixels (image pixels for a layer - the coordinate an ImageKey's
// texcoords are in) to the composite's pixels, by exactly the path the layer
// shader draws them: world = worldMatrix * (q + imageOffset), then NDC, then the
// framebuffer's pixel grid (vDestCoord = (ndc + 1) * size / 2, y up).
//
// aWorldMatrix is the node's world transform WITHOUT the image offset (see
// SRTExpans::worldCSRTMatrix and MeshTransformer, which translates by the mesh
// origin offset inside it); aImageOffset is TimeKeyExpans::imageOffset().
inline QMatrix4x4 waveContentToScreen(
    const CameraInfo& aCamera, const QSizeF& aCompositeSize, const QMatrix4x4& aWorldMatrix,
    const QVector2D& aImageOffset
) {
    QMatrix4x4 world = aWorldMatrix;
    world.translate(aImageOffset.x(), aImageOffset.y());

    QMatrix4x4 ndcToPixels;
    ndcToPixels.scale(float(aCompositeSize.width()) * 0.5f, float(aCompositeSize.height()) * 0.5f, 1.0f);
    ndcToPixels.translate(1.0f, 1.0f, 0.0f);

    return ndcToPixels * aCamera.viewMatrix() * world;
}

// THE INVERSE, and the ONLY one: a screen point (the shader's `vDestCoord`, y UP)
// to the content pixel that lands there. The painter and the preview must agree
// texel-for-texel - measured: the paint path's own inverse put the same press 89
// content pixels away from where the shader sampled, so a stroke and its rubylith
// sat apart on screen.
inline QVector2D contentFromScreen(
    const CameraInfo& aCamera, const QSizeF& aCompositeSize, const QMatrix4x4& aWorldMatrix,
    const QVector2D& aImageOffset, const QVector2D& aScreenPoint, bool* aOk = nullptr
) {
    const QMatrix4x4 toScreen = waveContentToScreen(aCamera, aCompositeSize, aWorldMatrix, aImageOffset);
    bool invertible = false;
    const QMatrix4x4 inv = toScreen.inverted(&invertible);
    if (aOk)
        *aOk = invertible;
    if (!invertible)
        return QVector2D();
    return (inv.map(QVector3D(aScreenPoint))).toVector2D();
}

// The same affine as a mat3 for the shader: the 2x2 linear part kept, the
// TRANSLATION moved from a QMatrix4x4's column 3 into the mat3's column 2 - the
// place vec3(q, 1) reads it from - and the bottom row set to (0,0,1). Copying
// columns 0..2 of the 4x4 instead would silently drop the translation, which
// every displacement is relative to.
inline QMatrix3x3 waveAffine3(const QMatrix4x4& aMatrix) {
    QMatrix3x3 out;
    for (int r = 0; r < 2; ++r) {
        out(r, 0) = aMatrix(r, 0);
        out(r, 1) = aMatrix(r, 1);
        out(r, 2) = aMatrix(r, 3);
    }
    out(2, 0) = 0.0f;
    out(2, 1) = 0.0f;
    out(2, 2) = 1.0f;
    return out;
}

// Builds one pass from a node's blended key data and its place in the scene.
// aClampMin/aClampMax are the content rectangle the sample may not leave (the
// image's own pixel box for a layer; a degenerate box for a folder, which has no
// image rectangle). A node whose transform collapses a dimension has no inverse
// mapping, so the returned params carry zero strength and the caller skips the
// pass rather than dividing by it.
inline WavePassParams wavePassFor(
    const CameraInfo& aCamera, const TimeInfo& aTime, const QSizeF& aCompositeSize,
    const WaveKey::Data& aData, const QMatrix4x4& aWorldMatrix, const QVector2D& aImageOffset,
    const QVector2D& aClampMin = QVector2D(), const QVector2D& aClampMax = QVector2D()
) {
    WavePassParams params;
    params.layerToScreen = waveContentToScreen(aCamera, aCompositeSize, aWorldMatrix, aImageOffset);
    bool invertible = false;
    params.screenToLayer = params.layerToScreen.inverted(&invertible);
    params.clampMin = aClampMin;
    params.clampMax = aClampMax;
    params.strength = aData.strength();
    params.wavelength = aData.wavelength();
    params.angleDeg = aData.angleDeg();
    params.phase = aData.phase();
    params.mix = aData.mix();
    params.speed = aData.speed();
    params.crest = aData.crest();
    params.dualAngleDeg = aData.dualAngleDeg();
    params.dualScale = aData.dualScale();
    params.deform = aData.deform();
    // frame / fps, never a wall clock: the phase a frame plays is a property of
    // the document, so an export renders exactly what the timeline showed.
    params.seconds = (aTime.fps > 0) ? float(aTime.frame.get()) / float(aTime.fps) : 0.0f;
    if (!invertible)
        params.strength = 0.0f;
    return params;
}

} // namespace core

#endif // CORE_WAVEPASS_H
