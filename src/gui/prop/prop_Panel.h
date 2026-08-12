#ifndef GUI_PROP_PANEL_H
#define GUI_PROP_PANEL_H

#include <functional>
#include <QFrame>
#include <QVBoxLayout>
#include "gui/prop/prop_HeaderButton.h"

namespace gui {

class GUIResources;

namespace prop {

    // Collapsible panel: a full-row header button (see HeaderButton) above
    // a content widget holding the groups. Collapsing hides the content, so
    // the panel height tracks the layout instead of fixed-height hacks.
    class Panel: public QFrame {
        Q_OBJECT
    public:
        Panel(const QString& aTitle, QWidget* aParent, GUIResources* aGUIResources);
        virtual ~Panel() {}
        void addGroup(QWidget* aGroup);
        void addStretch();

        std::function<void()> onCollapsed;

    private:
        HeaderButton* mHeader;
        QWidget* mContent;
        QVBoxLayout* mLayout;
    };

} // namespace prop
} // namespace gui

#endif // GUI_PROP_PANEL_H
