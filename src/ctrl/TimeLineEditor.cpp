#include <QPainter>
#include "core/ResourceUpdatingWorkspace.h"
#include "core/TimeKeyExpans.h"
#include "ffd/ffd_Target.h"
#include "qapplication.h"
#include "qclipboard.h"
#include "qjsonarray.h"
#include "qjsondocument.h"
#include "qjsonobject.h"
#include "util/TreeIterator.h"
#include "cmnd/ScopedMacro.h"
#include "cmnd/BasicCommands.h"
#include "ctrl/TimeLineEditor.h"
#include "ctrl/CmndName.h"
#include "ctrl/time/time_Renderer.h"
#include "core/FFDKeyUpdater.h"
#include "core/ImageKeyUpdater.h"

using namespace core;

namespace {

static const int kTimeLineFpsA = 60;
static const int kTimeLineFpsB = 30;
static const int kTimeLineFpsC = 10;
static const int kTimeLineMargin = 14;
static const int kHeaderHeight = 22;
static const int kDefaultMaxFrame = 600;

} // namespace

namespace ctrl {

//-------------------------------------------------------------------------------------------------
TimeLineEditor::TimeLineEditor():
    mProject(),
    mRows(),
    mSelectingRow(),
    mTimeMax(),
    mState(State_Standby),
    mTimeCurrent(kTimeLineMargin),
    mTimeScale(),
    mFocus(mRows, mTimeScale, kTimeLineMargin),
    mMoveRef(),
    mMoveFrame(),
    mOnUpdatingKey(false),
    mShowSelectionRange(false) {
    mRows.reserve(64);

    const std::array<int, 3> kFrameList = {kTimeLineFpsA, kTimeLineFpsB, kTimeLineFpsC};
    mTimeScale.setFrameList(kFrameList);

    // reset max frame
    setMaxFrame(kDefaultMaxFrame);
}

void TimeLineEditor::setMaxFrame(int aValue) {
    mTimeMax = aValue;
    mTimeScale.setMaxFrame(mTimeMax);
    mTimeCurrent.setMaxFrame(mTimeMax);
    mTimeCurrent.setFrame(mTimeScale, core::Frame(0));
}

void TimeLineEditor::setProject(Project* aProject) {
    clearRows();
    mProject.reset();

    if (aProject) {
        mProject = aProject->pointee();
        setMaxFrame(mProject->attribute().maxFrame());
    } else {
        setMaxFrame(kDefaultMaxFrame);
    }
}

void TimeLineEditor::clearRows() {
    mRows.clear();
    clearState();
}

void TimeLineEditor::clearState() {
    mFocus.clear();
    mState = State_Standby;
    mMoveRef = nullptr;
    mMoveFrame = 0;
    mShowSelectionRange = false;
}

void TimeLineEditor::pushRow(ObjectNode* aNode, util::Range aWorldTB, bool aClosedFolder) {
    const int left = kTimeLineMargin;
    const int right = left + mTimeScale.maxPixelWidth();
    const QRect rect(QPoint(left, aWorldTB.min()), QPoint(right, aWorldTB.max()));
    mRows.push_back(TimeLineRow(aNode, rect, aClosedFolder, aNode == mSelectingRow));
}

void TimeLineEditor::updateRowSelection(const core::ObjectNode* aRepresent) {
    mSelectingRow = aRepresent;
    for (auto& row : mRows) {
        row.selecting = (row.node && row.node == aRepresent);
    }
}

void TimeLineEditor::updateKey() {
    if (!mOnUpdatingKey) {
        clearState();
    }
}

void TimeLineEditor::updateProjectAttribute() {
    clearState();
    if (mProject) {
        const int newMaxFrame = mProject->attribute().maxFrame();
        if (mTimeMax != newMaxFrame) {
            setMaxFrame(newMaxFrame);

            const int newRowRight = kTimeLineMargin + mTimeScale.maxPixelWidth();
            for (auto& row : mRows) {
                row.rect.setRight(newRowRight);
            }
        }
    }
}

TimeLineEditor::UpdateFlags TimeLineEditor::updateCursor(const AbstractCursor& aCursor) {
    TimeLineEditor::UpdateFlags flags = 0;

    if (!mProject) {
        return flags;
    }

    const QPoint worldPoint = aCursor.worldPoint();

    if (aCursor.emitsLeftPressedEvent()) {
        // a selection range is exists.
        if (mState == State_EncloseKeys) {
            if (mFocus.isInRange(worldPoint) && beginMoveKeys(worldPoint)) {
                mState = State_MoveKeys;
                flags |= UpdateFlag_ModView;
            } else {
                mShowSelectionRange = false;
                mState = State_Standby;
                flags |= UpdateFlag_ModView;
            }
        }

        // idle state
        if (mState == State_Standby) {
            const auto target = mFocus.reset(worldPoint);

            const QVector2D handlePos(mTimeCurrent.handlePos());

            if ((aCursor.screenPos() - handlePos).length() < mTimeCurrent.handleRange()) {
                mState = State_MoveCurrent;
                flags |= UpdateFlag_ModView;
            } else if (aCursor.screenPos().y() < kHeaderHeight) {
                mTimeCurrent.setHandlePos(mTimeScale, aCursor.worldPos().toPoint());
                mState = State_MoveCurrent;
                flags |= UpdateFlag_ModView;
                flags |= UpdateFlag_ModFrame;
            } else if (target.isValid()) {
                beginMoveKey(target);
                mState = State_MoveKeys;
                flags |= UpdateFlag_ModView;
            } else {
                mShowSelectionRange = true;
                mState = State_EncloseKeys;
                flags |= UpdateFlag_ModView;
            }
        }
    } else if (aCursor.emitsLeftDraggedEvent()) {
        if (mState == State_MoveCurrent) {
            mTimeCurrent.setHandlePos(mTimeScale, aCursor.worldPos().toPoint());
            flags |= UpdateFlag_ModView;
            flags |= UpdateFlag_ModFrame;
        } else if (mState == State_MoveKeys) {
            if (!modifyMoveKeys(aCursor.worldPoint())) {
                mState = State_Standby;
                mMoveRef = nullptr;
                mFocus.clear();
            }
            flags |= UpdateFlag_ModView;
            flags |= UpdateFlag_ModFrame;
        } else if (mState == State_EncloseKeys) {
            mFocus.update(aCursor.worldPoint());
            flags |= UpdateFlag_ModView;
        }
    } else if (aCursor.emitsLeftReleasedEvent()) {
        if (mState != State_EncloseKeys || !mFocus.hasRange()) {
            mMoveRef = nullptr;
            mState = State_Standby;
            mShowSelectionRange = false;
            flags |= UpdateFlag_ModView;
        }
    } else {
        if (mState != State_EncloseKeys) {
            mFocus.reset(aCursor.worldPoint());
        }
    }

    if (mFocus.viewIsChanged()) {
        flags |= UpdateFlag_ModView;
    }

    return flags;
}

void TimeLineEditor::beginMoveKey(const time::Focuser::SingleFocus& aTarget) {
    XC_ASSERT(aTarget.isValid());

    mOnUpdatingKey = true;
    {
        cmnd::ScopedMacro macro(mProject->commandStack(), CmndName::tr("Move key"));

        auto notifier = TimeLineUtil::createMoveNotifier(*mProject, *aTarget.node, aTarget.pos);
        macro.grabListener(notifier);

        mMoveRef = new TimeLineUtil::MoveFrameOfKey(notifier->event());
        mProject->commandStack().push(mMoveRef);
        mMoveFrame = aTarget.pos.index();
    }
    mOnUpdatingKey = false;
}

bool TimeLineEditor::beginMoveKeys(const QPoint& aWorldPos) {
    bool success = false;
    mOnUpdatingKey = true;
    {
        auto notifier = new TimeLineUtil::Notifier(*mProject);
        notifier->event().setType(TimeLineEvent::Type_MoveKey);

        if (mFocus.select(notifier->event())) {
            cmnd::ScopedMacro macro(mProject->commandStack(), CmndName::tr("Move keys"));

            macro.grabListener(notifier);
            mMoveRef = new TimeLineUtil::MoveFrameOfKey(notifier->event());
            mProject->commandStack().push(mMoveRef);
            mMoveFrame = mTimeScale.frame(aWorldPos.x() - kTimeLineMargin);
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
            mFocus.moveBoundingRect(clampedAdd);
            mProject->onTimeLineModified(modEvent, false);
        }
        mOnUpdatingKey = false;
        return true;
    }
    return false;
}

bool TimeLineEditor::checkContactWithKeyFocus(core::TimeLineEvent& aEvent, const QPoint& aPos) {
    if (mFocus.hasRange() && !mFocus.isInRange(aPos)) {
        return false;
    }
    return mFocus.select(aEvent);
}

bool TimeLineEditor::retrieveFocusTargets(core::TimeLineEvent& aEvent) {
    if (mFocus.hasRange()) {
        return mFocus.select(aEvent);
    }
    return false;
}

bool isKeyJsonValid(QJsonObject json) {
    if (json.contains("TargetsSize") && json["TargetsSize"] != 0 && json.contains("Keys") &&
        json.value("Keys").toArray().size() != 0) {
        return true;
    }
    return false;
}

QVector2D objToVec(QJsonObject obj, QString varName) {
    return QVector2D(obj[varName + "X"].toDouble(), obj[varName + "Y"].toDouble());
}

util::Easing::Param objToEasing(QJsonObject obj) {
    util::Easing::Param easing;
    easing.range = util::Easing::rangeToEnum(obj["aRange"].toString());
    easing.type = util::Easing::easingToEnum(obj["eType"].toString());
    easing.weight = obj["eWeight"].toDouble();
    // qDebug() << easing.type << easing.range << easing.weight;
    return easing;
}

TimeKey* getKeyFromObj(QJsonObject obj, util::LifeLink::Pointee<Project> project, bool isFolder) {
    TimeKeyType type = TimeLine::getTimeKeyType(obj["Type"].toString());
    // We're losing precision for float casts from json strings because
    // the cast rounds at the third decimal for some godforsaken reason.
    switch (type) {
        case TimeKeyType_Move: {
            MoveKey* moveKey = new MoveKey;
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
            RotateKey* rotateKey = new RotateKey;
            rotateKey->setRotate(obj["Rotate"].toDouble());
            rotateKey->data().easing() = objToEasing(obj);
            rotateKey->setFrame(obj["Frame"].toInt());
            return rotateKey;
        }
        case TimeKeyType_Scale: {
            ScaleKey* scaleKey = new ScaleKey;
            scaleKey->setScale(objToVec(obj, "Scale"));
            scaleKey->data().easing() = objToEasing(obj);
            scaleKey->setFrame(obj["Frame"].toInt());
            return scaleKey;
        }
        case TimeKeyType_Depth: {
            DepthKey* depthKey = new DepthKey;
            depthKey->setDepth(obj["Depth"].toDouble());
            depthKey->data().easing() = objToEasing(obj);
            depthKey->setFrame(obj["Frame"].toInt());
            return depthKey;
        }
        case TimeKeyType_Opa: {
            OpaKey* opaKey = new OpaKey;
            opaKey->setOpacity(obj["Opacity"].toDouble());
            opaKey->data().easing() = objToEasing(obj);
            opaKey->setFrame(obj["Frame"].toInt());
            return opaKey;
        }
        case TimeKeyType_Bone: {
            auto* boneKey = new BoneKey;
            QJsonArray boneArray = obj["Bones"].toArray();
            QList<core::Bone2*> bones;
            for (QJsonValue bone : boneArray) {
                QJsonObject boneObj = bone.toObject();
                auto* newBone = new core::Bone2;
                newBone->deserializeFromJson(boneObj, false);
                bones.append(newBone);
            }
            boneKey->data().topBones().append(bones);
            boneKey->setFrame(obj["Frame"].toInt());
            return boneKey;
        }
        case TimeKeyType_Pose: {
            PoseKey* poseKey = new PoseKey;
            QJsonArray boneArray = obj["Bone"].toArray();
            QList<core::Bone2*> bones;
            for (QJsonValue bone : boneArray) {
                QJsonObject boneObj = bone.toObject();
                core::Bone2* newBone = new core::Bone2;
                newBone->deserializeFromJson(boneObj, false);
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
            MeshKey* meshKey = new MeshKey;
            QJsonObject mesh = obj["Mesh"].toObject();
            meshKey->data().deserializeFromJson(mesh);
            meshKey->setFrame(obj["Frame"].toInt());
            return meshKey;
        }
        case TimeKeyType_FFD: {
            /*
            FFDKey* ffdKey = new FFDKey;
            ffdKey->data().easing() = objToEasing(obj);
            ffdKey->deserializeFromJson(obj);
            ffdKey->setFrame(obj["Frame"].toInt());
            */

            // Why not? You may be asking yourself.
            // To that I answer a web of virtual functions and gl shenanigans
            return nullptr;
        }
        case TimeKeyType_Image: {
            // Key type not acknowledged by folders
            if (isFolder) {
                return nullptr;
            }
            ImageKey* imageKey = new ImageKey;
            imageKey->data().easing() = objToEasing(obj);
            if (imageKey->deserializeFromJson(obj, project)) {
                return imageKey;
            } else {
                return nullptr;
            }
        }
        case TimeKeyType_HSV: {
            HSVKey* hsvKey = new HSVKey;
            hsvKey->data().easing() = objToEasing(obj);
            QList<int> hsv{obj["Hue"].toInt(), obj["Saturation"].toInt(), obj["Value"].toInt(), obj["Absolute"].toInt()};
            hsvKey->setHSV(hsv);
            hsvKey->setFrame(obj["Frame"].toInt());
            return hsvKey;
        }
        // If you end up here you've done goofed.
        case TimeKeyType_TERM: {
            return nullptr;
        }
    }
    return nullptr;
}

QList<TimeKey*> TimeLineEditor::getTypesFromCb(util::LifeLink::Pointee<Project> project) {
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
        TimeKey* pastedKey = getKeyFromObj(keyObj, project, false);
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
    QStringList keyTypeIgnore{"Mesh", "Image", "FFD"};
    QStringList keyErrored;
    for (QJsonValue key : tlKeys) {
        auto keyObj = key.toObject();
        TimeKey* pastedKey = getKeyFromObj(keyObj, project, isFolder);
        if (pastedKey != nullptr) {
            keyList.append(pastedKey);
        } else {
            auto keyType = TimeLine::getTimeKeyType(keyObj["Type"].toString());
            keyErrored.append(TimeLine::getTimeKeyName(keyType));
            nullLog++;
        }
    }
    QString returnString;
    if (keyErrored.contains("FFD")) {
        returnString = "FFD pasting is unsupported.";
    } else if (keyList.empty()) {
        returnString = "No keys to copy.";
    }
    // qDebug() << project.address->fileName();
    int frameLessThanZero = 0;
    int timelineHasKey = 0;
    int pastedKeys = 0;

    if (!keyList.empty()) {
        for (int x = 0; x < keyList.size(); x++) {
            TimeKey* keyframe = keyList[x];
            int newFrame = keyframe->frame();
            TimeKeyType type = keyframe->type();
            TimeLine* timeLine = objItem->node().timeLine();
            // invalid frame
            if (newFrame < 0) {
                frameLessThanZero++;
            } else {
                if (timeLine->hasTimeKey(type, newFrame)) {
                    timelineHasKey++;
                } else {
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
            QStringList keys{"Move", "Rotate", "Scale", "Depth", "Opa", "Bone", "Pose", "HSV"};
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

bool TimeLineEditor::pasteCopiedKeys(core::TimeLineEvent& aEvent, const QPoint& aWorldPos) {
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

    clearState();
    return true;
}

void TimeLineEditor::deleteCheckedKeys(core::TimeLineEvent& aEvent) {
    XC_ASSERT(!aEvent.targets().isEmpty());

    mOnUpdatingKey = true;
    {
        cmnd::Stack& stack = mProject->commandStack();

        // create notifier
        auto notifier = new TimeLineUtil::Notifier(*mProject);
        notifier->event() = aEvent;
        notifier->event().setType(core::TimeLineEvent::Type_RemoveKey);

        // push delete keys command
        cmnd::ScopedMacro macro(stack, CmndName::tr("Delete keys"));
        macro.grabListener(notifier);

        for (auto target : aEvent.targets()) {
            core::TimeLine* line = target.pos.line();
            XC_PTR_ASSERT(line);
            stack.push(line->createRemover(target.pos.type(), target.pos.index(), true));
        }
    }
    mOnUpdatingKey = false;

    clearState();
}

void TimeLineEditor::updateWheel(int aDelta, bool aInvertScaling) {
    mTimeScale.update(aInvertScaling ? -aDelta : aDelta);
    mTimeCurrent.update(mTimeScale);

    const int lineWidth = mTimeScale.maxPixelWidth();

    for (TimeLineRow& row : mRows) {
        row.rect.setWidth(lineWidth);
    }
}

void TimeLineEditor::setFrame(core::Frame aFrame) { mTimeCurrent.setFrame(mTimeScale, aFrame); }

core::Frame TimeLineEditor::currentFrame() const { return mTimeCurrent.frame(); }

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
    renderer.renderHeader(kHeaderHeight, kTimeLineFpsA);
    // renderer.renderHandle(mTimeCurrent.handlePos(), mTimeCurrent.handleRange());

    if (mShowSelectionRange) {
        renderer.renderSelectionRange(mFocus.visualRect());
    }
}

} // namespace ctrl
