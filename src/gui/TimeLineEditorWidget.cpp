#include <QMenu>
#include <QMessageBox>
#include "core/TimeKeyExpans.h"
#include "gui/TimeLineEditorWidget.h"
#include "gui/MouseSetting.h"
#include "gui/obj/obj_Item.h"
#include "qapplication.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QMimeData>

namespace gui {
//-------------------------------------------------------------------------------------------------
TimeCursor::TimeCursor(QWidget* aParent):
    QWidget(aParent), mBodyColor(QColor(230, 230, 230, 180)), mEdgeColor(QColor(80, 80, 80, 180)) {
    this->setAutoFillBackground(false);
}

void TimeCursor::setCursorPos(const QPoint& aPos, int aHeight) {
    const QPoint range(5, 5);
    QRect rect;
    rect.setTopLeft(aPos - range);
    rect.setBottomRight(aPos + range + QPoint(0, aHeight));
    this->setGeometry(rect);
}

void TimeCursor::paintEvent(QPaintEvent* aEvent) {
    (void)aEvent;
    QPainter painter;
    painter.begin(this);

    const QPoint range(5, 5);
    const QBrush kBrushBody(mBodyColor);
    const QBrush kBrushEdge(mEdgeColor);

    painter.setPen(QPen(kBrushEdge, 1));
    painter.setBrush(kBrushBody);
    painter.drawLine(range + QPoint(0, range.y()), range + QPoint(0, this->geometry().height()));

    painter.setRenderHint(QPainter::Antialiasing);
    painter.drawEllipse(QPointF(range), range.x() - static_cast<qreal>(0.5f), range.y() - static_cast<qreal>(0.5f));

    painter.end();
}

QColor TimeCursor::edgeColor() const { return mEdgeColor; }

void TimeCursor::setEdgeColor(const QColor& cursorEdgeColor) { mEdgeColor = cursorEdgeColor; }

QColor TimeCursor::bodyColor() const { return mBodyColor; }

void TimeCursor::setBodyColor(const QColor& cursorBodyColor) { mBodyColor = cursorBodyColor; }

//-------------------------------------------------------------------------------------------------
TimeLineEditorWidget::TimeLineEditorWidget(ViaPoint& aViaPoint, QWidget* aParent):
    QWidget(aParent),
    mViaPoint(aViaPoint),
    mProject(),
    mTimeLineSlot(),
    mTreeRestructSlot(),
    mProjectAttrSlot(),
    mEditor(),
    mCamera(),
    mTimeCursor(this),
    mKeyCommandMap(*aViaPoint.keyCommandMap()),
    mCopyKey(),
    mPasteKey(),
    mDeleteKey(),
    mTargets(),
    mCopyTargets(),
    mPastePos(),
    mOnPasting(),
    mTimelineTheme() {
    mTimeCursor.show();

    mEditor.reset(new ctrl::TimeLineEditor());
    this->setMouseTracking(true);
    this->setContextMenuPolicy(Qt::CustomContextMenu);

    this->connect(this, &QWidget::customContextMenuRequested, this, &TimeLineEditorWidget::onContextMenuRequested);

    {
        mCopyKey = new QAction(tr("Copy key"), this);
        mCopyKey->connect(mCopyKey, &QAction::triggered, this, &TimeLineEditorWidget::onCopyKeyTriggered);

        mPasteKey = new QAction(tr("Paste key"), this);
        mPasteKey->connect(mPasteKey, &QAction::triggered, this, &TimeLineEditorWidget::onPasteKeyTriggered);

        mDeleteKey = new QAction(tr("Delete key"), this);
        mDeleteKey->connect(mDeleteKey, &QAction::triggered, this, &TimeLineEditorWidget::onDeleteKeyTriggered);

        mCopyToClipboard = new QAction(tr("Copy key to clipboard"), this);
        mCopyToClipboard->connect(
            mCopyToClipboard, &QAction::triggered, this, &TimeLineEditorWidget::onCopyCBTriggered
        );

        // This is as annoying as you think it is, but I'm too lazy to think of a better way to do this and since no
        // other easing functions will be supported this will do.
        mSelectEasing = new QMenu(tr("Change key(s) easing to..."), this);
        {
            auto* none = new QAction(tr("None"), this);
            auto* linear = new QAction(tr("Linear"), this);
            auto* sine = new QAction(tr("Sine"), this);
            auto* quad = new QAction(tr("Quad"), this);
            auto* cubic = new QAction(tr("Cubic"), this);
            auto* quart = new QAction(tr("Quart"), this);
            auto* quint = new QAction(tr("Quint"), this);
            auto* expo = new QAction(tr("Expo"), this);
            auto* circ = new QAction(tr("Circ"), this);
            auto* back = new QAction(tr("Back"), this);
            auto* elastic = new QAction(tr("Elastic"), this);
            auto* bounce = new QAction(tr("Bounce"), this);

            QVector<QAction*> easings{
            none, linear, sine, quad, cubic, quart, quint, expo, circ, back, elastic, bounce,
            };
            int x = 0;
            for(auto easing: easings) {
                mSelectEasing->addAction(easing);
                mSelectEasing->connect(easing, &QAction::triggered, [=] {
                    onSelectEasingTriggered(x);
                });
                x++;
            }
        }
        mSelectRange = new QMenu(tr("Change key(s) range to..."), this);
        {
            auto* in = new QAction(tr("In"), this);
            auto* out = new QAction(tr("Out"), this);
            auto* all = new QAction(tr("All"), this);

            QVector<QAction*> ranges{in, out, all};
            int x = 0;
            for (auto range: ranges) {
                mSelectRange->addAction(range);
                mSelectEasing->connect(range, &QAction::triggered, [=] {
                    onSelectRangeTriggered(x);
                });
                x++;
            }
        }

        mSelectSpacing = new QAction(tr("Select spacing between keys"), this);
        mSelectSpacing->connect(mSelectSpacing, &QAction::triggered, [=] {onSelectSpacingTriggered();});
    }
    this->update();
    {
        auto key = mKeyCommandMap.get("Copy");
        if (key)
            key->invoker = [=]() {
                mTargets = core::TimeLineEvent();
                mEditor->retrieveFocusTargets(mTargets);
                mCopyKey->trigger();
            };
    }
    {
        auto key = mKeyCommandMap.get("Paste");
        if (key)
            key->invoker = [=]() {
                if (!mCopyTargets.targets().isEmpty()) {
                    mPastePos = this->mapFromGlobal(QCursor::pos());
                    mPasteKey->trigger();
                }
            };
    }
    {
        auto key = mKeyCommandMap.get("Delete");
        if (key)
            key->invoker = [=]() {
                mTargets = core::TimeLineEvent();
                if (mEditor->retrieveFocusTargets(mTargets)) {
                    mDeleteKey->trigger();
                }
            };
    }
}

void TimeLineEditorWidget::setProject(core::Project* aProject) {
    if (mProject) {
        mProject->onTimeLineModified.disconnect(mTimeLineSlot);
        mProject->onTreeRestructured.disconnect(mTreeRestructSlot);
        mProject->onProjectAttributeModified.disconnect(mProjectAttrSlot);
    }

    if (aProject) {
        mProject = aProject->pointee();
        mTimeLineSlot = aProject->onTimeLineModified.connect(this, &TimeLineEditorWidget::onTimeLineModified);

        mTreeRestructSlot = aProject->onTreeRestructured.connect(this, &TimeLineEditorWidget::onTreeRestructured);

        mProjectAttrSlot =
            aProject->onProjectAttributeModified.connect(this, &TimeLineEditorWidget::onProjectAttributeModified);
    } else {
        mProject.reset();
    }

    mEditor->setProject(aProject);
    updateSize();
}

void TimeLineEditorWidget::setFrame(core::Frame aFrame) {
    mEditor->setFrame(aFrame);

    // particial rendering
    updateTimeCursorPos();
}

core::Frame TimeLineEditorWidget::currentFrame() const { return mEditor->currentFrame(); }

int TimeLineEditorWidget::maxFrame() const { return mEditor->maxFrame(); }

void TimeLineEditorWidget::updateTimeCursorPos() {
    if (mCamera) {
        auto pos = mEditor->currentTimeCursorPos();
        mTimeCursor.setCursorPos(
            QPoint(pos.x(), pos.y() - static_cast<int>(mCamera->leftTopPos().y())), mCamera->screenHeight()
        );
    }
}

void TimeLineEditorWidget::updateCamera(const core::CameraInfo& aCamera) {
    mCamera = &aCamera;
    updateSize();
    updateTimeCursorPos();
}

void TimeLineEditorWidget::updateLines(QTreeWidgetItem* aTopNode) {
    mEditor->clearRows();

    if (!aTopNode || !aTopNode->isExpanded())
        return;

    for (int i = 0; i < aTopNode->childCount(); ++i) {
        updateLinesRecursive(aTopNode->child(i));
    }
    updateSize();
}

void TimeLineEditorWidget::updateLinesRecursive(QTreeWidgetItem* aItem) {
    if (!aItem)
        return;

    const bool isClosedFolder = !aItem->isExpanded() && aItem->childCount() > 0;
    obj::Item* objItem = obj::Item::cast(aItem);
    if (objItem && !objItem->isTopNode()) {
        int screenTop = mCamera ? static_cast<int>(-mCamera->leftTopPos().y()) : 0;

        const QRect vrect = objItem->visualRect();
        const int t = vrect.top() + screenTop;
        const int b = vrect.bottom() + screenTop;

        mEditor->pushRow(&objItem->node(), util::Range(t, b), isClosedFolder);
    }

    if (!aItem->isExpanded())
        return;

    for (int i = 0; i < aItem->childCount(); ++i) {
        updateLinesRecursive(aItem->child(i));
    }
    updateSize();
}

void TimeLineEditorWidget::updateLineSelection(core::ObjectNode* aRepresent) {
    mEditor->updateRowSelection(aRepresent);
    this->update();
}

bool TimeLineEditorWidget::updateCursor(const core::AbstractCursor& aCursor) {
    ctrl::TimeLineEditor::UpdateFlags flags = mEditor->updateCursor(aCursor);

    if (flags & ctrl::TimeLineEditor::UpdateFlag_ModView) {
        updateTimeCursorPos();
        this->update();
    }
    return (flags & ctrl::TimeLineEditor::UpdateFlag_ModFrame);
}

void TimeLineEditorWidget::updateWheel(QWheelEvent* aEvent, int aMouseX, int& aFrameBefore, int& aPixelAfter) {
    aFrameBefore = mEditor->frameAtPixel(aMouseX);
    mEditor->updateWheel(aEvent->angleDelta().y(), mViaPoint.mouseSetting().invertTimeLineScaling);
    aPixelAfter = mEditor->pixelAtFrame(aFrameBefore);
    updateSize();
}

void TimeLineEditorWidget::updateSize() {
    // get inner size
    // add enough margin to coordinate height with ObjectTreeWidget.
    QSize size = mEditor->modelSpaceSize() + QSize(0, 128);

    if (mCamera) {
        const QSize camsize = mCamera->screenSize();
        if (size.width() < camsize.width())
            size.setWidth(camsize.width());
        if (size.height() < camsize.height())
            size.setHeight(camsize.height());
    }
    this->resize(size);
    this->update();
}

void TimeLineEditorWidget::updateProjectAttribute() {
    mEditor->updateProjectAttribute();
    updateTimeCursorPos();
    updateSize();
}

void TimeLineEditorWidget::updateTheme(theme::Theme& aTheme) {
    Q_UNUSED(aTheme) // TODO
    mTimelineTheme.reset();
}
QSize TimeLineEditorWidget::getEditorSize() const {
    return mEditor->modelSpaceSize();
}

int TimeLineEditorWidget::frameAtPixel(int aPixelX) const {
    return mEditor->frameAtPixel(aPixelX);
}

int TimeLineEditorWidget::pixelAtFrame(int aFrame) const {
    return mEditor->pixelAtFrame(aFrame);
}

void TimeLineEditorWidget::paintEvent(QPaintEvent* aEvent) {
    QPainter painter;
    painter.begin(this);
    if (mCamera) {
        mEditor->render(painter, *mCamera, mTimelineTheme, aEvent->rect());
    }
    painter.end();
}

void TimeLineEditorWidget::onTimeLineModified(core::TimeLineEvent&, bool) {
    if (!mOnPasting) {
        mCopyTargets = core::TimeLineEvent();
    }
    mEditor->updateKey();
    this->update();
}

void TimeLineEditorWidget::onTreeRestructured(core::ObjectTreeEvent&, bool) { mCopyTargets = core::TimeLineEvent(); }

void TimeLineEditorWidget::onProjectAttributeModified(core::ProjectEvent&, bool) {
    mCopyTargets = core::TimeLineEvent();
}

void TimeLineEditorWidget::onContextMenuRequested(const QPoint& aPos) {
    QMenu menu(this);

    mTargets = core::TimeLineEvent();
    if (mEditor->checkContactWithKeyFocus(mTargets, aPos)) {
        menu.addAction(mCopyKey);
        menu.addAction(mCopyToClipboard);
        menu.addSeparator();
        menu.addAction(mSelectSpacing);
        menu.addMenu(mSelectEasing);
        menu.addMenu(mSelectRange);
        menu.addSeparator();
        menu.addAction(mDeleteKey);
    } else {
        mPastePos = aPos;
        mPasteKey->setEnabled(mCopyTargets.hasAnyTargets());
        menu.addAction(mPasteKey);
    }
    menu.exec(this->mapToGlobal(aPos));
    this->update();
}

// Type, frame, easing type, easing range, easing weight.
void addVecToJson(QJsonObject* json, QString title, QVector2D vec) {
    json->insert(title + "X", vec.x());
    json->insert(title + "Y", vec.y());
}

// Tf means Type & Frame in case you're wondering.
void addTfToObj(QJsonObject* json, int keyType, core::TimeKey* timeKey) {
    json->insert("Type", core::TimeLine::getTimeKeyName(static_cast<core::TimeKeyType>(keyType)));
    json->insert("Frame", timeKey->frame());
}
template<typename keyData>
void addStandardToObj(QJsonObject* json, keyData data, int keyType, core::TimeKey* timeKey) {
    addTfToObj(json, keyType, timeKey);
    json->insert("eType", util::Easing::getTypeName(data.easing().type));
    json->insert("eRange", util::Easing::getRangeName(data.easing().range));
    json->insert("eWeight", data.easing().weight);
}

QJsonObject getKeyTypeSerialized(int keyType, core::TimeKey* timeKey, core::ObjectNode* node) {
    switch (keyType) {
    // General data for all keys: Type, Frame and Easing {type, range & weight}
    case core::TimeKeyType_Move: {
        core::MoveKey::Data data = static_cast<const core::MoveKey*>(timeKey)->data();
        // Data types: pos(Vec2D), centroid(Vec2D), spline(int) //
        QJsonObject move;
        addStandardToObj(&move, data, keyType, timeKey);
        addVecToJson(&move, "Pos", data.pos());
        addVecToJson(&move, "Centre", data.centroid());
        move["Spline"] = core::MoveKey::getSplineName(data.spline());
        return move;
    }
    case core::TimeKeyType_Rotate: {
        auto data = static_cast<const core::RotateKey*>(timeKey)->data();
        // Data types: Rotate (float) //
        QJsonObject rotate;
        addStandardToObj(&rotate, data, keyType, timeKey);
        rotate["Rotate"] = data.rotate();
        return rotate;
    } break;
    case core::TimeKeyType_Scale: {
        auto data = static_cast<const core::ScaleKey*>(timeKey)->data();
        // Data types: Scale (vec2d) //
        QJsonObject scale;
        addStandardToObj(&scale, data, keyType, timeKey);
        addVecToJson(&scale, "Scale", data.scale());
        return scale;
    }
    case core::TimeKeyType_Depth: {
        auto data = static_cast<const core::DepthKey*>(timeKey)->data();
        // Data types: Depth (float) //
        QJsonObject depth;
        addStandardToObj(&depth, data, keyType, timeKey);
        depth["Depth"] = data.depth();
        return depth;
    }
    case core::TimeKeyType_Opa: {
        auto data = static_cast<const core::OpaKey*>(timeKey)->data();
        // Data types: Opacity (float) //
        QJsonObject opa;
        addStandardToObj(&opa, data, keyType, timeKey);
        opa["Opacity"] = data.opacity();
        return opa;
    }
    case core::TimeKeyType_Bone: {
        auto data = static_cast<const core::BoneKey*>(timeKey)->data();
        /* Data types:
        LocalPos(vec2d), LocalAngle(float), Range(vec2d * 2),
        ---
        Shape[Valid(bool), SegmentStart(vec2d), SegmentDir(vec2d), Unit(vec2d),
        DirAngle(float), Length(float), Radius (vec2d * 2), RootBendAngle(float *2),
        TailBendAngle(float * 2), Bounding(vec2d), Polygon{PolyCount(int), Vertices(vec2d)}],
        ---
        WorldPos(vec2d), WorldAngle(float), Rotate(float),
        Children(Bones with same data type as previously described minus children of their own)
        */
        QJsonObject bones;
        addTfToObj(&bones, keyType, timeKey);
        QJsonArray boneArray;
        for (auto bone : data.topBones()) {
            QJsonObject topBone;
            topBone["Bone"] = bone->serializeToJson(false);
            boneArray.append(topBone);
        }
        bones["Bones"] = boneArray;
        return bones;
    }
    case core::TimeKeyType_Pose: {
        auto data = static_cast<const core::PoseKey*>(timeKey)->data();
        // Data types: Same as the bone key.
        QJsonObject pose;
        addTfToObj(&pose, keyType, timeKey);
        QJsonArray boneArray;
        for (auto bone : data.topBones()) {
            QJsonObject topBone;
            topBone["Bone"] = bone->serializeToJson(false);
            boneArray.append(topBone);
        }
        pose["Poses"] = boneArray;
        return pose;
    }
    case core::TimeKeyType_Mesh: {
        auto data = static_cast<const core::MeshKey*>(timeKey)->data();
        // Data for MeshKey: Origin Offset (vec2d), Vertex count (int), Vertices (vec2d), Edge count (int),
        // Edges (ints), Face count (int), Faces (ints)
        QJsonObject mesh;
        addTfToObj(&mesh, keyType, timeKey);
        mesh["Mesh"] = data.serializeToJson();
        return mesh;
    } break;
    case core::TimeKeyType_FFD: {
        auto data = static_cast<const core::FFDKey*>(timeKey)->data();
        // Data for FFDKey: Vertex count (int), Vertex positions (vec3)
        QJsonObject ffd;
        addStandardToObj(&ffd, data, keyType, timeKey);
        ffd["FFD"] = static_cast<const core::FFDKey*>(timeKey)->serializeToJson();
        // ⚠ IF THE IMAGE'S GRIDMESH DOESN'T MATCH THE TARGET'S THE DEFORMATION WON'T OCCUR ⚠ //
        /* We need the cell size to keep consistency with the vertex data, as the same image will
        always generate the same mesh for any given cell size, we also need to check if there is an
        image key close to our target to get the correct cellsize for the image being deformed. */
        if (node->timeLine()->map(core::TimeKeyType_Image).size() != 0) {
            int dataFrame = timeKey->frame();
            int closest = 0;
            int index = 0;
            for (auto nodeM : node->timeLine()->map(core::TimeKeyType_Image)) {
                if (nodeM->frame() == dataFrame || nodeM->frame() - dataFrame <= closest - dataFrame) {
                    closest = index;
                }
                index++;
            }
            ffd["cellSize"] =
                static_cast<const core::ImageKey*>(node->timeLine()->timeKey(core::TimeKeyType_Image, closest))
                ?
                static_cast<const core::ImageKey*>(node->timeLine()->timeKey(core::TimeKeyType_Image, closest))
                      ->data()
                      .gridMesh()
                      .cellSize() : 0;
        } else {
            ffd["cellSize"] = node->timeLine()->current().areaImageKey()->data().gridMesh().cellSize();
        }
        return ffd;
    }
    case core::TimeKeyType_Image: {
        auto data = static_cast<const core::ImageKey*>(timeKey)->data();
        /* Data for ImageKey: Identifier(QString), BlendMode(QString), Offset(vec2d), GridMesh[Size(qsize),
        OriginOffset(vec2d), CellPx(int), IndexCount(int), Indices(uint32 * indexcount),
        VertexCount(int), Positions(vec3d), Offsets(vec3d), TexCoords(vec2d), Normals (vec3d),
        ConnectSize(size_t), XCMemSize(uint64), XCMemData (char*)]
        ---
        Regarding the correct identification of image resources, it is entirely possible for this method as currently
        implemented to not get the correct image because there is more than one resource with the same identifier.
        If this happens to you all I can say is git gud lmao.
        */
        QJsonObject img;
        addStandardToObj(&img, data, keyType, timeKey);
        img["Image"] = static_cast<const core::ImageKey*>(timeKey)->serializeToJson();
        // qDebug() << QJsonDocument(img).toJson(QJsonDocument::Indented).toStdString().c_str();
        return img;
    }
    case core::TimeKeyType_HSV: {
        auto data = static_cast<const core::HSVKey*>(timeKey)->data();
        // Data types: HSV (List<int>) //
        QJsonObject hsv;
        addStandardToObj(&hsv, data, keyType, timeKey);
        hsv["Hue"] = data.hsv()[0];
        hsv["Saturation"] = data.hsv()[1];
        hsv["Value"] = data.hsv()[2];
        hsv["Absolute"] = data.hsv()[3];
        return hsv;
    }
    }
    // Returns empty object if no case is found
    return QJsonObject();
}

void TimeLineEditorWidget::onCopyKeyTriggered(bool) {
    mCopyTargets = mTargets;
    QSettings settings;
    auto cbCopy = settings.value("generalsettings/keys/autocb");
    bool copyToCb = !cbCopy.isValid() || cbCopy.toBool();
    if (copyToCb) {
        onCopyCBTriggered(true);
    }
}

void TimeLineEditorWidget::onCopyCBTriggered(bool) {
    if (!mCopyTargets.hasAnyTargets()) {
        mCopyTargets = mTargets;
    }
    QJsonObject targets;
    QJsonArray keys;
    targets["TargetsSize"] = mCopyTargets.targets().size();
    qDebug() << mCopyTargets.targets().size();
    for (auto cTarget : mCopyTargets.targets()) {
        qDebug() << cTarget.pos.key();
        core::TimeKeyType keyType = cTarget.pos.key()->type();
        qDebug() << keyType;
        core::TimeKey* timeKey = cTarget.node->timeLine()->timeKey(keyType, cTarget.pos.key()->frame());
        keys.append(getKeyTypeSerialized(keyType, timeKey, cTarget.node));
    }
    targets["Keys"] = keys;
    // There be IO dragons here
    // qDebug() << QJsonDocument(targets).toJson(QJsonDocument::Indented).toStdString().c_str();
    QClipboard* cb = QGuiApplication::clipboard();
    cb->setText(QJsonDocument(targets).toJson(QJsonDocument::Indented).toStdString().c_str());
}

void TimeLineEditorWidget::onPasteKeyTriggered(bool) {
    mOnPasting = true;
    if (!mEditor->pasteCopiedKeys(mCopyTargets, mPastePos)) {
        QMessageBox::warning(nullptr, tr("Operation Error"), tr("Failed to paste keys. "
        "\n(If you want to paste across objects copy the key to the clipboard and right-click and paste to the node instead)"));
    }
    mOnPasting = false;
}

// It's called easingType, but it can also be Range depending on the function that calls it
void assignEasing(const util::LinkPointer<core::Project>& mProject, int easingType,
    const core::TimeLineEvent::Target* target, const core::TimeKey* key, int frame, bool assignEasing){
    XC_PTR_ASSERT(key);
    switch(key->type()) {
    case core::TimeKeyType_Move: {
        auto newData =  dynamic_cast<const core::MoveKey*>(key)->data();
        if(assignEasing) { newData.easing().type = static_cast<util::Easing::Type>(easingType); }
        else { newData.easing().range = static_cast<util::Easing::Range>(easingType); }
        ctrl::TimeLineUtil::assignMoveKeyData(*mProject, *target->node, frame, newData);
    }
        break;
    case core::TimeKeyType_Rotate: {
        auto newData =  dynamic_cast<const core::RotateKey*>(key)->data();
        if(assignEasing) { newData.easing().type = static_cast<util::Easing::Type>(easingType); }
        else { newData.easing().range = static_cast<util::Easing::Range>(easingType); }
        ctrl::TimeLineUtil::assignRotateKeyData(*mProject, *target->node, frame, newData);
    }
        break;
    case core::TimeKeyType_Scale:{
        auto newData =  dynamic_cast<const core::ScaleKey*>(key)->data();
        if(assignEasing) { newData.easing().type = static_cast<util::Easing::Type>(easingType); }
        else { newData.easing().range = static_cast<util::Easing::Range>(easingType); }
        ctrl::TimeLineUtil::assignScaleKeyData(*mProject, *target->node, frame, newData);
    }
        break;
    case core::TimeKeyType_Depth:{
        auto newData =  dynamic_cast<const core::DepthKey*>(key)->data();
        if(assignEasing) { newData.easing().type = static_cast<util::Easing::Type>(easingType); }
        else { newData.easing().range = static_cast<util::Easing::Range>(easingType); }
        ctrl::TimeLineUtil::assignDepthKeyData(*mProject, *target->node, frame, newData);
    }
        break;
    case core::TimeKeyType_Opa:{
        auto newData =  dynamic_cast<const core::OpaKey*>(key)->data();
        if(assignEasing) { newData.easing().type = static_cast<util::Easing::Type>(easingType); }
        else { newData.easing().range = static_cast<util::Easing::Range>(easingType); }
        ctrl::TimeLineUtil::assignOpaKeyData(*mProject, *target->node, frame, newData);
    }
        break;
    case core::TimeKeyType_Pose:{
        auto newData =  dynamic_cast<const core::PoseKey*>(key)->data();
        if(assignEasing) { newData.easing().type = static_cast<util::Easing::Type>(easingType); }
        else { newData.easing().range = static_cast<util::Easing::Range>(easingType); }
        ctrl::TimeLineUtil::assignPoseKeyEasing(*mProject, *target->node, frame, newData.easing());
    }
        break;
    case core::TimeKeyType_FFD: {
        auto newData =  dynamic_cast<const core::FFDKey*>(key)->data();
        if(assignEasing) { newData.easing().type = static_cast<util::Easing::Type>(easingType); }
        else { newData.easing().range = static_cast<util::Easing::Range>(easingType); }
        ctrl::TimeLineUtil::assignFFDKeyEasing(*mProject, *target->node, frame, newData.easing());
    }
        break;
    case core::TimeKeyType_HSV:{
        auto newData =  dynamic_cast<const core::HSVKey*>(key)->data();
        if(assignEasing) { newData.easing().type = static_cast<util::Easing::Type>(easingType); }
        else { newData.easing().range = static_cast<util::Easing::Range>(easingType); }
        ctrl::TimeLineUtil::assignHSVKeyData(*mProject, *target->node, frame, newData);
    }
        break;
    default:
        qDebug("You somehow tried to modify an unsupported key, you absolute fool!");
    }
}

void TimeLineEditorWidget::onSelectEasingTriggered(int easingType) {
    for(auto target: mTargets.targets()) {
        auto type = target.pos.key()->type();
        if(type != core::TimeKeyType_Bone && type != core::TimeKeyType_Mesh && type != core::TimeKeyType_Image){
            int frame = target.pos.key()->frame();
            auto key = target.pos.line()->timeKey(target.pos.key()->type(), frame);
            assignEasing(mProject, easingType, &target, key, frame, true);
        }
    }
}
void TimeLineEditorWidget::onSelectRangeTriggered(int rangeType) {
    for(auto target: mTargets.targets()) {
        auto type = target.pos.key()->type();
        if(type != core::TimeKeyType_Bone && type != core::TimeKeyType_Mesh && type != core::TimeKeyType_Image){
            int frame = target.pos.key()->frame();
            auto key = target.pos.line()->timeKey(target.pos.key()->type(), frame);
            assignEasing(mProject, rangeType, &target, key, frame, false);
        }
    }
}

bool targetsAreSeparated(QList<core::TimeLineEvent::Target> &targets){
    int frame = -1;
    bool isDifferent = false;
    for(auto target: targets){
        if(frame == -1){ frame = target.pos.key()->frame();}
        if(target.pos.key()->frame() != frame){ isDifferent = true; }
    }
    return isDifferent;
}
QList<QList<core::TimeLineEvent::Target>*> sortTargets(QList<core::TimeLineEvent::Target>* targets){
    QList<QMap<int, core::TimeKey*>> targetMap;
    QList<QList<core::TimeLineEvent::Target>*> targetLists;
    for(auto target: *targets){
        if(targetMap.empty()){
            targetMap.emplaceBack(target.pos.line()->map(target.pos.key()->type()));
            targetLists.emplace_back(new QList{target});
        }
        else{
            bool containsKey = false;
            // Peak programming right here
            for(const auto& map: targetMap){
                for(const auto& key: map){
                    if(key == target.pos.key()){ containsKey = true; }
                }
            }
            if(containsKey) { targetLists.back()->emplace_back(target); }
            else{
                targetMap.emplaceBack(target.pos.line()->map(target.pos.key()->type()));
                targetLists.emplaceBack(new QList{target});
            }
        }
    }
    for(auto target: targetLists){
        std::sort(target->begin(), target->end(), ctrl::TimeLineUtil::MoveFrameOfKey::greaterThan);
    }
    XC_ASSERT(targetMap.size() == targetLists.size());
    return targetLists;
}

QString keyToString(const core::TimeKeyType key) {
    switch (key) {
    case core::TimeKeyType_Bone:
        return "Bone";
    case core::TimeKeyType_Mesh:
        return "Mesh";
    case core::TimeKeyType_Image:
        return "Image";
    case core::TimeKeyType_HSV:
        return "HSV";
    case core::TimeKeyType_FFD:
        return "FFD";
    case core::TimeKeyType_Depth:
        return "Depth";
    case core::TimeKeyType_Move:
        return "Move";
    case core::TimeKeyType_Opa:
        return "Opacity";
    case core::TimeKeyType_Pose:
        return "Pose";
    case core::TimeKeyType_Rotate:
        return "Rotate";
    case core::TimeKeyType_Scale:
        return "Scale";
    default:
        return "Unknown";
    }
}

void TimeLineEditorWidget::onSelectSpacingTriggered() {
    if(!targetsAreSeparated(mTargets.targets())){
        QMessageBox msg;
        msg.setText(tr("A minimum of two keys in different frames are needed."));
        msg.setWindowTitle(tr("Not enough targets"));
        msg.setIcon(QMessageBox::Information);
        msg.setStandardButtons(QMessageBox::Ok);
        msg.exec();
        return;
    }
    auto *diag = new QDialog;
    diag->setObjectName("Dialog");
    diag->resize(400, 100);
    QSizePolicy sizePolicy(QSizePolicy::Policy::MinimumExpanding, QSizePolicy::Policy::MinimumExpanding);
    diag->setSizePolicy(sizePolicy);
    auto verticalLayout = new QVBoxLayout(diag);
    verticalLayout->setObjectName("verticalLayout");
    auto label = new QLabel(diag);
    label->setObjectName("label");
    QSizePolicy sizePolicy1(QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Maximum);
    label->setSizePolicy(sizePolicy1);
    verticalLayout->addWidget(label);

    auto frameSpacing = new QSpinBox(diag);
    frameSpacing->setMaximum(mProject->attribute().maxFrame());
    frameSpacing->setMinimum(1);
    frameSpacing->setValue(1);
    frameSpacing->setObjectName("frameSpacing");
    verticalLayout->addWidget(frameSpacing);

    auto *buttonBox = new QDialogButtonBox(diag);
    buttonBox->setObjectName("buttonBox");
    buttonBox->setOrientation(Qt::Horizontal);
    buttonBox->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonBox->setCenterButtons(true);
    verticalLayout->addWidget(buttonBox);

    diag->setWindowTitle(tr("Key spacing"));
    label->setText(
        R"(<html><head/><body><p align="center">)" + tr("Number of frames the selected keys should be spaced by:") +
        "</p></body></html>"
    );
    bool connected= connect(buttonBox, &QDialogButtonBox::accepted, this, [diag](){ diag->accept();});
    connected = connected && connect(buttonBox, &QDialogButtonBox::rejected, this, [diag](){ diag->reject();});
    if(!connected){
        diag->deleteLater();
        return;
    }
    QMetaObject::connectSlotsByName(diag);
    frameSpacing->setFocus();
    diag->exec();

    if(diag->result() == QDialog::Accepted){
        // We sort in reverse as to avoid most conflicts while moving keys
        auto targets = sortTargets(&mTargets.targets());
        int spacing = frameSpacing->value();
        // We ignore the first keyframe for each target, which are at the back in this case
        QList<core::TimeKey*> ignoredTargets;
        for(auto target: targets) { ignoredTargets.emplaceBack(target->back().pos.key()); }
        // We move the keys and check if the move was successful
        QList<QPair<core::TimeLineEvent::Target, int>> conflicts;
        QList<QPair<core::TimeLineEvent::Target, int>> outsideRange;
        for(auto target: targets) {
            int tSize = static_cast<int>(target->size()) - 1;
            int initialFrame = target->back().pos.key()->frame();
            for(auto key : *target) {
                int frameAccumulation = spacing * tSize;
                if (!ignoredTargets.contains(key.pos.key())) {
                    int frame = key.pos.key()->frame();
                    int dest = initialFrame + frameAccumulation;
                    const core::TimeKeyType keyType = key.pos.type();
                    if(mProject->attribute().maxFrame() < dest){ outsideRange.emplace_back(key, dest); }
                    else if(!key.pos.line()->move(keyType, frame, dest)) {
                        conflicts.emplace_back(key, dest);
                    }
                    else {
                        // Sigh...
                        void(true);
                    }
                    tSize -= 1;
                }
            }
        }
        if (!outsideRange.empty() || !conflicts.empty()) {
            QMessageBox msg;
            QStringList errorLog;
            for (const auto& [key, frame] : conflicts) {
                errorLog.append(
                    tr("Key conflict detected for key type ") + keyToString(key.pos.type()) +
                    tr(" in node ") + key.node->name() + tr(" at frame ") + QString::number(frame));
            }
            for (const auto& [key, frame] : outsideRange) {
                errorLog.append(
                    tr("Destination for key type ") + keyToString(key.pos.type()) + tr(" in node ") + key.node->name() +
                    tr(" is outside maximum frame for project, ignored attempt to move key to frame ") + QString::number(frame));
            }
            msg.setWindowTitle(tr("Move error"));
            msg.setText(tr("Unable to move ") + QString::number(conflicts.size() + outsideRange.size()) + tr(" key(s), due to the following reasons: "));
            msg.setDetailedText(errorLog.join("\n"));
            msg.exec();
        }
    }
    diag->deleteLater();
}

void TimeLineEditorWidget::onDeleteKeyTriggered(bool) { mEditor->deleteCheckedKeys(mTargets); }

QColor TimeLineEditorWidget::headerContentColor() const { return mTimelineTheme.headerContentColor(); }

void TimeLineEditorWidget::setHeaderContentColor(const QColor& headerContentColor) {
    mTimelineTheme.setHeaderContentColor(headerContentColor);
}

QColor TimeLineEditorWidget::headerBackgroundColor() const { return mTimelineTheme.headerBackgroundColor(); }

void TimeLineEditorWidget::setHeaderBackgroundColor(const QColor& headerBackgroundColor) {
    mTimelineTheme.setHeaderBackgroundColor(headerBackgroundColor);
}

QColor TimeLineEditorWidget::trackColor() const { return mTimelineTheme.trackColor(); }

void TimeLineEditorWidget::setTrackColor(const QColor& trackColor) { mTimelineTheme.setTrackColor(trackColor); }

QColor TimeLineEditorWidget::trackTextColor() const { return mTimelineTheme.trackTextColor(); }

void TimeLineEditorWidget::setTrackTextColor(const QColor& trackTextColor) {
    mTimelineTheme.setTrackTextColor(trackTextColor);
}

QColor TimeLineEditorWidget::trackEdgeColor() const { return mTimelineTheme.trackEdgeColor(); }

void TimeLineEditorWidget::setTrackEdgeColor(const QColor& trackEdgeColor) {
    mTimelineTheme.setTrackEdgeColor(trackEdgeColor);
}

QColor TimeLineEditorWidget::trackSelectColor() const { return mTimelineTheme.trackSelectColor(); }

void TimeLineEditorWidget::setTrackSelectColor(const QColor& trackSelectColor) {
    mTimelineTheme.setTrackSelectColor(trackSelectColor);
}

QColor TimeLineEditorWidget::trackSeperatorColor() const { return mTimelineTheme.trackSeperatorColor(); }

void TimeLineEditorWidget::setTrackSeperatorColor(const QColor& trackSeperatorColor) {
    mTimelineTheme.setTrackSeperatorColor(trackSeperatorColor);
}


} // namespace gui
