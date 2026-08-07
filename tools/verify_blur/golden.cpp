// See golden.h for the workflow. Design constraints that make the comparison fair:
//   - every layer bitmap is canvas-sized (the GridMeshCreator wedge-inset artifact only
//     shows at layer-mesh edges; full-canvas layers move it to the canvas border, which
//     case.json's borderCrop then excludes from the diff)
//   - the bottom layer is fully opaque, so the flattened result has alpha 255 everywhere
//     (AE readbacks are straight-alpha RGBA8, Krita exports straight alpha; a fully opaque
//     canvas sidesteps premultiplied-vs-straight ambiguity)
//   - content rects are integer-aligned with no antialiasing, so any divergence is blend
//     math, not rasterization noise
#include "golden.h"

#include <cstring>
#include <fstream>
#include <vector>
#include <QBuffer>
#include <QDir>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/ObjectTree.h"
#include "core/Project.h"
#include "ctrl/ImageFileLoader.h"
#include "deps/zip_file.h"
#include "gl/DeviceInfo.h"
#include "img/PSDFormat.h"
#include "img/PSDWriter.h"
#include "harness.h"
#include "scene.h"

namespace golden {
namespace {

using namespace hb;

//-------------------------------------------------------------------------------------------------
// abstract document model: children are listed TOP-FIRST (topmost layer/group first),
// matching the ORA spec's document order; the PSD emitter reverses as it writes
enum class Mode { Normal, Multiply, Screen, PassThrough, Hue, Saturation, Color, Luminosity, DarkerColor, LighterColor };

struct Node {
    bool isGroup = false;
    QString name;
    Mode mode = Mode::Normal;
    int opacity255 = 255;
    bool visible = true;
    // layers only: content painted into a canvas-sized bitmap (transparent elsewhere)
    QRect content;
    QColor color;
    QColor rightColor; // if valid, the content's right half uses this color instead
    int alpha = 255;
    // procedural painters (exclusive with flat/rightColor): paint the same geometry the
    // S12 GPU-vs-CPU suite uses (verify_blur.cpp fillWedgeOpaque) so a blend-mode
    // divergence region can be cross-checked against a reference app on a FLAT scene
    // (identity transforms = exact texel sampling, so any divergence is blend math,
    // not resampling). offX/offY shift the drawn geometry within the layer bitmap.
    bool sweep = false; // vertical gradient bar (v, 255-v, 128), opaque
    bool wedge = false; // full wedge pattern, opaque where covered, base color elsewhere
    int offX = 0, offY = 0;
    std::vector<Node> children; // groups only, top-first
};

struct Doc {
    QSize canvas{64, 64};
    std::vector<Node> children; // top-first
};

Node layer(const QString& aName, const QRect& aContent, const QColor& aColor, int aAlpha = 255) {
    Node n;
    n.name = aName;
    n.content = aContent;
    n.color = aColor;
    n.alpha = aAlpha;
    return n;
}

Node group(Mode aMode, int aOpacity255, const QString& aName, std::vector<Node> aChildren) {
    Node n;
    n.isGroup = true;
    n.name = aName;
    n.mode = aMode;
    n.opacity255 = aOpacity255;
    n.children = std::move(aChildren);
    return n;
}

//-------------------------------------------------------------------------------------------------
// layer bitmap painting
QImage paintLayer(const Doc& aDoc, const Node& aNode) {
    QImage img(aDoc.canvas, QImage::Format_RGBA8888);
    img.fill(Qt::transparent);
    for (int y = aNode.content.top(); y <= aNode.content.bottom() && y < aDoc.canvas.height(); ++y) {
        if (y < 0)
            continue;
        auto* scan = img.scanLine(y);
        for (int x = aNode.content.left(); x <= aNode.content.right() && x < aDoc.canvas.width(); ++x) {
            if (x < 0)
                continue;
            auto* px = scan + x * 4;
            px[0] = px[1] = px[2] = px[3] = 0;
            const int wx = x - aNode.offX, wy = y - aNode.offY;
            if (aNode.wedge) {
                // fillWedgeOpaque port: the base color shows where the pattern does not cover
                px[3] = aNode.alpha;
                px[0] = (uchar)aNode.color.red();
                px[1] = (uchar)aNode.color.green();
                px[2] = (uchar)aNode.color.blue();
                const auto over = [&](int r, int g, int b) {
                    px[0] = (uchar)r;
                    px[1] = (uchar)g;
                    px[2] = (uchar)b;
                    px[3] = 255;
                };
                if (wx >= 6 && wx <= 40 && wy >= 8 && wy <= 28)
                    over(200, 80, 60);
                if (wx >= 44 && wx <= 56 && wy >= 6 && wy <= 40) {
                    const int v = (wy - 6) * 255 / 34;
                    over(v, 255 - v, 128);
                }
                if (wy >= 30 && wy <= 42 && wx >= 8 && wx <= 8 + (wy - 30) * 2)
                    over(40, 200, 240);
                if (wy >= 5 && wy <= 40 && wx == 3 + wy)
                    over(255, 255, 255);
                if ((wx - 30) * (wx - 30) + (wy - 38) * (wy - 38) <= 4)
                    over(255, 255, 255);
            } else if (aNode.sweep) {
                const int v = (wy - aNode.content.top()) * 255 / qMax(1, aNode.content.height());
                px[0] = (uchar)qBound(0, v, 255);
                px[1] = (uchar)qBound(0, 255 - v, 255);
                px[2] = 128;
                px[3] = aNode.alpha;
            } else {
                const bool right = aNode.rightColor.isValid() && x >= aDoc.canvas.width() / 2;
                const QColor& c = right ? aNode.rightColor : aNode.color;
                px[0] = (uchar)c.red();
                px[1] = (uchar)c.green();
                px[2] = (uchar)c.blue();
                px[3] = (uchar)aNode.alpha;
            }
        }
    }
    return img;
}

//-------------------------------------------------------------------------------------------------
// PSD emission through the production img::PSDWriter (mirrors the S11 record layout:
// bottom-first file order, per group [bounding-close, members bottom-first, open])
const char* psdModeKey(Mode aMode) {
    switch (aMode) {
    case Mode::Multiply:
        return "mul ";
    case Mode::Screen:
        return "scrn";
    case Mode::PassThrough:
        return "pass";
    case Mode::Hue:
        return "hue ";
    case Mode::Saturation:
        return "sat ";
    case Mode::Color:
        return "colr";
    case Mode::Luminosity:
        return "lum ";
    case Mode::DarkerColor:
        return "dkCl";
    case Mode::LighterColor:
        return "lgCl";
    default:
        return "norm";
    }
}

struct PsdEmitter {
    img::PSDFormat& format;
    const Doc& doc;

