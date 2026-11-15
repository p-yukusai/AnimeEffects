#include "gui/PropertyWidget.h"

#include "gui/QmlHost.h"
#include "gui/qml/InspectorViewModel.h"
#include "gui/qml/QmlTheme.h"
#include "gui/theme/Colors.h"

#include <QQuickWidget>
#include <QQmlContext>
#include <QUrl>
#include <QQuickImageProvider>
#include <QVBoxLayout>

namespace gui {

namespace {

// The mask card's THUMBNAIL. A QQuickImage caches by URL, so the model bumps a
// revision the URL carries and the provider builds the matte on demand - no second
// copy of the field is kept anywhere, and every state the document can reach (a
// base value, a key at the playhead, a blended frame) shows what that frame holds.
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
    mUiFont(qmlUiFont(aLocale)),
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
    // ...and the mesh-spacing field's hover/scrub/edit preview reaches the viewport
    mModel->onMeshPreviewRequested.connect(
        [this](core::ObjectNode* aNode, bool aActive) { onMeshPreviewRequested(aNode, aActive); }
    );

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
    // An image replacement or a tree restructure can change the mask's content
    // rect (and therefore whether a target is paintable); rebuild the Tools
    // panel's derived list too.
    onMaskTargetsChanged();
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
            onMaskTargetsChanged();
            return;
        }
    }
    mModel->refreshKeys();
    onMaskTargetsChanged();
}

void PropertyWidget::onFrameUpdated() { mModel->refreshFrame(); }

void PropertyWidget::onPlayBackStateChanged(bool aIsActive) { mModel->setPlaybackActive(aIsActive); }

void PropertyWidget::setMaskPaintState(
    core::ObjectNode* aNode, bool aLayerMask, bool aFilterInput, core::TimeKeyType aLane,
    core::FilterMaskChannel aChannel
) {
    mModel->setMaskPaintState(aNode, aLayerMask, aFilterInput, aLane, aChannel);
}

void PropertyWidget::invertMask() {
    if (mModel)
        mModel->invertMask();
}

void PropertyWidget::refreshTheme() {
    mTheme->refresh();
    mModel->refreshTheme();
    mQuick->setClearColor(theme::Colors::current().base);
}

} // namespace gui
