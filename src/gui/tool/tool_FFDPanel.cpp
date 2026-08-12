#include "gui/tool/tool_FFDPanel.h"
#include "gui/tool/tool_ItemTable.h"

namespace {
int kButtonSize = 24;
int kButtonSpace = kButtonSize;
} // namespace

namespace gui {
namespace tool {

    FFDPanel::FFDPanel(QWidget* aParent, GUIResources& aResources):
        QGroupBox(aParent),
        mResources(aResources),
        mParam(),
        mTypeGroup(),
        mHardnessGroup(),
        mRadius(),
        mPressure(),
        mBlur(),
        mEraseHardnessGroup(),
        mEraseRadius(),
        mErasePressure() {
        this->setTitle(tr("FFD"));
        mResources.onThemeChanged.connect(this, &FFDPanel::onThemeUpdated);
        createBrush();
        updateTypeParam(mParam.type);
    }

    void FFDPanel::applyIcons() {
        mTypeGroup->setIcons(
            QVector<QIcon>() << mResources.icon("move") << mResources.icon("pencil") << mResources.icon("eraser")
        , QSize(18, 18));
        mHardnessGroup->setIcons(
            QVector<QIcon>() << mResources.icon("hardness-1") << mResources.icon("hardness-2")
                             << mResources.icon("hardness-3"),
            QSize(18, 18)
        );
        mEraseHardnessGroup->setIcons(
            QVector<QIcon>() << mResources.icon("hardness-1") << mResources.icon("hardness-2")
                             << mResources.icon("hardness-3"),
            QSize(18, 18)
        );
    }

    void FFDPanel::onThemeUpdated(theme::Theme&) {
        applyIcons();
    }

    void FFDPanel::createBrush() {
        if (mResources.getTheme().contains("high_dpi")) {
            kButtonSize = 32;
            kButtonSpace = kButtonSize;
        }
        // type
        mTypeGroup.reset(new SingleOutItem(3, QSize(kButtonSpace, kButtonSpace), this));
        mTypeGroup->setChoice(mParam.type);
        mTypeGroup->setToolTips(QStringList() << tr("Move vertex") << tr("Deform mesh") << tr("Erase deformations"));
        mTypeGroup->connect([=](int aIndex) {
            this->mParam.type = (ctrl::FFDParam::Type)aIndex;
            this->updateTypeParam(this->mParam.type);
            this->onParamUpdated(true);
        });

        // hardness
        mHardnessGroup.reset(new SingleOutItem(3, QSize(kButtonSpace, kButtonSpace), this));
        mHardnessGroup->setChoice(mParam.hardness);
        mHardnessGroup->setToolTips(QStringList() << tr("Soft") << tr("Normal") << tr("Hard"));
        mHardnessGroup->connect([=](int aIndex) {
            this->mParam.hardness = aIndex;
            this->onParamUpdated(false);
        });

        static const int kScale = 100;

        // radius
        mRadius.reset(new SliderItem(tr("Radius"), this->palette(), this));
        mRadius->setAttribute(util::Range(5, 1000), mParam.radius, 50);
        mRadius->connectOnAny([=](int aValue) {
            this->mParam.radius = aValue;
            this->onParamUpdated(false);
        });

        // pressure
        mPressure.reset(new SliderItem(tr("Pressure"), this->palette(), this));
        mPressure->setAttribute(util::Range(0, kScale), mParam.pressure * kScale, kScale / 10);
        mPressure->connectOnAny([=](int aValue) {
            this->mParam.pressure = (float)aValue / kScale;
            this->onParamUpdated(false);
        });

        // blur
        mBlur.reset(new SliderItem(tr("Blur"), this->palette(), this));
        mBlur->setAttribute(util::Range(0, kScale), mParam.blur * kScale, kScale / 10);
        mBlur->connectOnAny([=](int aValue) {
            this->mParam.blur = (float)aValue / kScale;
            this->onParamUpdated(false);
        });

        // erase hardness
        mEraseHardnessGroup.reset(new SingleOutItem(3, QSize(kButtonSpace, kButtonSpace), this));
        mEraseHardnessGroup->setChoice(mParam.eraseHardness);
        mEraseHardnessGroup->setToolTips(QStringList() << tr("Soft") << tr("Normal") << tr("Hard"));
        mEraseHardnessGroup->connect([=](int aIndex) {
            this->mParam.eraseHardness = aIndex;
            this->onParamUpdated(false);
        });

        // erase radius
        mEraseRadius.reset(new SliderItem(tr("Radius"), this->palette(), this));
        mEraseRadius->setAttribute(util::Range(5, 1000), mParam.eraseRadius, 50);
        mEraseRadius->connectOnAny([=](int aValue) {
            this->mParam.eraseRadius = aValue;
            this->onParamUpdated(false);
        });

        // erase pressure
        mErasePressure.reset(new SliderItem(tr("Pressure"), this->palette(), this));
        mErasePressure->setAttribute(util::Range(0, kScale), mParam.erasePressure * kScale, kScale / 10);
        mErasePressure->connectOnAny([=](int aValue) {
            this->mParam.erasePressure = (float)aValue / kScale;
            this->onParamUpdated(false);
        });
    }

    void FFDPanel::updateTypeParam(ctrl::FFDParam::Type aType) {
        const bool showPencil = (aType == ctrl::FFDParam::Type_Pencil);
        const bool showEraser = (aType == ctrl::FFDParam::Type_Eraser);

        mHardnessGroup->setVisible(showPencil);
        mRadius->setVisible(showPencil);
        mPressure->setVisible(showPencil);
        mBlur->setVisible(showPencil);

        mEraseHardnessGroup->setVisible(showEraser);
        mEraseRadius->setVisible(showEraser);
        mErasePressure->setVisible(showEraser);
    }

    int FFDPanel::updateGeometry(const QPoint& aPos, int aWidth) {
        static const int kItemLeft = 8;
        // content starts at the stylesheet's content top
        const int kItemTop = this->contentsMargins().top();

        const int itemWidth = aWidth - kItemLeft * 2;
        QPoint curPos(kItemLeft, kItemTop);

        // type
        curPos.setY(mTypeGroup->updateGeometry(curPos, itemWidth) + curPos.y() + 5);

        if (mParam.type == ctrl::FFDParam::Type_Pencil) {
            // hardness
            curPos.setY(mHardnessGroup->updateGeometry(curPos, itemWidth) + curPos.y() + 5);
            // radius
            curPos.setY(mRadius->updateGeometry(curPos, itemWidth) + curPos.y() + 5);
            // pressure
            curPos.setY(mPressure->updateGeometry(curPos, itemWidth) + curPos.y() + 5);
            // blur
            curPos.setY(mBlur->updateGeometry(curPos, itemWidth) + curPos.y() + 5);
        } else if (mParam.type == ctrl::FFDParam::Type_Eraser) {
            // erase hardness
            curPos.setY(mEraseHardnessGroup->updateGeometry(curPos, itemWidth) + curPos.y() + 5);
            // eraseRadius
            curPos.setY(mEraseRadius->updateGeometry(curPos, itemWidth) + curPos.y() + 5);
            // erasePressure
            curPos.setY(mErasePressure->updateGeometry(curPos, itemWidth) + curPos.y() + 5);
        }

        // myself: card height = content extent + bottom padding (see ViewPanel)
        const int b = this->contentsMargins().bottom();
        this->setGeometry(aPos.x(), aPos.y(), aWidth, curPos.y() + b);

        return aPos.y() + curPos.y() + b;
    }

} // namespace tool
} // namespace gui
