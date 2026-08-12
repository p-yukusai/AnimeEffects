#ifndef GUI_PROP_ITEMS_H
#define GUI_PROP_ITEMS_H

#include "gui/GUIResources.h"


#include <functional>
#include <array>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QVector2D>
#include <QHBoxLayout>
#include <QTreeView>
#include <QCheckBox>
#include <QLineEdit>
#include "util/Easing.h"
#include "gui/prop/prop_ItemBase.h"

namespace gui {
namespace prop {

    template<typename SpinBoxType>
    void stepByHelper(SpinBoxType* spinBox, int steps);

    //-------------------------------------------------------------------------------------------------
    class SpinBox: public QSpinBox {
        Q_OBJECT

    public:
        SpinBox(QWidget* parent = nullptr);

        virtual void stepBy(int steps) override;

        bool mTriggeredByArrows;
    };

    //-------------------------------------------------------------------------------------------------
    class DoubleSpinBox: public QDoubleSpinBox {
        Q_OBJECT

    public:
        DoubleSpinBox(QWidget* parent = nullptr);

        virtual void stepBy(int steps) override;
        // display "12.5" / "120" instead of "12.500" / "120.00" (decimals
        // remains the precision cap)
        virtual QString textFromValue(double aValue) const override;

        bool mTriggeredByArrows;
    };

    //-------------------------------------------------------------------------------------------------
    class CheckItem: public ItemBase {
    public:
        typedef std::function<void(bool)> UpdateType;

        CheckItem(QWidget* aParent);

        QCheckBox& box() { return *mBox; }
        const QCheckBox& box() const { return *mBox; }

        bool value() const { return mBox->isChecked(); }
        void setValue(bool aValue, bool aSignal);

        virtual QWidget* itemWidget() { return mBox; }
        virtual void setItemEnabled(bool aEnable);
        virtual void setItemVisible(bool aVisible);

        UpdateType onValueUpdated;

    private:
        void onEditingFinished();

        QCheckBox* mBox;
        bool mStamp;
        bool mSignal;
        bool mIsEnable;
    };

    //-------------------------------------------------------------------------------------------------
    class ComboItem: public ItemBase {
    public:
        typedef std::function<void(int, int)> UpdateType;

        ComboItem(QWidget* aParent);

        QComboBox& box() { return *mBox; }
        const QComboBox& box() const { return *mBox; }

        int value() const { return mBox->currentIndex(); }
        void setValue(int aValue, bool aSignal);

        virtual QWidget* itemWidget() { return mBox; }
        virtual void setItemEnabled(bool aEnable);
        virtual void setItemVisible(bool aVisible);

        UpdateType onValueUpdated;

    private:
        void onEditingFinished();

        QComboBox* mBox;
        int mStamp;
        bool mSignal;
    };

    //-------------------------------------------------------------------------------------------------
    class Combo2DItem: public ItemBase {
    public:
        typedef std::function<void(QPoint, QPoint)> UpdateType;

        Combo2DItem(QWidget* aParent);

        QComboBox& box(int aIndex) { return *mBox.at(aIndex); }
        const QComboBox& box(int aIndex) const { return *mBox.at(aIndex); }

        QPoint value() const;
        void setValue(QPoint aValue, bool aSignal);

        virtual QLayout* itemLayout() { return mLayout; }
        virtual void setItemEnabled(bool aEnable);
        virtual void setItemVisible(bool aVisible);

        UpdateType onValueUpdated;

    private:
        void onEditingFinished();

        QHBoxLayout* mLayout;
        std::array<QComboBox*, 2> mBox;
        QPoint mStamp;
        bool mSignal;
    };

    //-------------------------------------------------------------------------------------------------
    class EasingItem: public ItemBase {
    public:
        typedef std::function<void(util::Easing::Param, util::Easing::Param)> UpdateType;

        EasingItem(QWidget* aParent, const GUIResources* mGUIResources);

