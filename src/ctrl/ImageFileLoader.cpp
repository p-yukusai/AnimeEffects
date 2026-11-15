#include <fstream>
#include <utility>
#include "XC.h"
#include "util/TextUtil.h"
#include "img/PSDReader.h"
#include "img/PSDUtil.h"
#include "img/ResourceNode.h"
#include "core/LayerNode.h"
#include "core/FolderNode.h"
#include "core/MaskKey.h"
#include "core/ObjectNodeUtil.h"
#include "ctrl/ImageFileLoader.h"
#include "gl/Global.h"

#include "img/oraParser.h"

#include <QMessageBox>

using namespace core;


namespace {

// A PSD LAYER MASK as the matte this application stores: the mask channel is plane
// data (8-bit, 0 = hidden, 255 = visible) over the mask record's own rect, which is
// the document box in Photoshop. Returns an empty image when the layer has no mask
// or the channel cannot be decoded - a mask is a fidelity nicety, never a reason to
// refuse a file.
QImage decodePsdMask(const img::PSDFormat::Layer& aLayer, const img::PSDFormat::LayerMask& aMask) {
    const img::PSDFormat::Channel* plane = nullptr;
    for (const img::PSDFormat::ChannelPtr& channel : aLayer.channels) {
        if (channel && channel->id == -2) {   // the user mask channel
            plane = channel.get();
            break;
        }
    }
    if (!plane || !plane->data)
        return QImage();

    int w = aMask.rect.width();
    int h = aMask.rect.height();
    if (w <= 0 || h <= 0) {
        w = aLayer.rect.width();
        h = aLayer.rect.height();
    }
    if (w <= 0 || h <= 0)
        return QImage();

    QImage field(w, h, QImage::Format_Grayscale8);
    if (plane->compressionId == 0) {
        if (plane->dataLength < size_t(w) * size_t(h))
            return QImage();
        for (int y = 0; y < h; ++y) {
            memcpy(field.scanLine(y), plane->data.get() + size_t(y) * size_t(w), size_t(w));
        }
        return field;
    }
    if (plane->compressionId == 1) {
        // the RLE decoder writes one plane with a stride, which is what a mask is
        std::vector<uint8> raw(size_t(w) * size_t(h));
        if (!img::PSDUtil::decodePlanePackBits(
                raw.data(), raw.size(), plane->data.get(), (size_t)plane->dataLength, w, h, 1
            )) {
            return QImage();
        }
        for (int y = 0; y < h; ++y) {
            memcpy(field.scanLine(y), raw.data() + size_t(y) * size_t(w), size_t(w));
        }
        return field;
    }
    return QImage();   // ZIP-compressed masks are not decoded here; the layer still loads
}

// Attach a decoded matte to a freshly created node, at the node's own content rect. The field is
// stored as it came: masks are one texel per content pixel (see core/MaskKey.h).
void attachMask(core::ObjectNode& aNode, const QImage& aField, const QRectF& aContentRect) {
    if (aField.isNull() || !aContentRect.isValid() || aContentRect.isEmpty())
        return;
    auto* key = new core::MaskKey();
    key->data().write(aField, aContentRect);
    key->data().easing(0) = util::Easing::defaultParam();
    aNode.timeLine()->grabDefaultKey(core::TimeKeyType_Mask, key);
}

} // namespace

