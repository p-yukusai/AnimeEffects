#ifndef GUI_TOOL_FFDPANEL_H
#define GUI_TOOL_FFDPANEL_H

#include <vector>
#include <functional>
#include <QGroupBox>
#include <QPushButton>
#include <QSlider>
#include <QButtonGroup>
#include "util/Signaler.h"
#include "ctrl/FFDParam.h"
#include "gui/GUIResources.h"
#include "gui/tool/tool_BrushPanel.h"
#include "gui/tool/tool_Items.h"

namespace gui {
namespace tool {

    class FFDPanel: public QGroupBox {
        Q_OBJECT
    public:
        FFDPanel(QWidget* aParent, GUIResources& aResources);

        int updateGeometry(const QPoint& aPos, int aWidth);

        BrushPanel* brushPanel() { return mBrush.data(); }

        const ctrl::FFDParam& param() const { return mParam; }

        // boost like signals
        util::Signaler<void(bool)> onParamUpdated;

    private:
        void createBrush();
        void applyIcons();
        void onThemeUpdated(theme::Theme&);
        void updateTypeParam(ctrl::FFDParam::Type aType);

        GUIResources& mResources;
        ctrl::FFDParam mParam;
        QScopedPointer<SingleOutItem> mTypeGroup;
        // the shared brush rows (Size, Hardness, Flow, the pressure switches),
        // hiding Opacity: a mesh displacement has no per-stroke cap
        QScopedPointer<BrushPanel> mBrush;
        QScopedPointer<SliderItem> mBlur;
    };

} // namespace tool
} // namespace gui

#endif // GUI_TOOL_FFDPANEL_H
