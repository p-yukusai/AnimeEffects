#include <QDebug>
#include <QCursor>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QFontMetrics>
#include <QAbstractButton>
#include <QTimer>
#include <QHeaderView>
#include <QStandardItem>
#include <QApplication>
#include <QScreen>
#include <QGuiApplication>
#include <QWindow>
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

    // The mock's chip labels (mock.js LABEL): the four primaries wear the
    // user vocabulary, the rest keep the engine name; directional curves add
    // the range with the mock's en-dash "In–Out".
    QString chipBaseLabel(util::Easing::Type aType) {
        switch (aType) {
        case util::Easing::Type_Floor: return QString("Hold");
        case util::Easing::Type_Round: return QString("Step");
        case util::Easing::Type_Cubic: return QString("Ease");

        default: return util::Easing::getTypeName(aType);
        }
    }

    QString chipRangeLabel(util::Easing::Range aRange) {
        return aRange == util::Easing::Range_InOut ? QString("In\u2013Out")
               : util::Easing::getRangeName(aRange);
    }

    // The chip's two text runs derived from a value: the name holds the type
    // (+ range for directional curves), the delta holds the blend percentage
    // only where a weight actually blends the curve with linear.
    void chipText(const util::Easing::Param& aValue, QString& aOutName, QString& aOutDelta) {
        aOutName = chipBaseLabel(aValue.type);
        const bool dirShow =
            util::Easing::Type_Sine <= aValue.type && aValue.type < util::Easing::Type_Custom;
        if (dirShow) aOutName += " " + chipRangeLabel(aValue.range);
        aOutDelta.clear();
        const bool blendShow = dirShow && aValue.weight < 1.0f;
        if (blendShow) aOutDelta = QString::number(qRound(aValue.weight * 100.0f)) + "%";
    }

    // -------------------------------------------------------------------------
    // EasingChip — the key-easing summary as ONE self-painting button. The old
    // chip was a QToolButton used as a container holding four child widgets
    // (a CurveGlyph, two QLabels, and a hand-painted caret) over the QSS
    // surface, which split the same control's look across QSS, a raw-painted
    // caret, and a per-render glyph-color setter. This paints the glyph, name,
    // delta, and a theme-tinted caret in a single paintEvent; the surface
    // (fill/border/radius + hover/pressed/disabled) still comes from the
    // `#easingChip` QSS rule (the stylesheet engine paints it because the
    // widget sets WA_StyledBackground).
    // -------------------------------------------------------------------------
    class EasingChip : public QAbstractButton {
    public:
        explicit EasingChip(GUIResources* aRes, QWidget* aParent = nullptr)
            : QAbstractButton(aParent), mRes(aRes) {
            setObjectName("easingChip");
            setCursor(Qt::PointingHandCursor);
            setToolTip(QCoreApplication::tr("Easing"));
            setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            setFixedHeight(kHeight);
            setFocusPolicy(Qt::NoFocus);
            setAttribute(Qt::WA_Hover);
            setAttribute(Qt::WA_StyledBackground); // let QSS paint the surface
            reloadCaret();
        }

        void setParam(const util::Easing::Param& aValue) {
            if (mValue == aValue) return;
            mValue = aValue;
            update();
        }

        // Re-tint the caret pixmap after an appearance change.
        void reloadCaret() {
            mCaret = (mRes ? mRes->icon("caret-down").pixmap(kCaret, kCaret) : QPixmap());
            update();
        }

    protected:
        QSize sizeHint() const override { return QSize(120, kHeight); }

        void paintEvent(QPaintEvent*) override {
            const theme::Colors& c = theme::Colors::current();
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing, true);

            // Curve glyph, left; caret, right; name (+ delta) between.
            const int gy = (height() - kGlyph) / 2;
            paintEasingCurve(p, QRectF(kMarginL, gy, kGlyph, kGlyph), mValue, c.accentBright, kStroke);

            const int caretX = width() - kMarginR - kCaret;
            const int caretY = (height() - kCaret) / 2;
            if (!mCaret.isNull()) p.drawPixmap(caretX, caretY, mCaret);

            QString name, delta;
            chipText(mValue, name, delta);
            QFontMetrics fm(font());
            // Reserve the delta's span (right-aligned before the caret) so a
            // long name is elided instead of drawing over it.
            const int textLeft = kMarginL + kGlyph + kGap;
            const int deltaW = delta.isEmpty() ? 0 : fm.horizontalAdvance(delta) + kGap;
            QRect nameRect(textLeft, 0, qMax(0, caretX - kGap - deltaW - textLeft), height());
            p.setPen(c.text);
            p.drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft,
                       fm.elidedText(name, Qt::ElideRight, nameRect.width()));
            if (!delta.isEmpty()) {
                const int dw = fm.horizontalAdvance(delta);
                QRect deltaRect(caretX - kGap - dw, 0, dw, height());
                p.setPen(c.textMuted);
                p.drawText(deltaRect, Qt::AlignVCenter | Qt::AlignLeft, delta);
            }
        }

    private:
        static const int kMarginL = 6, kMarginR = 8, kGap = 8;
        static const int kGlyph = 20, kCaret = 10, kHeight = 24;
        static constexpr qreal kStroke = 1.5;

        GUIResources* mRes;
        util::Easing::Param mValue;
        QPixmap mCaret;
    };

    //-------------------------------------------------------------------------------------------------
    EasingItem::EasingItem(QWidget* aParent, GUIResources* mGUIResources):
        mChip(), mPicker(), mStamp(), mGUIResources(mGUIResources) {
        // A chip (glyph + name + delta + caret) that opens the shape-first
        // picker popover. Exposed as itemWidget() so the KeyGroup row caps it
        // at 70% of the field column like the sibling combo/integer rows (a
        // full-column itemLayout avoided that cap and read too wide). Height
        // matches the field-row standard: the combo/spin siblings settle at
        // 24px (propertywidget.ssa), so 30px was a row taller than its peers.
        mChip = new EasingChip(mGUIResources, aParent);

        // The picker popover: a floating Qt::Popup (top-level native window).
        // Rounded corners need a translucent backing — on X11/Wayland the
        // compositor composites it; on Windows the masked rounded paint in
        // paintEvent clips the corner cut. The widget itself sets the flags in
        // its ctor (see EasingPickWidget), so setting them here too is a no-op.
        mPicker = new EasingPickWidget(mStamp);
        mPicker->setWindowFlags(mPicker->windowFlags() | Qt::Popup | Qt::FramelessWindowHint);
        mPicker->setAttribute(Qt::WA_TranslucentBackground, true);
        mPicker->hide();

        mChip->connect(mChip, &QAbstractButton::clicked, [this] { openPopover(); });
        // EasingItem is not a QObject, so use the functor form (connection is
        // tied to the sender's lifetime, and mPicker is deleted in ~EasingItem).
        //
        // Live-apply split (see EasingPickWidget):
        //   changed  — the draft changed live; re-render the chip so it tracks
        //              the menu as it is edited. Does not touch the model.
        //   applied  — a gesture committed to the model; forward to the model
        //              (onValueUpdated) and store the committed value in mStamp.
        //   dismissed— user closed the menu (✕); just hide it (the popup is a
        //              Qt::Popup, so an outside click hides it on its own).
        mPicker->connect(mPicker, &EasingPickWidget::changed,
                         [this](util::Easing::Param aLive) { updateChip(aLive); });
        mPicker->connect(mPicker, &EasingPickWidget::applied,
                         [this](util::Easing::Param aNext) {
                             if (this->onValueUpdated)
                                 this->onValueUpdated(this->mStamp, aNext);
                             this->mStamp = aNext;
                             this->updateChip(this->mStamp);
                         });
        mPicker->connect(mPicker, &EasingPickWidget::dismissed, [this] {
            this->mPicker->hide();
        });

        mThemeSlot = mGUIResources->onThemeChanged.connect([this](theme::Theme&) {
            refreshIcon();
        });
        updateChip();
    }

    EasingItem::~EasingItem() {
        // The item outlives its widgets in the KeyGroup teardown, but the
        // GUIResources signer outlives the whole dock; drop the slot so a
        // later theme change never reaches a destroyed item.
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
        updateChip();
    }

    void EasingItem::updateChip() {
        updateChip(mStamp);
    }

    void EasingItem::updateChip(const util::Easing::Param& aValue) {
        // The chip renders itself from a value; this is how it tracks the menu
        // live. The picker emits changed(v) on every edit and we pass the live
        // v here (not mStamp, which only advances on a commit).
        mChip->setParam(aValue);
    }

    void EasingItem::openPopover() {
        // 0-opacity first-frame trick: the Popup's live height is DPI-correct
        // only after its native window is realized (a hidden popup's
        // heightForWidth/sizeHint is stale — hidden 477 vs live 488 on this
        // host; user saw 407→489). Show the tall Custom at opacity 0 for one
        // frame so Qt sizes the native window live, then flip back to the
        // requested mode at opacity 1. At that point liveCustomHeight() ==
        // geometry().height() is authoritative — use it to reserve Custom
        // height once, nudge up if it would clip, and let Custom grow down in
        // place. (The popup opens flush at chip.bottom, gap 0.)
        const util::Easing::Param requested = mStamp;
        const bool wasCustom = requested.type == util::Easing::Type_Custom;
        if (!wasCustom) {
            util::Easing::Param c = requested;
            c.type = util::Easing::Type_Custom;
            mPicker->setValue(c);
        } else {
            mPicker->setValue(requested);
        }
        const QPoint anchor = mChip->mapToGlobal(QPoint(0, mChip->height()));
        const QScreen* scr = mChip->screen();
        if (!scr) scr = QGuiApplication::primaryScreen();
        const QRect avail = scr ? scr->availableGeometry() : QRect();
        int left = anchor.x();
        if (!avail.isNull()) {
            const int w = mPicker->width() > 0 ? mPicker->width() : 300;
            if (left + w > avail.right() + 1) left = avail.right() + 1 - w;
            if (left < avail.left()) left = avail.left();
        }
        // Realize the native window at the tall Custom height (0 opacity).
        mPicker->setWindowOpacity(0.0);
        // Treat the popup as a transient child of the app window. A popup with
        // no transient parent is left to the WM/compositor, which will nudge a
        // growing popup to keep it on-screen — that is the "menu nudges to the
        // right on mode switch" drift (the chip sits near the right screen
        // edge, so a rightward slide is the closest on-screen adjustment). On
        // X11 the popup is still override-redirect (unmanaged, so this is a
        // no-op there); on Wayland it stops the compositor relocating it.
        mPicker->winId();
        if (auto* ph = mPicker->windowHandle()) {
            if (auto* top = mChip->window()) {
                if (auto* wh = top->windowHandle())
                    ph->setTransientParent(wh);
            }
        }
        mPicker->move(left, anchor.y());
        mPicker->show();
        mPicker->raise();
        // Two ticks: the live geometry is realized by then. We are STILL in
        // Custom here, so reserve the Custom (tallest) height — reading it
        // after flipping back to the requested mode gave the normal height and
        // let the tall Custom panel clip off the bottom of the screen.
        QTimer::singleShot(0, mPicker, [this, requested, wasCustom, avail, left, anchor]() {
            if (!mPicker->isVisible()) return;
            QTimer::singleShot(0, mPicker, [this, requested, wasCustom, avail, left, anchor]() {
                if (!mPicker->isVisible()) return;
                // Reserve the tallest (Custom) height so the popup never needs
                // to be relocated when switching to Custom. The height is a
                // KNOWN constant, not a measured one — the previous measurement
                // (liveCustomHeight / heightForWidth / sizeHint) oscillated
                // 476/478/510 and was nowhere near the real ~489. Use the
                // deterministic constant and let the popup grow down in place.
                const int reserveH = EasingPickWidget::kCustomHeight;
                int top = anchor.y();
                if (!avail.isNull() && reserveH > 0) {
                    if (top + reserveH > avail.bottom() + 1)
                        top = qMax(avail.top(), avail.bottom() + 1 - reserveH);
                }
                // Now flip back to the requested mode (sizes the normal panel).
                if (!wasCustom) mPicker->setValue(requested);
                QTimer::singleShot(0, mPicker, [this, left, top]() {
                    if (!mPicker->isVisible()) return;
                    // Pin the anchor once; every later refit re-asserts it so
                    // the popup can never drift to the right on a mode switch.
                    // top is reserved for the tallest (Custom) height, so
                    // switching to Custom grows the popup down in place.
                    mPicker->move(left, top);
                    mPicker->setAnchorPoint(QPoint(left, top));
                    mPicker->setWindowOpacity(1.0);
                });
            });
        });
    }

    void EasingItem::setItemEnabled(bool aEnable) {
        mChip->setEnabled(aEnable);
        mItemEnabled = aEnable;
    }

    void EasingItem::setItemVisible(bool aVisible) {
        mChip->setVisible(aVisible);
    }

    QWidget* EasingItem::itemWidget() { return mChip; }

    util::Easing::Param EasingItem::value() const {
        return mStamp;
    }

    void EasingItem::setValue(util::Easing::Param aValue, bool aSignal) {
        Q_UNUSED(aSignal); // the picker applies only on user Apply
        mStamp = aValue;
        mPicker->setValue(aValue);
        updateChip();
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
