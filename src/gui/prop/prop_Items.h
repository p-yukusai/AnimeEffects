#ifndef GUI_PROP_ITEMS_H
#define GUI_PROP_ITEMS_H

#include "gui/GUIResources.h"


#include <functional>
#include <array>
#include <QPushButton>
#include <QAbstractButton>
#include <QPixmap>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QVector2D>
#include <QVector>
#include <QHBoxLayout>
#include <QTreeView>
#include <QCheckBox>
#include <QLineEdit>
#include <QToolButton>
#include <QLabel>
#include "util/Easing.h"
#include "gui/prop/prop_ItemBase.h"

namespace gui {
namespace prop {

    class EasingPickWidget;
    class EasingChip;

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
    // The easing chip: the timeline header's easing control (design section 1),
    // one self-painting button filling that bar's fixed 196px slot. It names the
    // timeline's KEY SELECTION, which is what the picker edits - not one key:
    //
    //   * nothing selected    -> disabled, "Select keys", no glyph;
    //   * one easing for all  -> its glyph, name, range and blend, the same
    //                            strings the Current Keys panel's row showed
    //                            (see chipText);
    //   * several keys        -> the count, in its own slot;
    //   * disagreeing types   -> "Mixed".
    //
    // Every part sits in a FIXED slot (glyph, name, range/blend, count, caret),
    // so a change of selection can never re-flow the bar beside it; a long name
    // elides instead of pushing the count. It paints its own surface (fill,
    // border, radius, hover/pressed/disabled) from the live palette, so it reads
    // the mock's chip in any host without a stylesheet rule of its own.
    class EasingChip: public QAbstractButton {
    public:
        explicit EasingChip(GUIResources* aResources, QWidget* aParent = nullptr);

        // What the chip names: one easing per selected key - empty is the
        // disabled "Select keys" - plus the Auto context of the key the chip
        // SHOWS (the first), so its glyph is the curve that key plays rather than
        // a shape no key has.
        void setValues(const QVector<util::Easing::Param>& aValues,
                       const util::Easing::AutoContext& aAutoContext);
        // WHAT THE SHOWN KEY'S SELECTION COVERS: empty when it names the whole key
        // or nothing, else the component names it spans (a graph-panel selection can
        // name Move X and Move Y without the key). The chip says so in its tooltip -
        // its painted slots stay the mock's - because the one value it paints is
        // then one of several, and a user must be able to tell that opening the
        // picker will edit more than the curve shown.
        void setScopeNote(const QString& aNote);

        // A live edit of the whole selection: the picker edits every key it was
        // opened for, so the chip shows the draft for all of them until the
        // committed easing comes back through setValues (see EasingPickWidget's
        // live-apply split).
        void setDraft(const util::Easing::Param& aDraft);

        // Re-tint the caret pixmap after an appearance change.
        void reloadCaret();

    protected:
        QSize sizeHint() const override;
        void paintEvent(QPaintEvent*) override;

    private:
        GUIResources* mRes;
        QVector<util::Easing::Param> mValues;
        util::Easing::AutoContext mAuto;
        QString mScopeNote;
        QPixmap mCaret;
    };

    //-------------------------------------------------------------------------------------------------
    // The easing control the timeline header hosts: the chip above and the ONE
    // easing picker it opens. It replaced the Current Keys panel's per-group
    // "Easing" row - that row named one key's easing, which is not what an easing
    // describes (it shapes the interval between two keys) - so the picker has one
    // home and one list of easings, reached from the chip and from the timeline's
    // key menu at the pointer.
    //
    // It owns no model: the timeline surface drives it. The surface hands it the
    // chip's state (setSelection), opens it for the keys an edit acts on
    // (openPicker) and receives every committed edit through onEasingApplied.
    class EasingItem {
    public:
        EasingItem(QWidget* aParent, GUIResources* aResources);
        ~EasingItem();

        // The chip, for the host to place in its slot.
        QWidget* itemWidget() { return mChip; }
        // The same widget, typed: the host connects its click.
        EasingChip* chip() { return mChip; }

        void setSelection(const QVector<util::Easing::Param>& aValues,
                          const util::Easing::AutoContext& aAutoContext,
                          const QString& aScopeNote = QString());

        // Re-read the theme's chip ink (the chip paints from the live palette).
        void refreshIcon();

        // THE single popover: opens at aGlobalAnchor - the bottom-left of the
        // chip, or the pointer a key menu was opened at - showing aShown.
        void openPicker(const QPoint& aGlobalAnchor, const QWidget* aAnchorWidget,
                        const util::Easing::Param& aShown,
                        const util::Easing::AutoContext& aAutoContext);
        // Is that popover on screen? The one question a host asks before re-reading
        // the document for it (see refreshShown): closed, there is nothing to keep in
        // step, and the next open loads the state itself.
        bool isPickerOpen() const;
        // THE DOCUMENT CAME BACK TO THE POPUP: re-read what it shows. The host calls
        // this from the notification it already receives (the shared selection's own
        // onChanged and the project's timeline modification), so an edit made in the
        // graph - a handle drag, a mode change, another key being selected - is shown
        // by the open picker too, not only the other way round. The picker IGNORES it
        // while a gesture is in progress in the popup (a drag there wins until it
        // ends) and while the popup is closed; TRUE when it adopted the value.
        bool refreshShown(const util::Easing::Param& aShown,
                          const util::Easing::AutoContext& aAutoContext);

        // Every edit the picker commits, in the editor's own currency. The host
        // (the timeline surface, which knows the keys the open acts on) is the
        // only writer: the control itself never touches the document.
        std::function<void(util::Easing::Param)> onEasingApplied;
        // A CONTINUOUS gesture the picker STREAMS (a bezier handle drag, a typed
        // spin value): the host opens ONE rewriteable command on the begin, applies
        // each step through it (so the document/graph follow the pointer), and
        // closes it on the end - one undo entry for the whole gesture. The control
        // still owns no model; these are the picker's live signals forwarded.
        std::function<void()> onEasingLiveBegin;
        std::function<void(util::Easing::Param)> onEasingLiveStep;
        std::function<void()> onEasingLiveEnd;
        // The popover went away (its close button, an outside click, Escape). The
        // host uses this to end a right-click's temporary selection.
        std::function<void()> onPickerClosed;

    private:
        void onDraftChanged(const util::Easing::Param& aDraft);

        EasingChip* mChip;
        EasingPickWidget* mPicker;
        GUIResources* mGUIResources;
        util::SlotId mThemeSlot;
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
