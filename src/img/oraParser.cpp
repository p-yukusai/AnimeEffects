//
// Created by yukusai on 02/07/2025.
//

#include "oraParser.h"

#include "XCAssert.h"

#include <QFileInfo>

bool oraParser::charIsEqualTo(const char* cStr, const std::string& str) { return std::string(cStr) == str; }

QPixmap oraParser::createPattern(const QSize& size, const QColor& background, const QColor& foreground) {
    QPixmap pixmap(size);
    pixmap.fill(background);
    {
        QPainter painter(&pixmap);
        painter.setPen(foreground);
        painter.drawLine(0, size.height() / 2, size.width(), size.height() / 2);
        painter.drawLine(size.width() / 2, 0, size.width() / 2, size.height());
    }
    return pixmap;
}

void oraParser::parse(layerStack* layer_stack, pugi::xml_node::iterator::pointer xml_node) {
    bool visAttrExists = false;

    if(charIsEqualTo(xml_node->name(), "layer")){
        layer_stack->type = IMAGE;
        oraImage.oraImage.layerNumber += 1;
    }
    else if(charIsEqualTo(xml_node->name(), "stack")){
        layer_stack->type = FOLDER;
    }
    else{
        layer_stack->type = UNKNOWN;
        qDebug() << "Unknown stack sibling value: " << xml_node->name();
    }

    if (layer_stack->type == UNKNOWN) { return; }
    for(auto attr: xml_node->attributes()){
        if(charIsEqualTo(attr.name(), "src")){
            QImage img;
            try {
                img = QImage::fromData(QByteArray::fromStdString(oraFile->read(attr.as_string())))
                          .convertedTo(QImage::Format_RGBA8888);
            }
            catch(...){
                qDebug() << "Unable to read image at " << attr.as_string() << ". Utilizing fallback.";
                img = createPattern(QSize(1028, 1028)).toImage();
            }
            layer_stack->image = img;
        }
        else if(charIsEqualTo(attr.name(), "name")){
            layer_stack->name = attr.as_string();
            if(layer_stack->name.empty()) { layer_stack->name = "Unknown"; }
        }
        else if(charIsEqualTo(attr.name(), "x")){
            layer_stack->x = attr.as_int();
        }
        else if(charIsEqualTo(attr.name(), "y")){
            layer_stack->y = attr.as_int();
        }
        else if(charIsEqualTo(attr.name(), "opacity")){
            // pugixml as_float() is strtod-based and honors LC_NUMERIC: under a
            // comma-decimal locale (de_DE, cs_CZ, ...) "0.8" parses as 0.0, silently
            // zeroing layer/stack opacity. QString::toFloat() always parses C-locale
            // (dot separator), which is what the ORA spec's xs:float uses.
            layer_stack->opacity = QString::fromUtf8(attr.as_string()).toFloat();
        }
        else if(charIsEqualTo(attr.name(), "visibility")){
            visAttrExists = true;
            layer_stack->isVisible = charIsEqualTo(attr.as_string(), "visible");
        }
        else if(charIsEqualTo(attr.name(), "composite-op")){
            layer_stack->composite = stringToComposite(attr.as_string());
        }
        else{
            qDebug() << "Unknown attribute: " << attr.name() << "; with value: " << attr.value();
        }
    }
    if(!visAttrExists){
        layer_stack->isVisible = true;
    }
    if (layer_stack->type == IMAGE) {
        layer_stack->rect = QRect(layer_stack->x, layer_stack->y, layer_stack->image.width(), layer_stack->image.height());
    }
    else {
        layer_stack->rect = QRect(layer_stack->x, layer_stack->y, 0, 0);
    }
    int capacity = 0;
    for ([[maybe_unused]] auto child: xml_node->children()) {
        capacity += 1;
    }
    // direct children only: legacy consumer img/Util.cpp:401 (the GUI ORA-resource
    // path) still walks with this; the project-import loader uses `subtree` below
    layer_stack->capacity = capacity;
    oraImage.layers.append(*layer_stack);
    const int selfIndex = (int)oraImage.layers.size() - 1;

    // recurse into children only: sibling iteration is the caller's job (a nested
    // sibling loop here re-appends the following siblings' descendants, duplicating
    // entries and scrambling the nesting the loader rebuilds from `capacity`). Each
    // child gets a FRESH layerStack: reusing one instance across elements bleeds
    // attributes (e.g. a stack without composite-op would inherit the previous
    // element's blend mode instead of the spec default svg:src-over).
    for (auto child: xml_node->children()){
        layerStack childLayer;
        parse(&childLayer, &child);
    }
    // the flat list is pre-order, so everything appended after this entry belongs to its
    // subtree; the loader rebuilds the nesting from these counts (`capacity` alone -
    // direct children - undercounts deeper descendants)
    oraImage.layers[selfIndex].subtree = (int)oraImage.layers.size() - selfIndex - 1;
}

bool oraParser::initialize() {
    const std::string stackStr = oraFile->read("stack.xml");
    XC_ASSERT(reader);
    const pugi::xml_parse_result result = reader->load_string(stackStr.c_str());
    if(result.status == pugi::xml_parse_status::status_ok){
        // Parse image
        const pugi::xml_node& Image = reader->child("image");
        oraImage.oraImage.width = Image.attribute("w").as_int();
        oraImage.oraImage.height = Image.attribute("h").as_int();
        oraImage.oraImage.oraVersion = Image.attribute("version").as_string();
        oraImage.oraImage.rect = QRect(0, 0, oraImage.oraImage.width, oraImage.oraImage.height);
        if(oraImage.oraImage.oraVersion.empty()){ oraImage.oraImage.oraVersion = "0.0.1"; }
        // Parse stacks
        pugi::xml_node mainStack = reader->child("image").child("stack");
        layerStack layer;
        layer.name = QFileInfo(QString::fromStdString(oraFile->get_filename())).baseName().toStdString();
        layer.type = ROOT;
        oraImage.layers.append(layer);
        // the root stack's children in document order, each with a FRESH layerStack:
        // reusing one instance across elements bleeds attributes (an element missing
        // an attribute would inherit the previous element's value instead of the
        // default), and sibling recursion belongs to this loop, not to parse()
        for (auto child: mainStack.children()){
            layerStack childLayer;
            parse(&childLayer, &child);
        }
    }
    else{
        qDebug() << result.status;
        delete reader;
        return false;
    }
    delete reader;
    return true;
}

