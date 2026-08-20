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
#include "ctrl/time/time_Marquee.h"
#include "ctrl/time/time_Selection.h"
#include "gui/theme/TimeLine.h"

namespace ctrl {

//-------------------------------------------------------------------------------------------------
class TimeLineEditor {
public:
    enum UpdateFlag { UpdateFlag_ModFrame = 1, UpdateFlag_ModView = 2, UpdateFlag_TERM };
    typedef unsigned int UpdateFlags;

    TimeLineEditor();

    // Ruler constants shared with the header drawing (time_Renderer) and the
    // playhead's frame-number badge (gui::TimeCursor).
    static constexpr int kHeaderHeight = 22;
    // Ruler number text geometry, relative to the header top.
    static constexpr int kNumberTop = -1;
    static constexpr int kNumberHeight = 14;

    void setProject(core::Project* aProject);
    void setFrame(core::Frame aFrame);

    core::TimeFormatType timeFormatType() const { return timelineFormat; }
    int fps() const { return mFps; }

    UpdateFlags updateCursor(const core::AbstractCursor& aCursor, Qt::KeyboardModifiers aModifiers);
    bool updateWheel(int aDelta, bool aInvertScaling);
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

    // The last frame's content position (px), frame 0 at 0 — the frame
    // range's end on the timeline's own axis.
    int frameEndPixel() const;
    int pixelsPerFrame() const;
    // New-scale position of the content point under the mouse, computed
    // pixel-exact from the old scale (see implementation for why).
    int anchorPixelAfterScale(int aContentPixel, int aOldSpacing) const;
    bool selectKeysAt(core::TimeLineEvent& aEvent, const QPoint& aPos);
    bool retrieveSelectionTargets(core::TimeLineEvent& aEvent) const;
    static QString pasteCbKeys(gui::obj::Item* objItem, util::LifeLink::Pointee<core::Project> project, bool isFolder);
    static QList<core::TimeKey*> getTypesFromCb(util::LifeLink::Pointee<core::Project> project, core::ObjectNode *node);
    bool pasteCopiedKeys(core::TimeLineEvent& aEvent, const QPoint& aWorldPos);
    void deleteCheckedKeys(core::TimeLineEvent& aEvent);
    util::LinkPointer<core::Project> mProject;

private:
    enum State { State_Standby, State_MoveCurrent, State_MoveKeys, State_EncloseKeys, State_TERM };
    enum MarqueeMode { Marquee_Replace, Marquee_Add, Marquee_Subtract };

    void setMaxFrame(int aValue);
    void clearState();
    void clearSelection();
    bool beginMoveKeys();
    bool modifyMoveKeys(const QPoint& aWorldPos);
    void applyMarquee(); // commit the active marquee box into the selection

    QVector<TimeLineRow> mRows;
    const core::ObjectNode* mSelectingRow;
    int mTimeMax;
    int mFps;
    State mState;
    time::Current mTimeCurrent;
    time::Scaler mTimeScale;
    time::Marquee mMarquee;
    time::Selection mSelection;
    QSettings mSettings;
    core::TimeFormatType timelineFormat = static_cast<core::TimeFormatType>(mSettings.value("generalsettings/ui/timeformat").toInt());
    TimeLineUtil::MoveFrameOfKey* mMoveRef;
    int mMoveFrame;
    int mPressFrame;       // frame where the current gesture pressed
    bool mOnUpdatingKey;
    MarqueeMode mMarqueeMode;
    core::TimeLineEvent mPreGestureSelection;
    core::TimeLineEvent::Target mPressedTarget; // plain-press target, for click-collapse
    bool mPressedTargetValid;
};

} // namespace ctrl

#endif // CTRL_TIMELINEEDITOR_H
