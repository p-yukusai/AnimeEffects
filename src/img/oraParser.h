//
// Created by yukusai on 02/07/2025.
//

#ifndef ORAPARSER_H
#define ORAPARSER_H

#include "deps/zip_file.h"
#include "img/BlendMode.h"
#include <QImage>
#include <QDebug>
#include <QString>
#include <QFile>
#include <QPainter>
#include <pugixml.hpp>

enum LayerType {
    ROOT,
    FOLDER,
    IMAGE,
    UNKNOWN
};

enum class PD {
    SRC_OVER,
    LIGHTER,
    DST_IN,
    DST_OUT,
    SRC_ATOP,
    DST_ATOP
};

enum class BLEND {
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
    DIFF,
    COLOR,
    LUMINOSITY,
    HUE,
    SATURATION,
    PLUS,
};

struct comp{
    BLEND blend = BLEND::NORMAL;
    PD pdComposite = PD::SRC_OVER;
};

struct image {
    int width = 0;
    int height = 0;
    std::string oraVersion = "0.0.1";
    QRect rect = QRect();
    int layerNumber = 0;
};

struct layerStack {
    LayerType type = IMAGE;
    int capacity = 0; // direct children (LEGACY: only the GUI resource path
                      // img/Util.cpp:401 still consumes it; direct-children
                      // undercounts nested stacks, the same class of bug the
                      // restored ImageFileLoader walk fixed via `subtree`)
    int subtree = 0;  // flat-list entries belonging to this element's subtree (folders only)
    std::string name = "";
    float opacity = 1.0f;
    bool isVisible = true;
    int x = 0;
    int y = 0;
    QImage image = QImage();
    comp composite = {};
    QRect rect = QRect();
};

struct openRaster {
    image oraImage{};
    QVector<layerStack> layers;
};



class oraParser {
public:
    miniz_cpp::zip_file* oraFile;
    pugi::xml_document* reader = new pugi::xml_document;
    openRaster oraImage;
    explicit oraParser(miniz_cpp::zip_file* file) { oraFile = file; }
    static bool charIsEqualTo(const char* cStr, const std::string& str);
    static QPixmap createPattern(const QSize & size, const QColor & background=Qt::white, const QColor & foreground=Qt::black);

    void parse(layerStack* layer_stack, pugi::xml_node::iterator::pointer xml_node);
    bool initialize();
    static img::BlendMode oraBlendToPSDBlend(BLEND blend){
        switch (blend) {
        case BLEND::NORMAL:
            return img::BlendMode_Normal;
        case BLEND::MULTIPLY:
            return img::BlendMode_Multiply;
        case BLEND::SCREEN:
            return img::BlendMode_Screen;
        case BLEND::OVERLAY:
            return img::BlendMode_Overlay;
        case BLEND::DARKEN:
            return img::BlendMode_Darken;
        case BLEND::LIGHTEN:
            return img::BlendMode_Lighten;
        case BLEND::COLOR_DODGE:
            return img::BlendMode_ColorDodge;
        case BLEND::COLOR_BURN:
            return img::BlendMode_ColorBurn;
        case BLEND::HARD_LIGHT:
            return img::BlendMode_HardLight;
        case BLEND::SOFT_LIGHT:
            return img::BlendMode_SoftLight;
        case BLEND::DIFF:
            return img::BlendMode_Difference;
        case BLEND::COLOR:
            return img::BlendMode_Color;
        case BLEND::LUMINOSITY:
            return img::BlendMode_Luminosity;
        case BLEND::HUE:
            return img::BlendMode_Hue;
        case BLEND::SATURATION:
            return img::BlendMode_Saturation;
             // Unsupported blend modes
        case BLEND::PLUS:
        default:
            return img::BlendMode_Normal;
        }
    }
    static comp stringToComposite(const std::string& comp){
        if(comp == "svg:src-over") return {BLEND::NORMAL};
        if(comp == "svg:multiply") return {BLEND::MULTIPLY};
        if(comp == "svg:screen") return {BLEND::SCREEN};
        if(comp == "svg:overlay") return {BLEND::OVERLAY};
        if(comp == "svg:darken") return {BLEND::DARKEN};
        if(comp == "svg:lighten") return {BLEND::LIGHTEN};
        if(comp == "svg:color-dodge") return {BLEND::COLOR_DODGE};
        if(comp == "svg:color-burn") return {BLEND::COLOR_BURN};
        if(comp == "svg:hard-light") return {BLEND::HARD_LIGHT};
        if(comp == "svg:soft-light") return {BLEND::SOFT_LIGHT};
        if(comp == "svg:difference") return {BLEND::DIFF};
        if(comp == "svg:color") return {BLEND::COLOR};
        if(comp == "svg:luminosity") return {BLEND::LUMINOSITY};
        if(comp == "svg:hue") return {BLEND::HUE};
        if(comp == "svg:saturation") return {BLEND::SATURATION};
        if(comp == "svg:plus") return {BLEND::NORMAL, PD::LIGHTER};
        if(comp == "svg:dst-in") return {BLEND::NORMAL, PD::DST_IN};
        if(comp == "svg:dst-out") return {BLEND::NORMAL, PD::DST_OUT};
        if(comp == "svg:src-atop") return {BLEND::NORMAL, PD::SRC_ATOP};
        if(comp == "svg:dst-atop") return {BLEND::NORMAL, PD::DST_ATOP};
        return {BLEND::NORMAL};
    }

    static const char* layerTypeToString(const LayerType type){
        switch (type) {
        case ROOT:
            return "ROOT";
        case FOLDER:
            return "FOLDER";
        case IMAGE:
            return "IMAGE";
        case UNKNOWN:
            return "UNKNOWN";
        }
        return "";
    }

    static void printLayers(const QVector<layerStack>& layers) {
        qDebug("<|LAYER STACK|>");
        for (const auto& layer : layers) {
            qDebug() << QString("<| " + QString(layerTypeToString(layer.type)) + " |>").toStdString().c_str();
            qDebug() << "name: " << layer.name.c_str();
            qDebug() << "type: " << layer.type;
            if (layer.type != UNKNOWN && layer.type != ROOT) {
                qDebug() << "x: " << layer.x;
                qDebug() << "y: " << layer.y;
                qDebug() << "is visible: " << layer.isVisible;
                qDebug() << "opacity: " << layer.opacity;
                qDebug() << "composite blend: " << QString::number(static_cast<int>(layer.composite.blend)).toStdString().c_str();
                qDebug() << "composite pd: " << QString::number(static_cast<int>(layer.composite.pdComposite)).toStdString().c_str();
                qDebug() << "capacity: " << layer.capacity;
                qDebug() << "image: " << (layer.image.isNull()? "no": "yes");
                qDebug() << "rect: " << layer.rect;
            }
            qDebug() << QString("<| /" + QString(layerTypeToString(layer.type)) + "/ |>").toStdString().c_str();
        }
    }

    void printSelf() const{
        qDebug("<|IMAGE|>");
        qDebug() << "image height: " << oraImage.oraImage.height;
        qDebug() << "image width: " << oraImage.oraImage.width;
        qDebug() << "image rect: " << oraImage.oraImage.rect;
        qDebug() << "openRaster spec version: " << oraImage.oraImage.oraVersion.c_str();
        printLayers(oraImage.layers);
        qDebug("<|IMAGE|>");
    }
};



#endif //ORAPARSER_H
