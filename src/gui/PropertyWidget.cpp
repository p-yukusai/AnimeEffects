#include "gui/PropertyWidget.h"

#include "gui/qml/InspectorViewModel.h"
#include "gui/qml/QmlTheme.h"
#include "gui/theme/Colors.h"

#include <QApplication>
#include <QKeyEvent>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QQmlContext>
#include <QUrl>
#include <QQuickImageProvider>
#include <QVBoxLayout>

namespace gui {

namespace {
QFont uiFontFromLocale(const LocaleParam& aLocale) {
    QFont font(QApplication::font());
    if (!aLocale.fontFamily.isEmpty())
        font.setFamily(aLocale.fontFamily);
    const QString size = aLocale.fontSize.trimmed();
    bool ok = false;
    if (size.endsWith(QStringLiteral("pt"), Qt::CaseInsensitive)) {
        const qreal pt = size.left(size.size() - 2).toDouble(&ok);
        if (ok) font.setPointSizeF(pt);
    } else if (size.endsWith(QStringLiteral("px"), Qt::CaseInsensitive)) {
        const int px = size.left(size.size() - 2).toInt(&ok);
        if (ok) font.setPixelSize(px);
    } else if (!size.isEmpty()) {
        const qreal pt = size.toDouble(&ok);
        if (ok) font.setPointSizeF(pt);
    }
    return font;
}

// A QQuickWidget that hands the application the keys its scene has no use for.
//
// QQuickWidget keeps Qt's default Qt::StrongFocus (QQuickWidgetPrivate::init calls
// setFocusPolicy) and its keyPressEvent does exactly one thing: it sends the SAME
// event to the offscreen QQuickWindow (qquickwidget.cpp, Qt 6.12) - it never calls
// QWidget::keyPressEvent. That is correct while the scene has an active focus item:
// QQuickDeliveryAgentPrivate::deliverKeyEvent gives the key to it. With NO
// activeFocusItem it does nothing at all, and the event is still accepted (the
// offscreen window's event() returns true and the key arrives here accepted), so
// QApplication::notify's propagation walk
//
//     bool def = key->isAccepted();
//     while (w) {
//         if (def) key->accept(); else key->ignore();
//         res = d->notify_helper(w, e);
//         if (res && key->isAccepted()) break;
//         if (!pr || w->isWindow()) break;
//         w = w->parentWidget();
//     }
//
// stops at this widget: the rest of the walk, and MainWindow::keyPressEvent with
// it, is never reached. The effect: the whole rebindable key map is dead while the
// inspector holds focus - Ctrl+Z, Ctrl+S, every binding - because a key pressed over
// the panel is delivered to an empty scene and dropped.
//
// So the scene gets the event FIRST, and only when it has nothing focused to give it
// to do we mark it ignored and let Qt walk on up to the window. The guard is not
// politeness: with an active focus item the scene's answer stands, and a TextInput's
// own Ctrl+Z (its text undo) leaves the event accepted, so the walk stops at the
// field the user is typing into. A focused item that REJECTS a key still lets it
// through, exactly as it always has - that behaviour is QQuickWidget's, and this
// class neither adds to it nor takes it away.
class KeyForwardingQuickWidget: public QQuickWidget {
public:
    using QQuickWidget::QQuickWidget;
protected:
    void keyPressEvent(QKeyEvent* aEvent) override {
        QQuickWidget::keyPressEvent(aEvent);
        forwardWhenSceneHasNoFocus(aEvent);
    }
    void keyReleaseEvent(QKeyEvent* aEvent) override {
        QQuickWidget::keyReleaseEvent(aEvent);
        forwardWhenSceneHasNoFocus(aEvent);
    }
private:
    // Both halves of the pair matter: MainWindow::keyReleaseEvent is what releases a
    // command held down through the key map (MoveCanvas, RotateCanvas), so a
    // swallowed release would leave one armed forever.
    void forwardWhenSceneHasNoFocus(QKeyEvent* aEvent) {
        QQuickWindow* window = quickWindow();
        if (window && window->activeFocusItem())
            return;
        aEvent->ignore();
    }
};
} // namespace


namespace {

// The mask card's THUMBNAIL. A QQuickImage caches by URL, so the model bumps a
// revision the URL carries and the provider builds the matte on demand - no second
// copy of the field is kept anywhere, and every state the document can reach (a
// default key, a key at the playhead, a blended frame) shows what that frame holds.
class MaskThumbnailProvider: public QQuickImageProvider {
public:
    explicit MaskThumbnailProvider(InspectorViewModel* aModel)
        : QQuickImageProvider(QQuickImageProvider::Image), mModel(aModel) {}

    QImage requestImage(const QString& aId, QSize* aSize, const QSize& aRequested) override {
        Q_UNUSED(aId);
        const QImage image = mModel ? mModel->maskThumbnail(aRequested) : QImage();
        if (aSize)
            *aSize = image.size();
        // NO MASK IS ANSWERED WITH A VALID, EMPTY IMAGE. Returning a null QImage makes
        // Qt log "Failed to get image from provider" on every refresh of every node
        // without a matte - measured on a project where the card is present for all 40
        // layers and the log filled with it.
        if (image.isNull()) {
            QImage blank(1, 1, QImage::Format_ARGB32_Premultiplied);
            blank.fill(Qt::transparent);
            if (aSize)
                *aSize = blank.size();
            return blank;
        }
        return image;
    }

private:
    InspectorViewModel* mModel;
};

} // namespace

PropertyWidget::PropertyWidget(ViaPoint& aViaPoint, QWidget* aParent, GUIResources* aGUIResources,
                               const LocaleParam& aLocale):
    QWidget(aParent),
    mProject(),
    mTimeLineSlot(),
    mNodeAttrSlot(),
    mResModifiedSlot(),
    mTreeRestructSlot(),
    mProjAttrSlot(),
    mModel(),
    mTheme(),
    mUiFont(uiFontFromLocale(aLocale)),
    mQuick() {
    Q_UNUSED(aGUIResources);
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);

