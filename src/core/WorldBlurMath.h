#ifndef CORE_WORLDBLURMATH_H
#define CORE_WORLDBLURMATH_H

#include <cmath>
#include <QVector2D>
#include "core/ObjectNode.h"
#include "core/TimeCacheAccessor.h"
#include "core/SRTExpans.h"

namespace core {

// A blurred layer/folder isolates its content and blurs it in the composite, which is in
// project space. The blur amount is in the content's own pixels, so the content-space
// blur circle maps through the accumulated world transform into an ellipse in the
// composite. The two separable 1D Gaussian passes must run along the ellipse's principal
// axes (the left singular vectors of the accumulated linear part) with the singular
// values as radii, otherwise rotation in the chain produces the wrong blur shape.
struct WorldBlurEllipse {
    QVector2D majorDir; // unit vector in pixel space (principal axis of the ellipse)
    QVector2D minorDir; // orthogonal to majorDir
    float majorRadius;  // largest singular value of the accumulated linear part
    float minorRadius;  // smallest singular value
};

// Accumulates the 2x2 linear part M = R(rotate)*S(scale) of every node from aNode up to
// the root (ancestors premultiplied, exactly like SRTExpans::worldCSRTMatrix) and
// returns its singular decomposition M = U*Sigma*V^T: majorDir/minorDir are the columns
// of U (the ellipse axes in project space), majorRadius/minorRadius the singular values.
// A user-controlled directional blur is an ellipse in the node's CONTENT space (radii
// aBlurX/aBlurY and an aAngleDeg rotation of its own X axis); it premultiplies into M as
// M' = M*R(angle)*diag(blurX, blurY) before the decomposition, so the node's own
// transform chain distorts the user ellipse exactly like it distorts the content. With
// defaults (1,1,0) this reduces to the plain world transform, so existing behavior is
// unchanged.
inline WorldBlurEllipse worldBlurEllipse(
    const TimeCacheAccessor& aAccessor, const ObjectNode* aNode,
    float aBlurX = 1.0f, float aBlurY = 1.0f, float aAngleDeg = 0.0f
) {
    float m00 = 1.0f, m01 = 0.0f, m10 = 0.0f, m11 = 1.0f; // row-major M
    for (const ObjectNode* n = aNode; n; n = n->parent()) {
        if (!n->timeLine())
            continue;
        const SRTExpans& srt = aAccessor.get(*n).srt();
        const float c = std::cos(srt.rotate());
        const float s = std::sin(srt.rotate());
        const QVector2D sc = srt.scale();
        // L = R(rotate)*S(scale); M = L * M (the parent's transform applies after the child's)
        const float l00 = c * sc.x();
        const float l01 = -s * sc.y();
        const float l10 = s * sc.x();
        const float l11 = c * sc.y();
        const float nm00 = l00 * m00 + l01 * m10;
        const float nm01 = l00 * m01 + l01 * m11;
        const float nm10 = l10 * m00 + l11 * m10;
        const float nm11 = l10 * m01 + l11 * m11;
        m00 = nm00;
        m01 = nm01;
        m10 = nm10;
        m11 = nm11;
    }
    // fold the content-space blur ellipse E = R(angle)*diag(blurX, blurY) into M on the
    // right: M' = M*E. The eigenvalue decomposition below then uses M'*M'^T.
    if (aBlurX != 1.0f || aBlurY != 1.0f || aAngleDeg != 0.0f) {
        const float rad = aAngleDeg * (3.14159265358979f / 180.0f);
        const float c = std::cos(rad);
        const float s = std::sin(rad);
        const float e00 = c * aBlurX, e01 = -s * aBlurY;
        const float e10 = s * aBlurX, e11 = c * aBlurY;
        const float nm00 = m00 * e00 + m01 * e10;
        const float nm01 = m00 * e01 + m01 * e11;
        const float nm10 = m10 * e00 + m11 * e10;
        const float nm11 = m10 * e01 + m11 * e11;
        m00 = nm00;
        m01 = nm01;
        m10 = nm10;
        m11 = nm11;
    }
    // singular values of M via the eigenvalues of A = M*M^T (symmetric 2x2, closed form)
    const float a00 = m00 * m00 + m01 * m01;
    const float a01 = m00 * m10 + m01 * m11;
    const float a11 = m10 * m10 + m11 * m11;
    const float tr = a00 + a11;
    const float det = a00 * a11 - a01 * a01;
    float disc = tr * tr - 4.0f * det;
    if (disc < 0.0f)
        disc = 0.0f;
    const float sqrtDisc = std::sqrt(disc);
    const float l1 = 0.5f * (tr + sqrtDisc);
    const float l2 = 0.5f * (tr - sqrtDisc);

    WorldBlurEllipse e;
    e.majorRadius = std::sqrt(l1 > 0.0f ? l1 : 0.0f);
    e.minorRadius = std::sqrt(l2 > 0.0f ? l2 : 0.0f);
    // eigenvector of A for l1 (a column of U): (A - l1*I) v = 0 -> v = (a01, l1 - a00)
    QVector2D v(a01, l1 - a00);
    if (v.lengthSquared() < 1e-12f)
        v = QVector2D(l1 - a11, a01);
    if (v.lengthSquared() < 1e-12f)
        v = QVector2D(1.0f, 0.0f);
    v.normalize();
    e.majorDir = v;
    e.minorDir = QVector2D(-v.y(), v.x());
    return e;
}

} // namespace core

#endif // CORE_WORLDBLURMATH_H
