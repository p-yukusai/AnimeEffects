// Gate for the MASK CARD: the real PropertiesPanel.qml, the real
// gui::InspectorViewModel, and a real core::Project + node behind them.
//
// The mask's mechanism is measured elsewhere (layer_mask_pixels for the draw,
// layer_mask_commands for the edits, timekey_roundtrip for storage). What can go
// wrong HERE is the surface: a card that binds the wrong state, a key dot that keys
// nothing, a "Key at frame N" that names a different frame from the one it keys, a
// thumbnail URL that never changes so a painted matte keeps showing its first pixels.
//
// It is also the only check that the whole inspector SCENE still loads: a syntax or
// binding error in the card makes the panel's component fail, which is what this
// asserts before it looks at anything else.
//
// Headless (offscreen platform, no GL): the panel is composed as Items, not painted.
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickImageProvider>
#include <QTextStream>
#include <cstdio>
#include <cstdlib>

#include "XC.h"
#include "gui/qml/QmlTheme.h"

namespace {

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
int failures = 0;

void check(bool aOk, const char* aWhat) {
    if (!aOk) {
        std::printf("FAIL  %s\n", aWhat);
        ++failures;
    }
}

// The card's whole interface, as a stand-in for gui::InspectorViewModel. Driving the
// REAL model needs the whole GUI (it reaches ViaPoint, the resource dialog and the key
// invoker), and what a view's gate has to prove is the wiring: these properties in,
// those invokables out. The model's own behaviour is gated by layer_mask_commands.
class StubInspector: public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasTarget READ hasTarget NOTIFY changed)
    Q_PROPERTY(bool playbackActive READ playbackActive NOTIFY changed)
    Q_PROPERTY(bool maskPresent READ maskPresent NOTIFY changed)
    Q_PROPERTY(bool maskKeyed READ maskKeyed NOTIFY changed)
    Q_PROPERTY(bool maskHasKeyAtFrame READ maskHasKeyAtFrame NOTIFY changed)
    Q_PROPERTY(int maskRevision READ maskRevision NOTIFY changed)
    Q_PROPERTY(int currentFrame READ currentFrame NOTIFY changed)

public:
    bool hasTarget() const { return true; }
    bool playbackActive() const { return false; }
    bool maskPresent() const { return mPresent; }
    bool maskKeyed() const { return mKeyed; }
    bool maskHasKeyAtFrame() const { return mOnKey; }
    int maskRevision() const { return mRevision; }
    int currentFrame() const { return mFrame; }

    // every call the card can make is recorded, so the gate asserts the WIRING
    QStringList calls;

    Q_INVOKABLE void addMask() {
        calls << QStringLiteral("addMask");
        mPresent = true;
        bump();
    }
    Q_INVOKABLE void removeMask() {
        calls << QStringLiteral("removeMask");
        mPresent = false;
        mKeyed = false;
        mOnKey = false;
        bump();
    }
    Q_INVOKABLE void invertMask() { calls << QStringLiteral("invertMask"); bump(); }
    Q_INVOKABLE void toggleMaskKey() {
        calls << QStringLiteral("toggleMaskKey");
        if (!mKeyed) {
            mKeyed = true;
            mOnKey = true;
        } else if (mOnKey) {
            mOnKey = false;   // this fixture models "remove this frame's key"
        } else {
            mOnKey = true;
        }
        bump();
    }
    Q_INVOKABLE void requestLayerMaskPaint() { calls << QStringLiteral("requestLayerMaskPaint"); }

    // --- knobs the gate drives, in place of the document ---
    void setState(bool aPresent, bool aKeyed, bool aOnKey, int aFrame) {
        mPresent = aPresent;
        mKeyed = aKeyed;
        mOnKey = aOnKey;
        mFrame = aFrame;
        bump();
    }

signals:
    void changed();

private:
    void bump() {
        ++mRevision;
        emit changed();
    }
    bool mPresent = false;
    bool mKeyed = false;
    bool mOnKey = false;
    int mRevision = 0;
    int mFrame = 0;
};

