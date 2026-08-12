#include <QDebug>
#include <QCursor>
#include <QHeaderView>
#include <QStandardItem>
#include <QApplication>
#include "util/SelectArgs.h"
#include "cmnd/ScopedMacro.h"
#include "gui/prop/prop_Items.h"
#include "GUIResources.h"
#include "util/Easing.h"
#include <QDialog>
#include <QToolButton>
#include "gui/prop/splineWidget.h"

namespace gui {
namespace prop {

    //-------------------------------------------------------------------------------------------------
    template<typename SpinBoxType>
    void stepByHelper(SpinBoxType* spinBox, int steps) {
        spinBox->mTriggeredByArrows = true;
        int stepRate = (QApplication::keyboardModifiers() == Qt::ShiftModifier) ? 10 : 1;
        spinBox->QAbstractSpinBox::stepBy(steps * stepRate);
        emit spinBox->editingFinished();
        spinBox->mTriggeredByArrows = false;
    }

    SpinBox::SpinBox(QWidget* parent): QSpinBox(parent), mTriggeredByArrows(false) {
        setAccelerated(true);
        // fill the shared form field column (ExpandingFieldsGrow)
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void SpinBox::stepBy(int steps) { stepByHelper(this, steps); }

    //-------------------------------------------------------------------------------------------------
    DoubleSpinBox::DoubleSpinBox(QWidget* parent): QDoubleSpinBox(parent), mTriggeredByArrows(false) {
        setAccelerated(true);
        // Always a dot separator: the project data (.anie/JSON) uses '.', so
        // the display must round-trip what you type/save regardless of the
        // system locale (a comma-locale would show "0,5" and reject the dot).
        setLocale(QLocale::c());
        // fill the shared form field column (ExpandingFieldsGrow)
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void DoubleSpinBox::stepBy(int steps) { stepByHelper(this, steps); }

    QString DoubleSpinBox::textFromValue(double aValue) const {
        QString text = QDoubleSpinBox::textFromValue(aValue);
        const QString sep = locale().decimalPoint();
        const int sepIdx = text.lastIndexOf(sep);
        if (sepIdx >= 0) {
            int end = text.size();
            while (end > sepIdx + sep.size() && text.at(end - 1) == '0') {
                --end;
            }
            text = text.left(end);
            if (text.endsWith(sep)) {
                text.chop(sep.size());
            }
        }
        // "-0" (from e.g. -0.000 at higher caps) reads as zero
        if (text == "-0") {
            text = "0";
        }
        return text;
    }

    //-------------------------------------------------------------------------------------------------
    CheckItem::CheckItem(QWidget* aParent): mBox(), mStamp(), mSignal(true), mIsEnable(true) {
        mBox = new QCheckBox(aParent);
        mBox->setFocusPolicy(Qt::NoFocus);

        mStamp = mBox->isChecked();

        mBox->connect(mBox, &QCheckBox::clicked, [=]() { this->onEditingFinished(); });
    }

    void CheckItem::onEditingFinished() {
        if (mStamp != value()) {
            if (onValueUpdated && mSignal) {
                onValueUpdated(value());
            }
            mStamp = value();
        }
    }

    void CheckItem::setItemEnabled(bool aEnable) {
        if (mIsEnable != aEnable) {
            mBox->setEnabled(aEnable);
            mIsEnable = aEnable;
        }
    }

    void CheckItem::setItemVisible(bool aVisible) { mBox->setVisible(aVisible); }

    void CheckItem::setValue(bool aValue, bool aSignal) {
        mSignal = aSignal;
        mBox->setChecked(aValue);
        mStamp = aValue;
        mSignal = true;
    }

    //-------------------------------------------------------------------------------------------------
    ComboItem::ComboItem(QWidget* aParent): mBox(), mStamp(), mSignal(true) {
        mBox = new QComboBox(aParent);
        // fill the shared form field column (ExpandingFieldsGrow)
        mBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        mStamp = mBox->currentIndex();

        mBox->connect(mBox, util::SelectArgs<int>::from(&QComboBox::currentIndexChanged), [=]() {
            this->onEditingFinished();
            mBox->clearFocus();
        });
    }

    void ComboItem::onEditingFinished() {
        if (mStamp != value()) {
            if (onValueUpdated && mSignal) {
                onValueUpdated(mStamp, value());
            }
            mStamp = value();
        }
    }

    void ComboItem::setItemEnabled(bool aEnable) { mBox->setEnabled(aEnable); }

    void ComboItem::setItemVisible(bool aVisible) { mBox->setVisible(aVisible); }

    void ComboItem::setValue(int aValue, bool aSignal) {
        mSignal = aSignal;
        mBox->setCurrentIndex(aValue);
        mStamp = aValue;
        mSignal = true;
    }

    //-------------------------------------------------------------------------------------------------
    Combo2DItem::Combo2DItem(QWidget* aParent): mLayout(), mBox(), mStamp(), mSignal(true) {
        mLayout = new QHBoxLayout();

        for (int i = 0; i < 2; ++i) {
            mBox[i] = new QComboBox(aParent);
            // expand with the shared field column; stretch 1 splits it 50/50
            mBox[i]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

            mLayout->addWidget(mBox[i], 1);
            mBox[i]->connect(mBox[i], util::SelectArgs<int>::from(&QComboBox::currentIndexChanged), [=]() {
                this->onEditingFinished();
                mBox[i]->clearFocus();
            });
        }
        mStamp = QPoint(mBox[0]->currentIndex(), mBox[1]->currentIndex());
    }

    void Combo2DItem::onEditingFinished() {
        if (mStamp != value()) {
            if (onValueUpdated && mSignal) {
                onValueUpdated(mStamp, value());
            }
            mStamp = value();
        }
    }

    void Combo2DItem::setItemEnabled(bool aEnable) {
        for (int i = 0; i < 2; ++i) {
            mBox[i]->setEnabled(aEnable);
        }
    }

    void Combo2DItem::setItemVisible(bool aVisible) {
        for (int i = 0; i < 2; ++i) {
            mBox[i]->setVisible(aVisible);
        }
    }

    QPoint Combo2DItem::value() const { return QPoint(mBox[0]->currentIndex(), mBox[1]->currentIndex()); }

    void Combo2DItem::setValue(QPoint aValue, bool aSignal) {
        mSignal = aSignal;
        mBox[0]->setCurrentIndex(aValue.x());
        mBox[1]->setCurrentIndex(aValue.y());
        mStamp = aValue;
        mSignal = true;
    }

    //-------------------------------------------------------------------------------------------------
    EasingItem::EasingItem(QWidget* aParent, const GUIResources* mGUIResources): mLayout(), mBox(), mDBox(), mStamp(), mSignal(true) {
        mLayout = new QGridLayout();
        // The stylesheet makes the default grid spacing collapse to 0; keep
        // the 2px gap the other multi-field rows use (Combo2DItem).
        mLayout->setSpacing(2);

        // The two logical columns split the shared field column 50/50; the
        // Custom button spans columns 1-3 and fills its half.
        mLayout->setColumnStretch(0, 1);
        mLayout->setColumnStretch(1, 1);

        for (int i = 0; i < 2; ++i) {
            mBox[i] = new QComboBox(aParent);
            mBox[i]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            mLayout->addWidget(mBox[i], 0, i);
            mBox[i]->connect(mBox[i], util::SelectArgs<int>::from(&QComboBox::currentIndexChanged), [=]() {
                this->onEditingFinished();
                mBox[i]->clearFocus();
            });
        }

        mDBox = new DoubleSpinBox(aParent);
        mLayout->addWidget(mDBox, 1, 0);
        mDBox->connect(mDBox, &QAbstractSpinBox::editingFinished, [=]() {
            this->onEditingFinished();
            if (!mDBox->mTriggeredByArrows) {
                mDBox->clearFocus();
            }
        });

        mBox[0]->addItems(util::Easing::getTypeNameList());
        mBox[1]->addItems(
            QStringList() << "In"
                          << "Out"
                          << "All"
        );
        mDBox->setRange(0.0f, 1.0f);
        mDBox->setSingleStep(0.1);

        // Custom spline
        mCustomEasing = new QPushButton;
        // objectName lets propertywidget.ssa style its height to match the fields
        mCustomEasing->setObjectName("customEasing");
        mCustomEasing->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        // col 1 only (no span): a span across cols 1-3 left two empty columns
        // whose 2px gaps trailed 4px of space after the row-0 combos
        mLayout->addWidget(mCustomEasing, 1, 1);
        mCustomEasing->setIcon(mGUIResources->icon("ease"));
        mCustomEasing->setText(QCoreApplication::tr("Custom"));
        mCustomEasing->setToolTip(QCoreApplication::tr("Bezier editor"));
        mCustomEasing->connect(mCustomEasing, &QToolButton::clicked, [=]() {
            mBox[0]->setCurrentIndex(util::Easing::Type_Custom);
            auto* splineWidgetClass = new Ui_splineWidget();
            auto* splineWidget = new QDialog();
            auto* cubicBezier = new util::Easing::CubicBezier(mCubicBezier);
            splineWidgetClass->setupUi(splineWidget, mGUIResources, cubicBezier);
            const auto result = splineWidget->exec();
            if (result == QDialog::Accepted) {
                mCubicBezier = *cubicBezier;
                onValueUpdated(mStamp, value());
            }
        });

        mStamp = value();
    }

    void EasingItem::onEditingFinished() {
        if (mStamp != value()) {
            if (onValueUpdated && mSignal) {
                onValueUpdated(mStamp, value());
            }
            mStamp = value();
        }
    }

    void EasingItem::setItemEnabled(bool aEnable) {
        for (int i = 0; i < 2; ++i) {
            mBox[i]->setEnabled(aEnable);
        }
        mDBox->setEnabled(aEnable);
        mCustomEasing->setEnabled(aEnable);
    }

    void EasingItem::setItemVisible(bool aVisible) {
        for (int i = 0; i < 2; ++i) {
            mBox[i]->setVisible(aVisible);
        }
        mDBox->setVisible(aVisible);
        mCustomEasing->setVisible(aVisible);
    }

    util::Easing::Param EasingItem::value() const {
        util::Easing::Param param;
        param.type = (util::Easing::Type)mBox[0]->currentIndex();
        param.range = (util::Easing::Range)mBox[1]->currentIndex();
        param.weight = mDBox->value();
        param.cubicBezier = mCubicBezier;
        return param;
    }

    void EasingItem::setValue(util::Easing::Param aValue, bool aSignal) {
        mSignal = aSignal;
        mBox[0]->setCurrentIndex(aValue.type);
        mBox[1]->setCurrentIndex(aValue.range);
        mDBox->setValue(aValue.weight);
        mCubicBezier = aValue.cubicBezier;
        mStamp = aValue;
        mSignal = true;
    }

    //-------------------------------------------------------------------------------------------------
    IntegerItem::IntegerItem(QWidget* aParent): mBox(), mStamp(), mSignal(true) {
        mBox = new SpinBox(aParent);

        mStamp = mBox->value();

        mBox->connect(mBox, &QAbstractSpinBox::editingFinished, [=]() {
            this->onEditingFinished();
            if (!mBox->mTriggeredByArrows) {
                mBox->clearFocus();
            }
        });
    }

    void IntegerItem::setRange(int aMin, int aMax) { mBox->setRange(aMin, aMax); }

    void IntegerItem::onEditingFinished() {
        if (mStamp != value()) {
            if (onValueUpdated && mSignal) {
                onValueUpdated(mStamp, value());
            }
            mStamp = value();
        }
    }

    void IntegerItem::setItemEnabled(bool aEnable) {
        mSignal = false;
        mBox->setEnabled(aEnable);
        mSignal = true;
    }

    void IntegerItem::setItemVisible(bool aVisible) { mBox->setVisible(aVisible); }

    //-------------------------------------------------------------------------------------------------
    DecimalItem::DecimalItem(QWidget* aParent): mBox(), mStamp(), mSignal(true) {
        mBox = new DoubleSpinBox(aParent);

        mStamp = mBox->value();
        // 2-decimal cap: sub-unit precision is plenty for float data and the
        // display trims trailing zeros anyway
        mBox->setDecimals(2);

        mBox->connect(mBox, &QAbstractSpinBox::editingFinished, [=]() {
            this->onEditingFinished();
            if (!mBox->mTriggeredByArrows) {
                mBox->clearFocus();
            }
        });
    }

    void DecimalItem::setRange(double aMin, double aMax) { mBox->setRange(aMin, aMax); }

    void DecimalItem::onEditingFinished() {
        if (mStamp != value()) {
            if (onValueUpdated && mSignal) {
                onValueUpdated(mStamp, value());
            }
            mStamp = value();
        }
    }

    void DecimalItem::setItemEnabled(bool aEnable) {
        mSignal = false;
        mBox->setEnabled(aEnable);
        mSignal = true;
    }

    void DecimalItem::setItemVisible(bool aVisible) { mBox->setVisible(aVisible); }

    //-------------------------------------------------------------------------------------------------
    Vector2DItem::Vector2DItem(QWidget* aParent): mLayout(), mStamp(), mSignal(true) {
#if 0
    mLayout = new QVBoxLayout();
#else
        mLayout = new QHBoxLayout();
#endif
        mLayout->setSpacing(2);

        for (int i = 0; i < 2; ++i) {
            mBox[i] = new DoubleSpinBox(aParent);
            // 2-decimal cap: sub-unit precision is plenty for float data and
            // the display trims trailing zeros anyway
            mBox[i]->setDecimals(2);
            // stretch 1 splits the shared field column 50/50
            mLayout->addWidget(mBox[i], 1);

            mBox[i]->connect(mBox[i], &QAbstractSpinBox::editingFinished, [=]() {
                this->onEditingFinished();
                if (!mBox[i]->mTriggeredByArrows) {
                    mBox[i]->clearFocus();
                }
            });
        }
        mStamp = QVector2D(mBox[0]->value(), mBox[1]->value());
    }

    QVector2D Vector2DItem::value() const { return QVector2D(mBox[0]->value(), mBox[1]->value()); }

    void Vector2DItem::setValue(QVector2D aValue) {
        mBox[0]->setValue(aValue.x());
        mBox[1]->setValue(aValue.y());
        mStamp = aValue;
    }

    void Vector2DItem::setRange(float aMin, float aMax) {
        mBox[0]->setRange(aMin, aMax);
        mBox[1]->setRange(aMin, aMax);
    }

    void Vector2DItem::onEditingFinished() {
        if (mStamp != value()) {
            if (onValueUpdated && mSignal) {
                onValueUpdated(mStamp, value());
            }
            mStamp = value();
        }
    }

    void Vector2DItem::setItemEnabled(bool aEnable) {
        mSignal = false;
        for (int i = 0; i < 2; ++i) {
            mBox[i]->setEnabled(aEnable);
        }
        mSignal = true;
    }

    void Vector2DItem::setItemVisible(bool aVisible) {
        for (int i = 0; i < 2; ++i) {
            mBox[i]->setVisible(aVisible);
        }
    }

    //-------------------------------------------------------------------------------------------------
    BrowseItem::BrowseItem(QWidget* aParent): mLayout(), mLine(), mButton() {
        mLayout = new QHBoxLayout();
        // mLayout->setSpacing(2);

        mLine = new QLineEdit(aParent);
        // mLine->setEnabled(false);
        mLine->setReadOnly(true);
        mLine->setFocusPolicy(Qt::NoFocus);
        // the line fills the shared field column, the browser button stays fixed
        mLayout->addWidget(mLine, 1);
        mButton = new QPushButton(aParent);
        mButton->connect(mButton, &QPushButton::clicked, [=]() { this->onButtonPushed(); });
        mButton->setObjectName("browser");
        mButton->setFocusPolicy(Qt::NoFocus);
        mButton->setCursor(Qt::PointingHandCursor);
        mLayout->addWidget(mButton);
    }

    void BrowseItem::setValue(const QString& aValue) { mLine->setText(aValue); }

    void BrowseItem::setItemEnabled(bool aEnable) {
        mLine->setEnabled(aEnable);
        mButton->setEnabled(aEnable);
    }

    void BrowseItem::setItemVisible(bool aVisible) {
        mLine->setVisible(aVisible);
        mButton->setVisible(aVisible);
    }

} // namespace prop
} // namespace gui
