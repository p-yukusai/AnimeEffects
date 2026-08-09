#ifndef IMG_BLENDMODE_H
#define IMG_BLENDMODE_H

#include <QString>

namespace img {

// Enum order mirrors the Photoshop dropdown grouping (darken group, lighten
// group, contrast, difference, color), with the two CSP-only modes next to
// their plain equivalents. It is DECORATIVE: the UI combo is built by explicit
// addMode() calls with insertSeparator() dividers and the mode carried in
// Qt::UserRole (see prop/prop_ConstantPanel.cpp build()), so the row index does
// not need to match the enum order. Persisted data (.anm/.psd) uses the quad ids
// from getQuadIdFromBlendMode(), so this order must NOT be relied on for file
// compatibility.
enum BlendMode {
    BlendMode_Normal,

    BlendMode_Darken,
    BlendMode_Multiply,
    BlendMode_ColorBurn,
    BlendMode_LinearBurn,
    BlendMode_DarkerColor,

    BlendMode_Lighten,
    BlendMode_Screen,
    BlendMode_ColorDodge,
    BlendMode_GlowDodge,
    BlendMode_LinearDodge, // Equal to Add
    BlendMode_AddGlow,
    BlendMode_LighterColor,

    BlendMode_Overlay,
    BlendMode_SoftLight,
    BlendMode_HardLight,
    BlendMode_VividLight,
    BlendMode_LinearLight,
    BlendMode_PinLight,
    BlendMode_HardMix,

    BlendMode_Difference,
    BlendMode_Exclusion,
    BlendMode_Subtract,
    BlendMode_Divide,

    BlendMode_Hue,
    BlendMode_Saturation,
    BlendMode_Color,
    BlendMode_Luminosity,

    BlendMode_TERM
};

// The two Clip Studio Paint modes above (Glow Dodge after Color Dodge, Add
// (Glow) after Linear Dodge) are reverse-engineered from CSP renders, see
// tools/csp_blend_re/RESULTS.md. Both are the classic per-channel formulas
// (add-clamp / color dodge), but CSP feeds the ALPHA-PREMULTIPLIED source
// color into the blend (out = F(bg, a*fg)) instead of blending the straight
// color and compositing with alpha afterwards like Photoshop; see
// isPremultipliedSrcBlendMode(). Glow Dodge's fg=1 edge yields 255 only when
// bg>0 (0/0 -> 0), not unconditional white.

BlendMode getBlendModeFromPSD(const std::string& aPSDMode, bool aCspTslyFlag = false);

// true for Hue, Saturation, Color, Luminosity, Darker Color and Lighter Color:
// the blend function needs the whole RGB triple of both pixels, not one scalar
// per channel (see BLEND_NONSEPARABLE in LayerDrawingFrag). Explicit list, not
// a range: the enum is ordered for the UI, not by blend family.
inline bool isNonSeparableBlendMode(BlendMode aMode) {
    switch (aMode) {
    case BlendMode_Hue:
    case BlendMode_Saturation:
    case BlendMode_Color:
    case BlendMode_Luminosity:
    case BlendMode_DarkerColor:
    case BlendMode_LighterColor:
        return true;
    default:
        return false;
    }
}

// true for the CSP modes: the blend receives src.rgb * src.a (premultiplied,
// rounded to 8-bit like CSP's stored layer pixels) and the result replaces
// the backdrop, so the framebuffer alpha pass is bypassed (opaque write).
// See BLEND_PREMULTIPLIED_SRC in LayerDrawingFrag.glsl.
//
// Coverage caveat: as FOLDER modes (S12) these two are verified only against
// AnimeEffects' own CPU reference (suiteFolderBlend) - neither Krita nor GIMP
// can author a folder whose members composite with one of these premultiplied-src
// modes, so there is no third-party golden to pin them against. The shader math
// itself is golden-covered for the color-dodge/add behavior.
inline bool isPremultipliedSrcBlendMode(BlendMode aMode) {
    return aMode == BlendMode_AddGlow || aMode == BlendMode_GlowDodge;
}

QString getBlendFuncNameFromBlendMode(BlendMode aMode);

QString getBlendNameFromBlendMode(BlendMode aMode);

QString getQuadIdFromBlendMode(BlendMode aMode);

BlendMode getBlendModeFromQuadId(const QString& aName);

} // namespace img

#endif // IMG_BLENDMODE_H