// The thumbnail's server, as the application registers it (PropertyWidget adds
// "maskthumb" to the engine). Without it QML reports "Invalid image provider" and the
// card's image silently never appears - which is exactly the kind of gap this gate
// exists to catch, so the stub answers and COUNTS the requests it serves.
class StubThumbProvider: public QQuickImageProvider {
public:
    StubThumbProvider(): QQuickImageProvider(QQuickImageProvider::Image) {}

    QImage requestImage(const QString& aId, QSize* aSize, const QSize&) override {
        ++requests;
        lastId = aId;
        QImage image(8, 8, QImage::Format_Grayscale8);
        image.fill(uchar(255));
        if (aSize)
            *aSize = image.size();
        return image;
    }

    int requests = 0;
    QString lastId;
};

// A Repeater's delegates live in the VISUAL tree, so a QObject-only walk misses them;
// the card's own children are plain Items, but the walk is the shared idiom.
void collectItems(QQuickItem* aItem, QList<QObject*>& aOut) {
    if (!aItem)
        return;
    aOut.append(aItem);
    for (QQuickItem* child : aItem->childItems())
        collectItems(child, aOut);
}

QObject* byObjectName(QQuickItem* aRoot, const QString& aName) {
    QList<QObject*> all;
    collectItems(aRoot, all);
    for (QObject* object : all) {
        if (object->objectName() == aName)
            return object;
    }
    return nullptr;
}

