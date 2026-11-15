#include <QDebug>
#include <QCursor>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QFontMetrics>
#include <QAbstractButton>
#include <QHeaderView>
#include <QStandardItem>
#include <QApplication>
#include <cstdio>
#include "util/SelectArgs.h"
#include "cmnd/ScopedMacro.h"
#include "gui/prop/prop_Items.h"
#include "gui/prop/EasingPickWidget.h"
#include "GUIResources.h"
#include "util/Easing.h"
#include "gui/theme/Colors.h"
#include <QDialog>
#include <QToolButton>

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

    // The chip's label: the application's ONE easing vocabulary, util::Easing's own
    // translated names. The mock's LABEL table gave the four primaries a second set of
    // names ("Hold", "Step", "Ease"), which meant the same easing was called two things
    // inside one application - the settings list and the picker said Floor/Round/Cubic
    // while the chip beside them said Hold/Step/Ease, and the four quick easings now in
    // the header had to pick one. They pick these.
    QString chipBaseLabel(const util::Easing::Param& aValue) {
        // The Param's user-facing name, not the type's: a Custom curve is named
        // by its two handle modes, so the Auto preset reads "Auto Clamped"
        // rather than the interpolation type "Custom".
        return util::Easing::getParamName(aValue);
    }

    QString chipRangeLabel(util::Easing::Range aRange) {
        return aRange == util::Easing::Range_InOut ? QString("In\u2013Out")
               : util::Easing::getRangeName(aRange);
    }

    // The chip's two text runs derived from a value: the name holds the type
    // (+ range for directional curves), the delta holds the blend percentage
    // only where a weight actually blends the curve with linear.
    void chipText(const util::Easing::Param& aValue, QString& aOutName, QString& aOutDelta) {
        aOutName = chipBaseLabel(aValue);
        const bool dirShow =
            util::Easing::Type_Sine <= aValue.type && aValue.type < util::Easing::Type_Custom;
        if (dirShow) aOutName += " " + chipRangeLabel(aValue.range);
        aOutDelta.clear();
        const bool blendShow = dirShow && aValue.weight < 1.0f;
        if (blendShow) aOutDelta = QString::number(qRound(aValue.weight * 100.0f)) + "%";
    }

    // The chip's geometry: the mock's own numbers (mock.css .ease-chip). A 24px
    // chip in the header's 40px bar, 4px corners, a 1px edge, 6px between every
    // slot, and the two muted slots' fixed widths (38 / 46) - the fixed slots are
    // the whole point: the selection's name and count change, and the bar beside
    // the chip must not.
    const int kHeight = 24;
    const int kRadius = 4;
    const int kBorder = 1;
    const int kPadL = 6;
    const int kPadR = 7;
    const int kGap = 6;
    const int kGlyph = 18;
    const int kCaret = 12;
    const int kRangeW = 38;
    const int kCountW = 46;
    const int kNameFontSize = 12;
    const int kMetaFontSize = 11;
    constexpr qreal kStroke = 1.5;
    constexpr qreal kDisabledOpacity = 0.45;

    // The chip is handed its state on every repaint of the timeline surface (see
    // TimeLineEditorWidget::pushEasingChipState), so an unchanged state must cost
    // nothing: a compare, not a repaint. The compare is the WHOLE AutoContext
    // (util::Easing::AutoContext::operator==): the arriving side of the shown key
    // belongs to the NEXT key, so a change to that key's In mode changes the glyph
    // even when both neighbours stand still.

    // -------------------------------------------------------------------------
    // EasingChip — the selection's easing as ONE self-painting button, filling
    // the timeline header's fixed 196px slot (design section 1). It replaced the
    // per-key row the Current Keys panel carried: a row named one key's easing,
    // which is not what an easing describes, and the header names the selection's
    // instead - so the one control that opens the picker is also the one that
    // says what it will edit.
    //
    // Every part sits in a FIXED slot (the mock's .ease-chip): glyph, name
    // (elastic, elided), range/blend, count, caret. Only the name gives up space,
    // so a longer easing name or a count cannot re-flow the bar beside the chip.
    // The surface is painted here too (fill/border/radius + hover/pressed/
    // disabled), so the control carries its whole look into any host and needs no
    // stylesheet rule of its own - the mock's own values, from the live palette.
    // -------------------------------------------------------------------------
    EasingChip::EasingChip(GUIResources* aRes, QWidget* aParent):
        QAbstractButton(aParent), mRes(aRes), mValues(), mAuto(), mCaret() {
        setObjectName("easingChip");
        setCursor(Qt::PointingHandCursor);
        setToolTip(QCoreApplication::tr("Easing"));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(kHeight);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover);
        reloadCaret();
        // "Select keys" until a selection says otherwise: the chip is the
        // picker's only entry point, and it must not open on nothing.
        setEnabled(false);
    }

    void EasingChip::setValues(const QVector<util::Easing::Param>& aValues,
                               const util::Easing::AutoContext& aAutoContext) {
        if (mValues == aValues && mAuto == aAutoContext)
            return;
        mValues = aValues;
        mAuto = aAutoContext;
        setEnabled(!mValues.isEmpty());
        update();
    }

    void EasingChip::setScopeNote(const QString& aNote) {
        if (mScopeNote == aNote)
            return;
        mScopeNote = aNote;
        setToolTip(aNote.isEmpty() ? QCoreApplication::tr("Easing")
                                   : QCoreApplication::tr("Easing \u2014 %1").arg(aNote));
    }

    void EasingChip::setDraft(const util::Easing::Param& aDraft) {
        for (util::Easing::Param& value : mValues)
            value = aDraft;
        update();
    }

    void EasingChip::reloadCaret() {
        mCaret = (mRes ? mRes->icon("caret-down").pixmap(kCaret, kCaret) : QPixmap());
        update();
    }

    QSize EasingChip::sizeHint() const {
        // The header's slot is the fixed width (TimeLineHeader's kEaseSlotWidth);
        // this is the same box the mock's .ease-chip carries, so the two agree
        // without the chip knowing where it is hosted.
        return QSize(196, kHeight);
    }

    void EasingChip::paintEvent(QPaintEvent*) {
        const theme::Colors& c = theme::Colors::current();
        const bool enabled = isEnabled();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        // The surface: the mock's .ease-chip. Its rest is the recessed surface
        // with a hairline edge, its hover raises the fill and lights the edge with
        // the outline token, and a press is the active fill - the same three steps
        // the QSS rule this chip used to read spelled.
        QColor fill = c.recessed;
        QColor edge = c.hairline;
        if (enabled && isDown()) {
            fill = c.active;
            edge = c.active;
        } else if (enabled && underMouse()) {
            fill = c.raised;
            edge = c.outline;
        }
        QPainterPath surface;
        surface.addRoundedRect(QRectF(0.5, 0.5, width() - 1.0, height() - 1.0), kRadius, kRadius);
        p.fillPath(surface, fill);
        p.setPen(QPen(edge, 1.0));
        p.drawPath(surface);

        QString name;
        QString range;
        QString count;
        if (mValues.isEmpty()) {
            name = QCoreApplication::tr("Select keys");
        } else {
            const util::Easing::Param& first = mValues.first();
            // "The same easing" is the same user-facing NAME, which for a Custom
            // curve includes its two handle modes: two Custom keys have no name
            // in common merely because their TYPE matches. A mixed selection
            // says so instead of naming one key's shape.
            const QString firstName = chipBaseLabel(first);
            bool agree = true;
            for (const util::Easing::Param& value : mValues) {
                if (chipBaseLabel(value) != firstName) {
                    agree = false;
                    break;
                }
            }
            if (agree) {
                chipText(first, name, range);
            } else {
                name = QCoreApplication::tr("Mixed");
            }
            if (mValues.size() > 1)
                count = QCoreApplication::tr("%n key(s)", nullptr, mValues.size());
        }

        QFont nameFont = font();
        nameFont.setPixelSize(kNameFontSize);
        nameFont.setWeight(QFont::Medium);
        const QFontMetrics nameMetrics(nameFont);
        QFont metaFont = font();
        metaFont.setPixelSize(kMetaFontSize);
        const QFontMetrics metaMetrics(metaFont);

        // The slots, right to left: the caret is always there (it is the affordance),
        // then the count, then the range/blend - each ONLY when it carries something -
        // and the name takes what is left after the glyph. The chip's EXTERIOR is what
        // is fixed (the header's 196px slot owns it, so the bar beside the chip never
        // moves); its interior adapts, because reserving empty boxes for a state that
        // has no glyph, no range/blend and no count was what squeezed the disabled
        // "Select keys" - 66.9px of text - into the 43px left over.
        const bool showGlyph = !mValues.isEmpty();
        const bool showRange = !range.isEmpty();
        const bool showCount = !count.isEmpty();
        // The count is the state's identity, so its slot keeps the mock's 46px and
        // grows only if a long count would not fit it (the name elides instead, which
        // is the trade the design asks for).
        const int countW = showCount
                               ? qMax(kCountW, metaMetrics.horizontalAdvance(count))
                               : 0;
        int slotRight = width() - kBorder - kPadR - kCaret;
        int countX = slotRight;
        if (showCount) {
            slotRight -= kGap + countW;
            countX = slotRight;
        }
        int rangeX = slotRight;
        if (showRange) {
            slotRight -= kGap + kRangeW;
            rangeX = slotRight;
        }
        int nameLeft = kBorder + kPadL;
        if (showGlyph)
            nameLeft += kGlyph + kGap;
        const QRect nameRect(nameLeft, 0, qMax(0, slotRight - kGap - nameLeft), height());

        // The glyph is the shown key's easing, shaped by its own neighbours (see
        // EasingChip::setValues), so an Auto selection draws the curve the
        // document plays rather than a shape no key has.
        if (showGlyph) {
            const int glyphY = (height() - kGlyph) / 2;
            // The preset opts into the handle icon: with no neighbours its real
            // curve IS the straight line, which cannot be told from Linear's -
            // the glyph then draws the canonical automatic pair instead (see
            // paintEasingCurve). Every other easing is unaffected by the flag.
            const util::Easing::Param& shown = mValues.first();
            const bool derivedSides = shown.type == util::Easing::Type_Custom
                && (util::Easing::isDerived(shown.handleOut)
                    || util::Easing::isDerived(mAuto.inMode));
            paintEasingCurve(p, QRectF(kBorder + kPadL, glyphY, kGlyph, kGlyph), shown,
                             enabled ? c.accentBright : c.textDisabled, kStroke, &mAuto,
                             derivedSides);
        }

        if (!mCaret.isNull()) {
            // The caret pixmap is tinted for the active theme only, so a disabled
            // chip fades it rather than re-loading a second tint of the same glyph.
            if (!enabled)
                p.setOpacity(kDisabledOpacity);
            const int caretX = width() - kBorder - kPadR - kCaret;
            p.drawPixmap(caretX, (height() - kCaret) / 2, mCaret);
            p.setOpacity(1.0);
        }

        p.setFont(nameFont);
        p.setPen(enabled ? c.text : c.textDisabled);
        p.drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft,
                   nameMetrics.elidedText(name, Qt::ElideRight, nameRect.width()));

        // The range/blend and the count: the mock's .erange / .eid, one step
        // smaller and muted. The app font's figures are tabular (see
        // TimeLineHeader), so a changing count cannot shift the two slots.
        p.setFont(metaFont);
        p.setPen(enabled ? c.textMuted : c.textDisabled);
        if (showRange) {
            const QRect rangeRect(rangeX, 0, kRangeW, height());
            p.drawText(rangeRect, Qt::AlignVCenter | Qt::AlignRight,
                       metaMetrics.elidedText(range, Qt::ElideRight, kRangeW));
        }
        if (showCount) {
            const QRect countRect(countX, 0, countW, height());
            p.drawText(countRect, Qt::AlignVCenter | Qt::AlignRight, count);
        }
    }

    //-------------------------------------------------------------------------------------------------
    EasingItem::EasingItem(QWidget* aParent, GUIResources* aResources):
        mChip(), mPicker(), mGUIResources(aResources), mThemeSlot() {
        // The chip, for the host to place (the header's 196px easing slot).
        // Its click belongs to the host: it opens the picker for the keys the
        // chip names, which only the timeline knows (see EasingItem::openPicker).
        mChip = new EasingChip(mGUIResources, aParent);

        // The picker popover: a floating Qt::Popup (top-level native window).
        // Rounded corners need a translucent backing — on X11/Wayland the
        // compositor composites it, on Windows the frameless flag makes Qt
        // layer the window so the alpha is honoured, and the popup is shaped
        // either by DWM (11 22H2+) or by the paintEvent mask. The widget itself
        // sets the flags in its ctor (see EasingPickWidget), so setting them
        // here too is a no-op.
        mPicker = new EasingPickWidget(util::Easing::Param());
        mPicker->setWindowFlags(mPicker->windowFlags() | Qt::Popup | Qt::FramelessWindowHint);
        mPicker->setAttribute(Qt::WA_TranslucentBackground, true);
        mPicker->hide();

        // EasingItem is not a QObject, so use the functor form (connection is
        // tied to the sender's lifetime, and mPicker is deleted in ~EasingItem).
        //
        // Live-apply split (see EasingPickWidget):
        //   changed  — the draft changed live; re-render the chip so it tracks
        //              the picker as it is edited. Does not touch the model.
        //   applied  — a gesture committed; hand it to the host, which owns the
        //              keys an open acts on and writes them.
        //   dismissed— the user closed the popup (✕); just hide it (the popup is
        //              a Qt::Popup, so an outside click hides it on its own).
        //   closed   — the popup went away (✕ or an outside click); the host
        //              ends the right-click's temporary selection on it.
        mPicker->connect(mPicker, &EasingPickWidget::changed,
                         [this](util::Easing::Param aLive) { onDraftChanged(aLive); });
        mPicker->connect(mPicker, &EasingPickWidget::applied,
                         [this](util::Easing::Param aNext) {
                             if (this->onEasingApplied)
                                 this->onEasingApplied(aNext);
                         });
        // A streamed gesture (bezier drag / typed spin): forwarded to the host,
        // which holds the ONE command open across it. The control stays a view.
        mPicker->connect(mPicker, &EasingPickWidget::liveEditStarted, [this] {
            if (this->onEasingLiveBegin)
                this->onEasingLiveBegin();
        });
        mPicker->connect(mPicker, &EasingPickWidget::liveEdited,
                         [this](util::Easing::Param aStep) {
                             if (this->onEasingLiveStep)
                                 this->onEasingLiveStep(aStep);
                         });
        mPicker->connect(mPicker, &EasingPickWidget::liveEditEnded, [this] {
            if (this->onEasingLiveEnd)
                this->onEasingLiveEnd();
        });
        mPicker->connect(mPicker, &EasingPickWidget::dismissed, [this] {
            this->mPicker->hide();
        });
        mPicker->connect(mPicker, &EasingPickWidget::closed, [this] {
            if (this->onPickerClosed)
                this->onPickerClosed();
        });

        mThemeSlot = mGUIResources->onThemeChanged.connect([this](theme::Theme&) {
            refreshIcon();
        });
        mChip->setValues(QVector<util::Easing::Param>(), util::Easing::AutoContext());
    }

    EasingItem::~EasingItem() {
        // The host outlives this control, but the GUIResources signer outlives the
        // whole dock; drop the slot so a later theme change never reaches a
        // destroyed item.
        if (mGUIResources) {
            mGUIResources->onThemeChanged.disconnect(mThemeSlot);
        }
        delete mPicker; // top-level popup: not parented, owned here
    }

    void EasingItem::refreshIcon() {
        // The chip paints everything from theme::Colors::current() at render
        // time, so only the caret pixmap (tinted by the icon pipeline) needs a
        // re-load for the new appearance; the glyph/text pick up the theme on
        // repaint.
        mChip->reloadCaret();
    }

    void EasingItem::setSelection(const QVector<util::Easing::Param>& aValues,
                                  const util::Easing::AutoContext& aAutoContext,
                                  const QString& aScopeNote) {
        mChip->setValues(aValues, aAutoContext);
        mChip->setScopeNote(aScopeNote);
    }

    void EasingItem::onDraftChanged(const util::Easing::Param& aDraft) {
        // Live: the picker edits every key it was opened for, so the chip shows
        // the draft for all of them while the edit is in flight. A streamed gesture
        // (bezier drag / typed spin) is already writing the model per step, and a
        // discrete edit writes it as it lands; the truth arrives back as the host's
        // next setSelection - so nothing is cached here, and the draft only covers
        // the popup's own rendering until then.
        mChip->setDraft(aDraft);
    }

    void EasingItem::openPicker(const QPoint& aGlobalAnchor, const QWidget* aAnchorWidget,
                                const util::Easing::Param& aShown,
                                const util::Easing::AutoContext& aAutoContext) {
        // The one place the popover is opened, whatever opened it. EasingPickWidget
        // owns the placement (the 0-opacity realize dance, the screen clamp and the
        // anchor it re-asserts on every refit) and needs the anchor's widget only
        // for its screen and its transient parent.
        mPicker->popUpAt(aGlobalAnchor, aAnchorWidget, aShown, aAutoContext);
    }

    bool EasingItem::isPickerOpen() const { return mPicker && mPicker->isVisible(); }

    bool EasingItem::refreshShown(const util::Easing::Param& aShown,
                                  const util::Easing::AutoContext& aAutoContext) {
        // A view, not a model: the picker decides what an external re-read may replace
        // (nothing while a gesture is in progress in it - see
        // EasingPickWidget::setShownValue). The control stays a pass-through.
        return mPicker && mPicker->setShownValue(aShown, aAutoContext);
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
