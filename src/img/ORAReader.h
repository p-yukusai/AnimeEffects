//
// OpenRaster parser for AnimeEffects by Yukusai
// Limitations:
// File "thumbnail.png", custom data fields and annotations will be ignored,
// blend modes outside those already supported by ANIE will likewise be ignored.
//
// OpenRaster specification from https://www.openraster.org/baseline/file-layout-spec.html
// Utilizes miniz-cpp library by Thomas Fussell with fixes by Kay Stenschke and pugixml library by Arseny Kapoulkine
//

#ifndef ANIMEEFFECTS_ORAREADER_H
#define ANIMEEFFECTS_ORAREADER_H

#include "deps/zip_file.h"
#include "img/BlendMode.h"
#include <QImage>
#include <QDebug>
#include <QString>
#include <QFile>
#include <QPainter>
#include <pugixml.hpp>

const int DefaultSize = 1028;
const std::string DefaultString = "Unknown";

enum PorterDuff {
    SRC_OVER,
    LIGHTER,
    DST_IN,
    DST_OUT,
    SRC_ATOP,
    DST_ATOP
};
enum Blend {
    NORMAL,
    MULTIPLY,
    SCREEN,
    OVERLAY,
    DARKEN,
    LIGHTEN,
    COLOR_DODGE,
    COLOR_BURN,
    HARD_LIGHT,
    SOFT_LIGHT,
    // Not named DIFFERENCE due to define conflict
    DIFF,
    COLOR,
    LUMINOSITY,
    HUE,
    SATURATION,
    PLUS,
};

struct composite{
    Blend blend = NORMAL;
    PorterDuff pdComposite = SRC_OVER;
};
struct layer{
    int sortID{};
    std::string name;
    QImage image;
    composite composite_op;
    float opacity{};
    bool isVisible{};
    int x{};
    int y{};
    QRect rect;
};
// No composite as ANIE does not support composing folders
struct stack{
    int sortID{};
    std::string name;
    float opacity{};
    bool isVisible = true;
    bool isRoot = false;
    QVector<layer> layers;
    QVector<stack> folders;
};
struct oraImage{
    int w{};
    int h{};
    int layerNumber{};
    int stackNumber{};
    int globalID{};
    std::string version{};
    stack mainStack;
    QRect rect;
};

class ORAReader {
public:
    miniz_cpp::zip_file* oraFile;
    QVector<stack> stackList;
    pugi::xml_document* reader = new pugi::xml_document;
    oraImage image;

    explicit ORAReader(miniz_cpp::zip_file* file){ oraFile = file; }
    static bool charIsEqualTo(const char* cStr, const std::string& str){
        return std::string(cStr) == str;
    }
    static QPixmap createPattern(const QSize & size, const QColor & background=Qt::white, const QColor & foreground=Qt::black){
        QPixmap pixmap(size);
        pixmap.fill(background);
        {
            QPainter painter(&pixmap);
            painter.setPen(foreground);
            painter.drawLine(0, size.height()/2, size.width(), size.height()/2);
            painter.drawLine(size.width()/2, 0, size.width()/2, size.height());
        }
        return pixmap;
    }

