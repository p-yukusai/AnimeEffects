#include "core/ResourceUpdatingWorkspace.h"
#include "core/BlurKey.h"
#include "core/TimeKeyExpans.h"
#include "ffd/ffd_Target.h"
#include "qapplication.h"
#include "qclipboard.h"
#include "qjsonarray.h"
#include "qjsondocument.h"
#include "qjsonobject.h"
#include "cmnd/ScopedMacro.h"
#include "cmnd/BasicCommands.h"
#include "ctrl/TimeLineEditor.h"
#include "ctrl/CmndName.h"
#include "ctrl/time/time_Renderer.h"
#include "core/FFDKeyUpdater.h"
#include "gui/ObjectTreeWidget.h"

using namespace core;

namespace {
    constexpr int kTimeLineMargin = 14;
    constexpr int kDefaultMaxFrame = 600;

} // namespace

namespace ctrl {

//-------------------------------------------------------------------------------------------------
TimeLineEditor::TimeLineEditor():
    mProject(),
    mRows(),
    mSelectingRow(),
    mTimeMax(),
    mState(State_Standby),
    mFps(60),
    mTimeCurrent(kTimeLineMargin),
    mTimeScale(),
    mMarquee(mRows, mTimeScale, kTimeLineMargin),
    mSelection(),
    mMoveRef(),
    mMoveFrame(),
    mPressFrame(),
    mOnUpdatingKey(false),
    mMarqueeMode(Marquee_Replace),
    mPreGestureSelection(),
    mPressedTarget(),
    mPressedTargetValid(false) {
    mRows.reserve(64);

    mTimeScale.setFps(mFps);

    // reset max frame
    setMaxFrame(kDefaultMaxFrame);
}

void TimeLineEditor::setMaxFrame(int aValue) {
    mTimeMax = aValue;
    mTimeScale.setMaxFrame(mTimeMax);
    mTimeCurrent.setMaxFrame(mTimeMax);
    mTimeCurrent.setFrame(mTimeScale, Frame(0));
}

void TimeLineEditor::setProject(Project* aProject) {
    clearRows();
    mProject.reset();

    if (aProject) {
        mProject = aProject->pointee();
        setMaxFrame(mProject->attribute().maxFrame());
        mFps = mProject->attribute().fps();
        mTimeScale.setFps(mFps);
    } else {
        setMaxFrame(kDefaultMaxFrame);
    }
}

void TimeLineEditor::clearRows() {
    mRows.clear();
    clearSelection();
}

// Gesture state only — the selection survives (it is the visual anchor of
// what is selected; the marquee box is transient).
void TimeLineEditor::clearState() {
    mState = State_Standby;
    mMoveRef = nullptr;
    mMoveFrame = 0;
    mPressFrame = 0;
    mMarquee.clear();
    mMarqueeMode = Marquee_Replace;
    mPreGestureSelection = core::TimeLineEvent();
    mPressedTarget = core::TimeLineEvent::Target();
    mPressedTargetValid = false;
}

void TimeLineEditor::clearSelection() {
    mSelection.clear();
    clearState();
}

void TimeLineEditor::pushRow(ObjectNode* aNode, util::Range aWorldTB, bool aClosedFolder) {
    constexpr int left = kTimeLineMargin;
    const int right = left + mTimeScale.maxPixelWidth();
    const QRect rect(QPoint(left, aWorldTB.min()), QPoint(right, aWorldTB.max()));
    mRows.push_back(TimeLineRow(aNode, rect, aClosedFolder, aNode == mSelectingRow));
}

void TimeLineEditor::updateRowSelection(const ObjectNode* aRepresent) {
    mSelectingRow = aRepresent;
    for (auto& row : mRows) {
        row.selecting = (row.node && row.node == aRepresent);
    }
}

void TimeLineEditor::updateKey() {
    // any timeline modification can invalidate the stored targets (keys
    // moved, removed or recreated), so the selection follows the change
    if (!mOnUpdatingKey) {
        clearSelection();
    }
}

void TimeLineEditor::updateProjectAttribute() {
    clearState(); // fps/maxFrame changes keep the targets valid
    if (mProject) {
        const int newMaxFrame = mProject->attribute().maxFrame();
        if (mTimeMax != newMaxFrame) {
            setMaxFrame(newMaxFrame);

            const int newRowRight = kTimeLineMargin + mTimeScale.maxPixelWidth();
            for (auto& row : mRows) {
                row.rect.setRight(newRowRight);
            }
        }

        const int newFps = mProject->attribute().fps();
        if (mFps != newFps) {
            mFps = newFps;
            mTimeScale.setFps(mFps);
        }
    }
}

TimeLineEditor::UpdateFlags TimeLineEditor::updateCursor(const AbstractCursor& aCursor, Qt::KeyboardModifiers aModifiers) {
    UpdateFlags flags = 0;

    if (!mProject) {
        return flags;
    }

    const QPoint worldPoint = aCursor.worldPoint();
    const bool ctrl = aModifiers.testFlag(Qt::ControlModifier);
    const bool shift = aModifiers.testFlag(Qt::ShiftModifier);

    if (aCursor.emitsLeftPressedEvent()) {
        const QVector2D handlePos(mTimeCurrent.handlePos());

        // playhead: handle or header click moves the current frame
        if ((aCursor.screenPos() - handlePos).length() < mTimeCurrent.handleRange()) {
            mState = State_MoveCurrent;
            flags |= UpdateFlag_ModView;
        } else if (aCursor.screenPos().y() < kHeaderHeight) {
            mTimeCurrent.setHandlePos(mTimeScale, aCursor.worldPos().toPoint());
            mState = State_MoveCurrent;
            flags |= UpdateFlag_ModView;
            flags |= UpdateFlag_ModFrame;
        } else {
            const time::Hit hit = mMarquee.hitTest(worldPoint, radiiFactor);

            if (hit.isValid()) {
                // clicked a key: standard creative-tool selection conventions.
                // plain click selects only this key (collapse); shift-click
                // and ctrl-click both toggle membership
                // a fresh press never inherits a previous gesture's pending
                // collapse target (an aborted move can leave one behind)
                mPressedTargetValid = false;
                const TimeLineEvent::Target target(*hit.node, hit.pos, hit.pos.index());
                if (shift || ctrl) {
                    mSelection.toggle(target);
                    flags |= UpdateFlag_ModView;
                } else {
                    // plain click: select this key. If it is already in the
                    // selection, keep the group for a pending drag-move and
                    // collapse to just this key on a click (no drag).
                    mPressedTarget = target;
                    mPressedTargetValid = true;
                    if (!mSelection.contains(target)) {
                        TimeLineEvent single;
                        single.pushTarget(*hit.node, hit.pos);
                        mSelection.set(single);
                        flags |= UpdateFlag_ModView;
                    }
                }
                mPressFrame = mTimeScale.frame(worldPoint.x() - kTimeLineMargin);
                mState = State_MoveKeys; // move is armed lazily on first drag
            } else {
                // empty space: marquee. plain replaces the selection, shift
                // adds to it, ctrl subtracts from it
                mMarquee.begin(worldPoint);
                mMarqueeMode = ctrl ? Marquee_Subtract : (shift ? Marquee_Add : Marquee_Replace);
                if (mMarqueeMode != Marquee_Replace) {
                    mPreGestureSelection = core::TimeLineEvent();
                    mSelection.assign(mPreGestureSelection);
                } else {
                    mSelection.clear();
                    flags |= UpdateFlag_ModView;
                }
                mState = State_EncloseKeys;
            }
        }
    } else if (aCursor.emitsLeftDraggedEvent()) {
        if (mState == State_MoveCurrent) {
            mTimeCurrent.setHandlePos(mTimeScale, aCursor.worldPos().toPoint());
            flags |= UpdateFlag_ModView;
            flags |= UpdateFlag_ModFrame;
        } else if (mState == State_MoveKeys) {
            if (!mMoveRef) {
                if (!beginMoveKeys()) {
                    mState = State_Standby;
                }
            }
            if (mState == State_MoveKeys && !modifyMoveKeys(aCursor.worldPoint())) {
                mState = State_Standby;
                mMoveRef = nullptr;
                flags |= UpdateFlag_ModView;
            }
        } else if (mState == State_EncloseKeys) {
            mMarquee.update(aCursor.worldPoint());
            applyMarquee();
            flags |= UpdateFlag_ModView;
        }
    } else if (aCursor.emitsLeftReleasedEvent()) {
        if (mState == State_MoveKeys) {
            // keys moved: re-resolve the selection to their new frames so
            // later copy/delete/move target the right keys
            if (mMoveRef) {
                mSelection.reindex();
                mMoveRef = nullptr;
            } else if (mPressedTargetValid) {
                // a plain click (no drag) always leaves exactly the clicked
                // key selected — idempotent when it was already alone
                TimeLineEvent single;
                single.pushTarget(*mPressedTarget.node, mPressedTarget.pos);
                mSelection.set(single);
                flags |= UpdateFlag_ModView;
            }
            clearState();
        } else if (mState == State_EncloseKeys) {
            const QRect box = mMarquee.rect();
            const bool degenerate = box.width() < 2 && box.height() < 2;
            // a bare modifier click on empty space: ctrl deselects, shift
            // keeps the selection; plain already cleared at press
            if (degenerate && mMarqueeMode == Marquee_Subtract) {
                mSelection.clear();
                flags |= UpdateFlag_ModView;
            }
            clearState(); // the box is transient; the selection highlights remain
        }
    } else {
        // hover: nothing to do — selection lives in the Selection object
    }

    return flags;
}

// Lazy move arming: called on the first drag of a State_MoveKeys gesture. The
// command is only pushed once the user actually drags, so a bare click leaves
// no phantom "Move keys" history entry. The delta is measured from the press
// frame, which was recorded when the gesture began.
bool TimeLineEditor::beginMoveKeys() {
    bool success = false;
    mOnUpdatingKey = true;
    {
        auto notifier = new TimeLineUtil::Notifier(*mProject);
        notifier->event().setType(TimeLineEvent::Type_MoveKey);
        mSelection.assign(notifier->event());

        if (!notifier->event().targets().isEmpty()) {
            cmnd::ScopedMacro macro(mProject->commandStack(), CmndName::tr("Move keys"));

            macro.grabListener(notifier);
            mMoveRef = new TimeLineUtil::MoveFrameOfKey(notifier->event());
            mProject->commandStack().push(mMoveRef);
            mMoveFrame = mPressFrame;
            success = true;
        } else {
            delete notifier;
            mMoveRef = nullptr;
        }
    }
    mOnUpdatingKey = false;
    return success;
}

bool TimeLineEditor::modifyMoveKeys(const QPoint& aWorldPos) {
    if (mProject->commandStack().isModifiable(mMoveRef)) {
        const int newFrame = mTimeScale.frame(aWorldPos.x() - kTimeLineMargin);
        const int addFrame = newFrame - mMoveFrame;
        TimeLineEvent modEvent;

        mOnUpdatingKey = true;
        int clampedAdd = addFrame;
        if (mMoveRef->modifyMove(modEvent, addFrame, util::Range(0, mTimeMax), &clampedAdd)) {
            mMoveFrame = newFrame;
            mProject->onTimeLineModified(modEvent, false);
        }
        mOnUpdatingKey = false;
        return true;
    }
    return false;
}

// The marquee box is committed into the selection on every drag update, so
// the highlights stay live while dragging and the box itself is transient.
void TimeLineEditor::applyMarquee() {
    if (mState != State_EncloseKeys)
        return;

    TimeLineEvent box;
    mMarquee.gather(box);

    if (mMarqueeMode == Marquee_Replace) {
        mSelection.set(box);
    } else if (mMarqueeMode == Marquee_Add) {
        TimeLineEvent merged = mPreGestureSelection;
        for (const TimeLineEvent::Target& t : box.targets()) {
            merged.pushTarget(*t.node, t.pos);
        }
        mSelection.set(merged);
    } else {
        mSelection.subtract(box);
    }
}

bool TimeLineEditor::selectKeysAt(TimeLineEvent& aEvent, const QPoint& aPos) {
    const time::Hit hit = mMarquee.hitTest(aPos, radiiFactor);
    if (!hit.isValid())
        return false;

    const TimeLineEvent::Target target(*hit.node, hit.pos, hit.pos.index());
    if (mSelection.contains(target)) {
        // right-click on a selected key: operate on the whole selection
        mSelection.assign(aEvent);
    } else {
        // right-click on an unselected key: select it and operate on it
        TimeLineEvent single;
        single.pushTarget(*hit.node, hit.pos);
        mSelection.set(single);
        mSelection.assign(aEvent);
    }
    return true;
}

bool TimeLineEditor::retrieveSelectionTargets(TimeLineEvent& aEvent) const {
    if (mSelection.empty())
        return false;
    mSelection.assign(aEvent);
    return true;
}

static bool isKeyJsonValid(QJsonObject json) {
    if (json.contains("TargetsSize") && json["TargetsSize"] != 0 && json.contains("Keys") &&
        json.value("Keys").toArray().size() != 0) {
        return true;
    }
    return false;
}

static QVector2D objToVec(QJsonObject obj, const QString& varName) {
    return {static_cast<float>(obj[varName + "X"].toDouble()), static_cast<float>(obj[varName + "Y"].toDouble())};
}

static util::Easing::Param objToEasing(QJsonObject obj) {
    util::Easing::Param easing;
    easing.range = util::Easing::rangeToEnum(obj["aRange"].toString());
    easing.type = util::Easing::easingToEnum(obj["eType"].toString());
    easing.weight = static_cast<float>(obj["eWeight"].toDouble());
    // qDebug() << easing.type << easing.range << easing.weight;
    return easing;
}

static TimeKey* getKeyFromObj(QJsonObject obj, util::LifeLink::Pointee<Project> project, bool isFolder, ObjectNode* aOwner) {
    TimeKeyType type = TimeLine::getTimeKeyType(obj["Type"].toString());
    // We're losing precision for float casts from json strings because
    // the cast rounds at the third decimal for some godforsaken reason.
    switch (type) {
        case TimeKeyType_Move: {
            auto* moveKey = new MoveKey;
            QVector2D pos = objToVec(obj, "Pos");
            QVector2D centre = objToVec(obj, "Centre");
            MoveKey::SplineType spline =
                obj["Spline"].toString() == "Catmull" ? MoveKey::SplineType_CatmullRom : MoveKey::SplineType_Linear;
            moveKey->data().setPos(pos);
            moveKey->data().setCentroid(centre);
            moveKey->data().setSpline(spline);
            moveKey->data().easing() = objToEasing(obj);
            moveKey->setFrame(obj["Frame"].toInt());
            return moveKey;
        }
        case TimeKeyType_Rotate: {
            auto* rotateKey = new RotateKey;
            rotateKey->setRotate(obj["Rotate"].toDouble());
            rotateKey->data().easing() = objToEasing(obj);
            rotateKey->setFrame(obj["Frame"].toInt());
            return rotateKey;
        }
        case TimeKeyType_Scale: {
            auto* scaleKey = new ScaleKey;
            scaleKey->setScale(objToVec(obj, "Scale"));
            scaleKey->data().easing() = objToEasing(obj);
            scaleKey->setFrame(obj["Frame"].toInt());
            return scaleKey;
        }
        case TimeKeyType_Depth: {
            auto* depthKey = new DepthKey;
            depthKey->setDepth(static_cast<float>(obj["Depth"].toDouble()));
            depthKey->data().easing() = objToEasing(obj);
            depthKey->setFrame(obj["Frame"].toInt());
            return depthKey;
        }
        case TimeKeyType_Opa: {
            auto* opaKey = new OpaKey;
            opaKey->setOpacity(static_cast<float>(obj["Opacity"].toDouble()));
            opaKey->data().easing() = objToEasing(obj);
            opaKey->setFrame(obj["Frame"].toInt());
            return opaKey;
        }
        case TimeKeyType_Bone: {
            auto* boneKey = new BoneKey;
            QJsonArray boneArray = obj["Bones"].toArray();
            // top bone count
            if (static_cast<int>(boneArray.size()) < 0) { return nullptr; }
            // deserialize all bones
            for (auto bone : boneArray) {
                auto* topBone = new Bone2();
                boneKey->data().topBones().push_back(topBone);
                topBone->deserializeFromJson(bone.toObject(), project.address, nullptr);
                topBone->updateWorldTransform();
            }
            boneKey->resetCaches(*project.address, *aOwner);
            boneKey->setFrame(obj["Frame"].toInt());

            return boneKey;
        }
        case TimeKeyType_Pose: {
            auto* poseKey = new PoseKey;
            QJsonArray boneArray = obj["Bone"].toArray();
            QList<Bone2*> bones;
            for (QJsonValue bone : boneArray) {
                QJsonObject boneObj = bone.toObject();
                auto* newBone = new Bone2;
                newBone->deserializeFromJson(boneObj, project.address, nullptr);
                bones.append(newBone);
            }
            poseKey->data().topBones() = bones;
            poseKey->setFrame(obj["Frame"].toInt());
            return poseKey;
        }
        case TimeKeyType_Mesh: {
            // Key type not acknowledged by folders
            if (isFolder) {
                return nullptr;
            }
            auto* meshKey = new MeshKey;
            QJsonObject mesh = obj["Mesh"].toObject();
            meshKey->data().deserializeFromJson(mesh);
            meshKey->setFrame(obj["Frame"].toInt());
            return meshKey;
        }
        case TimeKeyType_FFD: {
            FFDKey* ffdKey = new FFDKey;
            ffdKey->data().easing() = objToEasing(obj);
            ffdKey->deserializeFromJson(obj);
            ffdKey->setFrame(obj["Frame"].toInt());
            return ffdKey;
        }
        case TimeKeyType_Image: {
            // Key type not acknowledged by folders
            if (isFolder) {
                return nullptr;
            }
            auto* imageKey = new ImageKey;
            imageKey->data().easing() = objToEasing(obj);
            if (imageKey->deserializeFromJson(obj, project)) {
                return imageKey;
            }
            return nullptr;
        }
        case TimeKeyType_HSV: {
            auto* hsvKey = new HSVKey;
            hsvKey->data().easing() = objToEasing(obj);
            QList hsv{obj["Hue"].toInt(), obj["Saturation"].toInt(), obj["Value"].toInt(), obj["Absolute"].toInt()};
            hsvKey->setHSV(hsv);
            hsvKey->setFrame(obj["Frame"].toInt());
            return hsvKey;
        }
        case TimeKeyType_Blur: {
            auto* blurKey = new BlurKey;
            blurKey->data().easing() = objToEasing(obj);
            blurKey->setAmount(obj["Amount"].toDouble());
            // directional data only exists in newer clipboard payloads; fall back to the
            // isotropic amount otherwise
            blurKey->setDirectional(obj["Directional"].toBool());
            if (blurKey->isDirectional()){
                blurKey->setBlurX(obj["BlurX"].toDouble());
                blurKey->setBlurY(obj["BlurY"].toDouble());
                blurKey->setAngleDeg(obj["Angle"].toDouble());
            }
            blurKey->setFrame(obj["Frame"].toInt());
            return blurKey;
        }
        // If you end up here you've done goofed.
        case TimeKeyType_TERM: {
            return nullptr;
        }
    }
    return nullptr;
}

QList<TimeKey*> TimeLineEditor::getTypesFromCb(util::LifeLink::Pointee<Project> project, ObjectNode* node) {
    QClipboard* qcb = QGuiApplication::clipboard(); // qDebug() << qcb->text();
    QJsonObject keyJson = QJsonDocument::fromJson(QByteArray::fromStdString(qcb->text().toStdString())).object();
    if (!isKeyJsonValid(keyJson)) {
        return {};
    }
    QJsonArray keys = keyJson["Keys"].toArray(); // qDebug() << keys;
    QList<TimeKey*> keyList;
    for (QJsonValue key : keys) {
        auto keyObj = key.toObject();
        // To get all keys isFolder is set to false.
        TimeKey* pastedKey = getKeyFromObj(keyObj, project, false, node);
        if (pastedKey != nullptr) {
            keyList.append(pastedKey);
        }
    }
    return keyList;
}

QString TimeLineEditor::pasteCbKeys(gui::obj::Item* objItem, util::LifeLink::Pointee<Project> project, bool isFolder) {
    XC_ASSERT(!objItem->isTopNode());
    QClipboard* qcb = QGuiApplication::clipboard(); // qDebug() << qcb->text();
    QJsonObject keyJson = QJsonDocument::fromJson(QByteArray::fromStdString(qcb->text().toStdString())).object();
    if (!isKeyJsonValid(keyJson))
        return "Invalid Json";
    QJsonArray tlKeys = keyJson["Keys"].toArray(); // qDebug() << keys;
    QList<TimeKey*> keyList;
    int nullLog = 0;
    QStringList keyErrored;
    for (QJsonValue key : tlKeys) {
        auto keyObj = key.toObject();
        TimeKey* pastedKey = getKeyFromObj(keyObj, project, isFolder, &objItem->node());
        if (pastedKey != nullptr) { keyList.append(pastedKey); }
        else {
            auto keyType = TimeLine::getTimeKeyType(keyObj["Type"].toString());
            keyErrored.append(TimeLine::getTimeKeyName(keyType));
            nullLog++;
        }
    }
    QString returnString;
    if (keyList.empty()) {
        returnString = "No keys to copy.";
    }
    // qDebug() << project.address->fileName();
    int frameLessThanZero = 0;
    int timelineHasKey = 0;

    if (!keyList.empty()) {
        int pastedKeys = 0;
        for (int x = 0; x < keyList.size(); x++) {
            TimeKey* keyframe = keyList[x];
            int newFrame = keyframe->frame();
            TimeKeyType type = keyframe->type();
            TimeLine* timeLine = objItem->node().timeLine();
            // invalid frame
            if (newFrame < 0) { frameLessThanZero++; }
            else {
                if (timeLine->hasTimeKey(type, newFrame)) { timelineHasKey++; }
                else {
                    if (type == TimeKeyType_FFD) {
                        // find area key and mesh
                        auto [aKey, aMesh] = gui::ObjectTreeWidget::getAreaMeshImpl(objItem->node(), keyframe->frame());
                        TimeKey* areaKey = aKey;
                        LayerMesh* areaMesh = aMesh;
                        timeLine->current().setFFDMesh(areaMesh);
                        timeLine->current().setFFDMeshParent(areaKey);
                        // connect to parent mesh
                        aKey->children().pushBack(keyframe);
                        /*
                        auto imgKey = gui::ObjectTreeWidget::getImageKey(objItem->node(), keyframe->frame());
                        if (aKey->type() != TimeKeyType_Image) {
                            imgKey->children().pushBack(keyframe);
                        }*/
                    }
                    // a key already exists.
                    // @todo something more fancy
                    cmnd::Stack& stack = project.address->commandStack();
                    // create notifier
                    auto notifier = new TimeLineUtil::Notifier(*project.address);
                    TimeLineEvent tEvnt = TimeLineEvent();
                    tEvnt.pushTarget(objItem->node(), TimeKeyPos(*timeLine, keyframe->type(), x));
                    tEvnt.setType(TimeLineEvent::Type_PushKey);
                    notifier->event() = tEvnt;
                    notifier->event().setType(TimeLineEvent::Type_PushKey);

                    // push paste keys command
                    cmnd::ScopedMacro macro(stack, CmndName::tr("Paste clipboard key"));
                    macro.grabListener(notifier);

                    QHash<const TimeKey*, TimeKey*> parentHash;
                    struct ChildInfo {
                        TimeKey* key;
                        TimeKey* parent;
                    };
                    QList<ChildInfo> childList;
                    auto line = timeLine;
                    XC_PTR_ASSERT(line);

                    auto copiedKey = keyframe;
                    XC_PTR_ASSERT(copiedKey);
                    auto parentKey = copiedKey->parent();

                    TimeKey* newKey = copiedKey->createClone();

                    newKey->setFrame(newFrame);

                    stack.push(new cmnd::GrabNewObject<TimeKey>(newKey));
                    stack.push(line->createPusher(type, newFrame, newKey));

                    if (newKey->canHoldChild()) {
                        parentHash[copiedKey] = newKey;
                    }
                    if (parentKey) {
                        ChildInfo info = {newKey, parentKey};
                        childList.push_back(info);
                    }

                    // connect to parents
                    for (auto child : childList) {
                        auto parent = child.parent;
                        // if the parent was also copied, connect to a new parent key.
                        auto it = parentHash.find(parent);
                        if (it != parentHash.end())
                            parent = it.value();
                        stack.push(new cmnd::PushBackTree<TimeKey>(&parent->children(), child.key));
                    }
                    pastedKeys++;
                }
            }
        }
        returnString = "Successfully pasted [" + QString::number(pastedKeys) + "] key(s).";
    }
    if (frameLessThanZero != 0 || timelineHasKey != 0 || nullLog != 0) {
        returnString.append(
            "\nNumber of errors is [" + QString::number(frameLessThanZero + timelineHasKey + nullLog) + "]"
        );
        // Error logging
        if (frameLessThanZero != 0) {
            returnString.append("\nError: Frame is less than zero [" + QString::number(frameLessThanZero) + "]");
        }
        if (timelineHasKey != 0) {
            returnString.append("\nError: Timeline already has a key [" + QString::number(timelineHasKey) + "]");
        }

        if (nullLog != 0) {
            returnString.append("\nError: Null key(s) detected [" + QString::number(nullLog) + "]");
            returnString.append("\nNull Log:");
            bool folderError = false;
            int folderErrorCount = 0;
            if (keyErrored.contains("Image") && !isFolder) {
                returnString.append("\nImage identifier could not be found in the resource holder.");
            } else if (keyErrored.contains("Image")) {
                folderError = true;
                folderErrorCount++;
            }
            if (keyErrored.contains("FFD") && !isFolder) {
                returnString.append("\nFFD key pasting is unsupported");
            } else if (keyErrored.contains("FFD")) {
                folderError = true;
                folderErrorCount++;
            }
            if (keyErrored.contains("Mesh") && isFolder) {
                folderError = true;
                folderErrorCount++;
            }
            QStringList keys{"Move", "Rotate", "Scale", "Depth", "Opa", "Bone", "Pose", "HSV", "Blur"};
            bool containsOtherKeys = false;
            int containsCount = 0;
            for (const QString& key : keys) {
                if (keyErrored.contains(key)) {
                    containsOtherKeys = true;
                    containsCount++;
                }
            }
            if (folderError) {
                returnString.append("\nKey cannot be used on a folder");
                returnString.append("\nNumber of types with a folder error: " + QString::number(folderErrorCount));
            }
            if (containsOtherKeys) {
                returnString.append("\nKey parameters are incorrect.");
                returnString.append("\nNumber of types with a param error: " + QString::number(containsCount));
            }
            QString keysErrored = "\nKey types errored: ";
            for (const QString& key : keyErrored) {
                keysErrored.append(key + ";");
            }
            returnString.append(keysErrored);
        }
    }
    return returnString;
}

bool TimeLineEditor::pasteCopiedKeys(TimeLineEvent& aEvent, const QPoint& aWorldPos) {
    XC_ASSERT(!aEvent.targets().isEmpty());

    // a minimum frame for key pasting
    auto pasteFrame = mTimeScale.frame(aWorldPos.x() - kTimeLineMargin);

    // a minimum frame in copied keys
    int copiedFrame = mTimeMax;
    for (auto target : aEvent.targets()) {
        copiedFrame = std::min(copiedFrame, target.pos.index());
    }

    const int frameOffset = pasteFrame - copiedFrame;

    // check validity
    for (auto target : aEvent.targets()) {
        auto newFrame = target.pos.index() + frameOffset;

        // invalid frame
        if (newFrame < 0 || mTimeMax < newFrame) {
            return false;
        }

        // a key already exists.
        auto type = target.pos.type();
        if (target.pos.line()->hasTimeKey(type, newFrame)) {
            return false;
        }
    }

    mOnUpdatingKey = true;
    {
        cmnd::Stack& stack = mProject->commandStack();

        // create notifier
        auto notifier = new TimeLineUtil::Notifier(*mProject);
        notifier->event() = aEvent;
        notifier->event().setType(TimeLineEvent::Type_CopyKey);

        // push paste keys command
        cmnd::ScopedMacro macro(stack, CmndName::tr("Paste keys"));
        macro.grabListener(notifier);

        QHash<const TimeKey*, TimeKey*> parentMap;
        struct ChildInfo {
            TimeKey* key;
            TimeKey* parent;
        };
        QList<ChildInfo> childList;

        for (auto target : aEvent.targets()) {
            auto type = target.pos.type();
            auto line = target.pos.line();
            XC_PTR_ASSERT(line);

            auto copiedKey = target.pos.key();
            XC_PTR_ASSERT(copiedKey);
            auto parentKey = copiedKey->parent();

            TimeKey* newKey = copiedKey->createClone();

            auto newFrame = copiedKey->frame() + frameOffset;
            newKey->setFrame(newFrame);

            stack.push(new cmnd::GrabNewObject<TimeKey>(newKey));
            stack.push(line->createPusher(type, newFrame, newKey));

            if (newKey->canHoldChild()) {
                parentMap[copiedKey] = newKey;
            }
            if (parentKey) {
                ChildInfo info = {newKey, parentKey};
                childList.push_back(info);
            }
        }
        // connect to parents
        for (auto child : childList) {
            auto parent = child.parent;
            // if the parent was also copied, connect to a new parent key.
            auto it = parentMap.find(parent);
            if (it != parentMap.end())
                parent = it.value();
            stack.push(new cmnd::PushBackTree<TimeKey>(&parent->children(), child.key));
        }
    }
    mOnUpdatingKey = false;

    clearSelection();
    return true;
}

void TimeLineEditor::deleteCheckedKeys(TimeLineEvent& aEvent) {
    XC_ASSERT(!aEvent.targets().isEmpty());

    mOnUpdatingKey = true;
    {
        cmnd::Stack& stack = mProject->commandStack();

        // create notifier
        auto notifier = new TimeLineUtil::Notifier(*mProject);
        notifier->event() = aEvent;
        notifier->event().setType(TimeLineEvent::Type_RemoveKey);

        // push delete keys command
        cmnd::ScopedMacro macro(stack, CmndName::tr("Delete keys"));
        macro.grabListener(notifier);

        for (auto target : aEvent.targets()) {
            TimeLine* line = target.pos.line();
            XC_PTR_ASSERT(line);
            stack.push(line->createRemover(target.pos.type(), target.pos.index(), true));
        }
    }
    mOnUpdatingKey = false;

    clearSelection();
}

void TimeLineEditor::updateWheel(int aDelta, bool aInvertScaling) {
    mTimeScale.update(aInvertScaling ? -aDelta : aDelta);
    mTimeCurrent.update(mTimeScale);

    const int lineWidth = mTimeScale.maxPixelWidth();

    for (TimeLineRow& row : mRows) {
        row.rect.setWidth(lineWidth);
    }
}

void TimeLineEditor::setFrame(Frame aFrame) { mTimeCurrent.setFrame(mTimeScale, aFrame); }

Frame TimeLineEditor::currentFrame() const { return mTimeCurrent.frame(); }

QSize TimeLineEditor::modelSpaceSize() const {
    int height = kHeaderHeight;

    if (!mRows.empty()) {
        height += mRows.back().rect.bottom() - mRows.front().rect.top();
    }

    const int width = mTimeScale.maxPixelWidth() + 2 * kTimeLineMargin;

    return QSize(width, height);
}

QPoint TimeLineEditor::currentTimeCursorPos() const { return mTimeCurrent.handlePos(); }

int TimeLineEditor::frameAtPixel(int aPixelX) const {
    return mTimeScale.frame(aPixelX - kTimeLineMargin);
}

int TimeLineEditor::pixelAtFrame(int aFrame) const {
    return mTimeScale.pixelWidth(aFrame) + kTimeLineMargin;
}

void TimeLineEditor::render(
    QPainter& aPainter, const CameraInfo& aCamera, theme::TimeLine& aTheme, const QRect& aCullRect
) const {
    if (aCamera.screenWidth() < 2 * kTimeLineMargin)
        return;

    const QRect camRect(-aCamera.leftTopPos().toPoint(), aCamera.screenSize());
    const QRect cullRect(aCullRect.marginsAdded(QMargins(2, 2, 2, 2))); // use culling

    constexpr int margin = kTimeLineMargin;
    const int bgn = mTimeScale.frame(cullRect.left() - margin - 5);
    const int end = mTimeScale.frame(cullRect.right() - margin + 5);

    time::Renderer renderer(aPainter, aCamera, aTheme, timelineFormat);
    renderer.setMargin(margin);
    renderer.setRange(util::Range(bgn, end));
    renderer.setTimeScale(mTimeScale);

    renderer.renderLines(mRows, camRect, cullRect);
    renderer.renderHeader(kHeaderHeight, mFps);
    // renderer.renderHandle(mTimeCurrent.handlePos(), mTimeCurrent.handleRange());

    if (mState == State_EncloseKeys && mMarquee.isActive()) {
        renderer.renderSelectionRange(mMarquee.rect());
    }
}

} // namespace ctrl
