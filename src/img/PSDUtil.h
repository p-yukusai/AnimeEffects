#ifndef IMG_PSDUTIL_H
#define IMG_PSDUTIL_H

#include <memory>
#include <QRect>
#include "XC.h"
#include "img/PSDFormat.h"

namespace img {

// CSP marks layers with proprietary settings via a "tsly" additional-info block whose
// 4-byte value is 0 (CSP-specific) instead of 1 (plain): CSP's Add (Glow)/Glow Dodge are
// exported with the plain "lddg"/"div " blend keys plus this flag, so importers need it
// to tell them apart from LinearDodge/ColorDodge. Adobe never writes "tsly", so files
// without the block always read as plain. See getBlendModeFromPSD.
//
// The dichotomy (0 = CSP glow mode, 1 = plain, identical 4CCs) was verified against a
// pair of CSP-authored PSDs exported from the same document - one with the glow modes,
// one with their non-glow equivalents. Those exports live outside the repo (verified on
// the reverse-engineering author's machine); nothing in tests/golden covers AddGlow/
// GlowDodge because neither Krita nor GIMP can write the "tsly" block.
inline bool psdHasCspTslyFlag(const PSDFormat::Layer& aLayer) {
    for (const auto& info : aLayer.additionalInfos) {
        if (info->key == "tsly" && info->dataLength >= 4)
            return info->data[0] == 0 && info->data[1] == 0 && info->data[2] == 0 && info->data[3] == 0;
    }
    return false;
}

// One additional-layer-info block by key, or null when the layer carries none.
// The block lives in the PSDFormat the layer belongs to, so the pointer is
// borrowed and stays valid exactly as long as that format does. The scan is
// deliberately linear: a layer carries a handful of these blocks, and the reader
// appends them while parsing, so an index would only be another thing to keep in
// sync with the list.
inline const PSDFormat::AdditionalLayerInfo* psdFindAdditionalInfo(const PSDFormat::Layer& aLayer, const char* aKey) {
    for (const auto& info : aLayer.additionalInfos) {
        if (info->key == aKey) {
            return info.get();
        }
    }
    return nullptr;
}

// Whether the layer carries red, green and blue planes, which are the only ones
// that can become a texture. An alpha-only or mask-only layer has nothing to
// draw, and a fill or adjustment layer keeps its appearance as parameters in an
// additional-info block instead of as pixel channels. See getChannelCount for the
// per-colour-mode channel counts this reads against.
inline bool psdHasRgbChannels(const PSDFormat::Layer& aLayer) {
    bool red = false;
    bool green = false;
    bool blue = false;
    for (const auto& channel : aLayer.channels) {
        if (channel->id == 0) {
            red = true;
        } else if (channel->id == 1) {
            green = true;
        } else if (channel->id == 2) {
            blue = true;
        }
    }
    return red && green && blue;
}

// The layer kinds this importer cannot fully reproduce. None means ordinary
// pixel data that imports as authored; every other value is a kind the user has
// to hear about, because the import either degrades to an empty layer (the
// fill/adjustment/no-data kinds) or keeps the pixels but loses the editability
// (text and smart objects).
//
// The keys are the per-layer additional-info 4CCs Photoshop writes, already
// parsed into PSDFormat::Layer::additionalInfos. The scan order matters: the
// text and smart-object keys are checked before the "no RGB planes" test,
// because those layers can legitimately carry their rasterized pixels in the
// layer record -- whether they actually do is a separate question the loader
// answers from the decoded image, so this classifier stays a pure key check.
enum PsdUnsupportedLayer {
    PsdUnsupportedLayer_None = 0,
    PsdUnsupportedLayer_SolidFill,    // "SoCo"
    PsdUnsupportedLayer_GradientFill, // "GdFl"
    PsdUnsupportedLayer_PatternFill,  // "PtFl"
    // "brit"/"levl"/"curv"/"hue2"/"blnc"/"vibA"/"phfl"/"grdm"/"mixr"/"nvrt"/
    // "thrs"/"post"/"expA"
    PsdUnsupportedLayer_Adjustment,
    PsdUnsupportedLayer_Text,        // "TySh"
    PsdUnsupportedLayer_SmartObject, // "SoLd"/"PlLd"
    PsdUnsupportedLayer_NoImageData, // no RGB planes and no kind key we know
};

inline PsdUnsupportedLayer psdUnsupportedLayerKind(const PSDFormat::Layer& aLayer) {
    if (psdFindAdditionalInfo(aLayer, "TySh")) {
        return PsdUnsupportedLayer_Text;
    }
    if (psdFindAdditionalInfo(aLayer, "SoLd") || psdFindAdditionalInfo(aLayer, "PlLd")) {
        return PsdUnsupportedLayer_SmartObject;
    }

    // A fill layer is only a loss when there are no pixels to fall back on. A
    // writer that flattened one into RGB planes imported correctly, so it must
    // not be warned about.
    const bool hasRgb = psdHasRgbChannels(aLayer);
    if (!hasRgb) {
        if (psdFindAdditionalInfo(aLayer, "SoCo")) {
            return PsdUnsupportedLayer_SolidFill;
        }
        if (psdFindAdditionalInfo(aLayer, "GdFl")) {
            return PsdUnsupportedLayer_GradientFill;
        }
        if (psdFindAdditionalInfo(aLayer, "PtFl")) {
            return PsdUnsupportedLayer_PatternFill;
        }
    }

    // The adjustment kinds Photoshop stores per layer. One list rather than a
    // chain of find calls: the keys are opaque 4CCs and this is the same scan the
    // tsly flag above does. Checked even when RGB planes are listed: an
    // adjustment layer's rect is zero, so any planes it carries are empty, and
    // the layer still renders as nothing.
    static const char* const kAdjustmentKeys[] = {
        "brit", "levl", "curv", "hue2", "blnc", "vibA", "phfl",
        "grdm", "mixr", "nvrt", "thrs", "post", "expA",
    };
    for (const char* key : kAdjustmentKeys) {
        if (psdFindAdditionalInfo(aLayer, key)) {
            return PsdUnsupportedLayer_Adjustment;
        }
    }

    if (!hasRgb) {
        // A plain layer with no colour planes: a mask-only remnant, or a writer
        // that dropped the pixels. Either way the renderer cannot draw it.
        return PsdUnsupportedLayer_NoImageData;
    }

    return PsdUnsupportedLayer_None;
}

// The kind in words, shared by every message that reports one, so the same layer
// is never called a "solid colour layer" on import and a "fill layer" on relink.
// Deliberately untranslated: these are fragments a sentence embeds, and the
// sentence is translated where it is built (ctrl::ImageFileLoader,
// gui::res::ResourceUpdater).
inline const char* psdUnsupportedLayerName(PsdUnsupportedLayer aKind) {
    switch (aKind) {
    case PsdUnsupportedLayer_SolidFill:
        return "solid fill";
    case PsdUnsupportedLayer_GradientFill:
        return "gradient fill";
    case PsdUnsupportedLayer_PatternFill:
        return "pattern fill";
    case PsdUnsupportedLayer_Adjustment:
        return "adjustment";
    case PsdUnsupportedLayer_Text:
        return "text";
    case PsdUnsupportedLayer_SmartObject:
        return "smart object";
    case PsdUnsupportedLayer_NoImageData:
        return "no image data";
    case PsdUnsupportedLayer_None:
        break;
    }
    return "unknown";
}

// Whether the kind's pixels survive as a flattened raster. Text and smart objects
// do: Photoshop writes the composite into the layer record, so the import looks
// right while its editability is gone. The empty kinds have nothing to draw.
inline bool psdUnsupportedLayerKeepsRaster(PsdUnsupportedLayer aKind) {
    return aKind == PsdUnsupportedLayer_Text || aKind == PsdUnsupportedLayer_SmartObject;
}

class PSDUtil {
public:
    enum ColorFormat { ColorFormat_RGB8, ColorFormat_RGBA8 };

