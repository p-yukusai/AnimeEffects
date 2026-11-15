#ifndef GUI_TOOL_BONEPANEL_H
#define GUI_TOOL_BONEPANEL_H

#include <QGroupBox>
#include <QPushButton>
#include <QButtonGroup>
#include "util/Signaler.h"
#include "ctrl/BoneParam.h"
#include "gui/GUIResources.h"
#include "gui/tool/tool_BrushPanel.h"
#include "gui/tool/tool_Items.h"

namespace gui {
namespace tool {

    class BonePanel: public QGroupBox {
        Q_OBJECT
    public:
        BonePanel(QWidget* aParent, GUIResources& aResources);

        int updateGeometry(const QPoint& aPos, int aWidth);

        BrushPanel* brushPanel() { return mBrush.data(); }

        const ctrl::BoneParam& param() const { return mParam; }

        // boost like signals
        util::Signaler<void(bool)> onParamUpdated;

    private:
        void createMode();
        void applyIcons();
        void onThemeUpdated(theme::Theme&);
        void updateTypeParam(ctrl::BoneEditMode aType);

        GUIResources& mResources;
        ctrl::BoneParam mParam;
        QScopedPointer<SingleOutItem> mTypeGroup;
        // ONE influence brush for both paint and erase (the shared rows), instead
        // of a separate radius+pressure pair per mode
        QScopedPointer<BrushPanel> mBrush;
    };

} // namespace tool
} // namespace gui

#endif // GUI_TOOL_BONEPANEL_H
