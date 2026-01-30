#ifndef CTRL_TIMELINEEDITOR_H
#define CTRL_TIMELINEEDITOR_H

#include <QRect>
#include <QVector>
#include <QScopedPointer>
#include "gui/obj/obj_Item.h"
#include "util/Range.h"
#include "util/LinkPointer.h"
#include "util/PlacePointer.h"
#include "core/Project.h"
#include "core/AbstractCursor.h"
#include "core/ObjectNode.h"
#include "core/CameraInfo.h"
#include "core/TimeFormat.h"
#include "core/TimeKeyPos.h"
#include "ctrl/TimeLineRow.h"
#include "ctrl/TimeLineUtil.h"
#include "ctrl/time/time_Current.h"
#include "ctrl/time/time_Scaler.h"
#include "ctrl/time/time_Focuser.h"
#include "gui/theme/TimeLine.h"

namespace ctrl {

//-------------------------------------------------------------------------------------------------
class TimeLineEditor {
public:
    enum UpdateFlag { UpdateFlag_ModFrame = 1, UpdateFlag_ModView = 2, UpdateFlag_TERM };
    typedef unsigned int UpdateFlags;

    TimeLineEditor();

    void setProject(core::Project* aProject);
    void setFrame(core::Frame aFrame);

    UpdateFlags updateCursor(const core::AbstractCursor& aCursor);
    void updateWheel(int aDelta, bool aInvertScaling);
    void updateKey();
    void updateProjectAttribute();

    void clearRows();
    void pushRow(core::ObjectNode* aNode, util::Range aWorldTB, bool aClosedFolder);
    void updateRowSelection(const core::ObjectNode* aRepresent);
    void render(QPainter& aPainter, const core::CameraInfo& aCamera, theme::TimeLine& aTheme, const QRect& aCullRect) const;

    core::Frame currentFrame() const;
    int maxFrame() const { return mTimeMax; }
    QSize modelSpaceSize() const;
    QPoint currentTimeCursorPos() const;
    
    int frameAtPixel(int aPixelX) const;
    int pixelAtFrame(int aFrame) const;
    bool checkContactWithKeyFocus(core::TimeLineEvent& aEvent, const QPoint& aPos);
    bool retrieveFocusTargets(core::TimeLineEvent& aEvent);
    QString pasteCbKeys(gui::obj::Item* objItem, util::LifeLink::Pointee<core::Project> project, bool isFolder);
    QList<core::TimeKey*> getTypesFromCb(util::LifeLink::Pointee<core::Project> project);
    bool pasteCopiedKeys(core::TimeLineEvent& aEvent, const QPoint& aWorldPos);
    void deleteCheckedKeys(core::TimeLineEvent& aEvent);
    util::LinkPointer<core::Project> mProject;

private:
    enum State { State_Standby, State_MoveCurrent, State_MoveKeys, State_EncloseKeys, State_TERM };

    void setMaxFrame(int aValue);
    void clearState();
    void beginMoveKey(const time::Focuser::SingleFocus& aTarget);
    bool beginMoveKeys(const QPoint& aWorldPos);
    bool modifyMoveKeys(const QPoint& aWorldPos);

    QVector<TimeLineRow> mRows;
    const core::ObjectNode* mSelectingRow;
    int mTimeMax;
    State mState;
    time::Current mTimeCurrent;
    time::Scaler mTimeScale;
    time::Focuser mFocus;
    QSettings mSettings;
    core::TimeFormatType timelineFormat = static_cast<core::TimeFormatType>(mSettings.value("generalsettings/ui/timeformat").toInt());
    TimeLineUtil::MoveFrameOfKey* mMoveRef;
    int mMoveFrame;
    bool mOnUpdatingKey;
    bool mShowSelectionRange;
};

} // namespace ctrl

#endif // CTRL_TIMELINEEDITOR_H
