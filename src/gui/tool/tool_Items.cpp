#include "XC.h"
#include "gui/tool/tool_Items.h"
#include "gui/tool/tool_ItemTable.h"
#include "gui/tool/ToolSlider.h"

namespace gui {
namespace tool {

    //-------------------------------------------------------------------------------------------------
    SingleOutItem::SingleOutItem(int aButtonCount, const QSize& aButtonSize, QWidget* aParent):
        mGroup(), mButtons(), mButtonSize(aButtonSize), mButtonNum(aButtonCount) {
        XC_PTR_ASSERT(aParent);
        mGroup = new QButtonGroup(aParent);
        mGroup->setExclusive(true);

        mButtons.resize(mButtonNum);

        for (int i = 0; i < mButtonNum; ++i) {
            mButtons[i] = new QPushButton(aParent);
            mButtons[i]->setCheckable(true);
            mButtons[i]->setFocusPolicy(Qt::NoFocus);
            // tagged so the QSS highlights these mode/option rows with the
            // brand accent (like the toolbox) without touching the View
            // Settings panel, which shares the tool dock stylesheet
            mButtons[i]->setProperty("modeButton", true);
            mGroup->addButton(mButtons[i]);
        }
    }

    void SingleOutItem::setToolTips(const QStringList& aTips) {
        int i = 0;
        for (auto tip : aTips) {
            if (i < mButtons.size()) {
                mButtons.at(i)->setToolTip(tip);
                ++i;
            } else {
                break;
            }
        }
    }

    void SingleOutItem::setIcons(const QVector<QIcon*>& aIcons, const QSize& aIconSize) {
        const QSize size = aIconSize.isValid() ? aIconSize : mButtonSize;
        int i = 0;
        for (auto icon : aIcons) {
            if (i < mButtons.size()) {
                mButtons.at(i)->setIcon(*icon);
                mButtons.at(i)->setIconSize(size);
                ++i;
            } else {
                break;
            }
        }
    }

    void SingleOutItem::setIcons(const QVector<QIcon>& aIcons, const QSize& aIconSize) {
        const QSize size = aIconSize.isValid() ? aIconSize : mButtonSize;
        int i = 0;
        for (auto icon : aIcons) {
            if (i < mButtons.size()) {
                mButtons.at(i)->setIcon(icon);
                mButtons.at(i)->setIconSize(size);
                ++i;
            } else {
                break;
            }
        }
    }


    void SingleOutItem::setChoice(int aButtonIndex) { mButtons.at(aButtonIndex)->setChecked(true); }

    void SingleOutItem::connect(const std::function<void(int)>& aPressed) {
        for (int i = 0; i < mButtonNum; ++i) {
            mGroup->connect(mButtons[i], &QPushButton::pressed, [=]() { aPressed(i); });
        }
    }

    int SingleOutItem::updateGeometry(const QPoint& aPos, int aWidth) {
        // type
        // 2px gap between mode buttons, matching the toolbox FlowLayout spacing
        ItemTable table(aPos, aWidth, mButtonSize, QSize(2, 2));
        for (auto button : mButtons) {
            table.pushGeometry(*button);
        }
        return table.height();
    }

    //-------------------------------------------------------------------------------------------------
    SliderItem::SliderItem(const QString& aLabel, const QPalette& aPalette, QWidget* aParent):
        mLabel(), mSlider(), mText(aLabel) {
        XC_PTR_ASSERT(aParent);

        mLabel = new QLabel(aParent);
        mLabel->setPalette(aPalette);
        mLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

        // The option-row slider is custom-painted (ToolSlider): QSS cannot
        // render a clean circular head on a thin track (Qt's subcontrol
        // border-radius cap), so the head/track are drawn with QPainter
        // using the theme tokens instead.
        mSlider = new ToolSlider(aParent);
        QSlider::connect(mSlider, &QSlider::valueChanged, [=](const int aValue) {
            this->updateText(aValue);
            // We have to emit this signal so the text update doesn't eat it completely
            emit mSlider->sliderMoved(aValue);
        });
    }

    void SliderItem::setAttribute(const util::Range& aRange, int aValue, int aPageStep, int aStep) {
        mSlider->setRange(aRange.min(), aRange.max());
        mSlider->setSingleStep(aStep);
        mSlider->setPageStep(aPageStep);
        mSlider->setValue(aValue);
        updateText(aValue);
    }

    void SliderItem::connectOnChanged(const std::function<void(int)>& aValueChanged) {
        mSlider->connect(mSlider, &QSlider::valueChanged, aValueChanged);
    }

    void SliderItem::connectOnMoved(const std::function<void(int)>& aSliderMoved) {
        mSlider->connect(mSlider, &QSlider::sliderMoved, aSliderMoved);
    }
    // In case we consumed a signal, we just check both
    void SliderItem::connectOnAny(const std::function<void(int)>& aValue) {
        QSlider::connect(mSlider, &QSlider::valueChanged, aValue);
        QSlider::connect(mSlider, &QSlider::sliderMoved, aValue);
    }

    int SliderItem::updateGeometry(const QPoint& aPos, int aWidth) {
        static const int kLabelHeight = 16;
        static const int kSliderHeight = 16;
        mLabel->setGeometry(aPos.x(), aPos.y(), aWidth, kLabelHeight);
        mSlider->setGeometry(aPos.x(), aPos.y() + kLabelHeight, aWidth, kSliderHeight);
        return kLabelHeight + kSliderHeight;
    }

    void SliderItem::updateText(int aValue) { mLabel->setText(mText + ":  " + QString::number(aValue)); }

    //-------------------------------------------------------------------------------------------------
    CheckBoxItem::CheckBoxItem(const QString& aLabel, QWidget* aParent): mCheckBox() {
        XC_PTR_ASSERT(aParent);

        mCheckBox = new QCheckBox(aLabel, aParent);
        mCheckBox->setObjectName("checkItem");
        mCheckBox->setFocusPolicy(Qt::NoFocus);
    }

    void CheckBoxItem::setToolTip(const QString& aTip) { mCheckBox->setToolTip(aTip); }

    void CheckBoxItem::setChecked(bool aChecked) { mCheckBox->setChecked(aChecked); }

    void CheckBoxItem::connect(const std::function<void(bool)>& aValueChanged) {
        mCheckBox->connect(mCheckBox, &QCheckBox::clicked, aValueChanged);
    }

    int CheckBoxItem::updateGeometry(const QPoint& aPos, int aWidth) {
        const int height = mCheckBox->sizeHint().height();
        mCheckBox->setGeometry(aPos.x(), aPos.y(), aWidth, height);
        // +1px so stacked rows keep a 2px gap (the 15px indicator already
        // leaves 1px slack inside the 16px sizeHint rect)
        return height + 1;
    }

} // namespace tool
} // namespace gui
