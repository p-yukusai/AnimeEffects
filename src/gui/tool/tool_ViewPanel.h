#ifndef GUI_TOOL_VIEWPANEL_H
#define GUI_TOOL_VIEWPANEL_H

#include <vector>
#include <functional>
#include <QGroupBox>
#include <QPushButton>
#include "gui/GUIResources.h"
#include "gui/tool/tool_FlowLayout.h"

namespace gui {
namespace tool {

    class ViewPanel: public QGroupBox {
    public:
        typedef std::function<void(bool)> PushDelegate;

        ViewPanel(QWidget* aParent, GUIResources& aResources, const QString& aTitle);

        // Returns the button it created. The panel's buttons are ICON-ONLY, so Qt
        // reports an empty accessible name for each of them and AT-SPI falls back to
        // the group box: every button answers to the panel's own title. A caller that
        // needs one of them addressed by name (the GUI probes do) sets an accessible
        // name on the returned pointer; callers that do not simply ignore it.
        QPushButton*
        addButton(const QString& aIconName, bool aCheckable, const QString& aToolTip, const PushDelegate& aDelegate);

        int updateGeometry(const QPoint& aPos, int aWidth);
        QPushButton* button(int aId) { return mButtons[aId]; }
        const QPushButton* button(int aId) const { return mButtons[aId]; }

    private:
        GUIResources& mGUIResources;
        void onThemeUpdated(theme::Theme&);
        std::vector<QPushButton*> mButtons;
        FlowLayout mLayout;
    };

} // namespace tool
} // namespace gui

#endif // GUI_TOOL_VIEWPANEL_H
