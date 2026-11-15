// The filter card's HEADER DOES NOT MOVE WHEN THE CARD EXPANDS.
//
// `notes.md`, 2026-10-26: "Expanding/collapsing a Filter card in the properties panel
// seems to affect the layout of the header despite the header staying the same height.
// The headers layout should not shift." The cause was that the header's x/y/width were a
// function of `expanded` - a 1px inset that existed only to leave room for the card's
// frame stroke - so every toggle moved the header's content +1px right and down and its
// right edge -1px left.
//
// The rule the fix states is "expansion may change INK, never GEOMETRY", and that rule
// is a QML geometry fact, which no C++ model test can see: `inspector_view_model` pins
// the expansion STATE, and this file pins the pixels it must not move.
//
// The measurement is the marker the card publishes for its own disclosure
// (`aeAccessibleDisclosure`, anchors.fill: parent of the header row), so it IS the
// header's rectangle, read through the same item tree a user sees. Delegates are walked
// with childItems(): a Repeater parents them into the VISUAL tree, so a QObject-tree
// lookup finds none of them (AGENTS.md's measured trap).
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/filter_card_geometry
#include <QApplication>
#include <QOpenGLWidget>
#include <QOpenGLVersionFunctionsFactory>
#include <QSurfaceFormat>
#include <QFont>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
#include <cstdio>

#include "XCAssert.h"
#include "XCReport.h"
#include "core/LayerNode.h"
#include "core/ShaderHolder.h"
#include "gl/DeviceInfo.h"
#include "gl/Global.h"
#include "core/TimeLine.h"
#include "gui/qml/InspectorViewModel.h"
#include "gui/AudioPlaybackWidget.h"
#include "gui/ViaPoint.h"
#include "gui/qml/FilterStackModel.h"
#include "gui/qml/QmlTheme.h"
#include "gui/theme/Icons.h"

class HarnessAssertHandler: public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};
class HarnessErrorHandler: public XCErrorHandler {
public:
    void critical(const QString&, const QString&, const QString&) const override { std::abort(); }
};
HarnessAssertHandler gAssertHandler;
HarnessErrorHandler gErrorHandler;
XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

namespace {

int failures = 0;

void check(bool aOk, const QString& aWhat) {
    std::printf("%s  %s\n", aOk ? "PASS" : "FAIL", qPrintable(aWhat));
    if (!aOk) ++failures;
}

// Every item under a root, delegates included: a Repeater's delegates are parented into
// the VISUAL tree, so QObject::findChildren misses them.
QList<QQuickItem*> visualTree(QQuickItem* aRoot) {
    QList<QQuickItem*> out;
    QList<QQuickItem*> stack{aRoot};
    while (!stack.isEmpty()) {
        QQuickItem* it = stack.takeLast();
        for (QQuickItem* child : it->childItems()) {
            out.append(child);
            stack.append(child);
        }
    }
    return out;
}

// The disclosure markers, in the order the stack shows its cards: sorted by their
// Y in the panel, NOT by the walk. visualTree's traversal is a LIFO stack, so it
// visits siblings bottom-first - with two cards that handed back the SECOND card as
// `first`, and the assertion then read "expanding the second moved the first" while
// the layout was perfectly correct.
QList<QQuickItem*> disclosures(QQuickItem* aRoot) {
    QList<QQuickItem*> out;
    for (QQuickItem* it : visualTree(aRoot)) {
        if (it->objectName() == QLatin1String("aeAccessibleDisclosure")) out.append(it);
    }
    std::sort(out.begin(), out.end(), [aRoot](QQuickItem* aLhs, QQuickItem* aRhs) {
        return aLhs->mapToItem(aRoot, 0, 0).y() < aRhs->mapToItem(aRoot, 0, 0).y();
    });
    return out;
}

QString stateOf(QQuickItem* aMarker) {
    return aMarker->property("accessibleValue").toString();
}

void press(QQuickItem* aMarker) {
    QMetaObject::invokeMethod(aMarker, "accessiblePress");
}

QRectF rectOf(QQuickItem* aItem, QQuickItem* aFrame) {
    const QPointF p = aItem->mapToItem(aFrame, 0, 0);
    return QRectF(p.x(), p.y(), aItem->width(), aItem->height());
}

// The card a disclosure marker's header belongs to: marker (fills the header) ->
// headerItem -> card.
QQuickItem* cardOf(QQuickItem* aMarker) {
    QQuickItem* header = aMarker ? aMarker->parentItem() : nullptr;
    return header ? header->parentItem() : nullptr;
}

QString describe(const QRectF& r) {
    return QStringLiteral("x=%1 y=%2 w=%3 h=%4")
        .arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
}

} // namespace