    mModel = new InspectorViewModel(&aViaPoint, this);
    mTheme = new QmlTheme(this);

    // a filter row's "paint mask" reaches the window, which owns the toolbox
    mModel->onMaskPaintRequested.connect([this](core::TimeKeyType aLane, core::FilterMaskChannel aChannel) {
        onMaskPaintRequested(aLane, aChannel);
    });
    // ...and the mask card's Paint asks for the node's own matte
    mModel->onLayerMaskPaintRequested.connect([this]() { onLayerMaskPaintRequested(); });

    mQuick = new KeyForwardingQuickWidget(this);
    mQuick->setResizeMode(QQuickWidget::SizeRootObjectToView);
    mQuick->rootContext()->setContextProperty(QStringLiteral("inspector"), mModel);
    mQuick->rootContext()->setContextProperty(QStringLiteral("theme"), mTheme);
    // QML's Qt.application.font is the process font; the dock's font comes from
    // LocaleParam and is applied to widgets through standard.ssa, so the scene
    // needs that font explicitly to match 8pt Geist (or the locale override).
    mQuick->rootContext()->setContextProperty(QStringLiteral("uiFont"), mUiFont);
    // the card's thumbnail (image://maskthumb/<anything>?<revision>)
    mQuick->engine()->addImageProvider(
        QStringLiteral("maskthumb"), new MaskThumbnailProvider(mModel)
    );
    mQuick->setClearColor(theme::Colors::current().base);
    mQuick->setSource(QUrl(QStringLiteral("qrc:/AnimeEffects/Dialogs/qml/PropertiesPanel.qml")));
    connect(mQuick, &QQuickWidget::statusChanged, this, [this](QQuickWidget::Status aStatus) {
        if (aStatus == QQuickWidget::Error) {
            for (const QQmlError& error : mQuick->errors())
                qWarning().noquote() << error.toString();
        }
    });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(mQuick);
}

PropertyWidget::~PropertyWidget() {
    unlinkProject();
    // The scene binds to the model and theme context objects. Tear it down
    // first, as every dialog shell here does, so teardown never re-reads a
    // destroyed context property.
    delete mQuick;
    mQuick = nullptr;
}

void PropertyWidget::unlinkProject() {
    if (mProject) {
        mProject->onTimeLineModified.disconnect(mTimeLineSlot);
        mProject->onNodeAttributeModified.disconnect(mNodeAttrSlot);
        mProject->onResourceModified.disconnect(mResModifiedSlot);
        mProject->onTreeRestructured.disconnect(mTreeRestructSlot);
        mProject->onProjectAttributeModified.disconnect(mProjAttrSlot);
        mProject.reset();
    }
}

void PropertyWidget::setProject(core::Project* aProject) {
    unlinkProject();
    mModel->setProject(nullptr);

    if (aProject) {
        mProject = aProject->pointee();

        mTimeLineSlot = aProject->onTimeLineModified.connect(this, &PropertyWidget::onKeyUpdated);
        mNodeAttrSlot = aProject->onNodeAttributeModified.connect(this, &PropertyWidget::onAttributeUpdated);
        mResModifiedSlot =
            aProject->onResourceModified.connect([=](core::ResourceEvent&, bool) { updateAllProperties(); });
        mTreeRestructSlot =
            aProject->onTreeRestructured.connect([=](core::ObjectTreeEvent&, bool) { updateAllProperties(); });
        mProjAttrSlot =
            aProject->onProjectAttributeModified.connect([=](core::ProjectEvent&, bool) { mModel->refresh(); });

        mModel->setProject(aProject);
    }
}

void PropertyWidget::updateAllProperties() {
    mModel->refresh();
}

void PropertyWidget::onSelectionChanged(core::ObjectNode* aRepresentNode) { mModel->setTarget(aRepresentNode); }

void PropertyWidget::onAttributeUpdated(core::ObjectNode&, bool) {
    mModel->refreshAttributes();
    onVisualUpdated();
}

void PropertyWidget::onKeyUpdated(core::TimeLineEvent& aEvent, bool) {
    // A MATTE'S FIELD is not a value the rows read: the card draws it as an IMAGE whose
    // URL carries a revision, and only refresh() bumps that. A brush stroke therefore
    // left the thumbnail showing the field it had before the stroke - measured on a
    // folder, painted, with the card still empty.
    for (const core::TimeLineEvent::Target& target : aEvent.targets()) {
        if (target.pos.type() == core::TimeKeyType_Mask) {
            mModel->refresh();
            return;
        }
    }
    mModel->refreshKeys();
}

void PropertyWidget::onFrameUpdated() { mModel->refreshFrame(); }

void PropertyWidget::onPlayBackStateChanged(bool aIsActive) { mModel->setPlaybackActive(aIsActive); }

void PropertyWidget::refreshTheme() {
    mTheme->refresh();
    mModel->refreshTheme();
    mQuick->setClearColor(theme::Colors::current().base);
}

} // namespace gui
