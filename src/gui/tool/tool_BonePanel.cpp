#include "gui/tool/tool_BonePanel.h"
#include "gui/tool/tool_ItemTable.h"

namespace {
int kButtonSize = 24;
int kButtonSpace = kButtonSize;
} // namespace

namespace gui {
namespace tool {

    BonePanel::BonePanel(QWidget* aParent, GUIResources& aResources):
        QGroupBox(aParent),
        mResources(aResources),
        mParam(),
        mTypeGroup(),
        mPIRadius(),
        mPIPressure(),
        mEIRadius(),
        mEIPressure() {
        this->setTitle(tr("Bone editor"));
        mResources.onThemeChanged.connect(this, &BonePanel::onThemeUpdated);
        createMode();
        updateTypeParam(mParam.mode);
    }

    void BonePanel::applyIcons() {
        mTypeGroup->setIcons(
            QVector<QIcon>() << mResources.icon("plus") << mResources.icon("minus") << mResources.icon("move")
                             << mResources.icon("bind") << mResources.icon("influence") << mResources.icon("paint-brush")
                             << mResources.icon("eraser")
        , QSize(18, 18));
    }

    void BonePanel::onThemeUpdated(theme::Theme&) {
        applyIcons();
    }

    void BonePanel::createMode() {
        if (mResources.getTheme().contains("high_dpi")) {
            kButtonSize = 32;
            kButtonSpace = kButtonSize;
        }

        // type
        mTypeGroup.reset(new SingleOutItem(ctrl::BoneEditMode_TERM, QSize(kButtonSpace, kButtonSpace), this));
        mTypeGroup->setChoice(mParam.mode);
        mTypeGroup->setToolTips(
            QStringList() << tr("Add bones") << tr("Remove bones") << tr("Move joints") << tr("Bind bone to node")
                          << tr("Adjust influence") << tr("Paint influence") << tr("Erase influence")
        );
        applyIcons();

        mTypeGroup->connect([=](int aIndex) {
            this->mParam.mode = (ctrl::BoneEditMode)aIndex;
            this->updateTypeParam((ctrl::BoneEditMode)aIndex);
            this->onParamUpdated(true);
        });

        static const int kScale = 100;

        // paint influence radius
        mPIRadius.reset(new SliderItem(tr("Radius"), this->palette(), this));
        mPIRadius->setAttribute(util::Range(5, 1000), mParam.piRadius, 50);
        mPIRadius->connectOnAny([=](int aValue) {
            this->mParam.piRadius = aValue;
            this->onParamUpdated(false);
        });

        // paint influence pressure
        mPIPressure.reset(new SliderItem(tr("Pressure"), this->palette(), this));
        mPIPressure->setAttribute(util::Range(0, kScale), static_cast<int>(mParam.piPressure * kScale), kScale / 10);
        mPIPressure->connectOnAny([=](const int aValue) {
            this->mParam.piPressure = static_cast<float>(aValue) / kScale;
            this->onParamUpdated(true);
        });
        // erase influence radius
        mEIRadius.reset(new SliderItem(tr("Radius"), this->palette(), this));
        mEIRadius->setAttribute(util::Range(5, 1000), mParam.eiRadius, 50);
        mEIRadius->connectOnAny([=](int aValue) {
            this->mParam.eiRadius = aValue;
            this->onParamUpdated(false);
        });

        // erase influence pressure
        mEIPressure.reset(new SliderItem(tr("Pressure"), this->palette(), this));
        mEIPressure->setAttribute(util::Range(0, kScale), mParam.eiPressure * kScale, kScale / 10);
        mEIPressure->connectOnAny([=](int aValue) {
            this->mParam.eiPressure = (float)aValue / kScale;
            this->onParamUpdated(false);
        });
    }

    void BonePanel::updateTypeParam(ctrl::BoneEditMode aType) {
        if (aType == ctrl::BoneEditMode_PaintInfl) {
            mPIRadius->show();
            mPIPressure->show();
        } else {
            mPIRadius->hide();
            mPIPressure->hide();
        }

        if (aType == ctrl::BoneEditMode_EraseInfl) {
            mEIRadius->show();
            mEIPressure->show();
        } else {
            mEIRadius->hide();
            mEIPressure->hide();
        }
    }

    int BonePanel::updateGeometry(const QPoint& aPos, int aWidth) {
        static const int kItemLeft = 8;
        // content starts at the stylesheet's content top
        const int kItemTop = this->contentsMargins().top();

        const int itemWidth = aWidth - kItemLeft * 2;
        QPoint curPos(kItemLeft, kItemTop);

        // type
        curPos.setY(mTypeGroup->updateGeometry(curPos, itemWidth) + curPos.y() + 5);

        if (mParam.mode == ctrl::BoneEditMode_PaintInfl) {
            // radius
            curPos.setY(mPIRadius->updateGeometry(curPos, itemWidth) + curPos.y() + 5);
            // pressure
            curPos.setY(mPIPressure->updateGeometry(curPos, itemWidth) + curPos.y() + 5);
        } else if (mParam.mode == ctrl::BoneEditMode_EraseInfl) {
            // radius
            curPos.setY(mEIRadius->updateGeometry(curPos, itemWidth) + curPos.y() + 5);
            // pressure
            curPos.setY(mEIPressure->updateGeometry(curPos, itemWidth) + curPos.y() + 5);
        }

        // myself: card height = content extent + bottom padding (see ViewPanel)
        const int b = this->contentsMargins().bottom();
        this->setGeometry(aPos.x(), aPos.y(), aWidth, curPos.y() + b);

        return aPos.y() + curPos.y() + b;
    }

} // namespace tool
} // namespace gui
