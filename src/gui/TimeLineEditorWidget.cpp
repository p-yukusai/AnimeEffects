#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <algorithm>
#include "core/TimeKeyExpans.h"
#include "core/BlurKey.h"
#include "core/TimeFormat.h"
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
#include "gui/theme/Colors.h"

namespace gui {
//-------------------------------------------------------------------------------------------------
TimeCursor::TimeCursor(QWidget* aParent):
    QWidget(aParent),
    mNumberColor(theme::Colors::current().text),
    mEdgeColor(theme::Colors::current().accentBright),
    mFrameText(),
    mBadgeColor(),
    mLineX(2) {
    this->setAutoFillBackground(false);
}

void TimeCursor::setCursorPos(const QPoint& aPos, int aHeight) {
    const QFontMetrics fm(font());
    const int badgeWidth = fm.horizontalAdvance(mFrameText) + ctrl::TimeLineEditor::kPillPadding * 2;
    // The playhead line sits in the middle of the widget; widen the widget as
    // needed so the frame-number pill fits around the line. aPos.y() is the
    // ruler header's top, so the pill lands exactly on the numbers row.
    mLineX = std::max(1, badgeWidth / 2);

    QRect rect;
    rect.setTopLeft(aPos - QPoint(mLineX, 0));
    rect.setBottomRight(aPos + QPoint(mLineX, 0) + QPoint(0, aHeight));
    this->setGeometry(rect);
    update();
}

void TimeCursor::setHeaderInfo(const QString& aFrameText, const QColor& aBadgeColor) {
    mFrameText = aFrameText;
    mBadgeColor = aBadgeColor;
    update();
}

void TimeCursor::paintEvent(QPaintEvent* aEvent) {
    (void)aEvent;
    QPainter painter;
    painter.begin(this);

    // The playhead pill wraps the number-text row. Its top edge rides one
    // pixel below the number text's top (kNumberTop + 1) so the pill and the
    // ruler numbers shift together as kNumberTop moves.
    const int pillTop = ctrl::TimeLineEditor::kNumberTop + 1;

    // Playhead line: starts at the bottom of the frame-number row (the pill
    // covers the row above it) and runs down the tracks.
    painter.setPen(QPen(mEdgeColor, 1));
    painter.drawLine(mLineX, pillTop + ctrl::TimeLineEditor::kNumberHeight, mLineX, this->geometry().height());

    // Frame-number pill on the numbers row: an accentBright rounded pill —
    // the playhead's identity. It must read on the plain ruler AND on the
    // temporary playback window's band, so it is token-driven (accentBright
    // fill + max-contrast text), not base-on-base like the old badge.
    if (!mFrameText.isEmpty()) {
        const QFontMetrics fm(font());
        const int textWidth = fm.horizontalAdvance(mFrameText);
        const int pillWidth = textWidth + ctrl::TimeLineEditor::kPillPadding * 2;
        const QRect pill(mLineX - pillWidth / 2, pillTop, pillWidth, ctrl::TimeLineEditor::kNumberHeight);
        QPainterPath path;
        path.addRoundedRect(
            QRectF(pill), ctrl::TimeLineEditor::kPillRadius, ctrl::TimeLineEditor::kPillRadius, Qt::AbsoluteSize
        );
        painter.fillPath(path, mBadgeColor);
        // Draw the number with the exact same rect and alignment as the ruler
        // numbers (time_Renderer::renderHeader) so it sits in the same row.
        const QRect numberRect(
            mLineX - textWidth / 2, ctrl::TimeLineEditor::kNumberTop, textWidth, ctrl::TimeLineEditor::kNumberHeight
        );
        painter.setPen(mNumberColor);
        painter.drawText(numberRect, mFrameText);
    }

    painter.end();
}

QColor TimeCursor::edgeColor() const { return mEdgeColor; }

void TimeCursor::setEdgeColor(const QColor& cursorEdgeColor) { mEdgeColor = cursorEdgeColor; }

QColor TimeCursor::numberColor() const { return mNumberColor; }

void TimeCursor::setNumberColor(const QColor& cursorNumberColor) { mNumberColor = cursorNumberColor; }

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

