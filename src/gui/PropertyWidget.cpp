#include "gui/PropertyWidget.h"
#include "XC.h"

#include <QResizeEvent>

namespace gui {

PropertyWidget::PropertyWidget(ViaPoint& aViaPoint, QWidget* aParent, GUIResources* aGUIResources):
    QScrollArea(aParent),
    mProject(),
    mGUIResources(aGUIResources),
    mTimeLineSlot(),
    mNodeAttrSlot(),
    mResModifiedSlot(),
    mTreeRestructSlot(),
    mProjAttrSlot(),
    mBoard() {
    this->setFocusPolicy(Qt::NoFocus);

    this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // this->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    this->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
    // The board's height is managed manually (see resizeEvent): with
    // widgetResizable the scroll area pins the widget to the size it had at
    // startup and ignores later growth, which compresses the field rows
    // when groups expand. Manual sizing keeps the board at its natural
    // height so rows never lose their sizeHint.
    this->setWidgetResizable(false);

    mBoard = new prop::Backboard(aViaPoint, this, mGUIResources);
    this->setWidget(mBoard);
}

PropertyWidget::~PropertyWidget() { unlinkProject(); }

void PropertyWidget::unlinkProject() {
    if (mProject) {
        mProject->onTimeLineModified.disconnect(mTimeLineSlot);
        mProject->onNodeAttributeModified.disconnect(mNodeAttrSlot);
        mProject->onResourceModified.disconnect(mResModifiedSlot);
        mProject->onTreeRestructured.disconnect(mTreeRestructSlot);
        mProject->onProjectAttributeModified.disconnect(mProjAttrSlot);
        mProject.reset();
    }
}

void PropertyWidget::setProject(core::Project* aProject) {
    unlinkProject();

    if (aProject) {
        mProject = aProject->pointee();

        mTimeLineSlot = aProject->onTimeLineModified.connect(this, &PropertyWidget::onKeyUpdated);

        mNodeAttrSlot = aProject->onNodeAttributeModified.connect(this, &PropertyWidget::onAttributeUpdated);

        mResModifiedSlot =
            aProject->onResourceModified.connect([=](core::ResourceEvent&, bool) { updateAllProperties(); });

        mTreeRestructSlot =
            aProject->onTreeRestructured.connect([=](core::ObjectTreeEvent&, bool) { updateAllProperties(); });

        mProjAttrSlot =
            aProject->onResourceModified.connect([=](core::ResourceEvent&, bool) { updateAllProperties(); });
    }

    mBoard->setProject(aProject);
}

void PropertyWidget::updateAllProperties() {
    mBoard->updateAttribute();
    mBoard->updateKey(true, true);
}

void PropertyWidget::onSelectionChanged(core::ObjectNode* aRepresentNode) { mBoard->setTarget(aRepresentNode); }

void PropertyWidget::onAttributeUpdated(core::ObjectNode&, bool) {
    mBoard->updateAttribute();
    onVisualUpdated();
}

void PropertyWidget::onKeyUpdated(core::TimeLineEvent& aEvent, bool) {
    mBoard->updateKey(!aEvent.targets().empty(), !aEvent.defaultTargets().empty());
}

void PropertyWidget::onFrameUpdated() { mBoard->updateFrame(); }

void PropertyWidget::onPlayBackStateChanged(bool aIsActive) { mBoard->setPlayBackActivity(aIsActive); }

void PropertyWidget::resizeEvent(QResizeEvent* aEvent) {
    QScrollArea::resizeEvent(aEvent);
    // Manual board sizing (widgetResizable is off): the board always keeps
    // its natural (sizeHint) height so the rows are never compressed, and
    // still fills the viewport when the content is shorter than it.
    mBoard->resize(viewport()->width(), qMax(mBoard->sizeHint().height(), viewport()->height()));
}

} // namespace gui
