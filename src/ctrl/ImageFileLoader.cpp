#include <fstream>
#include <utility>
#include "XC.h"
#include "util/TextUtil.h"
#include "img/PSDReader.h"
#include "img/PSDUtil.h"
#include "img/ResourceNode.h"
#include "core/LayerNode.h"
#include "core/FolderNode.h"
#include "core/ObjectNodeUtil.h"
#include "ctrl/ImageFileLoader.h"
#include "gl/Global.h"

#include "img/oraParser.h"

#include <QMessageBox>

using namespace core;

namespace ctrl {

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

    mem = img::Util::recreateForBiLinearSampling(mem, aCanvasSize);
    const QRect rect(QPoint(-1, -1), aCanvasSize + QSize(2, 2));

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
    mLog(), mFileInfo(), mGLDeviceInfo(std::move(aDeviceInfo)), mCanvasSize(512, 512), mForceCanvasSize(false) {}

void ImageFileLoader::setCanvasSize(const QSize& aSize, bool aForce) {
    if (aSize.width() <= 0 || aSize.height() <= 0)
        return;

    mCanvasSize = aSize;
    mForceCanvasSize = aForce;
}

bool ImageFileLoader::load(const QString& aPath, core::Project& aProject, util::IProgressReporter& aReporter) {
    XC_DEBUG_REPORT("------------------------------------------");

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
    typedef PSDFormat::LayerList::reverse_iterator ReverseIterator;

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

        XC_REPORT() << "name =" << name << "size =" << rect.width() << "," << rect.height();

        // check the image has valid size as a texture.
        if (!checkTextureSizeError(rect.width(), rect.height())) {
            return false;
        }

        if (layer.entryType == PSDFormat::LayerEntryType_Layer) {
            // create layer resource (Note that the rect be modified.)
            auto resNode = createLayerResource(format->header(), layer, name, rect);
            if (!resNode->data().hasImage()) {
                mLog = "Failed to decode the image data of a layer ('" + name + "').";
                return false;
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

    XC_DEBUG_REPORT("------------------------------------------");

    mLog = "success";
    return true;
}
QString tr(const QString& str){
    return QCoreApplication::translate("image_file_loader", str.toStdString().c_str());
}
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
        QAbstractButton* cancelButton = loadMerged.addButton(tr("Cancel file load"), QMessageBox::NoRole);
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