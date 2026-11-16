#include "gui/tool/tool_BonePanel.h"
#include "gui/tool/tool_ItemTable.h"

namespace {
const int kButtonSize = 24;
const int kButtonSpace = kButtonSize;
} // namespace

namespace gui {
namespace tool {

    BonePanel::BonePanel(QWidget* aParent, GUIResources& aResources):
        QGroupBox(aParent),
        mResources(aResources),
        mParam(),
        mTypeGroup(),
        mBrush() {
        this->setTitle(tr("Bone editor"));
        mResources.onThemeChanged.connect(this, &BonePanel::onThemeUpdated);
        createMode();
        updateTypeParam(mParam.mode);
    }

    void BonePanel::applyIcons() {
        mTypeGroup->setIcons(
            QVector<QIcon>() << mResources.icon("plus-bold") << mResources.icon("minus-bold") << mResources.icon("navigation-arrow")
                             << mResources.icon("link") << mResources.icon("envelope")
                             << mResources.icon("paint-envelope") << mResources.icon("eraser")
                             << mResources.icon("paint-brush")
        , QSize(18, 18));
    }

    void BonePanel::onThemeUpdated(theme::Theme&) {
        applyIcons();
    }

    void BonePanel::createMode() {

        // type
        mTypeGroup.reset(new SingleOutItem(ctrl::BoneEditMode_TERM, QSize(kButtonSpace, kButtonSpace), this));
        mTypeGroup->setChoice(mParam.mode);
        mTypeGroup->setToolTips(
            QStringList() << tr("Add bones") << tr("Remove bones") << tr("Move joints") << tr("Bind bone to node")
                          << tr("Adjust envelope") << tr("Grow envelope") << tr("Shrink envelope")
                          << tr("Paint weights")
        );
        applyIcons();

        mTypeGroup->connect([=](int aIndex) {
            this->mParam.mode = (ctrl::BoneEditMode)aIndex;
            this->updateTypeParam((ctrl::BoneEditMode)aIndex);
            this->onParamUpdated(true);
        });

        mBrush.reset(new BrushPanel(this, mParam.brush));
        mBrush->onParamUpdated.connect([=](bool aDirect) { this->onParamUpdated(aDirect); });
    }

    void BonePanel::updateTypeParam(ctrl::BoneEditMode aType) {
        const bool influencing = (aType == ctrl::BoneEditMode_PaintInfl
            || aType == ctrl::BoneEditMode_EraseInfl || aType == ctrl::BoneEditMode_PaintWeights);
        mBrush->setVisible(influencing);
    }

    int BonePanel::updateGeometry(const QPoint& aPos, int aWidth) {
        static const int kItemLeft = 8;
        // content starts at the stylesheet's content top
        const int kItemTop = this->contentsMargins().top();

        const int itemWidth = aWidth - kItemLeft * 2;
        QPoint curPos(kItemLeft, kItemTop);

        // type
        curPos.setY(mTypeGroup->updateGeometry(curPos, itemWidth) + curPos.y() + 5);

        if (mParam.mode == ctrl::BoneEditMode_PaintInfl || mParam.mode == ctrl::BoneEditMode_EraseInfl
            || mParam.mode == ctrl::BoneEditMode_PaintWeights) {
            // the brush block answers an ABSOLUTE bottom edge, so a bare gap
            curPos.setY(mBrush->updateGeometry(curPos, itemWidth) + 5);
        }

        // myself: card height = content extent + bottom padding (see ViewPanel)
        const int b = this->contentsMargins().bottom();
        this->setGeometry(aPos.x(), aPos.y(), aWidth, curPos.y() + b);

        return aPos.y() + curPos.y() + b;
    }

} // namespace tool
} // namespace gui