// LINK STUBS. Two symbols this gate never calls come along with the model sources:
// `Project::deserialize` references the audio widget's deserializer, and
// `InspectorViewModel`'s two ASSET actions reference the resource picker. Linking their
// real providers would drag AudioPlaybackWidget, ResourceDialog and KeyCommandInvoker - the
// whole gui shell - into a test that measures card geometry. Both are unreachable from
// here (no project is loaded, and no asset action is invoked), so they answer nothing.
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) {
    return false;
}

img::ResourceNode* gui::ViaPoint::requireOneResource() { return nullptr; }


int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // A LIVE GL CONTEXT, for the same reason psd_layer_identity needs one: a
    // core::LayerNode's mesh holder binds GL in its constructor, so building the node this
    // gate puts in the inspector needs a current context. Nothing renders here.
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setVersion(gl::Global::kVersion.first, gl::Global::kVersion.second);
    QSurfaceFormat::setDefaultFormat(format);
    QOpenGLWidget glWidget;
    glWidget.resize(4, 4);
    glWidget.show();
    glWidget.makeCurrent();
    if (!glWidget.context()) {
        std::printf("FAIL  no GL context (run this under xvfb-run)\n");
        return 2;
    }
    auto* funcs = QOpenGLVersionFunctionsFactory::get<gl::Global::Functions>(glWidget.context());
    if (!funcs || !funcs->initializeOpenGLFunctions()) {
        std::printf("FAIL  no GL functions\n");
        return 2;
    }
    gl::Global::setContext(glWidget);
    gl::Global::setFunctions(*funcs);

    gui::QmlTheme theme;
    QTemporaryDir iconDir;
    if (!iconDir.isValid()) {
        std::printf("FAIL  a scratch dir for the tinted icons\n");
        return 2;
    }
    theme::tintIcons(QStringLiteral(AE_ICON_SRC), iconDir.path(), theme::Colors::current());
    theme::setIconDir(iconDir.path());

    // Two filters on one node, pushed straight into the timeline's stack: this gate
    // measures CARD GEOMETRY, and building a core::Project here would drag the audio
    // widget (Project::deserialize) and the whole gui shell into a test that needs
    // neither. The rows are built from the same entries `addFilter` writes, so the panel
    // sees exactly what a real add produces.
    core::ShaderHolder shaderHolder;
    auto* layer = new core::LayerNode(QStringLiteral("card"), shaderHolder);
    layer->timeLine()->setFilterEntries({
        core::FilterEntry(core::TimeKeyType_HSV, true),
        core::FilterEntry(core::TimeKeyType_Blur, true),
    });

    // The model's ViaPoint is only reached by the two ASSET actions (both null-guarded)
    // and mProject only by the frame/asset paths, so a null pair is enough here.
    gui::InspectorViewModel inspector(nullptr, nullptr);
    inspector.setTarget(layer);

    QQmlEngine engine;
    const QFont uiFont = QApplication::font();
    engine.rootContext()->setContextProperty(QStringLiteral("inspector"), &inspector);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    engine.rootContext()->setContextProperty(QStringLiteral("uiFont"), uiFont);

    QQmlComponent component(&engine,
                            QUrl(QStringLiteral("file:" AE_QML_DIR "/PropertiesPanel.qml")));
    if (component.isError()) {
        for (const auto& error : component.errors())
            QTextStream(stderr) << "panel load error: " << error.toString() << "\n";
        return 2;
    }

    QQuickWindow window;
    QScopedPointer<QObject> root(component.create());
    auto* rootItem = qobject_cast<QQuickItem*>(root.data());
    if (!rootItem) {
        std::printf("FAIL  the panel component is not an Item\n");
        return 2;
    }
    rootItem->setParentItem(window.contentItem());
    rootItem->setWidth(320);
    rootItem->setHeight(760);
    window.resize(320, 760);
    window.show();
    QCoreApplication::processEvents();

    auto markers = disclosures(rootItem);
    check(markers.size() == 2, QStringLiteral("two filter cards publish a disclosure (found %1)")
                                  .arg(markers.size()));
    if (markers.size() != 2) return 1;

    auto* first = markers.at(0);
    auto* second = markers.at(1);
    const QRectF header0 = rectOf(first, rootItem);
    const QRectF other0 = rectOf(second, rootItem);
    std::printf("MEASURED  card 1 header (collapsed): %s\n", qPrintable(describe(header0)));
    std::printf("MEASURED  card 2 header (collapsed): %s\n", qPrintable(describe(other0)));
    check(header0.height() == 30.0, QStringLiteral("the header row is 30px tall (%1)").arg(header0.height()));
    // The header spans its CARD (not the panel): the card is the marker's grandparent
    // (marker -> headerItem -> card), which is what "the header is the row" means.
    QQuickItem* card = first->parentItem() ? first->parentItem()->parentItem() : nullptr;
    check(card && qFuzzyCompare(first->width(), card->width()),
          QStringLiteral("the header spans its card (%1 of %2)")
              .arg(first->width())
              .arg(card ? card->width() : -1.0));
    check(stateOf(first) == QLatin1String("Collapsed"), QStringLiteral("card 1 reports Collapsed"));

    // ---- expanding the FIRST card must not move its own header ----
    const qreal cardHeight0 = cardOf(first) ? cardOf(first)->height() : -1.0;
    press(first);
    QCoreApplication::processEvents();
    const QRectF header1 = rectOf(first, rootItem);
    std::printf("MEASURED  card 1 header (expanded):  %s\n", qPrintable(describe(header1)));
    check(stateOf(first) == QLatin1String("Expanded"), QStringLiteral("card 1 reports Expanded"));
    // The FIXTURE must be able to show the defect: if the body did not grow, "the header
    // did not move" would be true for the trivial reason that nothing moved at all.
    check(cardOf(first) && cardOf(first)->height() > cardHeight0,
          QStringLiteral("the card grew when it expanded (%1 -> %2)")
              .arg(cardHeight0)
              .arg(cardOf(first) ? cardOf(first)->height() : -1.0));
    check(header1 == header0,
          QStringLiteral("the header did not move when its card expanded (%1 -> %2)")
              .arg(describe(header0), describe(header1)));

    // ---- and expanding the SECOND card must not move the first one's header ----
    const QRectF other1 = rectOf(second, rootItem);
    press(second);
    QCoreApplication::processEvents();
    const QRectF header2 = rectOf(first, rootItem);
    std::printf("MEASURED  card 1 header (2 expanded): %s\n", qPrintable(describe(header2)));
    check(stateOf(second) == QLatin1String("Expanded"), QStringLiteral("card 2 reports Expanded"));
    check(header2 == header0,
          QStringLiteral("card 1's header stayed put while card 2 expanded (%1 -> %2)")
              .arg(describe(header0), describe(header2)));
    // NOT asserted here: that an expansion moves the cards BELOW it. That is the
    // Column's business and `inspector_view_model` already proves the state; this gate
    // exists for the one thing the model cannot see, which is the header's own rect.
    Q_UNUSED(other1);

    // ---- collapsing again is the same invariant in reverse ----
    press(first);
    QCoreApplication::processEvents();
    check(rectOf(first, rootItem) == header0,
          QStringLiteral("the header did not move when its card collapsed again"));

    if (failures) {
        std::printf("FAILED %d check(s)\n", failures);
        return 1;
    }
    std::printf("OK: a filter card's header does not move when the card expands\n");
    return 0;
}