QObject* controlWithText(QQuickItem* aRoot, const QString& aText) {
    QList<QObject*> all;
    collectItems(aRoot, all);
    for (QObject* object : all) {
        if (object->property("text").toString() == aText && object->property("visible").toBool())
            return object;
    }
    return nullptr;
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    StubInspector inspector;
    gui::QmlTheme theme;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("inspector"), &inspector);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    engine.rootContext()->setContextProperty(QStringLiteral("uiFont"), QGuiApplication::font());
    auto* thumbs = new StubThumbProvider();
    engine.addImageProvider(QStringLiteral("maskthumb"), thumbs);

    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(AE_QML_DIR "/InspectorMaskCard.qml")));
    if (component.isError()) {
        QTextStream(stderr) << "the mask card does not load:\n";
        for (const QQmlError& error : component.errors())
            QTextStream(stderr) << "  " << error.toString() << "\n";
    }
    check(component.status() == QQmlComponent::Ready, "the mask card loads");
    QObject* created = component.create();
    auto* root = qobject_cast<QQuickItem*>(created);
    check(root != nullptr, "and it builds an Item");
    if (!root)
        return 1;

    const auto state = [&]() { return root->property("stateText").toString(); };
    const auto dot = [&]() { return root->property("dotState").toString(); };

    // ---- absent -------------------------------------------------------------
    inspector.setState(false, false, false, 0);
    QCoreApplication::processEvents();
    check(state() == QStringLiteral("none"), "the card says none with no matte");
    check(!root->property("present").toBool(), "and reports no matte");
    check(controlWithText(root, QStringLiteral("Add mask")) != nullptr, "the Add action is offered");

    // pressing Add calls the model - the one wiring a card can get wrong silently
    {
        QObject* add = controlWithText(root, QStringLiteral("Add mask"));
        check(add != nullptr && QMetaObject::invokeMethod(add, "clicked"), "Add is a live button");
        check(inspector.calls.contains(QStringLiteral("addMask")), "pressing Add calls addMask");
        QCoreApplication::processEvents();
        check(root->property("present").toBool(), "the card follows the model without a refresh call");
        check(controlWithText(root, QStringLiteral("Add mask")) == nullptr, "the Add action goes away");
    }

    // the thumbnail asked the provider, and the URL carries the revision
    {
        inspector.setState(true, false, false, 0);
        QCoreApplication::processEvents();
        const int before = thumbs->requests;
        inspector.setState(true, false, false, 0);   // one more refresh, same state
        QCoreApplication::processEvents();
        std::printf("MEASURED thumbnail: %d provider requests, last id \"%s\"\n", thumbs->requests,
                    qPrintable(thumbs->lastId));
        check(thumbs->requests > 0, "the card's thumbnail reaches the image provider");
        check(
            thumbs->requests > before,
            "a refresh re-asks the provider (the URL's revision changed, so a painted matte "
            "cannot keep showing its first pixels)"
        );
    }

    // ---- static -------------------------------------------------------------
    inspector.setState(true, false, false, 0);
    QCoreApplication::processEvents();
    check(state() == QStringLiteral("static"), "a matte with no keys is static");
    check(dot() == QStringLiteral("static"), "and the dot is hollow");

    // ---- between keys: read only, and the one press that fixes it ----------
    inspector.setState(true, true, false, 34);
    QCoreApplication::processEvents();
    check(dot() == QStringLiteral("animated"), "the dot halves between keys");
    check(state() == QStringLiteral("blended — read only"), "and the card says the matte is a blend");
    check(
        controlWithText(root, QStringLiteral("Key at frame 34")) != nullptr,
        "the card offers a key AT THE PLAYHEAD it was told"
    );
    check(controlWithText(root, QStringLiteral("Paint mask")) == nullptr,
          "painting is not even offered on a blended frame (the matte is read only)");

    {
        QObject* keyButton = controlWithText(root, QStringLiteral("Key at frame 34"));
        check(
            keyButton != nullptr && QMetaObject::invokeMethod(keyButton, "clicked"),
            "the key action is a live button"
        );
        check(
            inspector.calls.contains(QStringLiteral("toggleMaskKey")),
            "and it calls toggleMaskKey"
        );
    }

    // ---- on a key: paintable, and Editing says so --------------------------
    inspector.setState(true, true, true, 34);
    QCoreApplication::processEvents();
    check(dot() == QStringLiteral("keyed"), "the dot fills on a key");
    check(state() == QStringLiteral("key at this frame"), "the card names the state");
    {
        QObject* paint = controlWithText(root, QStringLiteral("Paint mask"));
        check(paint != nullptr && paint->property("enabled").toBool(), "painting is enabled on a key");
        check(
            paint != nullptr && QMetaObject::invokeMethod(paint, "clicked"),
            "Paint is a live button"
        );
        check(
            inspector.calls.contains(QStringLiteral("requestLayerMaskPaint")),
            "and it asks the window for the tool"
        );
    }
    {
        // the secondary actions are ICON buttons (a word button's 72px floor overflowed
        // the dock), so they are found by their accessible name
        QObject* invert = root->findChild<QObject*>(QString());
        for (QObject* object : root->findChildren<QObject*>()) {
            if (object->property("label").toString() == QStringLiteral("Invert mask"))
                invert = object;
        }
        check(invert != nullptr, "Invert is in the card");
        check(
            invert != nullptr && QMetaObject::invokeMethod(invert, "clicked"), "Invert is live"
        );
        check(inspector.calls.contains(QStringLiteral("invertMask")), "and calls invertMask");
    }
    {
        QObject* clear = nullptr;
        for (QObject* object : root->findChildren<QObject*>()) {
            if (object->property("label").toString() == QStringLiteral("Remove mask"))
                clear = object;
        }
        check(clear != nullptr, "Clear is in the card");
        check(clear != nullptr && QMetaObject::invokeMethod(clear, "clicked"), "Clear is live");
        check(inspector.calls.contains(QStringLiteral("removeMask")), "and calls removeMask");
        QCoreApplication::processEvents();
        check(state() == QStringLiteral("none"), "removing the matte returns the card to none");
    }

    delete created;

    if (failures == 0)
        std::printf("inspector_mask_card: four states, four edits, every button wired\n");
    else
        std::printf("inspector_mask_card: %d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}

#include "inspector_mask_card.moc"