    void parseStack(stack* curStack, pugi::xml_node* stk){ // NOLINT(*-no-recursion)
        if(!curStack->isRoot){
            image.globalID += 1;
            image.stackNumber += 1;
            curStack->sortID = image.globalID;
            bool visAttrExists = false;
            for(auto attr: stk->attributes()){
                if(charIsEqualTo(attr.name(), "name")){
                    curStack->name = attr.as_string();
                    if(curStack->name.empty()) { curStack->name = DefaultString; }
                }
                else if(charIsEqualTo(attr.name(), "opacity")){
                    curStack->opacity = attr.as_float();
                }
                else if(charIsEqualTo(attr.name(), "visibility")){
                    visAttrExists = true;
                    curStack->isVisible = charIsEqualTo(attr.as_string(), "visible");
                }
                else if(charIsEqualTo(attr.name(), "composite-op")){
                    continue; // Composite folder operations are unsupported in anie
                }
                else{
                    qDebug() << "Unknown stack attribute: " << attr.name() << "; with value: " << attr.value();
                }
            }
            if(!visAttrExists){
                curStack->isVisible = true;
            }
        }
        for(auto child: stk->children()){
            if(charIsEqualTo(child.name(), "layer")){
                parseLayer(&curStack->layers.emplace_back(), child);
            }
            else if(charIsEqualTo(child.name(), "stack")){
                parseStack(&curStack->folders.emplace_back(), &child);
            }
            else{
                qDebug() << "Unknown stack child: " << child.name();
            }
        }
    }
    void parseLayer(layer* curLayer, const pugi::xml_node& lyr){
        image.layerNumber += 1;
        image.globalID += 1;
        curLayer->sortID = image.globalID;
        bool visAttrExists = false;
        for(auto attr: lyr.attributes()){
            if(charIsEqualTo(attr.name(), "src")){
                QImage img;
                try {
                    img = QImage::fromData(QByteArray::fromStdString(oraFile->read(attr.as_string())))
                              .convertedTo(QImage::Format_RGBA8888);
                }
                catch(...){
                    qDebug() << "Unable to read image at " << attr.as_string() << ". Utilizing fallback.";
                    img = createPattern(QSize(DefaultSize, DefaultSize)).toImage();
                }
                curLayer->image = img;
            }
            else if(charIsEqualTo(attr.name(), "name")){
                curLayer->name = attr.as_string();
                if(curLayer->name.empty()) { curLayer->name = DefaultString; }
            }
            else if(charIsEqualTo(attr.name(), "x")){
                curLayer->x = attr.as_int();
            }
            else if(charIsEqualTo(attr.name(), "y")){
                curLayer->y = attr.as_int();
            }
            else if(charIsEqualTo(attr.name(), "opacity")){
                curLayer->opacity = attr.as_float();
            }
            else if(charIsEqualTo(attr.name(), "visibility")){
                visAttrExists = true;
                curLayer->isVisible = charIsEqualTo(attr.as_string(), "visible");
            }
            else if(charIsEqualTo(attr.name(), "composite-op")){
                curLayer->composite_op = stringToComposite(attr.as_string());
            }
            else{
                qDebug() << "Unknown layer attribute: " << attr.name() << "; with value: " << attr.value();
            }
        }
        if(!visAttrExists){
            curLayer->isVisible = true;
        }
        curLayer->rect = QRect(curLayer->x, curLayer->y, curLayer->image.width(), curLayer->image.height());
    }
    bool initialize(){
        std::string stackStr = oraFile->read("stack.xml");
        XC_ASSERT(reader);
        pugi::xml_parse_result result = reader->load_string(stackStr.c_str());
        if(result.status == pugi::xml_parse_status::status_ok){
            // Parse image
            const pugi::xml_node& Image = reader->child("image");
            image.w = Image.attribute("w").as_int();
            image.h = Image.attribute("h").as_int();
            image.version = Image.attribute("version").as_string();
            image.globalID = -1;
            if(image.version.empty()){ image.version = DefaultString; }
            // Parse stacks
            pugi::xml_node mainStack = reader->child("image").child("stack");
            image.mainStack.isRoot = true;
            image.mainStack.name = oraFile->get_filename();
            image.mainStack.sortID = -1;
            parseStack(&image.mainStack, &mainStack);
        }
        else{
            qDebug() << result.status;
            delete reader;
            return false;
        }
        image.rect = QRect(0, 0, image.w, image.h);
        delete reader;
        return true;
    }
    static void printComposite(const composite& comp, const std::string& depth){
        switch (comp.blend) {
        case NORMAL:        qDebug() << depth.c_str() << "layer blend: " << "NORMAL"; break;
        case MULTIPLY:      qDebug() << depth.c_str() << "layer blend: " << "MULTIPLY"; break;
        case SCREEN:        qDebug() << depth.c_str() << "layer blend: " << "SCREEN"; break;
        case OVERLAY:       qDebug() << depth.c_str() << "layer blend: " << "OVERLAY"; break;
        case DARKEN:        qDebug() << depth.c_str() << "layer blend: " << "DARKEN"; break;
        case LIGHTEN:       qDebug() << depth.c_str() << "layer blend: " << "LIGHTEN"; break;
        case COLOR_DODGE:   qDebug() << depth.c_str() << "layer blend: " << "COLOR_DODGE"; break;
        case COLOR_BURN:    qDebug() << depth.c_str() << "layer blend: " << "COLOR_BURN"; break;
        case HARD_LIGHT:    qDebug() << depth.c_str() << "layer blend: " << "HARD_LIGHT"; break;
        case SOFT_LIGHT:    qDebug() << depth.c_str() << "layer blend: " << "SOFT_LIGHT"; break;
        case DIFF:          qDebug() << depth.c_str() << "layer blend: " << "DIFFERENCE"; break;
        case COLOR:         qDebug() << depth.c_str() << "layer blend: " << "COLOR"; break;
        case LUMINOSITY:    qDebug() << depth.c_str() << "layer blend: " << "LUMINOSITY"; break;
        case HUE:           qDebug() << depth.c_str() << "layer blend: " << "HUE"; break;
        case SATURATION:    qDebug() << depth.c_str() << "layer blend: " << "SATURATION"; break;
        case PLUS:          qDebug() << depth.c_str() << "layer blend: " << "PLUS"; break;
        }
        switch (comp.pdComposite) {
        case SRC_OVER:      qDebug() << depth.c_str() << "layer composite: " << "SRC_OVER"; break;
        case LIGHTER:       qDebug() << depth.c_str() << "layer composite: " << "LIGHTER"; break;
        case DST_IN:        qDebug() << depth.c_str() << "layer composite: " << "DST_IN"; break;
        case DST_OUT:       qDebug() << depth.c_str() << "layer composite: " << "DST_OUT"; break;
        case SRC_ATOP:      qDebug() << depth.c_str() << "layer composite: " << "SRC_ATOP"; break;
        case DST_ATOP:      qDebug() << depth.c_str() << "layer composite: " << "DST_ATOP"; break;
        }
    }
    static void printLayer(const layer& lyr, const std::string& depth){
        qDebug() << QString::fromStdString(depth).remove(depth.length() -1, 1).toStdString().c_str() << depth.c_str() << "<layer>";
        qDebug() << depth.c_str() << "layer name: " << lyr.name.c_str();
        qDebug() << depth.c_str() << "layer x: " << lyr.x;
        qDebug() << depth.c_str() << "layer y: " << lyr.y;
        qDebug() << depth.c_str() << "layer opacity: " << lyr.opacity;
        qDebug() << depth.c_str() << "layer visible: " << lyr.isVisible;
        printComposite(lyr.composite_op, depth);
        qDebug() << depth.c_str() << "layer image: " << lyr.image;
        qDebug() << depth.c_str() << "layer rect: " << lyr.rect;
        qDebug() << QString::fromStdString(depth).remove(depth.length() - 1, 1).toStdString().c_str() << depth.c_str() << "</layer>";
    }
    static void printStack(const stack& stk, int stackDepth){ // NOLINT(*-no-recursion)
        QString chara = "-";
        auto depth = chara.repeated(stackDepth * 4).append('|').toStdString();
        qDebug() << QString::fromStdString(depth).remove(depth.length() - 1, 1).toStdString().c_str() << depth.c_str() << "<stack>";
        if(!stk.isRoot){
            qDebug() << depth.c_str() << "stack name: " << stk.name.c_str();
            qDebug() << depth.c_str() << "stack opacity: " << stk.opacity;
            qDebug() << depth.c_str() << "stack is visible: " << stk.isVisible;
        }
        else{ qDebug() << depth.c_str() << "image root stack"; }
        for (const auto& sStk : stk.layers) {
            printLayer(sStk, depth);
        }
        for (const auto& sStk : stk.folders) {
            stackDepth += 1;
            printStack(sStk, stackDepth);
        }
        qDebug() << QString::fromStdString(depth).remove(depth.length() - 1, 1).toStdString().c_str() << depth.c_str() << "</stack>";
    }
    void printSelf() const{
        qDebug("<|IMAGE|>");
        qDebug() << "image height: " << image.h;
        qDebug() << "image width: " << image.w;
        qDebug() << "image rect: " << image.rect;
        qDebug() << "openRaster spec version: " << (image.version.empty()? "Unknown": image.version.c_str());
        int stackDepth = 1;
        printStack(image.mainStack, stackDepth);
        qDebug("<|IMAGE|>");
    }
    static img::BlendMode oraBlendToPSDBlend(Blend blend){
        switch (blend) {
        case NORMAL:
            return img::BlendMode_Normal;
        case MULTIPLY:
            return img::BlendMode_Multiply;
        case SCREEN:
            return img::BlendMode_Screen;
        case OVERLAY:
            return img::BlendMode_Overlay;
        case DARKEN:
            return img::BlendMode_Darken;
        case LIGHTEN:
            return img::BlendMode_Lighten;
        case COLOR_DODGE:
            return img::BlendMode_ColorDodge;
        case COLOR_BURN:
            return img::BlendMode_ColorBurn;
        case HARD_LIGHT:
            return img::BlendMode_HardLight;
        case SOFT_LIGHT:
            return img::BlendMode_SoftLight;
        case DIFF:
            return img::BlendMode_Difference;
        case COLOR:
            return img::BlendMode_Color;
        case LUMINOSITY:
            return img::BlendMode_Luminosity;
        case HUE:
            return img::BlendMode_Hue;
        case SATURATION:
            return img::BlendMode_Saturation;
             // Unsupported blend modes
        case PLUS:
        default:
            return img::BlendMode_Normal;
        }
    }
    static composite stringToComposite(const std::string& comp){
        if(comp == "svg:src-over") return {NORMAL};
        if(comp == "svg:multiply") return {MULTIPLY};
        if(comp == "svg:screen") return {SCREEN};
        if(comp == "svg:overlay") return {OVERLAY};
        if(comp == "svg:darken") return {DARKEN};
        if(comp == "svg:lighten") return {LIGHTEN};
        if(comp == "svg:color-dodge") return {COLOR_DODGE};
        if(comp == "svg:color-burn") return {COLOR_BURN};
        if(comp == "svg:hard-light") return {HARD_LIGHT};
        if(comp == "svg:soft-light") return {SOFT_LIGHT};
        if(comp == "svg:difference") return {DIFF};
        if(comp == "svg:color") return {COLOR};
        if(comp == "svg:luminosity") return {LUMINOSITY};
        if(comp == "svg:hue") return {HUE};
        if(comp == "svg:saturation") return {SATURATION};
        if(comp == "svg:plus") return {NORMAL, LIGHTER};
        if(comp == "svg:dst-in") return {NORMAL, DST_IN};
        if(comp == "svg:dst-out") return {NORMAL, DST_OUT};
        if(comp == "svg:src-atop") return {NORMAL, SRC_ATOP};
        if(comp == "svg:dst-atop") return {NORMAL, DST_ATOP};
        return {NORMAL};
    }

};


#endif // ANIMEEFFECTS_ORAREADER_H