    static XCMemBlock
    makeClippedImage(const uint8* aTarget, const QRect& aRectT, const uint8* aBase, const QRect& aRectB);

    static size_t encodePackBits(const uint8* aSrc, uint8* aDst, size_t aLength);

    static XCMemBlock
    encodePlanePackBits(const uint8* aSrc, size_t aSrcLength, int aWidth, int aHeight, int aSrcStride);

    static bool makeChanneledImage(
        PSDFormat::Layer& aDst, const PSDFormat::Header& aHeader, const XCMemBlock& aSrc, ColorFormat aSrcFormat
    );

    static bool makeChanneledImage(
        PSDFormat::ImageData& aDst, const PSDFormat::Header& aHeader, const XCMemBlock& aSrc, ColorFormat aSrcFormat
    );

    static bool makeChanneledImage(
        PSDFormat::ChannelList& aDst,
        const PSDFormat::Header& aHeader,
        const XCMemBlock& aSrc,
        ColorFormat aSrcFormat,
        int aSrcWidth,
        int aSrcHeight
    );

    static bool decodePlanePackBits(
        uint8* aDst, size_t aDstLength, const uint8* aSrc, size_t aSrcLength, int aWidth, int aHeight, int aDstStride
    );

    static XCMemBlock
    makeInterleavedImage(const PSDFormat::Header& aHeader, const PSDFormat::Layer& aLayer, ColorFormat aFormat);

    static XCMemBlock
    makeInterleavedImage(const PSDFormat::Header& aHeader, const PSDFormat::ImageData& aImageData, ColorFormat aFormat);

    static XCMemBlock makeInterleavedImage(
        const PSDFormat::Header& aHeader,
        const PSDFormat::ChannelList& aChannels,
        ColorFormat aFormat,
        int aWidth,
        int aHeight
    );
};

} // namespace img

#endif // IMG_PSDUTIL_H
