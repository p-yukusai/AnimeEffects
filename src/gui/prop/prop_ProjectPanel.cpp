#include "gui/prop/prop_ProjectPanel.h"

namespace gui {
namespace prop {

    ProjectPanel::ProjectPanel(core::Project& aProject, QWidget* aParent, GUIResources* aGUIResources):
        Panel("Project", aParent, aGUIResources),
        mProject(aProject),
        mAttributes(new AttrGroup("Time", 0, aGUIResources))

    {}

} // namespace prop
} // namespace gui