    void setRect(img::PSDFormat::Layer* aLayer, int l, int t, int r, int b) {
        aLayer->rect.edge[0] = t;
        aLayer->rect.edge[1] = l;
        aLayer->rect.edge[2] = b;
        aLayer->rect.edge[3] = r;
    }

    void emitLayer(const Node& aNode) {
        auto* rec = new img::PSDFormat::Layer();
        rec->name = aNode.name.toStdString();
        const int w = doc.canvas.width(), h = doc.canvas.height();
        setRect(rec, 0, 0, w, h);
        rec->blendMode = psdModeKey(aNode.mode);
        rec->opacity = (uint8)aNode.opacity255;
        rec->flags = aNode.visible ? 0 : 0x02; // bit 1 = hidden
        const QImage img = paintLayer(doc, aNode);
        const int size = w * h;
        for (const sint16 id : {sint16(0), sint16(1), sint16(2), sint16(-1)}) {
            auto* chan = new img::PSDFormat::Channel();
            chan->id = id;
            chan->compressionId = 0;
            chan->dataLength = (uint32)size;
            chan->data.reset(new uint8[(size_t)size]);
            const int plane = id < 0 ? 3 : (int)id;
            for (int y = 0; y < h; ++y) {
                const auto* scan = img.constScanLine(y);
                auto* dst = chan->data.get() + y * w;
                for (int x = 0; x < w; ++x)
                    dst[x] = scan[x * 4 + plane];
            }
            rec->channels.push_back(img::PSDFormat::ChannelPtr(chan));
        }
        format.layerAndMaskInfo().layers.push_back(img::PSDFormat::LayerPtr(rec));
    }

