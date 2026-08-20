#include "gui/tool/tool_SRTPanel.h"

namespace {
const int kButtonSize = 24;
const int kButtonSpace = kButtonSize;
} // namespace

namespace gui {
namespace tool {

    SRTPanel::SRTPanel(QWidget* aParent, GUIResources& aResources):
        QGroupBox(aParent),
        mResources(aResources),
        mParam(),
        mTypeGroup(),
        mAddMove(),
        mAddRotate(),
        mAddScale(),
        mAdjust() {
        this->setTitle(tr("Transform"));
        mResources.onThemeChanged.connect(this, &SRTPanel::onThemeUpdated);
        createMode();
        updateTypeParam(mParam.mode);
    }

    void SRTPanel::applyIcons() {
        mTypeGroup->setIcons(
            QVector<QIcon>() << mResources.icon("navigation-arrow") << mResources.icon("crosshair"), QSize(18, 18));
    }

    void SRTPanel::onThemeUpdated(theme::Theme&) {
        applyIcons();
    }

    void SRTPanel::createMode() {
        // mode
        mTypeGroup.reset(new SingleOutItem(2, QSize(kButtonSpace, kButtonSpace), this));
        mTypeGroup->setChoice(mParam.mode);
        mTypeGroup->setToolTips(QStringList() << tr("Transform") << tr("Move Centroid"));
        applyIcons();
        mTypeGroup->connect([=](int aIndex) {
            this->mParam.mode = aIndex;
            this->updateTypeParam(aIndex);
            this->onParamUpdated(true);
        });

        mAddMove.reset(new CheckBoxItem(tr("Create move key"), this));
        mAddMove->setToolTip(tr("The transformation will create a move key regardless of its type."));
        mAddMove->connect([=](bool aChecked) {
            this->mParam.necessarilyMove = aChecked;
            this->onParamUpdated(false);
        });
        mAddRotate.reset(new CheckBoxItem(tr("Create rotate key"), this));
        mAddRotate->setToolTip(tr("The transformation will create a rotate key regardless of its type."));
        mAddRotate->connect([=](bool aChecked) {
            this->mParam.necessarilyRotate = aChecked;
            this->onParamUpdated(false);
        });
        mAddScale.reset(new CheckBoxItem(tr("Create scale key"), this));
        mAddScale->setToolTip(tr("The transformation will create a scale key regardless of its type."));
        mAddScale->connect([=](bool aChecked) {
            this->mParam.necessarilyScale = aChecked;
            this->onParamUpdated(false);
        });

        mAdjust.reset(new CheckBoxItem(tr("Adjust centroid position"), this));
        mAdjust->setToolTip(tr("Move the centroid in relation to the image, the opposite will be true when not toggled."
        ));
        mAdjust->setChecked(mParam.adjustPosition);
        mAdjust->connect([=](bool aChecked) {
            this->mParam.adjustPosition = aChecked;
            this->onParamUpdated(false);
        });
    }

    void SRTPanel::updateTypeParam(int aType) {
        auto showSRT = aType == 0;
        auto showCent = aType == 1;

        mAddMove->setVisible(showSRT);
        mAddRotate->setVisible(showSRT);
        mAddScale->setVisible(showSRT);

        mAdjust->setVisible(showCent);
    }

    int SRTPanel::updateGeometry(const QPoint& aPos, int aWidth) {
        static const int kItemLeft = 8;
        // content starts at the stylesheet's content top
        const int kItemTop = this->contentsMargins().top();

        const int itemWidth = aWidth - kItemLeft * 2;
        QPoint curPos(kItemLeft, kItemTop);

        // type
        curPos.setY(mTypeGroup->updateGeometry(curPos, itemWidth) + curPos.y() + 5);

        if (mParam.mode == 0) {
            curPos.setY(mAddMove->updateGeometry(curPos, itemWidth) + curPos.y());
            curPos.setY(mAddRotate->updateGeometry(curPos, itemWidth) + curPos.y());
            curPos.setY(mAddScale->updateGeometry(curPos, itemWidth) + curPos.y());
            curPos.setY(curPos.y() + 5);
        } else {
            curPos.setY(mAdjust->updateGeometry(curPos, itemWidth) + curPos.y());
            curPos.setY(curPos.y() + 5);
        }

        // myself: card height = content extent + bottom padding (see ViewPanel)
        const int b = this->contentsMargins().bottom();
        this->setGeometry(aPos.x(), aPos.y(), aWidth, curPos.y() + b);

        return aPos.y() + curPos.y() + b;
    }

} // namespace tool
} // namespace gui
