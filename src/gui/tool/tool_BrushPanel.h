#ifndef GUI_TOOL_BRUSHPANEL_H
#define GUI_TOOL_BRUSHPANEL_H

#include <QGroupBox>
#include <QScopedPointer>
#include "util/Signaler.h"
#include "ctrl/Brush.h"
#include "gui/tool/tool_Items.h"

namespace gui {
namespace tool {

    // THE brush settings block: Size, Hardness, Flow, Opacity and the two tablet
    // pressure switches, editing the shared ctrl::BrushParam in place. Every
    // painting tool embeds this one widget instead of re-declaring its own
    // radius/pressure/hardness sliders, so "size 40, hardness 60%" is one row set
    // in one place and the tools cannot drift apart.
    //
    // A tool that cannot honour a row hides it rather than changing its meaning:
    // the FFD mesh brush has no per-stroke cap (a displacement cannot be blended
    // like alpha), so it shows Flow and hides Opacity.
    class BrushPanel: public QWidget {
        Q_OBJECT
    public:
        enum Row {
            Row_Size = 1,
            Row_Hardness = 2,
            Row_Flow = 4,
            Row_Opacity = 8,
            Row_Pressure = 16,
            Row_All = Row_Size | Row_Hardness | Row_Flow | Row_Opacity | Row_Pressure,
        };

        BrushPanel(QWidget* aParent, ctrl::BrushParam& aParam, int aRows = Row_All);

        // the param lives in the owning tool panel; this widget writes it
        ctrl::BrushParam& param() { return mParam; }
        void refresh();
        // The familiar adjustments: one step is one notch (x1.1 on the size, 5% on
        // the hardness), shared by the [ ] keys and the Alt+right-drag gesture.
        void adjustSize(int aSteps);
        void adjustHardness(int aSteps);

        int updateGeometry(const QPoint& aPos, int aWidth);

        util::Signaler<void(bool)> onParamUpdated;

    private:
        void createItems(int aRows);

        ctrl::BrushParam& mParam;
        QScopedPointer<SliderItem> mSize;
        QScopedPointer<SliderItem> mHardness;
        QScopedPointer<SliderItem> mFlow;
        QScopedPointer<SliderItem> mOpacity;
        QScopedPointer<CheckBoxItem> mPressureSize;
        QScopedPointer<CheckBoxItem> mPressureFlow;
        int mRows;
    };

} // namespace tool
} // namespace gui

#endif // GUI_TOOL_BRUSHPANEL_H