    void emitFolderRecord(const QString& aName, Mode aMode, int aOpacity255, bool aIsBounding) {
        auto* rec = new img::PSDFormat::Layer();
        rec->name = aName.toStdString();
        // mimic the folder records real writers emit (data/sample.psd): 0x0 rect, flags
        // 0x18, four RLE channels with empty payloads (PSDWriter adds the compression-id
        // bytes to the record's length field). A zero-channel/full-canvas record like
        // S11 writes loads fine in AE but Krita silently refuses to open the file.
        setRect(rec, 0, 0, 0, 0);
        rec->blendMode = psdModeKey(aMode);
        rec->opacity = (uint8)aOpacity255;
        rec->flags = 0x18;
        for (const sint16 id : {sint16(-1), sint16(0), sint16(1), sint16(2)}) {
            auto* chan = new img::PSDFormat::Channel();
            chan->id = id;
            chan->compressionId = 1; // RLE, zero rows for the 0x0 rect
            chan->dataLength = 0;
            chan->data.reset(new uint8[1]);
            rec->channels.push_back(img::PSDFormat::ChannelPtr(chan));
        }
        auto* info = new img::PSDFormat::AdditionalLayerInfo();
        info->key = "lsct";
        info->dataLength = 12; // entry (4) + 8BIM (4) + key (4)
        info->data.reset(new uint8[12]);
        std::memset(info->data.get(), 0, 12);
        info->data[3] = aIsBounding ? 3 : 1; // bounding section divider / open folder
        std::memcpy(info->data.get() + 4, "8BIM", 4);
        // real writers stamp the group's blend key on both records
        std::memcpy(info->data.get() + 8, psdModeKey(aMode), 4);
        rec->additionalInfos.push_back(img::PSDFormat::AdditionalLayerInfoPtr(info));
        format.layerAndMaskInfo().layers.push_back(img::PSDFormat::LayerPtr(rec));
    }

    void emitChildren(const std::vector<Node>& aChildren) { // top-first in, bottom-first out
        for (auto it = aChildren.rbegin(); it != aChildren.rend(); ++it) {
            if (it->isGroup) {
                emitFolderRecord("</Layer group>", it->mode, 255, true);
                emitChildren(it->children);
                emitFolderRecord(it->name, it->mode, it->opacity255, false);
            } else {
                emitLayer(*it);
            }
        }
    }
};

bool writePsd(const QString& aPath, const Doc& aDoc) {
    img::PSDFormat format;
    format.header().version = 1;
    format.header().channels = 4;
    format.header().height = aDoc.canvas.height();
    format.header().width = aDoc.canvas.width();
    format.header().depth = 8;
    format.header().mode = img::PSDFormat::ColorMode_RGB;
    format.imageData().compressionId = 0;

    PsdEmitter emitter{format, aDoc};
    emitter.emitChildren(aDoc.children);

    // merged image data: zeroed raw channels sized the way PSDReader's raw branch parses
    // the section (the layered import never reads the merged pixels; Krita builds the
    // document from the layer records)
    {
        const int w = aDoc.canvas.width(), h = aDoc.canvas.height();
        const uint32 size = (uint32)(((w * h + 7) / 8) * h);
        for (const sint16 id : {sint16(0), sint16(1), sint16(2), sint16(-1)}) {
            auto* chan = new img::PSDFormat::Channel();
            chan->id = id;
            chan->compressionId = 0;
            chan->dataLength = size;
            chan->data.reset(new uint8[size]);
            std::memset(chan->data.get(), 0, size);
            format.imageData().channels.push_back(img::PSDFormat::ChannelPtr(chan));
        }
    }

    std::ofstream out(aPath.toLocal8Bit().constData(), std::ios::binary);
    img::PSDWriter writer(out, format);
    out.close();
    return writer.resultCode() == img::PSDWriter::ResultCode_Success;
}

//-------------------------------------------------------------------------------------------------
// ORA emission (miniz zip; document order is top-first per the spec, which is also the
// order oraParser flattens and ImageFileLoader stacks)
const char* oraCompositeOp(Mode aMode) {
    switch (aMode) {
    case Mode::Multiply:
        return "svg:multiply";
    case Mode::Screen:
        return "svg:screen";
    case Mode::Hue:
        return "svg:hue";
    case Mode::Saturation:
        return "svg:saturation";
    case Mode::Color:
        return "svg:color";
    case Mode::Luminosity:
        return "svg:luminosity";
    default:
        return "svg:src-over"; // PassThrough/dkCl/lgCl have no ORA equivalent; never emitted
    }
}

QString oraOpacity(int aOpacity255) { return QString::number(aOpacity255 / 255.0, 'f', 3); }

struct OraEmitter {
    const Doc& doc;
    miniz_cpp::zip_file& zip;
    int layerCounter = 0;

