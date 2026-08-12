#ifndef GUI_PROP_BACKBOARD_H
#define GUI_PROP_BACKBOARD_H

#include <QEvent>
#include <QFrame>
#include <QWidget>
#include <QVBoxLayout>
#include "core/Project.h"
#include "core/ObjectNode.h"
#include "gui/ViaPoint.h"
#include "gui/prop/prop_ConstantPanel.h"
#include "gui/prop/prop_DefaultKeyPanel.h"
#include "gui/prop/prop_CurrentKeyPanel.h"

namespace gui {
namespace prop {

    class Backboard: public QWidget {
    public:
        Backboard(ViaPoint& aViaPoint, QWidget* aParent, GUIResources* aGUIResources);
        void setProject(core::Project* aProject);
        void setTarget(core::ObjectNode* aNode);
        void setPlayBackActivity(bool aIsActive);

        void updateAttribute();
        void updateKey(bool aUpdateKey, bool aUppdateDefaultKey);
        void updateFrame();

    protected:
        bool event(QEvent* aEvent) override;

    private:
        void resetLayout();

        ViaPoint& mViaPoint;
        GUIResources* mGUIResources{};
        core::Project* mProject;
        QVBoxLayout* mLayout;
        QScopedPointer<ConstantPanel> mConstantPanel;
        QScopedPointer<DefaultKeyPanel> mDefaultKeyPanel;
        QScopedPointer<CurrentKeyPanel> mCurrentKeyPanel;
        // hairline separators between the panels; hidden with the panels
        // when no layer is selected (otherwise they float on the board)
        QScopedPointer<QFrame> mSeparator0;
        QScopedPointer<QFrame> mSeparator1;
    };

} // namespace prop
} // namespace gui

#endif // GUI_PROP_BACKBOARD_H
