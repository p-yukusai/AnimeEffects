#include <QCoreApplication>
#include "gui/prop/prop_Panel.h"
#include "gui/prop/prop_AttrGroup.h"
#include "gui/prop/prop_KeyGroup.h"

namespace gui {
namespace prop {

    Panel::Panel(const QString& aTitle, QWidget* aParent, GUIResources* aGUIResources):
        QFrame(nullptr),
        mHeader(nullptr),
        mContent(nullptr),
        mLayout(nullptr) {
        this->setObjectName("propertyPanel");
        this->setFrameShape(QFrame::NoFrame);
        this->setFocusPolicy(Qt::NoFocus);

        // The panel header is a real button spanning the full row (see
        // HeaderButton); collapsing hides the content widget, so the panel
        // shrinks to the header band via the layout.
        mHeader = new HeaderButton(aTitle, aGUIResources, "propertyPanelHeader", this, 14);
        mContent = new QWidget(this);
        mLayout = new QVBoxLayout(mContent);
        // 4px column gap so the subsections read as distinct rows; the
        // sections' own breathing (form bottom margin) adds on top
        mLayout->setSpacing(4);
        mLayout->setContentsMargins(0, 0, 0, 0);

        auto* vbox = new QVBoxLayout(this);
        vbox->setSpacing(0);
        vbox->setContentsMargins(0, 0, 0, 0);
        vbox->addWidget(mHeader);
        vbox->addWidget(mContent);

        mHeader->setChecked(true);
        this->connect(mHeader, &QAbstractButton::toggled, this, [this](bool aChecked) {
            mContent->setVisible(aChecked);
            if (!aChecked && onCollapsed) {
                onCollapsed();
            }
            // same-frame relayout (see the group toggles): the panel keeps
            // its old height for one event-loop pass otherwise
            for (QWidget* w = this; w != nullptr; w = w->parentWidget()) {
                if (QLayout* l = w->layout()) {
                    l->activate();
                }
            }
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        });
    }

    void Panel::addGroup(QWidget* aGroup) {
        mLayout->addWidget(aGroup);

        // keep the panel's geometry honest when a child group collapses:
        // the layout propagates the size change on its own, this just makes
        // the scroll area re-measure immediately
        if (auto* attr = qobject_cast<AttrGroup*>(aGroup)) {
            this->connect(attr, &AttrGroup::expansionChanged, this, [this](bool) {
                this->updateGeometry();
            });
        } else if (auto* key = qobject_cast<KeyGroup*>(aGroup)) {
            this->connect(key, &KeyGroup::expansionChanged, this, [this](bool) {
                this->updateGeometry();
            });
        }
    }

    void Panel::addStretch() { mLayout->addStretch(); }

} // namespace prop
} // namespace gui