    QString pngEntry(const Node& aNode) {
        const QString entry = QString("data/layer%1.png").arg(layerCounter++);
        const QImage img = paintLayer(doc, aNode);
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        img.save(&buffer, "PNG");
        zip.writestr(entry.toStdString(), buffer.data().toStdString());
        return entry;
    }

    void emitChildren(QString& aXml, const std::vector<Node>& aChildren) {
        for (const Node& n : aChildren) {
            const QString vis = n.visible ? "visible" : "hidden";
            if (n.isGroup) {
                aXml += QString("<stack name='%1' opacity='%2' visibility='%3' composite-op='%4'>\n")
                            .arg(n.name, oraOpacity(n.opacity255), vis, oraCompositeOp(n.mode));
                emitChildren(aXml, n.children);
                aXml += "</stack>\n";
            } else {
                aXml += QString(
                            "<layer name='%1' src='%2' x='0' y='0' opacity='%3' visibility='%4'"
                            " composite-op='%5'/>\n")
                            .arg(n.name, pngEntry(n), oraOpacity(n.opacity255), vis, oraCompositeOp(n.mode));
            }
        }
    }
};

bool writeOra(const QString& aPath, const Doc& aDoc) {
    miniz_cpp::zip_file zip;
    zip.writestr("mimetype", "image/openraster");
    QString xml = "<?xml version='1.0' encoding='UTF-8'?>\n";
    xml += QString("<image w='%1' h='%2' version='0.0.1'>\n<stack name='root'>\n")
               .arg(aDoc.canvas.width())
               .arg(aDoc.canvas.height());
    OraEmitter emitter{aDoc, zip, 0};
    emitter.emitChildren(xml, aDoc.children);
    xml += "</stack>\n</image>\n";
    zip.writestr("stack.xml", xml.toStdString());
    // ImageFileLoader requires a mergedimage.png of the canvas size (contents unused)
    {
        QImage merged(aDoc.canvas, QImage::Format_RGBA8888);
        merged.fill(QColor(128, 128, 128, 255));
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        merged.save(&buffer, "PNG");
        zip.writestr("mergedimage.png", buffer.data().toStdString());
    }
    zip.save(aPath.toStdString());
    return QFile::exists(aPath);
}

//-------------------------------------------------------------------------------------------------
// the compiled-in case set. Painters (canvas 64x64):
//   bg     opaque, left half steel blue / right half ochre (distinct multiply/screen bases)
//   red    (200,60,50)   rect x 8..31  y 8..31
//   blue   (60,80,190)   rect x 24..47 y 24..47 (overlaps red)
//   green  (60,180,90)   rect x 16..47 y 40..55
//   yellow (220,200,80)  rect x 36..55 y 8..27
//   semi   (200,120,220) rect x 12..39 y 36..55 at alpha 160
//   ghost  opaque black full canvas (only used hidden; visible => glaring diff)
Node bgLayer() {
    Node n = layer("bg", QRect(0, 0, 64, 64), QColor(90, 140, 200));
    n.rightColor = QColor(170, 120, 90);
    return n;
}
Node redLayer() { return layer("red", QRect(8, 8, 24, 24), QColor(200, 60, 50)); }
Node blueLayer() { return layer("blue", QRect(24, 24, 24, 24), QColor(60, 80, 190)); }
Node greenLayer() { return layer("green", QRect(16, 40, 32, 16), QColor(60, 180, 90)); }
Node yellowLayer() { return layer("yellow", QRect(36, 8, 20, 20), QColor(220, 200, 80)); }
Node semiLayer() { return layer("semi", QRect(12, 36, 28, 20), QColor(200, 120, 220), 160); }
Node ghostLayer() { return layer("ghost", QRect(0, 0, 64, 64), QColor(0, 0, 0)); }

Doc flatBaseline() { return Doc{{64, 64}, {blueLayer(), redLayer(), bgLayer()}}; }

Doc groupMultiply() {
    return Doc{{64, 64}, {group(Mode::Multiply, 255, "grpMul", {blueLayer(), redLayer(), semiLayer()}), bgLayer()}};
}

Doc groupScreenOpacity() {
    // fractional group opacity: goldens for fractional-opacity PSD groups are authored
    // with GIMP - Krita IGNORES the opacity byte on PSD group records (probe-verified:
    // op=0 renders full strength) while GIMP honors it and lerps exactly like AE
    return Doc{{64, 64}, {group(Mode::Screen, 180, "grpScr", {greenLayer(), yellowLayer()}), bgLayer()}};
}

Doc groupNormal() { return Doc{{64, 64}, {group(Mode::Normal, 255, "grpNorm", {blueLayer(), redLayer()}), bgLayer()}}; }

Doc groupPassThrough() {
    Node mulBlue = blueLayer();
    mulBlue.mode = Mode::Multiply;
    return Doc{{64, 64}, {group(Mode::PassThrough, 255, "grpPass", {mulBlue, redLayer()}), bgLayer()}};
}

Doc groupOpacityHidden() {
    Node ghost = ghostLayer();
    ghost.visible = false;
    // fractional-opacity multiply group + a hidden layer (GIMP-authored, see above)
    return Doc{{64, 64}, {group(Mode::Multiply, 128, "grpMulHalf", {greenLayer(), ghost}), bgLayer()}};
}

Doc nestedGroups() {
    Node inner = group(Mode::Multiply, 255, "inner", {blueLayer()});
    return Doc{{64, 64}, {group(Mode::Screen, 255, "outer", {inner, yellowLayer()}), bgLayer()}};
}

// saturated two-tone top layer for the non-separable modes: its hue/saturation/luma
// differ from both bg halves, and it straddles the bg's own left/right color split
Node hsvLayer() {
    Node n = layer("hsv", QRect(12, 12, 40, 40), QColor(60, 80, 190));
    n.rightColor = QColor(200, 180, 60);
    return n;
}

Doc layerBlend(Mode aMode) {
    Node top = hsvLayer();
    top.mode = aMode;
    return Doc{{64, 64}, {top, bgLayer()}};
}

// non-separable blend over a FRACTIONAL-ALPHA backdrop: the semi layer (alpha 160,
// overlapping the hsv layer's region) sits over a bg that only covers the top part of
// the canvas, so under the hue layer's lower strip the backdrop is semi-transparent
// (premultiplied, alpha 160/255). The whole-RGB formulas require the STRAIGHT backdrop
// color, so this pins the dest un-premultiply in blendColor's BLEND_NONSEPARABLE
// branch: without it the hue blend sees alpha-scaled saturation/luma in that strip and
// diverges from Krita by ~30/255. The hsv layer must fully cover the semi layer: the
// harness readback is the raw PREMULTIPLIED framebuffer (the suite's opaque-canvas
// constraint keeps premult == straight), so any final pixel with alpha < 1 would diff
// against Krita's straight PNG by definition, not by blend math.
Doc layerBlendOverSemi(Mode aMode) {
    Node bg = bgLayer();
    bg.content = QRect(0, 0, 64, 40);
    Node semi = semiLayer();
    semi.content = QRect(12, 36, 28, 16); // fully inside the hsv layer's rect below
    Node top = hsvLayer();
    top.mode = aMode;
    return Doc{{64, 64}, {top, semi, bg}};
}

// S12 Color-mode parity probe: the wedge geometry whose GPU-vs-CPU replica check shows
// max=54/255 on a handful of pixels near the gray-crossing gradient bar (the S12 comment
// originally blamed ClipColor's "ill-conditioned ratios", but the ratios are algebraically
// bounded <= 1 where their branches fire — the more likely cause is the replica's
// resampler vs GPU bilinear at the wedge's hard edges under transform). This FLAT scene
// (identity layers, exact texel sampling) imports the same content through PSD and renders
// with Krita as truth. RESULT: passes at max=1/255 — our GPU Color formula is faithful and
// the S12 spike is a replica-resampling artifact (see the comment at verify_blur.cpp's
// runCase in the S12 suite).
Doc colorWedgeGray() {
    // opaque base + the full wedge pattern overlaid, top Content layer drawn shifted
    // (7, 9) so the two layers' edges, gray-crossing gradient bar and 1px line do not
    // coincide; both fully opaque for the premult==straight readback.
    Node bg = layer("bg", QRect(0, 0, 64, 64), QColor(90, 140, 200));
    bg.wedge = true;
    Node top = layer("top", QRect(0, 0, 64, 64), QColor(40, 60, 120));
    top.wedge = true;
    top.offX = 7;
    top.offY = 9;
    top.mode = Mode::Color;
    return Doc{{64, 64}, {top, bg}};
}

Doc colorSweepGray() {
    // no hard edges at all: pure gradient crossing gray, offset so the two gray rows
    // differ. Also passes at max=1/255, confirming the S12 spike is not ClipColor math.
    Node b = layer("bg", QRect(0, 0, 64, 64), QColor(0, 0, 0));
    b.sweep = true;
    Node top = layer("top", QRect(0, 0, 64, 64), QColor(0, 0, 0));
    top.sweep = true;
    top.offY = 16;
    top.mode = Mode::Color;
    return Doc{{64, 64}, {top, b}};
}

Doc groupBlend(Mode aMode, const QString& aName) {
    return Doc{{64, 64}, {group(aMode, 255, aName, {hsvLayer()}), bgLayer()}};
}

struct CaseSpec {
    QString name;
    QString fileName;
    Doc doc;
    QString author = "krita"; // which reference program authors expected.png
};

std::vector<CaseSpec> caseSpecs() {
    return {
        {"psd_flat_baseline", "input.psd", flatBaseline()},
        {"psd_group_multiply", "input.psd", groupMultiply()},
        {"psd_group_screen_opacity", "input.psd", groupScreenOpacity(), "gimp"},
        {"psd_group_normal", "input.psd", groupNormal()},
        {"psd_group_passthrough", "input.psd", groupPassThrough()},
        {"psd_group_opacity_hidden", "input.psd", groupOpacityHidden(), "gimp"},
        {"psd_nested_groups", "input.psd", nestedGroups()},
        {"ora_flat_baseline", "input.ora", flatBaseline()},
        {"ora_stack_multiply", "input.ora", groupMultiply()},
        {"ora_stack_screen_opacity", "input.ora", groupScreenOpacity()},
        {"ora_nested_stacks", "input.ora", nestedGroups()},
        // non-separable modes (whole-RGB blends): layers via both formats, folders via
        // PSD group records and ORA stack composite-ops (dkCl/lgCl have no ORA op)
        {"psd_layer_hue", "input.psd", layerBlend(Mode::Hue)},
        {"psd_layer_saturation", "input.psd", layerBlend(Mode::Saturation)},
        {"psd_layer_color", "input.psd", layerBlend(Mode::Color)},
        {"psd_layer_luminosity", "input.psd", layerBlend(Mode::Luminosity)},
        {"psd_layer_darker_color", "input.psd", layerBlend(Mode::DarkerColor)},
        {"psd_layer_lighter_color", "input.psd", layerBlend(Mode::LighterColor)},
        {"psd_group_color", "input.psd", groupBlend(Mode::Color, "grpColr")},
        {"psd_group_luminosity", "input.psd", groupBlend(Mode::Luminosity, "grpLum")},
        {"ora_stack_hue", "input.ora", groupBlend(Mode::Hue, "grpHue")},
        {"ora_layer_saturation", "input.ora", layerBlend(Mode::Saturation)},
        // fractional-alpha backdrop under a non-separable blend (dest un-premultiply)
        {"psd_layer_hue_over_semi", "input.psd", layerBlendOverSemi(Mode::Hue)},
        // S12 Color-mode parity probes (wedge with hard edges vs edge-free gray sweep)
        {"psd_layer_color_wedge", "input.psd", colorWedgeGray()},
        {"psd_layer_color_sweep", "input.psd", colorSweepGray()},
    };
}

QJsonObject defaultCaseJson(const CaseSpec& aSpec) {
    QJsonObject json;
    json["name"] = aSpec.name;
    json["input"] = aSpec.fileName;
    json["author"] = aSpec.author; // author_goldens.sh dispatches on this
    json["canvas"] = QJsonArray{aSpec.doc.canvas.width(), aSpec.doc.canvas.height()};
    json["frame"] = 0;
    // diff policy: ignore a border band (layer-mesh wedge artifact lives at the canvas
    // edge), tolerate 8-bit compositing quantization between AE and Krita
    json["borderCrop"] = 3;
    json["maxChannelDiff"] = 6;
    json["meanDiff"] = 0.6;
    json["provenance"] =
        QString("expected.png authored by tests/golden/author_goldens.sh (flatpak %1, headless)").arg(aSpec.author);
    return json;
}

//-------------------------------------------------------------------------------------------------
// cropped diff helpers: both buffers are RGBA8 in GL row order (row 0 = canvas bottom)
std::vector<uint8_t> croppedRows(const std::vector<uint8_t>& aBytes, const QSize& aSize, int aCrop) {
    const int w = aSize.width(), h = aSize.height();
    const int iw = w - 2 * aCrop, ih = h - 2 * aCrop;
    std::vector<uint8_t> out((size_t)iw * ih * 4);
    for (int y = 0; y < ih; ++y) {
        const auto* src = aBytes.data() + ((size_t)(y + aCrop) * w + aCrop) * 4;
        std::memcpy(out.data() + (size_t)y * iw * 4, src, (size_t)iw * 4);
    }
    return out;
}

bool expectedPngToGlBytes(const QString& aPath, const QSize& aCanvas, std::vector<uint8_t>& aOut) {
    QImageReader reader(aPath);
    // real-canvas goldens (e.g. 12480x7020 RGBA = 350MB) exceed Qt's 128MB default
    reader.setAllocationLimit(2048);
    const QImage img = reader.read().convertToFormat(QImage::Format_RGBA8888);
    if (img.isNull())
        return false;
    if (img.size() != aCanvas)
        return false;
    const int w = aCanvas.width(), h = aCanvas.height();
    aOut.resize((size_t)w * h * 4);
    for (int y = 0; y < h; ++y) { // flip: PNG row 0 is the canvas top, GL row 0 the bottom
        const auto* src = img.constScanLine(h - 1 - y);
        std::memcpy(aOut.data() + (size_t)y * w * 4, src, (size_t)w * 4);
    }
    return true;
}

} // namespace

//-------------------------------------------------------------------------------------------------
int generateInputs(const QString& aRootDir) {
    QDir().mkpath(aRootDir);
    int written = 0;
    for (const CaseSpec& spec : caseSpecs()) {
        const QString dirPath = aRootDir + "/" + spec.name;
        QDir().mkpath(dirPath);
        const QString inputPath = dirPath + "/" + spec.fileName;
        const bool ok = spec.fileName.endsWith(".ora") ? writeOra(inputPath, spec.doc) : writePsd(inputPath, spec.doc);
        if (!ok) {
            std::fprintf(stderr, "golden-gen: FAILED to write %s\n", inputPath.toUtf8().constData());
            return 1;
        }
        const QString jsonPath = dirPath + "/case.json";
        if (!QFile::exists(jsonPath)) { // hand-tuned tolerances/xfail are owner data; never clobber
            QFile file(jsonPath);
            if (file.open(QIODevice::WriteOnly))
                file.write(QJsonDocument(defaultCaseJson(spec)).toJson());
        }
        std::printf("golden-gen: %s\n", inputPath.toUtf8().constData());
        ++written;
    }
    std::printf(
        "golden-gen: %d cases written under %s (author expected.png with tests/golden/author_goldens.sh)\n", written,
        aRootDir.toUtf8().constData());
    return 0;
}

//-------------------------------------------------------------------------------------------------
void suite(const QString& aRootDir, const QString& aOnlyCase) {
    suiteHeader("S14 golden images: import -> render vs reference-exported expected.png");
    const int failsBefore = gFails;
    const int checksBefore = gChecks;

    QDir root(aRootDir);
    if (!root.exists()) {
        std::printf("    [SKIP] %s does not exist (run with --golden-gen to create cases)\n", aRootDir.toUtf8().constData());
        caseReport("golden images", true, "skipped");
        return;
    }
    root.mkpath(".scratch");

    int ran = 0, skipped = 0, xfailed = 0;
    const QStringList dirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& dir : dirs) {
        if (dir.startsWith('.'))
            continue;
        if (!aOnlyCase.isEmpty() && dir != aOnlyCase)
            continue;
        const QString caseDir = root.absoluteFilePath(dir);
        QFile jsonFile(caseDir + "/case.json");
        if (!jsonFile.exists() || !jsonFile.open(QIODevice::ReadOnly)) {
            std::printf("    [SKIP] %s: no case.json\n", dir.toUtf8().constData());
            continue;
        }
        const QJsonObject json = QJsonDocument::fromJson(jsonFile.readAll()).object();
        const QString name = json["name"].toString(dir);
        const QString input = json["input"].toString("input.psd");
        const QJsonArray canvasArr = json["canvas"].toArray();
        const QSize canvas(canvasArr[0].toInt(64), canvasArr[1].toInt(64));
        const int frame = json["frame"].toInt(0);
        const int crop = json["borderCrop"].toInt(3);
        const int maxTol = json["maxChannelDiff"].toInt(6);
        const double meanTol = json["meanDiff"].toDouble(0.6);
        const QString xfail = json["xfail"].toString();

        const QString expectedPath = caseDir + "/expected.png";
        if (!QFile::exists(expectedPath)) {
            std::printf(
                "    [SKIP] %s: no expected.png (author it with tests/golden/author_goldens.sh)\n",
                name.toUtf8().constData());
            ++skipped;
            continue;
        }
        ++ran;

        // import through the production loader (GL context is current)
        hb::StubAnimator animator;
        core::Project project("", animator, nullptr);
        ctrl::ImageFileLoader loader(gl::DeviceInfo::instance());
        loader.setOraImportMode(ctrl::ImageFileLoader::OraImportMode::Layered);
        hb::NullProgressReporter reporter;
        const QString inputPath = caseDir + "/" + input;
        if (!expect(loader.load(inputPath, project, reporter), QString("%1: loads (%2)").arg(name, loader.log())))
            continue;

        // render through the production composite wiring
        const std::vector<uint8_t> actual =
            scene::fixtureFor(canvas).render(project.objectTree(), scene::makeCamera(canvas), frame);

        std::vector<uint8_t> expected;
        if (!expect(
                expectedPngToGlBytes(expectedPath, canvas, expected),
                QString("%1: expected.png readable and %2x%3").arg(name).arg(canvas.width()).arg(canvas.height())))
            continue;

        const scene::Diff d =
            scene::diffImages(croppedRows(actual, canvas, crop), croppedRows(expected, canvas, crop));
        if (qEnvironmentVariableIsSet("VERBOSE_GOLDEN")) {
            std::printf("    [GOLD] %s: %s\n", name.toUtf8().constData(), diffStr(d).toUtf8().constData());
        }
        const bool ok = d.maxDiff <= maxTol && d.mean <= meanTol;
        if (!xfail.isEmpty()) {
            ++xfailed;
            ++gChecks;
            std::printf(
                "    [%s] %s: %s (xfail: %s)\n", ok ? "XPASS" : "XFAIL", name.toUtf8().constData(),
                diffStr(d).toUtf8().constData(), xfail.toUtf8().constData());
        } else {
            expect(ok, QString("%1: render == expected (%2)").arg(name, diffStr(d)));
        }
        if (!ok) { // dump evidence for eyeballing (GL-ordered readback flipped to view orientation)
            scene::dumpPNG(root.filePath(".scratch/" + name + "_actual.png"), actual, canvas);
            scene::dumpPNG(root.filePath(".scratch/" + name + "_expected.png"), expected, canvas);
        }
    }

    caseReport(
        QString("golden images (%1 run, %2 skipped, %3 xfail)").arg(ran).arg(skipped).arg(xfailed),
        gFails == failsBefore, QString("%1 checks").arg(gChecks - checksBefore));
}

} // namespace golden