namespace ctrl {

//-------------------------------------------------------------------------------------------------
// The sentence for one layer the importer could not reproduce. The kind words
// come from img::psdUnsupportedLayerName, which the relink walker also uses, so
// the same layer is named the same way on both paths; only the sentence itself
// lives here, under the class's translation context (see the tr() note near the
// ORA prompt below).
QString unsupportedLayerWarning(const QString& aName, img::PsdUnsupportedLayer aKind, bool aFlattened) {
    const QString kind = QString::fromLatin1(img::psdUnsupportedLayerName(aKind));
    if (aFlattened) {
        return ImageFileLoader::tr(
                   "Layer \"%1\" is not supported (%2); it was imported as a flattened image.")
            .arg(aName, kind);
    }
    return ImageFileLoader::tr("Layer \"%1\" is not supported (%2); it was imported empty.").arg(aName, kind);
}

//-------------------------------------------------------------------------------------------------
img::ResourceNode* createLayerResource(
    const img::PSDFormat::Header& aHeader, const img::PSDFormat::Layer& aLayer, const QString& aName, QRect& aInOutRect
) {
    // create texture image
    auto imagePair = img::Util::createTextureImage(aHeader, aLayer);
    aInOutRect = imagePair.second;

    // create resource
    auto resNode = new img::ResourceNode(aName);
    resNode->data().grabImage(imagePair.first, aInOutRect.size(), img::Format_RGBA8);
    resNode->data().setPos(aInOutRect.topLeft());
    resNode->data().setIsLayer(true);
    resNode->data().setBlendMode(img::getBlendModeFromPSD(aLayer.blendMode, img::psdHasCspTslyFlag(aLayer)));
    resNode->data().setIsVisible(aLayer.isVisible());
    return resNode;
}

img::ResourceNode* createLayerResource(const layer& aLayer){
    auto imagePair = img::Util::createTextureImage(aLayer.image);
    auto resNode = new img::ResourceNode(QString::fromStdString(aLayer.name));
    resNode->data().grabImage(imagePair.first, imagePair.second.size(), img::Format_RGBA8);
    resNode->data().setPos(imagePair.second.topLeft());
    resNode->data().setIsLayer(true);
    resNode->data().setBlendMode(ORAReader::oraBlendToPSDBlend(aLayer.composite_op.blend));
    resNode->data().setIsVisible(aLayer.isVisible);
    return resNode;
}

img::ResourceNode* createLayerResource(const layerStack& aLayer){
    auto imagePair = img::Util::createTextureImage(aLayer.image);
    auto resNode = new img::ResourceNode(QString::fromStdString(aLayer.name));
    resNode->data().grabImage(imagePair.first, imagePair.second.size(), img::Format_RGBA8);
    resNode->data().setPos(imagePair.second.topLeft());
    resNode->data().setIsLayer(true);
    resNode->data().setBlendMode(oraParser::oraBlendToPSDBlend(aLayer.composite.blend));
    resNode->data().setIsVisible(aLayer.isVisible);
    return resNode;
}

img::ResourceNode* createFolderResource(const QString& aName, const QPoint& aPos) {
    auto resNode = new img::ResourceNode(aName);
    resNode->data().setPos(aPos);
    resNode->data().setIsLayer(false);
    return resNode;
}

img::ResourceNode* createMergedResource(
    const img::PSDFormat::Header& aHeader,
    const img::PSDFormat::ImageData& aImageData,
    const QSize& aCanvasSize,
    const QString& aName,
    QRect& aInOutRect
) {
    // The merged alpha channel (id -1) is only meaningful when the file declares
    // transparency (negative layer count). Flattened RGB files carry a dummy zero
    // alpha that would blank the content (and its white-matting would destroy the
    // RGB), so drop it and import the bitmap as opaque.
    img::PSDFormat::ChannelList channels;
    for (const img::PSDFormat::ChannelPtr& channel : aImageData.channels) {
        if (aImageData.hasTransparency || channel->id >= 0) {
            auto copy = new img::PSDFormat::Channel();
            copy->id = channel->id;
            copy->compressionId = channel->compressionId;
            copy->dataLength = channel->dataLength;
            if (channel->dataLength > 0) {
                copy->data.reset(new uint8[channel->dataLength]);
                std::memcpy(copy->data.get(), channel->data.get(), channel->dataLength);
            }
            channels.push_back(img::PSDFormat::ChannelPtr(copy));
        }
    }

    auto mem = img::PSDUtil::makeInterleavedImage(
        aHeader, channels, img::PSDUtil::ColorFormat_RGBA8, aCanvasSize.width(), aCanvasSize.height());
    if (mem.data == nullptr) {
        return nullptr;
    }

    mem = img::Util::prepareForSampling(mem, aCanvasSize);
    // the texture image is the layer's own rect, unpadded, so the artwork sits at the texture's
    // origin (the mip chain's level grids are anchored to multiples of 2^l in the TEXTURE - see
    // img::Util::prepareForSampling) and the merged image still lands at (0, 0) of the canvas: the
    // ring used to be paired with a rect at (-1, -1) that cancelled its (1, 1) inset
    const QRect rect(QPoint(0, 0), aCanvasSize);

    auto resNode = new img::ResourceNode(aName);
    resNode->data().grabImage(mem, rect.size(), img::Format_RGBA8);
    resNode->data().setPos(rect.topLeft());
    resNode->data().setIsLayer(true);
    resNode->data().setBlendMode(img::BlendMode_Normal);
    resNode->data().setIsVisible(true);
    aInOutRect = rect;
    return resNode;
}

FolderNode* createTopNode(const QString& aName, const QRect& aInitialRect) {
    // create tree top node
    auto* node = new FolderNode(aName);
    node->setInitialRect(aInitialRect);
    node->setDefaultOpacity(1.0f);
    node->setDefaultPosture(QVector2D());
    return node;
}

//-------------------------------------------------------------------------------------------------
ImageFileLoader::ImageFileLoader(gl::DeviceInfo  aDeviceInfo):
    mLog(),
    mWarnings(),
    mFileInfo(),
    mGLDeviceInfo(std::move(aDeviceInfo)),
    mCanvasSize(512, 512),
    mForceCanvasSize(false) {}

void ImageFileLoader::setCanvasSize(const QSize& aSize, bool aForce) {
    if (aSize.width() <= 0 || aSize.height() <= 0)
        return;

    mCanvasSize = aSize;
    mForceCanvasSize = aForce;
}

bool ImageFileLoader::load(const QString& aPath, core::Project& aProject, util::IProgressReporter& aReporter) {
    XC_DEBUG_REPORT("------------------------------------------");

    mWarnings.clear();
    mFileInfo = QFileInfo(aPath);
    const QString suffix = mFileInfo.suffix();

    if (aPath.isEmpty() || !mFileInfo.isFile()) {
        return createEmptyCanvas(aProject, "topnode", mCanvasSize);
    }
    if (suffix == "psd") {
        return loadPsd(aProject, aReporter);
    }
    if (suffix == "ora") {
        return loadOra(aProject, aReporter);
    }
    return loadImage(aProject, aReporter);
}

//-------------------------------------------------------------------------------------------------
bool ImageFileLoader::createEmptyCanvas(core::Project& aProject, const QString& aTopName, const QSize& aCanvasSize) {
    // check the image has valid size as a texture.
    if (!checkTextureSizeError(static_cast<uint32>(aCanvasSize.width()), static_cast<uint32>(aCanvasSize.height()))) {
        mLog = "invalid canvas size";
        return false;
    }
    XC_DEBUG_REPORT("canvas size = (%d, %d)", aCanvasSize.width(), aCanvasSize.height());

    // set canvas size
    aProject.attribute().setImageSize(aCanvasSize);

    // create tree top node
    FolderNode* topNode = createTopNode(aTopName, QRect(QPoint(0, 0), aCanvasSize));
    aProject.objectTree().grabTopNode(topNode);

    mLog = "success";
    return true;
}

//-------------------------------------------------------------------------------------------------
bool ImageFileLoader::loadImage(core::Project& aProject, util::IProgressReporter& aReporter) {
    aReporter.setSection("Loading the Image File...");
    aReporter.setMaximum(1);
    aReporter.setProgress(0);

    QImage image(mFileInfo.filePath());
    if (image.isNull()) {
        mLog = "Failed to load image file";
        return false;
    }

    auto size = mForceCanvasSize ? mCanvasSize : image.size();
    auto name = mFileInfo.baseName();

    if (!createEmptyCanvas(aProject, name, size)) {
        return false;
    }

    {
        auto topNode = aProject.objectTree().topNode();
        XC_PTR_ASSERT(topNode);

        // resource tree stack
        img::ResourceNode* resTree = createFolderResource("topnode", QPoint(0, 0));
        aProject.resourceHolder().pushImageTree(*resTree, mFileInfo.absoluteFilePath());

        // create layer resource (Note that the rect be modified.)
        auto resNode = img::Util::createResourceNode(image, name, true);
        resNode->data().setPos(image.rect().topLeft());
        resTree->children().pushBack(resNode);

        // create layer node
        // the LayerNode eagerly compiles its shaders, which needs a current GL context
        gl::Global::makeCurrentIfReady();
        auto* layerNode = new LayerNode(name, aProject.objectTree().shaderHolder());
        layerNode->setInitialRect(resNode->data().rect());
        layerNode->setDefaultImage(resNode->handle());
        layerNode->setDefaultOpacity(1.0f);
        layerNode->setDefaultPosture(resNode->data().center());
        topNode->children().pushBack(layerNode);
    }

    aReporter.setProgress(1);
    mLog = "success";
    return true;
}

//-------------------------------------------------------------------------------------------------
bool ImageFileLoader::loadPsd(core::Project& aProject, util::IProgressReporter& aReporter) {
    using img::PSDFormat;
    using img::PSDReader;
    using img::PSDUtil;

    aReporter.setSection("Loading PSD file...");
    aReporter.setMaximum(1);
    aReporter.setProgress(0);

    // open file
    QScopedPointer<std::ifstream> file;
    {
        auto path = mFileInfo.filePath();
        file.reset(new std::ifstream(path.toLocal8Bit(), std::ios::binary));
        XC_DEBUG_REPORT() << "image path =" << path;

        if (file->fail()) {
            mLog = "Can not find a file.";
            return false;
        }
    }

    // read psd
    PSDReader reader(*file);

    if (reader.resultCode() != PSDReader::ResultCode_Success) {
        mLog = "error(" + QString::number(reader.resultCode()) + ") " + QString::fromStdString(reader.resultMessage());
        return false;
    }
    aReporter.setProgress(1);
    file->close(); // do not use anymore

    // the renderer only produces 8-bit RGBA, so reject the color modes / bit depths
    // it cannot represent with a clear message instead of failing later on decode
    const auto& header = reader.format()->header();
    if (header.mode != img::PSDFormat::ColorMode_RGB) {
        mLog = QStringLiteral("Unsupported PSD color mode: %1. Only RGB is supported.")
                   .arg(QString::fromLatin1(img::PSDFormat::colorModeName((img::PSDFormat::ColorMode)header.mode)));
        return false;
    }
    if (header.depth != 8) {
        mLog = QStringLiteral("Unsupported PSD bit depth: %1-bit. Only 8-bit is supported.")
                   .arg(header.depth);
        return false;
    }

    // update reporter
    aReporter.setSection(QCoreApplication::translate("Image Loader", "Building an Object Tree..."));
    aReporter.setMaximum(reader.format()->layerAndMaskInfo().layerCount);
    aReporter.setProgress(0);
    int progress = 0;

    // Layer masks are parsed and re-written by the reader but never composited,
    // so a masked layer renders exactly as if its mask were absent. Counting them
    // here and reporting one line at the end keeps that file-wide omission from
    // burying the per-layer notes that explain why a particular layer is missing.
    int ignoredMaskCount = 0;

    // build tree by a psd format
    std::unique_ptr<PSDFormat>& format = reader.format();
    PSDFormat::LayerList& layers = format->layerAndMaskInfo().layers;

    img::Util::TextFilter textFilter(*format);

    auto canvasSize = mForceCanvasSize ? mCanvasSize : QSize((int)format->header().width, (int)format->header().height);

    // check the image has valid size as a texture.
    if (!checkTextureSizeError((uint32)canvasSize.width(), (uint32)canvasSize.height())) {
        mLog = "invalid canvas size";
        return false;
    }
    XC_DEBUG_REPORT("image size = (%d, %d)", canvasSize.width(), canvasSize.height());

    aProject.attribute().setImageSize(canvasSize);

    // create tree top node
    FolderNode* topNode = createTopNode(mFileInfo.baseName(), QRect(QPoint(), canvasSize));
    aProject.objectTree().grabTopNode(topNode);

    // tree stack
    std::vector<FolderNode*> treeStack;
    treeStack.push_back(topNode);
    float globalDepth = 0.0f;

    // resource tree stack
    std::vector<img::ResourceNode*> resStack;
    resStack.push_back(createFolderResource("topnode", QPoint(0, 0)));
    aProject.resourceHolder().pushImageTree(*resStack.back(), mFileInfo.absoluteFilePath());

    if (layers.empty()) {
        // flattened PSD (no layer records): the merged image data section holds the
        // whole bitmap; import it as a single opaque layer instead of an empty canvas
        const auto& imageData = format->imageData();
        QRect rect;
        const QString name = mFileInfo.baseName();
        auto resNode = createMergedResource(format->header(), imageData, canvasSize, name, rect);
        if (resNode) {
            resStack.back()->children().pushBack(resNode);

            // create layer node
            // the LayerNode eagerly compiles its shaders, which needs a current GL context
            gl::Global::makeCurrentIfReady();
            auto* layerNode = new LayerNode(name, aProject.objectTree().shaderHolder());
            layerNode->setInitialRect(rect);
            layerNode->setDefaultImage(resNode->handle());
            layerNode->setDefaultDepth(0.0f);
            layerNode->setDefaultOpacity(1.0f);
            topNode->children().pushBack(layerNode);
            aReporter.setProgress(1);
        } else {
            mLog = "failed to decode the merged image data";
            return false;
        }
    }

    // for each layer
    for (auto itr = layers.rbegin(); itr != layers.rend(); ++itr) {
        FolderNode* current = treeStack.back();
        XC_PTR_ASSERT(current);
        img::ResourceNode* resCurrent = resStack.back();
        XC_PTR_ASSERT(resCurrent);

        PSDFormat::Layer& layer = *((*itr).get());
        const QString name = textFilter.get(layer.name);
        QRect rect(layer.rect.left(), layer.rect.top(), layer.rect.width(), layer.rect.height());
        const float parentDepth = ObjectNodeUtil::getInitialWorldDepth(*current);

        // A LAYER MASK BECOMES A MATTE (core::MaskKey): the channel is decoded here
        // and attached to the node once it exists. What cannot be decoded still counts,
        // so the warning keeps naming the layers that lost it.
        QImage maskField;
        if (layer.mask) {
            maskField = decodePsdMask(layer, *layer.mask);
            if (maskField.isNull())
                ++ignoredMaskCount;
        }

        XC_REPORT() << "name =" << name << "size =" << rect.width() << "," << rect.height();

        // check the image has valid size as a texture.
        if (!checkTextureSizeError(rect.width(), rect.height())) {
            return false;
        }

        if (layer.entryType == PSDFormat::LayerEntryType_Layer) {
            // A fill or adjustment layer keeps its appearance as parameters, not
            // as pixel channels, and a plain layer stripped of its colour planes
            // has nothing to decode either. None of those is a broken file: the
            // project must still open, so such a layer becomes the same 1x1
            // transparent placeholder the zero-size adjustment path already
            // produced, and the warning below says which layer was lost and what
            // it used to be.
            const auto unsupportedKind = img::psdUnsupportedLayerKind(layer);
            const QPoint layerPos = rect.topLeft();

            // create layer resource (Note that the rect be modified.)
            auto resNode = createLayerResource(format->header(), layer, name, rect);
            if (!resNode->data().hasImage()) {
                if (unsupportedKind == img::PsdUnsupportedLayer_None) {
                    // The layer had colour planes and a non-empty rect, so this is
                    // a genuine decode failure rather than an unsupported kind.
                    mLog = "Failed to decode the image data of a layer ('" + name + "').";
                    return false;
                }

                // value-initialised, so the placeholder is a fully transparent
                // RGBA pixel, exactly what createTextureImage returns for a
                // zero-size layer.
                XCMemBlock placeholder(new uint8[4](), 4);
                rect = QRect(layerPos, QSize(1, 1));
                resNode->data().grabImage(placeholder, rect.size(), img::Format_RGBA8);
                resNode->data().setPos(layerPos);
            }
            if (unsupportedKind != img::PsdUnsupportedLayer_None) {
                // hasImage() decides between the two outcomes: a text or smart
                // object with its rasterized pixels looks right but is no longer
                // editable, while a fill or adjustment has nothing to draw.
                const bool flattened =
                    img::psdUnsupportedLayerKeepsRaster(unsupportedKind) && resNode->data().hasImage();
                mWarnings.append(unsupportedLayerWarning(name, unsupportedKind, flattened));
            }
            resCurrent->children().pushBack(resNode);

            // create layer node
            // the LayerNode eagerly compiles its shaders, which needs a current GL context
            gl::Global::makeCurrentIfReady();
            auto* layerNode = new LayerNode(name, aProject.objectTree().shaderHolder());
            layerNode->setVisibility(layer.isVisible());
            layerNode->setClipped(layer.clipping != 0);
            layerNode->setInitialRect(rect);
            layerNode->setDefaultImage(resNode->handle());
            layerNode->setDefaultDepth(globalDepth - parentDepth);
            layerNode->setDefaultOpacity(static_cast<float>(layer.opacity) / 255.0f);

            // the matte covers the layer's own content box, which is what every
            // content-space field in this application means by "content rect"
            attachMask(
                *layerNode, maskField, QRectF(QPointF(0.0, 0.0), QSizeF(layerNode->initialRect().size()))
            );

            current->children().pushBack(layerNode);

            // update depth
            globalDepth -= 1.0f;
        } else if (layer.entryType == PSDFormat::LayerEntryType_Bounding) {
            // create bounding box
            current->setInitialRect(calculateBoundingRectFromChildren(*current));

            // pop tree
            treeStack.pop_back();
            resStack.pop_back();
        } else {
            // create folder resource
            auto resNode = createFolderResource(name, rect.topLeft());
            resCurrent->children().pushBack(resNode);
            resStack.push_back(resNode);

            // create folder node
            auto* folderNode = new FolderNode(name);
            folderNode->setVisibility(layer.isVisible());
            folderNode->setClipped(layer.clipping != 0);
            folderNode->setDefaultDepth(globalDepth - parentDepth);
            folderNode->setDefaultOpacity(static_cast<float>(layer.opacity) / 255.0f);
            // 'diss' (dissolve) is the only group mode still unsupported, so
            // only it (plus unknown 4CCs) reads back as TERM; hue/sat/colr/lum/
            // dkCl/lgCl are fully mapped modes. Fall back to Normal for TERM,
            // matching the pre-feature behavior that dropped the group mode
            // entirely (TERM would index the shader arrays out of bounds at
            // render time - presentShader(mPresentShaders[TERM]) is OOB).
            const auto folderBlend = img::getBlendModeFromPSD(layer.blendMode, img::psdHasCspTslyFlag(layer));
            folderNode->setBlendMode(
                folderBlend == img::BlendMode_TERM ? img::BlendMode_Normal : folderBlend);

            // A GROUP MASK masks the group's COMPOSITE in Photoshop, which is exactly
            // what a folder's matte does here. It covers the document box (a group's
            // mask rect is drawn in document space), and the folder's own content box
            // is that canvas once it has been measured.
            if (!maskField.isNull()) {
                // the folder's content space is the CANVAS this import establishes, so the
                // field is anchored to the same box the folder will be measured against -
                // not the header's, which a forced canvas size can differ from
                const QRectF groupRect(QPointF(0.0, 0.0), QSizeF(canvasSize));
                attachMask(*folderNode, maskField, groupRect);
            }

            // push tree
            current->children().pushBack(folderNode);
            treeStack.push_back(folderNode);

            // update depth
            globalDepth -= 1.0f;
        }

        ++progress;
        aReporter.setProgress(progress);
    }

    // setup default positions
    setDefaultPosturesFromInitialRects(*topNode);

    if (ignoredMaskCount > 0) {
        // One line for the whole file. Naming every masked layer would push the
        // per-layer notes that explain an actually-missing layer out of the log.
        mWarnings.append(tr("Layer masks are not supported by AnimeEffects; %1 ignored.").arg(ignoredMaskCount));
    }

    XC_DEBUG_REPORT("------------------------------------------");

    mLog = "success";
    return true;
}
// The free tr() that used to stand here asked for the context "image_file_loader",
// which no catalog has, while lupdate files these five strings under "ImageFileLoader"
// (the class) and all six catalogs translate them THERE. So the ORA import prompt was
// English in every language with its translations sitting one context away. The class
// declares tr() for itself now (see ImageFileLoader.h), which is the same mechanism
// the Qt Ui classes use and the context the catalogs already carry.
void ImageFileLoader::parseOraLayer(layer &lyr, FolderNode* current, img::ResourceNode* resCurrent,  const float* globalDepth, const float* parentDepth, core::Project* aProject){
    auto resNode = createLayerResource(lyr);
    resCurrent->children().pushBack(resNode);
    // create layer node
    // the LayerNode eagerly compiles its shaders, which needs a current GL context
    gl::Global::makeCurrentIfReady();
    auto* layerNode = new LayerNode(QString::fromStdString(lyr.name), aProject->objectTree().shaderHolder());
    layerNode->setVisibility(lyr.isVisible);
    layerNode->setClipped(false); // unsupported for now
    layerNode->setInitialRect(lyr.rect);
    layerNode->setDefaultImage(resNode->handle());
    layerNode->setDefaultDepth(*globalDepth - *parentDepth);
    layerNode->setDefaultOpacity(lyr.opacity);
    // push back
    current->children().pushBack(layerNode);
}
// FUTURE: Add sorting, as it currently folder and layer structure will not be preserved.
void ImageFileLoader::parseOraStack( // NOLINT(*-no-recursion)
    stack &stk, std::vector<FolderNode*>& treeStack, std::vector<img::ResourceNode*>& resStack,
    float* globalDepth, QRect rect, core::Project* aProject, int* progress, util::IProgressReporter& aReporter){
    FolderNode* current = treeStack.back();
    XC_PTR_ASSERT(current);
    img::ResourceNode* resCurrent = resStack.back();
    XC_PTR_ASSERT(resCurrent);
    const float* parentDepth = new float{ObjectNodeUtil::getInitialWorldDepth(*current)};
    // create folder resource
    if(!stk.isRoot){
        // create node
        auto resNode = createFolderResource(QString::fromStdString(stk.name), rect.topLeft());
        resCurrent->children().pushBack(resNode);
        resStack.push_back(resNode);
        // create folder node
        auto* folderNode = new FolderNode(QString::fromStdString(stk.name));
        folderNode->setVisibility(stk.isVisible);
        folderNode->setClipped(false);
        folderNode->setDefaultDepth(*globalDepth - *parentDepth);
        folderNode->setDefaultOpacity(stk.opacity);
        // push tree
        current->children().pushBack(folderNode);
        treeStack.back() = folderNode;
        // update depth and ID
        *globalDepth -= 1.0f;
        // update vars
        current = treeStack.back();
        resCurrent = resStack.back();

    }
    // parse layers
    for (auto &lyr : stk.layers) {
        parseOraLayer(lyr, current, resCurrent, globalDepth, parentDepth, aProject);
        *progress+= 1;
        aReporter.setProgress(*progress);
        // update depth and ID
        *globalDepth -= 1.0f;
    }
    // parse child folders
    for(auto &child: stk.folders){
        FolderNode* childCurrent = current;
        parseOraStack(child, treeStack, resStack, globalDepth, rect, aProject, progress, aReporter);
        childCurrent->setInitialRect(calculateBoundingRectFromChildren(*current));
    }
    if(stk.isRoot){
        treeStack.back()->setInitialRect(calculateBoundingRectFromChildren(*treeStack.back()));
    }
}

bool ImageFileLoader::loadOra(Project& aProject, util::IProgressReporter& aReporter) {
    auto* oraFile = new miniz_cpp::zip_file(mFileInfo.filePath().toStdString());
    {
        auto path = mFileInfo.filePath();
        XC_DEBUG_REPORT() << "oraFile path =" << path;
        try{
            if (!oraFile->has_file("mimetype")){
                mLog = "Unable to find mimetype";
                return false;
            }
            auto mimetype = oraFile->read("mimetype");
            if (mimetype != "image/openraster"){
                mLog = "Unable to read mimetype";
                return false;
            }
            XC_DEBUG_REPORT() << "oraFile file has valid mimetype";
        }
        catch (...){
            mLog = std::string("Unable to unzip " + path.toStdString() + " into memory, aborting.").c_str();
            return false;
        }
    }
    // The layered/merged choice is interactive by default (mOraImportMode == Ask);
    // headless callers pre-select it via setOraImportMode so no dialog is shown.
    bool merged;
    if (mOraImportMode == OraImportMode::Ask) {
        QMessageBox loadMerged;
        loadMerged.setWindowTitle(tr("Select ORA file type"));
        loadMerged.setText(tr("How do you wish to load this ORA file?"));
        QAbstractButton* layerButton = loadMerged.addButton(tr("Load layered"), QMessageBox::YesRole);
        QAbstractButton* mergeButton = loadMerged.addButton(tr("Load merged"), QMessageBox::YesRole);
        // Any other outcome (this button, Escape or the window close box) falls
        // through to the cancel branch below, so the button is not read back.
        loadMerged.addButton(tr("Cancel file load"), QMessageBox::NoRole);
        loadMerged.exec();
        if (loadMerged.clickedButton() != mergeButton && loadMerged.clickedButton() != layerButton) {
            mLog = "User cancelled image load";
            return false;
        }
        merged = loadMerged.clickedButton() == mergeButton;
    } else {
        merged = mOraImportMode == OraImportMode::Merged;
    }
    {
        aReporter.setSection("Loading ORA file...");
        aReporter.setMaximum(100);
        aReporter.setProgress(0);
        if(merged){
            aReporter.setProgress(20);
            auto imageBytes = QByteArray::fromStdString(oraFile->read("mergedimage.png"));
            QImage image = QImage::fromData(imageBytes);
            if (image.isNull()) {
                mLog = "Failed to load image file";
                return false;
            }
            auto size = mForceCanvasSize ? mCanvasSize : image.size();
            auto name = mFileInfo.baseName();
            if (!createEmptyCanvas(aProject, name, size)) {
                aReporter.setProgress(0);
                return false; }

            {
                auto topNode = aProject.objectTree().topNode();
                XC_PTR_ASSERT(topNode);
                aReporter.setProgress(40);
                // resource tree stack
                img::ResourceNode* resTree = createFolderResource("topnode", QPoint(0, 0));
                aProject.resourceHolder().pushImageTree(*resTree, mFileInfo.absoluteFilePath());

                // create layer resource (Note that the rect be modified.)
                auto resNode = img::Util::createResourceNode(image, name, true);
                resTree->children().pushBack(resNode);
                aReporter.setProgress(60);
                // create layer node
                // the LayerNode eagerly compiles its shaders, which needs a current GL context
                gl::Global::makeCurrentIfReady();
                auto* layerNode = new LayerNode(name, aProject.objectTree().shaderHolder());
                layerNode->setInitialRect(resNode->data().rect());
                layerNode->setDefaultImage(resNode->handle());
                layerNode->setDefaultOpacity(1.0f);
                layerNode->setDefaultPosture(resNode->data().center());
                topNode->children().pushBack(layerNode);
                aReporter.setProgress(80);
            }

            aReporter.setProgress(100                                                                                                                                                                                                                                                                           );
            mLog = "Success";
            return true;
        }
        #ifdef NEW_ORA_PARSER
        aReporter.setProgress(25);
        auto reader = oraParser(oraFile);
        aReporter.setProgress(50);
        if (!reader.initialize()) {
            return false;
        }
        aReporter.setProgress(100);
        reader.printSelf();
        // helpers
        const auto* image = &reader.oraImage.oraImage;
        auto* layers = &reader.oraImage.layers;
        // update reporter
        aReporter.setSection(QCoreApplication::translate("Image Loader", "Building object trees..."));
        aReporter.setMaximum(image->layerNumber);
        aReporter.setProgress(0);
        auto progress = new int{0};
        auto canvasSize = mForceCanvasSize ? mCanvasSize : QSize(image->width, image->height);
        QImage img = QImage::fromData(QByteArray::fromStdString(oraFile->read("mergedimage.png")));
        if (img.isNull()) {
            mLog =
                "Unable to get data from merged image, the file is either corrupted or does not follow the "
                "openRaster spec.";
            return false;
        }
        if (img.size() != QSize(image->width, image->height)) {
            mLog = "Merged image size is not equal to the size declared on stack.xml, invalid file.";
            return false;
        }
        aProject.attribute().setImageSize(canvasSize);
        // create tree top node
        FolderNode* topNode = createTopNode(mFileInfo.baseName(), QRect(QPoint(), canvasSize));
        aProject.objectTree().grabTopNode(topNode);
        // tree stack
        std::vector<FolderNode*> treeStack;
        treeStack.push_back(topNode);
        float globalDepth = 0.0f;
        // resource tree stack
        std::vector<img::ResourceNode*> resStack;
        resStack.push_back(createFolderResource("topnode", QPoint(0, 0)));
        aProject.resourceHolder().pushImageTree(*resStack.back(), mFileInfo.absoluteFilePath());
        // Parse mainStack
        aReporter.setProgress(*progress);
        QVector<int> skipped;
        //FolderNode* prev = treeStack.back();
        QVector<FolderNode*> prev;
        QVector<img::ResourceNode*> resPrev;
        // img::ResourceNode* resPrev = resStack.back();
        FolderNode* current = treeStack.back();
        img::ResourceNode* resCurrent = resStack.back();
        // for each layer
        for (const auto& lyr: *layers) {
            // close every folder whose subtree is complete: the flat list is pre-order,
            // so the current element is the first entry beyond them
            while (!skipped.empty() && skipped.back() == 0) {
                current->setInitialRect(calculateBoundingRectFromChildren(*current));
                current = prev.back();
                resCurrent = resPrev.back();
                skipped.pop_back();
                prev.pop_back();
                resPrev.pop_back();
            }
            // the current element is a descendant of every still-open folder (nesting
            // means the innermost counter alone is not enough: deeper elements also
            // count toward their ancestors' subtrees)
            for (auto& remaining : skipped)
                remaining -= 1;
            XC_PTR_ASSERT(current);
            XC_PTR_ASSERT(resCurrent);
            const float* parentDepth = new float{ObjectNodeUtil::getInitialWorldDepth(*current)};
            // create a folder resource
            if(lyr.type == IMAGE){
                auto resNode = createLayerResource(lyr);
                resCurrent->children().pushBack(resNode);
// create layer node
                // the LayerNode eagerly compiles its shaders, which needs a current GL context
                gl::Global::makeCurrentIfReady();
auto* layerNode = new LayerNode(QString::fromStdString(lyr.name), aProject.objectTree().shaderHolder());
                layerNode->setVisibility(lyr.isVisible);
                layerNode->setClipped(false); // unsupported for now
                layerNode->setInitialRect(lyr.rect);
                layerNode->setDefaultImage(resNode->handle());
                layerNode->setDefaultDepth(globalDepth - *parentDepth);
                layerNode->setDefaultOpacity(lyr.opacity);
                layerNode->setBlendMode(oraParser::oraBlendToPSDBlend(lyr.composite.blend));
                // push back
                current->children().pushBack(layerNode);
                *progress+= 1;
                aReporter.setProgress(*progress);
                // update depth and ID
                globalDepth -= 1.0f;
            }
            // parse child folders
            if (lyr.type == FOLDER){
                prev.append(current);
                resPrev.append(resCurrent);
                skipped.append(0);
                // create node
                auto resNode = createFolderResource(QString::fromStdString(lyr.name), lyr.rect.topLeft());
                resCurrent->children().pushBack(resNode);
                resStack.push_back(resNode);
                // create folder node
                auto* folderNode = new FolderNode(QString::fromStdString(lyr.name));
                folderNode->setVisibility(lyr.isVisible);
                folderNode->setClipped(false);
                folderNode->setDefaultDepth(globalDepth - *parentDepth);
                folderNode->setDefaultOpacity(lyr.opacity);
                // ORA stacks carry composite-op like layers; oraBlendToPSDBlend maps
                // unsupported ops (svg:plus, hue/sat/color/luminosity) to Normal
                folderNode->setBlendMode(oraParser::oraBlendToPSDBlend(lyr.composite.blend));
                // push tree
                current->children().pushBack(folderNode);
                treeStack.back() = folderNode;
                // update depth and ID
                globalDepth -= 1.0f;
                skipped.back() = lyr.subtree; // flat entries belonging to this folder
                // update vars
                current = treeStack.back();
                resCurrent = resStack.back();
            }
            treeStack.back()->setInitialRect(calculateBoundingRectFromChildren(*treeStack.back()));
        }

        setDefaultPosturesFromInitialRects(*topNode);
        // setup default positions
        mLog = "Success";
        aReporter.setMaximum(1);
        aReporter.setProgress(1);
        delete progress;
        return true;
        #endif

        #ifdef OLD_ORA_PARSER
        aReporter.setProgress(50);
        ORAReader reader = ORAReader(oraFile);
        if (!reader.initialize()) {
            return false;
        }
        aReporter.setProgress(100);
        reader.printSelf();
        // update reporter
        aReporter.setSection(QCoreApplication::translate("Image Loader", "Building object trees..."));
        aReporter.setMaximum(reader.image.layerNumber); // Progress reported by stack size and not layer number
        aReporter.setProgress(0);
        int* progress = new int{0};
        auto canvasSize = mForceCanvasSize ? mCanvasSize : QSize(reader.image.w, reader.image.h);
        QImage image = QImage::fromData(QByteArray::fromStdString(oraFile->read("mergedimage.png")));
        if (image.isNull()) {
            mLog =
                "Unable to get data from merged image, the file is either corrupted or does not follow the "
                "openRaster spec.";
            return false;
        }
        if (image.size() != QSize(reader.image.w, reader.image.h)) {
            mLog = "Merged image size is not equal to the size declared on stack.xml, invalid file.";
            return false;
        }
        aProject.attribute().setImageSize(canvasSize);
        // create tree top node
        FolderNode* topNode = createTopNode(mFileInfo.baseName(), QRect(QPoint(), canvasSize));
        aProject.objectTree().grabTopNode(topNode);
        // tree stack
        std::vector<FolderNode*> treeStack;
        treeStack.resize(reader.image.mainStack.folders.size() + 1);
        treeStack.push_back(topNode);
        auto* globalDepth = new float{0.0f};
        auto* ID = new int{0};
        // resource tree stack
        std::vector<img::ResourceNode*> resStack;
        resStack.resize(reader.image.globalID + 1);
        resStack.push_back(createFolderResource("topnode", QPoint(0, 0)));
        aProject.resourceHolder().pushImageTree(*resStack.back(), mFileInfo.absoluteFilePath());
        // Parse mainStack
        aReporter.setProgress(*progress);
        reader.image.mainStack.isRoot = true;
        reader.image.mainStack.name = reader.oraFile->get_filename();
        reader.image.mainStack.sortID = *ID;
        parseOraStack(
            reader.image.mainStack, treeStack, resStack, globalDepth, reader.image.rect, &aProject, progress, aReporter
        );
        setDefaultPosturesFromInitialRects(*topNode);
        // setup default positions
        mLog = "Success";
        aReporter.setMaximum(1);
        aReporter.setProgress(1);
        delete progress;
        delete globalDepth;
        return true;
        #endif
    }
    return false;
}

QRect ImageFileLoader::calculateBoundingRectFromChildren(const ObjectNode& aNode) {
    QRect rect;
    for (auto child : aNode.children()) {
        if (child->initialRect().isValid()) {
            rect = rect.isValid() ? rect.united(child->initialRect()) : child->initialRect();
        }
    }
    return rect;
}
void ImageFileLoader::setDefaultPosturesFromInitialRects(ObjectNode& aNode) {
    ObjectNode::Iterator itr(&aNode);
    while (itr.hasNext()) {
        auto node = itr.next();
        auto parent = node->parent();
        const bool isTop = !parent;
        const bool parentIsTop = parent != nullptr && !parent->parent();

        QVector2D pos;
        QVector2D parentPos;
        if (!isTop && parent != nullptr) {
            // parent position
            parentPos = (parent->initialRect().isValid() && !parentIsTop)
                ? util::MathUtil::getCenter(parent->initialRect())
                : QVector2D();

            // node position
            pos = (node->initialRect().isValid()) ? util::MathUtil::getCenter(node->initialRect()) : parentPos;
        }

        // set
        if (node->type() == ObjectType_Layer) {
            ((LayerNode*)node)->setDefaultPosture(pos - parentPos);
        } else if (node->type() == ObjectType_Folder) {
            ((FolderNode*)node)->setDefaultPosture(pos - parentPos);
        }
    }
}

bool ImageFileLoader::checkTextureSizeError(uint32 aWidth, uint32 aHeight) {
    const auto maxSize = (uint32)mGLDeviceInfo.maxTextureSize;
    if (maxSize < aWidth || maxSize < aHeight) {
        mLog = QString("The image size over the max texture size of your current device. ") + "image size(" +
            QString::number(aWidth) + ", " + QString::number(aHeight) + "), " + "max size(" + QString::number(maxSize) +
            ", " + QString::number(maxSize) + ")";
        return false;
    }
    return true;
}

} // namespace ctrl