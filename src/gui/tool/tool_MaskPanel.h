#ifndef GUI_TOOL_MASKPANEL_H
#define GUI_TOOL_MASKPANEL_H

#include <QGroupBox>
#include <QPushButton>
#include <QScopedPointer>
#include "util/Signaler.h"
#include "ctrl/MaskParam.h"
#include "gui/GUIResources.h"
#include "gui/tool/tool_BrushPanel.h"
#include "gui/tool/tool_Items.h"

namespace gui {
namespace tool {

    // The MASK PAINT tool's panel: the shared brush rows, plus WHICH field is being
    // painted (the filter lane and which of its two mask inputs) and a Clear.
    class MaskPanel: public QGroupBox {
        Q_OBJECT
    public:
        MaskPanel(QWidget* aParent, GUIResources& aResources);

        int updateGeometry(const QPoint& aPos, int aWidth);
        void refresh();

        BrushPanel* brushPanel() { return mBrush.data(); }

        const ctrl::MaskParam& param() const { return mParam; }
        void setLane(core::TimeKeyType aLane);
        // the node's own matte: no filter lane, no channel
        void setLayerMask();
        void setLaneAndChannel(core::TimeKeyType aLane, core::FilterMaskChannel aChannel);
        // the eyedropper's result lands here
        void setValue(float aValue);

        // boost like signals
        util::Signaler<void(bool)> onParamUpdated;
        util::Signaler<void()> onClearRequested;

    private:
        void createItems();
        void applyIcons();
        void onThemeUpdated(theme::Theme&);

        GUIResources& mResources;
        ctrl::MaskParam mParam;
        QScopedPointer<SingleOutItem> mLaneGroup;
        QScopedPointer<SingleOutItem> mChannelGroup;
        QScopedPointer<SliderItem> mValue;
        QScopedPointer<BrushPanel> mBrush;
        QScopedPointer<QPushButton> mClear;
    };

} // namespace tool
} // namespace gui

#endif // GUI_TOOL_MASKPANEL_H