        QComboBox& comboBox(int aIndex) { return *mBox.at(aIndex); }
        const QComboBox& comboBox(int aIndex) const { return *mBox.at(aIndex); }

        util::Easing::Param value() const;
        void setValue(util::Easing::Param aValue, bool aSignal);

        virtual QLayout* itemLayout() { return mLayout; }
        virtual void setItemEnabled(bool aEnable);
        virtual void setItemVisible(bool aVisible);

        UpdateType onValueUpdated;

    private:
        void onEditingFinished();

        QGridLayout* mLayout;
        std::array<QComboBox*, 2> mBox;
        util::Easing::CubicBezier mCubicBezier;
        DoubleSpinBox* mDBox;
        util::Easing::Param mStamp;
        QPushButton* mCustomEasing;
        bool mSignal;
    };

    //-------------------------------------------------------------------------------------------------
    class IntegerItem: public ItemBase {
    public:
        typedef std::function<void(int, int)> UpdateType;

        IntegerItem(QWidget* aParent);

        SpinBox& box() { return *mBox; }
        const SpinBox& box() const { return *mBox; }

        int value() const { return mBox->value(); }
        void setValue(int aValue) {
            mBox->setValue(aValue);
            mStamp = aValue;
        }
        void setRange(int aMin, int aMax);

        virtual QWidget* itemWidget() { return mBox; }
        virtual void setItemEnabled(bool aEnable);
        virtual void setItemVisible(bool aVisible);

        UpdateType onValueUpdated;

    private:
        void onEditingFinished();

        SpinBox* mBox;
        int mStamp;
        bool mSignal;
    };

    //-------------------------------------------------------------------------------------------------
    class DecimalItem: public ItemBase {
    public:
        typedef std::function<void(double, double)> UpdateType;

        DecimalItem(QWidget* aParent);

        DoubleSpinBox& box() { return *mBox; }
        const DoubleSpinBox& box() const { return *mBox; }

        double value() const { return mBox->value(); }
        void setValue(double aValue) {
            mBox->setValue(aValue);
            mStamp = aValue;
        }
        void setRange(double aMin, double aMax);

        virtual QWidget* itemWidget() { return mBox; }
        virtual void setItemEnabled(bool aEnable);
        virtual void setItemVisible(bool aVisible);

        UpdateType onValueUpdated;

    private:
        void onEditingFinished();

        DoubleSpinBox* mBox;
        double mStamp;
        bool mSignal;
    };

    //-------------------------------------------------------------------------------------------------
    class Vector2DItem: public ItemBase {
    public:
        typedef std::function<void(QVector2D, QVector2D)> UpdateType;

        Vector2DItem(QWidget* aParent);

        QVector2D value() const;
        void setValue(QVector2D aValue);
        void setRange(float aMin, float aMax);

        virtual QLayout* itemLayout() { return mLayout; }
        virtual void setItemEnabled(bool aEnable);
        virtual void setItemVisible(bool aVisible);

        UpdateType onValueUpdated;

    private:
        void onEditingFinished();

        QHBoxLayout* mLayout;
        DoubleSpinBox* mBox[2];
        QVector2D mStamp;
        bool mSignal;
    };

    //-------------------------------------------------------------------------------------------------
    class BrowseItem: public ItemBase {
    public:
        typedef std::function<void()> UpdateType;

        BrowseItem(QWidget* aParent);

        QString value() const { return mLine->text(); }
        void setValue(const QString& aValue);

        virtual QLayout* itemLayout() { return mLayout; }
        virtual void setItemEnabled(bool aEnable);
        virtual void setItemVisible(bool aVisible);

        UpdateType onButtonPushed;

    private:
        QHBoxLayout* mLayout;
        QLineEdit* mLine;
        QPushButton* mButton;
    };

} // namespace prop
} // namespace gui

#endif // GUI_PROP_ITEMS_H
