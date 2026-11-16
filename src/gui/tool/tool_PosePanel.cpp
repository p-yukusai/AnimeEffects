#include "gui/tool/tool_PosePanel.h"
#include "gui/tool/tool_ItemTable.h"

namespace {
const int kButtonSize = 24;
const int kButtonSpace = kButtonSize;
} // namespace

namespace gui {
namespace tool {

    PosePanel::PosePanel(QWidget* aParent, GUIResources& aResources):
        QGroupBox(aParent), mResources(aResources), mParam(), mTypeGroup(), mDIWeight(), mBrush() {
        this->setTitle(tr("Pose editor"));
        mResources.onThemeChanged.connect(this, &PosePanel::onThemeUpdated);
        createMode();
        updateTypeParam(mParam.mode);
    }

    void PosePanel::applyIcons() {
        mTypeGroup->setIcons(
            QVector<QIcon>() << mResources.icon("navigation-arrow") << mResources.icon("pencil-simple") << mResources.icon("eraser")
        , QSize(18, 18));
    }

    void PosePanel::onThemeUpdated(theme::Theme&) {
        applyIcons();
    }

    void PosePanel::createMode() {
        // type
        mTypeGroup.reset(new SingleOutItem(ctrl::PoseEditMode_TERM, QSize(kButtonSpace, kButtonSpace), this));
        mTypeGroup->setChoice(mParam.mode);
        mTypeGroup->setToolTips(QStringList() << tr("Move bone") << tr("Pull bones") << tr("Erase bone pose"));
        applyIcons();

        mTypeGroup->connect([=](int aIndex) {
            this->mParam.mode = (ctrl::PoseEditMode)aIndex;
            this->updateTypeParam((ctrl::PoseEditMode)aIndex);
            this->onParamUpdated(true);
        });

        static const int kScale = 100;

        // drawing pressure
        mDIWeight.reset(new SliderItem(tr("Weight"), this->palette(), this));
        mDIWeight->setAttribute(util::Range(0, kScale), mParam.diWeight * kScale, kScale / 10);
        mDIWeight->connectOnAny([=](int aValue) {
            this->mParam.diWeight = (float)aValue / kScale;
            this->onParamUpdated(false);
        });
        mBrush.reset(new BrushPanel(this, mParam.brush));
        mBrush->onParamUpdated.connect([=](bool aDirect) { this->onParamUpdated(aDirect); });
    }

    void PosePanel::updateTypeParam(ctrl::PoseEditMode aType) {
        const bool isDraw = aType == ctrl::PoseEditMode_Draw;
        const bool isErase = aType == ctrl::PoseEditMode_Erase;
        mDIWeight->setVisible(isDraw);
        mBrush->setVisible(isErase);
    }

    int PosePanel::updateGeometry(const QPoint& aPos, int aWidth) {
        static const int kItemLeft = 8;
        // content starts at the stylesheet's content top
        const int kItemTop = this->contentsMargins().top();

        const int itemWidth = aWidth - kItemLeft * 2;
        QPoint curPos(kItemLeft, kItemTop);

        // type
        curPos.setY(mTypeGroup->updateGeometry(curPos, itemWidth) + curPos.y() + 5);

        if (mParam.mode == ctrl::PoseEditMode_Draw) {
            // weight
            curPos.setY(mDIWeight->updateGeometry(curPos, itemWidth) + curPos.y() + 5);
        } else if (mParam.mode == ctrl::PoseEditMode_Erase) {
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
