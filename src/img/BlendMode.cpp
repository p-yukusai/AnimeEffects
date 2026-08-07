#include "img/BlendMode.h"
#include "img/BlendModeName.h"

namespace img {

BlendMode getBlendModeFromPSD(const std::string& aMode, bool aCspTslyFlag) {
    // Note: CSP exports its own "Add (Glow)"/"Glow Dodge" modes as plain
    // "lddg"/"div " keys PLUS a "tsly" layer-info flag (value 0 = CSP-specific
    // layer, 1 = plain; verified against a pair of CSP-authored PSDs - the same
    // document exported with the glow modes vs the plain equivalents differs ONLY
    // in that flag, the blend 4CCs are identical). Adobe never writes "tsly", so
    // aCspTslyFlag is false for every non-CSP file. The .anm format carries the
    // modes via the "adgl"/"gldd" quad ids instead.
    if (aMode == "norm") {
        return BlendMode_Normal;
    } else if (aMode == "pass") {
        // pass-through only appears on group records (and is Photoshop's default group
        // mode): the children blend with the backdrop directly, which is exactly what a
        // Normal (non-composite) folder does here
        return BlendMode_Normal;
    } else if (aMode == "dark") {
        return BlendMode_Darken;
    } else if (aMode == "mul ") {
        return BlendMode_Multiply;
    } else if (aMode == "idiv") {
        return BlendMode_ColorBurn;
    } else if (aMode == "lbrn") {
        return BlendMode_LinearBurn;
    } else if (aMode == "lite") {
        return BlendMode_Lighten;
    } else if (aMode == "scrn") {
        return BlendMode_Screen;
    } else if (aMode == "div ") {
        return aCspTslyFlag ? BlendMode_GlowDodge : BlendMode_ColorDodge;
    } else if (aMode == "lddg") {
        return aCspTslyFlag ? BlendMode_AddGlow : BlendMode_LinearDodge;
    } else if (aMode == "over") {
        return BlendMode_Overlay;
    } else if (aMode == "sLit") {
        return BlendMode_SoftLight;
    } else if (aMode == "hLit") {
        return BlendMode_HardLight;
    } else if (aMode == "vLit") {
        return BlendMode_VividLight;
    } else if (aMode == "lLit") {
        return BlendMode_LinearLight;
    } else if (aMode == "pLit") {
        return BlendMode_PinLight;
    } else if (aMode == "hMix") {
        return BlendMode_HardMix;
    } else if (aMode == "diff") {
        return BlendMode_Difference;
    } else if (aMode == "smud") {
        return BlendMode_Exclusion;
    } else if (aMode == "fsub") {
        return BlendMode_Subtract;
    } else if (aMode == "fdiv") {
        return BlendMode_Divide;
    } else if (aMode == "hue ") {
        return BlendMode_Hue;
    } else if (aMode == "sat ") {
        return BlendMode_Saturation;
    } else if (aMode == "colr") {
        return BlendMode_Color;
    } else if (aMode == "lum ") {
        return BlendMode_Luminosity;
    } else if (aMode == "dkCl") {
        return BlendMode_DarkerColor;
    } else if (aMode == "lgCl") {
        return BlendMode_LighterColor;
    }

    return BlendMode_TERM;
}

QString getBlendFuncNameFromBlendMode(BlendMode aMode) {
    switch (aMode) {
    case BlendMode_Normal:
        return "Normal";
    case BlendMode_Darken:
        return "Darken";
    case BlendMode_Multiply:
        return "Multiply";
    case BlendMode_ColorBurn:
        return "ColorBurn";
    case BlendMode_LinearBurn:
        return "LinearBurn";
    case BlendMode_Lighten:
        return "Lighten";
    case BlendMode_Screen:
        return "Screen";
    case BlendMode_ColorDodge:
        return "ColorDodge";
    case BlendMode_LinearDodge:
        return "LinearDodge";
    case BlendMode_Overlay:
        return "Overlay";
    case BlendMode_SoftLight:
        return "SoftLight";
    case BlendMode_HardLight:
        return "HardLight";
    case BlendMode_VividLight:
        return "VividLight";
    case BlendMode_LinearLight:
        return "LinearLight";
    case BlendMode_PinLight:
        return "PinLight";
    case BlendMode_HardMix:
        return "HardMix";
    case BlendMode_Difference:
        return "Difference";
    case BlendMode_Exclusion:
        return "Exclusion";
    case BlendMode_Subtract:
        return "Subtract";
    case BlendMode_Divide:
        return "Divide";
    case BlendMode_AddGlow:
        return "AddGlow";
    case BlendMode_GlowDodge:
        return "GlowDodge";
    case BlendMode_Hue:
        return "Hue";
    case BlendMode_Saturation:
        return "Saturation";
    case BlendMode_Color:
        return "Color";
    case BlendMode_Luminosity:
        return "Luminosity";
    case BlendMode_DarkerColor:
        return "DarkerColor";
    case BlendMode_LighterColor:
        return "LighterColor";
    default:
        return "Normal";
    }
}

QString getBlendNameFromBlendMode(BlendMode aMode) {
    switch (aMode) {
    case BlendMode_Normal:
        return BlendModeName::tr("Normal");
    case BlendMode_Darken:
        return BlendModeName::tr("Darken");
    case BlendMode_Multiply:
        return BlendModeName::tr("Multiply");
    case BlendMode_ColorBurn:
        return BlendModeName::tr("Color Burn");
    case BlendMode_LinearBurn:
        return BlendModeName::tr("Linear Burn");
    case BlendMode_Lighten:
        return BlendModeName::tr("Lighten");
    case BlendMode_Screen:
        return BlendModeName::tr("Screen");
    case BlendMode_ColorDodge:
        return BlendModeName::tr("Color Dodge");
    case BlendMode_LinearDodge:
        return BlendModeName::tr("Linear Dodge");
    case BlendMode_Overlay:
        return BlendModeName::tr("Overlay");
    case BlendMode_SoftLight:
        return BlendModeName::tr("Soft Light");
    case BlendMode_HardLight:
        return BlendModeName::tr("Hard Light");
    case BlendMode_VividLight:
        return BlendModeName::tr("Vivid Light");
    case BlendMode_LinearLight:
        return BlendModeName::tr("Linear Light");
    case BlendMode_PinLight:
        return BlendModeName::tr("Pin Light");
    case BlendMode_HardMix:
        return BlendModeName::tr("Hard Mix");
    case BlendMode_Difference:
        return BlendModeName::tr("Difference");
    case BlendMode_Exclusion:
        return BlendModeName::tr("Exclusion");
    case BlendMode_Subtract:
        return BlendModeName::tr("Subtract");
    case BlendMode_Divide:
        return BlendModeName::tr("Divide");
    case BlendMode_AddGlow:
        return BlendModeName::tr("Add (Glow)");
    case BlendMode_GlowDodge:
        return BlendModeName::tr("Glow Dodge");
    case BlendMode_Hue:
        return BlendModeName::tr("Hue");
    case BlendMode_Saturation:
        return BlendModeName::tr("Saturation");
    case BlendMode_Color:
        return BlendModeName::tr("Color");
    case BlendMode_Luminosity:
        return BlendModeName::tr("Luminosity");
    case BlendMode_DarkerColor:
        return BlendModeName::tr("Darker Color");
    case BlendMode_LighterColor:
        return BlendModeName::tr("Lighter Color");
    default:
        return BlendModeName::tr("Normal");
    }
}

QString getQuadIdFromBlendMode(BlendMode aMode) {
    switch (aMode) {
    case BlendMode_Normal:
        return "norm";
    case BlendMode_Darken:
        return "dark";
    case BlendMode_Multiply:
        return "mul ";
    case BlendMode_ColorBurn:
        return "idiv";
    case BlendMode_LinearBurn:
        return "lbrn";
    case BlendMode_Lighten:
        return "lite";
    case BlendMode_Screen:
        return "scrn";
    case BlendMode_ColorDodge:
        return "div ";
    case BlendMode_LinearDodge:
        return "lddg";
    case BlendMode_Overlay:
        return "over";
    case BlendMode_SoftLight:
        return "sLit";
    case BlendMode_HardLight:
        return "hLit";
    case BlendMode_VividLight:
        return "vLit";
    case BlendMode_LinearLight:
        return "lLit";
    case BlendMode_PinLight:
        return "pLit";
    case BlendMode_HardMix:
        return "hMix";
    case BlendMode_Difference:
        return "diff";
    case BlendMode_Exclusion:
        return "smud";
    case BlendMode_Subtract:
        return "fsub";
    case BlendMode_Divide:
        return "fdiv";
    // CSP's own quad ids for the glow modes (the .anm format). CSP writes
    // these modes to PSD as plain "lddg"/"div " (no way to distinguish them
    // from the standard modes there), so there is no PSD import mapping.
    case BlendMode_AddGlow:
        return "adgl";
    case BlendMode_GlowDodge:
        return "gldd";
    case BlendMode_Hue:
        return "hue ";
    case BlendMode_Saturation:
        return "sat ";
    case BlendMode_Color:
        return "colr";
    case BlendMode_Luminosity:
        return "lum ";
    case BlendMode_DarkerColor:
        return "dkCl";
    case BlendMode_LighterColor:
        return "lgCl";
    default:
        return "    ";
    }
}

BlendMode getBlendModeFromQuadId(const QString& aName) {
    if (aName == "norm") {
        return BlendMode_Normal;
    } else if (aName == "dark") {
        return BlendMode_Darken;
    } else if (aName == "mul ") {
        return BlendMode_Multiply;
    } else if (aName == "idiv") {
        return BlendMode_ColorBurn;
    } else if (aName == "lbrn") {
        return BlendMode_LinearBurn;
    } else if (aName == "lite") {
        return BlendMode_Lighten;
    } else if (aName == "scrn") {
        return BlendMode_Screen;
    } else if (aName == "div ") {
        return BlendMode_ColorDodge;
    } else if (aName == "lddg") {
        return BlendMode_LinearDodge;
    } else if (aName == "over") {
        return BlendMode_Overlay;
    } else if (aName == "sLit") {
        return BlendMode_SoftLight;
    } else if (aName == "hLit") {
        return BlendMode_HardLight;
    } else if (aName == "vLit") {
        return BlendMode_VividLight;
    } else if (aName == "lLit") {
        return BlendMode_LinearLight;
    } else if (aName == "pLit") {
        return BlendMode_PinLight;
    } else if (aName == "hMix") {
        return BlendMode_HardMix;
    } else if (aName == "diff") {
        return BlendMode_Difference;
    } else if (aName == "smud") {
        return BlendMode_Exclusion;
    } else if (aName == "fsub") {
        return BlendMode_Subtract;
    } else if (aName == "fdiv") {
        return BlendMode_Divide;
    } else if (aName == "adgl") {
        return BlendMode_AddGlow;
    } else if (aName == "gldd") {
        return BlendMode_GlowDodge;
    } else if (aName == "hue ") {
        return BlendMode_Hue;
    } else if (aName == "sat ") {
        return BlendMode_Saturation;
    } else if (aName == "colr") {
        return BlendMode_Color;
    } else if (aName == "lum ") {
        return BlendMode_Luminosity;
    } else if (aName == "dkCl") {
        return BlendMode_DarkerColor;
    } else if (aName == "lgCl") {
        return BlendMode_LighterColor;
    }

    return BlendMode_TERM;
}

} // namespace img