    // The ruler strip: own paint and own input, over the track surface and
    // under the playhead (raised below so its pill overlays the window pills).
    mTimeRuler.reset(new TimeRuler(*this, this));
    mTimeRuler->setGeometry(0, 0, this->width(), ctrl::TimeLineEditor::kHeaderHeight);
    mTimeRuler->show();
    mTimeCursor.raise();
    this->connect(mTimeRuler.data(), &TimeRuler::frameChanged, this, &TimeLineEditorWidget::rulerFrameChanged);

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

        // Entries bind their easing type explicitly so the menu order is free
        // to group the stepped holds apart from the serialized enum order.
        mSelectEasing = new QMenu(tr("Change key(s) easing to"), this);
        {
            // The entries come from util::Easing's shared display-order table
            // and their labels from getTypeName (one translation source).
            // Custom is set through the spline dialog, so it is skipped here.
            for (int i = 0; i < util::Easing::displayCount(); ++i) {
                const auto type = util::Easing::typeAtDisplayIndex(i);
                if (type == util::Easing::Type_Custom)
                    continue;
                auto* action = new QAction(util::Easing::getTypeName(type), this);
                mSelectEasing->addAction(action);
                mSelectEasing->connect(action, &QAction::triggered, [this, type] {
                    onSelectEasingTriggered(static_cast<int>(type));
                });
            }
        }
        mSelectRange = new QMenu(tr("Change key(s) range to"), this);
        {
            auto* in = new QAction(tr("In"), this);
            auto* out = new QAction(tr("Out"), this);
            auto* all = new QAction(tr("All"), this);

            QVector<QAction*> ranges{in, out, all};
            int x = 0;
            for (auto range: ranges) {
                mSelectRange->addAction(range);
                mSelectRange->connect(range, &QAction::triggered, [=] {
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
            key->addListener([=]() {
                mTargets = core::TimeLineEvent();
                mEditor->retrieveSelectionTargets(mTargets);
                mCopyKey->trigger();
            });
    }
    {
        auto key = mKeyCommandMap.get("Paste");
        if (key)
            key->addListener([=]() {
                if (!mCopyTargets.targets().isEmpty()) {
                    // Anchor keyboard paste at the playhead, not the mouse:
                    // QCursor::pos() is a screen coordinate, and mapped into
                    // this widget it only denotes a frame while the cursor
                    // actually hovers the timeline. Elsewhere (viewport,
                    // object tree) it maps to an arbitrary frame, so Ctrl+V
                    // looked like it pasted at random. The context menu keeps
                    // its own click-position anchor (onContextMenuRequested).
                    const auto& scale = mEditor->timeScale();
                    const int x =
                        scale.pixelWidth(mEditor->currentFrame().get()) + ctrl::TimeLineEditor::kTimeLineMargin;
                    mPastePos = QPoint(x, 0);
                    mPasteKey->trigger();
                }
            });
    }
    {
        auto key = mKeyCommandMap.get("Delete");
        if (key)
            key->addListener([=]() {
                mTargets = core::TimeLineEvent();
                if (mEditor->retrieveSelectionTargets(mTargets)) {
                    mDeleteKey->trigger();
                }
            });
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
    // The project owns the ruler's content (frame range, fps, play window):
    // the surface repaint alone leaves the strip stale until the next
    // camera update.
    if (mTimeRuler)
        mTimeRuler->update();
}

void TimeLineEditorWidget::setFrame(core::Frame aFrame) {
    mEditor->setFrame(aFrame);

    // particial rendering
    updateTimeCursorPos();
}

core::Frame TimeLineEditorWidget::currentFrame() const { return mEditor->currentFrame(); }

void TimeLineEditorWidget::updateTimeCursorPos() {
    if (mCamera) {
        auto pos = mEditor->currentTimeCursorPos();
        const core::TimeFormat format(mEditor->frameRange(), mEditor->fps());
        const QString frameText = format.frameToString(mEditor->currentFrame().get(), mEditor->timeFormatType());
        // The playhead pill is token-driven (accentBright fill, max-contrast
        // text, accentBright line): it must read on the plain ruler AND on
        // the temporary window's band, and follow accent hue changes, so the
        // QSS-substituted colors do not drive it.
        mTimeCursor.setHeaderInfo(frameText, theme::Colors::current().accentBright);
        mTimeCursor.setNumberColor(theme::Colors::current().accentBrightText);
        mTimeCursor.setEdgeColor(theme::Colors::current().accentBright);
        // The playhead pill overlays the ruler numbers, so repaint the full
        // ruler strip (22px) each tick.
        if (mTimeRuler)
            mTimeRuler->update();
        // The playhead overlays the viewport: pinned to the top (on the fixed
        // ruler) and following the content horizontally.
        const QPoint viewportPos(mCamera->leftTopPos().toPoint().x() + pos.x(), 0);
        mTimeCursor.setCursorPos(viewportPos, mCamera->screenHeight());
    }
}

void TimeLineEditorWidget::updateCamera(const core::CameraInfo& aCamera) {
    mCamera = &aCamera;
    updateSize();
    updateTimeCursorPos();
    if (mTimeRuler)
        mTimeRuler->setCamera(&aCamera);
}

void TimeLineEditorWidget::resizeEvent(QResizeEvent* aEvent) {
    QWidget::resizeEvent(aEvent);
    if (mTimeRuler)
        mTimeRuler->setGeometry(0, 0, this->width(), ctrl::TimeLineEditor::kHeaderHeight);
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

bool TimeLineEditorWidget::updateCursor(const core::AbstractCursor& aCursor, Qt::KeyboardModifiers aModifiers) {
    ctrl::TimeLineEditor::UpdateFlags flags = mEditor->updateCursor(aCursor, aModifiers);

    if (flags & ctrl::TimeLineEditor::UpdateFlag_ModView) {
        updateTimeCursorPos();
        this->update();
    }
    return (flags & ctrl::TimeLineEditor::UpdateFlag_ModFrame);
}

bool TimeLineEditorWidget::updateWheel(QWheelEvent* aEvent, int aMouseX, int& aPixelAfter) {
    const int oldSpacing = mEditor->pixelsPerFrame();
    const bool changed =
        mEditor->updateWheel(aEvent->angleDelta().y(), mViaPoint.mouseSetting().invertTimeLineScaling);
    if (changed) {
        // Pixel-exact zoom anchor: the content point under the mouse stays put,
        // regardless of whether it lies inside the frame range.
        aPixelAfter = mEditor->anchorPixelAfterScale(aMouseX, oldSpacing);
        updateSize();
    } else {
        // Zoom clamped at floor/ceiling: keep the view exactly still instead of
        // re-deriving a scroll offset (frame<->pixel rounding would drift it).
        aPixelAfter = aMouseX;
    }
    return changed;
}

void TimeLineEditorWidget::updateSize() {
    // The widget is a viewport-fixed surface sized by TimeLineWidget; content
    // size changes only need a repaint (the paint translates by the camera).
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
    if (mTimeRuler)
        mTimeRuler->updateTheme();
    updateTimeCursorPos();
}
QSize TimeLineEditorWidget::getEditorSize() const {
    return mEditor->modelSpaceSize();
}

int TimeLineEditorWidget::frameStartPixel() const {
    return mEditor->frameStartPixel();
}

int TimeLineEditorWidget::frameEndPixel() const {
    return mEditor->frameEndPixel();
}

void TimeLineEditorWidget::paintEvent(QPaintEvent* aEvent) {
    QPainter painter;
    painter.begin(this);
    if (mCamera) {
        // The surface is viewport-fixed; the content translates by the view
        // origin. Everything then lands at its viewport position — fixed UI
        // included — and nothing is clipped by the frame range.
        const QPoint origin = mCamera->leftTopPos().toPoint();
        painter.translate(origin);
        mEditor->render(painter, *mCamera, mTimelineTheme, aEvent->rect().translated(-origin));
        // The hairline under the ruler separates it from the track area. It
        // sits on the first track row (y = kHeaderHeight), below the ruler
        // widget, and stays fixed like the strip it frames.
        painter.translate(-origin);
        painter.setPen(QPen(QBrush(mTimelineTheme.rulerLineColor()), 1));
        painter.drawLine(0, ctrl::TimeLineEditor::kHeaderHeight, this->width(), ctrl::TimeLineEditor::kHeaderHeight);
    }
    painter.end();
}

void TimeLineEditorWidget::onTimeLineModified(core::TimeLineEvent& aEvent, bool aUndo) {
    if (!mOnPasting) {
        mCopyTargets = core::TimeLineEvent();
    }
    mEditor->updateKey();
    // Creating keys always selects its output: clear the prior selection and
    // keep only the keys a push/copy just created. An undo/redo that REMOVES
    // keys (aUndo=true) must not select a notification over keys that are no
    // longer in their lane — refresh() already dropped them, so only select on
    // a forward (exec/redo) creation.
    if (!aUndo &&
        (aEvent.type() == core::TimeLineEvent::Type_PushKey || aEvent.type() == core::TimeLineEvent::Type_CopyKey)) {
        mEditor->selectCreatedKeys(aEvent);
    }
    this->update();
}

void TimeLineEditorWidget::onTreeRestructured(core::ObjectTreeEvent&, bool) { mCopyTargets = core::TimeLineEvent(); }

void TimeLineEditorWidget::onProjectAttributeModified(core::ProjectEvent&, bool) {
    mCopyTargets = core::TimeLineEvent();
}

void TimeLineEditorWidget::onContextMenuRequested(const QPoint& aPos) {
    // The ruler's context menu is reserved for the temporary playback
    // window: it shows only while a window is active, and only when the
    // click is on the ruler itself — the window lives there, so its clear
    // action sits next to it, and the track menu stays key-focused. The
    // ruler region is the editor's predicate, the same one the gesture
    // machinery uses.
    if (mEditor->isOnRuler(aPos.y())) {
        if (!mEditor->playWindow().active())
            return;
        QMenu menu(this);
        auto* clearWindow = menu.addAction(tr("Clear playback window"));
        clearWindow->connect(clearWindow, &QAction::triggered, [this]() {
            mEditor->clearPlayWindow();
            this->update();
            // The window's band and pills are the ruler's paint; the surface
            // repaint alone would leave them on screen until the next move.
            if (mTimeRuler)
                mTimeRuler->update();
        });
        menu.exec(this->mapToGlobal(aPos));
        this->update();
        return;
    }

    QMenu menu(this);

    // customContextMenuRequested delivers surface coordinates, but every
    // editor query below runs in model space: rows are pushed there
    // (updateLinesRecursive shifts them by the view origin) and mouse
    // events land there through AbstractCursor's toWorldPos. Feeding raw
    // surface coordinates silently missed every key whenever the view was
    // scrolled — no highlight, no key actions — and anchored paste at a
    // shifted frame. The ruler check above stays in surface space on
    // purpose: the ruler strip is pinned to the widget top.
    const QPoint worldPos = mCamera ? mCamera->toWorldPos(QPointF(aPos)).toPoint() : aPos;

    mTargets = core::TimeLineEvent();
    const bool clickedOnKey = mEditor->selectKeysAt(mTargets, worldPos);

    mPastePos = worldPos;
    mPasteKey->setEnabled(mCopyTargets.hasAnyTargets());

    // The key menu applies when the click is on a key or when there is a
    // current selection, so keys stay operable from anywhere once selected.
    if (clickedOnKey || mEditor->retrieveSelectionTargets(mTargets)) {
        menu.addAction(mCopyKey);
        menu.addAction(mCopyToClipboard);
        menu.addAction(mPasteKey);
        menu.addSeparator();
        menu.addAction(mSelectSpacing);
        menu.addMenu(mSelectEasing);
        menu.addMenu(mSelectRange);
        menu.addSeparator();
        menu.addAction(mDeleteKey);
    } else {
        menu.addAction(mPasteKey);
    }
    menu.exec(this->mapToGlobal(aPos));
    // A right-click on an unselected key lent it a temporary selection for
    // this menu only; the previous selection is not restored, so drop the
    // temporary selection now that the interaction is over, then repaint so
    // the highlight clears.
    mEditor->endTemporarySelection();
    this->update();
}

// Type, frame, easing type, easing range, easing weight.
static void addVecToJson(QJsonObject* json, const QString& title, const QVector2D vec) {
    json->insert(title + "X", vec.x());
    json->insert(title + "Y", vec.y());
}

// Tf means Type & Frame in case you're wondering.
static void addTfToObj(QJsonObject* json, int keyType, const core::TimeKey* timeKey) {
    json->insert("Type", core::TimeLine::getTimeKeyName(static_cast<core::TimeKeyType>(keyType)));
    json->insert("Frame", timeKey->frame());
}
template<typename keyData>
static void addStandardToObj(QJsonObject* json, keyData data, const int keyType, core::TimeKey* timeKey) {
    addTfToObj(json, keyType, timeKey);
    json->insert("eType", util::Easing::getTypeName(data.easing().type));
    json->insert("eRange", util::Easing::getRangeName(data.easing().range));
    json->insert("eWeight", data.easing().weight);
}

QJsonObject getKeyTypeSerialized(int keyType, core::TimeKey* timeKey, core::ObjectNode* node) {
    switch (keyType) {
    // General data for all keys: Type, Frame and Easing {type, range & weight}
    case core::TimeKeyType_Move: {
        core::MoveKey::Data data = dynamic_cast<const core::MoveKey*>(timeKey)->data();
        // Data types: pos(Vec2D), centroid(Vec2D), spline(int) //
        QJsonObject move;
        addStandardToObj(&move, data, keyType, timeKey);
        addVecToJson(&move, "Pos", data.pos());
        addVecToJson(&move, "Centre", data.centroid());
        move["Spline"] = core::MoveKey::getSplineName(data.spline());
        return move;
    }
    case core::TimeKeyType_Rotate: {
        auto data = dynamic_cast<const core::RotateKey*>(timeKey)->data();
        // Data types: Rotate (float) //
        QJsonObject rotate;
        addStandardToObj(&rotate, data, keyType, timeKey);
        rotate["Rotate"] = data.rotate();
        return rotate;
    } break;
    case core::TimeKeyType_Scale: {
        auto data = dynamic_cast<const core::ScaleKey*>(timeKey)->data();
        // Data types: Scale (vec2d) //
        QJsonObject scale;
        addStandardToObj(&scale, data, keyType, timeKey);
        addVecToJson(&scale, "Scale", data.scale());
        return scale;
    }
    case core::TimeKeyType_Depth: {
        auto data = dynamic_cast<const core::DepthKey*>(timeKey)->data();
        // Data types: Depth (float) //
        QJsonObject depth;
        addStandardToObj(&depth, data, keyType, timeKey);
        depth["Depth"] = data.depth();
        return depth;
    }
    case core::TimeKeyType_Opa: {
        auto data = dynamic_cast<const core::OpaKey*>(timeKey)->data();
        // Data types: Opacity (float) //
        QJsonObject opa;
        addStandardToObj(&opa, data, keyType, timeKey);
        opa["Opacity"] = data.opacity();
        return opa;
    }
    case core::TimeKeyType_Bone: {
        auto data = dynamic_cast<const core::BoneKey*>(timeKey)->data();
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
            topBone["Bone"] = bone->serializeToJson();
            boneArray.append(topBone);
        }
        bones["Bones"] = boneArray;
        return bones;
    }
    case core::TimeKeyType_Pose: {
        auto data = dynamic_cast<const core::PoseKey*>(timeKey)->data();
        // Data types: Same as the bone key.
        QJsonObject pose;
        addTfToObj(&pose, keyType, timeKey);
        QJsonArray boneArray;
        for (auto bone : data.topBones()) {
            QJsonObject topBone;
            topBone["Bone"] = bone->serializeToJson();
            boneArray.append(topBone);
        }
        pose["Poses"] = boneArray;
        return pose;
    }
    case core::TimeKeyType_Mesh: {
        auto data = dynamic_cast<const core::MeshKey*>(timeKey)->data();
        // Data for MeshKey: Origin Offset (vec2d), Vertex count (int), Vertices (vec2d), Edge count (int),
        // Edges (ints), Face count (int), Faces (ints)
        QJsonObject mesh;
        addTfToObj(&mesh, keyType, timeKey);
        mesh["Mesh"] = data.serializeToJson();
        return mesh;
    } break;
    case core::TimeKeyType_FFD: {
        auto data = dynamic_cast<const core::FFDKey*>(timeKey)->data();
        // Data for FFDKey: Vertex count (int), Vertex positions (vec3)
        QJsonObject ffd;
        addStandardToObj(&ffd, data, keyType, timeKey);
        ffd["FFD"] = dynamic_cast<const core::FFDKey*>(timeKey)->serializeToJson();
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
                dynamic_cast<const core::ImageKey*>(node->timeLine()->timeKey(core::TimeKeyType_Image, closest))
                ?
                dynamic_cast<const core::ImageKey*>(node->timeLine()->timeKey(core::TimeKeyType_Image, closest))
                      ->data()
                      .gridMesh()
                      .cellSize() : 0;
        } else {
            ffd["cellSize"] = node->timeLine()->current().areaImageKey()->data().gridMesh().cellSize();
        }
        return ffd;
    }
    case core::TimeKeyType_Image: {
        auto data = dynamic_cast<const core::ImageKey*>(timeKey)->data();
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
        img["Image"] = dynamic_cast<const core::ImageKey*>(timeKey)->serializeToJson();
        // qDebug() << QJsonDocument(img).toJson(QJsonDocument::Indented).toStdString().c_str();
        return img;
    }
    case core::TimeKeyType_HSV: {
        auto data = dynamic_cast<const core::HSVKey*>(timeKey)->data();
        // Data types: HSV (List<int>) //
        QJsonObject hsv;
        addStandardToObj(&hsv, data, keyType, timeKey);
        hsv["Hue"] = data.hsv()[0];
        hsv["Saturation"] = data.hsv()[1];
        hsv["Value"] = data.hsv()[2];
        hsv["Absolute"] = data.hsv()[3];
        return hsv;
    }
    case core::TimeKeyType_Blur: {
        auto data = dynamic_cast<const core::BlurKey*>(timeKey)->data();
        // Data types: Amount, BlurX, BlurY, Angle (floats) //
        QJsonObject blur;
        addStandardToObj(&blur, data, keyType, timeKey);
        blur["Amount"] = data.amount();
        blur["Directional"] = data.isDirectional();
        blur["BlurX"] = data.blurX();
        blur["BlurY"] = data.blurY();
        blur["Angle"] = data.angleDeg();
        return blur;
    }
    default: {
        return {};
    }
    }
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
    case core::TimeKeyType_Blur:{
        auto newData =  dynamic_cast<const core::BlurKey*>(key)->data();
        if(assignEasing) { newData.easing().type = static_cast<util::Easing::Type>(easingType); }
        else { newData.easing().range = static_cast<util::Easing::Range>(easingType); }
        ctrl::TimeLineUtil::assignBlurKeyData(*mProject, *target->node, frame, newData);
    }
        break;
    default:
        qDebug("You somehow tried to modify an unsupported key, you absolute fool!");
    }
}

void TimeLineEditorWidget::onSelectEasingTriggered(const int easingType) {
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
    int frame = 0;
    bool hasFrame = false;
    bool isDifferent = false;
    for(auto target: targets){
        if(!hasFrame){ frame = target.pos.key()->frame(); hasFrame = true; }
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

static QString keyToString(const core::TimeKeyType key) {
    switch (key) {
    case core::TimeKeyType_Bone:
        return "Bone";
    case core::TimeKeyType_Mesh:
        return "Mesh";
    case core::TimeKeyType_Image:
        return "Image";
    case core::TimeKeyType_HSV:
        return "HSV";
    case core::TimeKeyType_Blur:
        return "Blur";
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
    // Spacing selects every Nth frame within the project's frame range: the
    // span, not the (possibly negative) end frame, bounds it.
    frameSpacing->setMaximum(qMax(1, mProject->attribute().frameRange().diff()));
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
        // We move the keys and check if the move was successful. Destinations
        // are unconstrained: the frame range does not gate the keying system,
        // so keys may be spaced past the last frame.
        QList<QPair<core::TimeLineEvent::Target, int>> conflicts;
        for(auto target: targets) {
            int tSize = static_cast<int>(target->size()) - 1;
            int initialFrame = target->back().pos.key()->frame();
            for(auto key : *target) {
                int frameAccumulation = spacing * tSize;
                if (!ignoredTargets.contains(key.pos.key())) {
                    int frame = key.pos.key()->frame();
                    int dest = initialFrame + frameAccumulation;
                    const core::TimeKeyType keyType = key.pos.type();
                    if(!key.pos.line()->move(keyType, frame, dest)) {
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
        if (!conflicts.empty()) {
            QMessageBox msg;
            QStringList errorLog;
            for (const auto& [key, frame] : conflicts) {
                errorLog.append(
                    tr("Key conflict detected for key type ") + keyToString(key.pos.type()) +
                    tr(" in node ") + key.node->name() + tr(" at frame ") + QString::number(frame));
            }
            msg.setWindowTitle(tr("Move error"));
            msg.setText(tr("Unable to move ") + QString::number(conflicts.size()) + tr(" key(s), due to the following reasons: "));
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

QColor TimeLineEditorWidget::rulerLineColor() const { return mTimelineTheme.rulerLineColor(); }

void TimeLineEditorWidget::setRulerLineColor(const QColor& rulerLineColor) {
    mTimelineTheme.setRulerLineColor(rulerLineColor);
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
