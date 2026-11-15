#include "gui/TimeLineHeader.h"
#include "gui/MainDisplayWidget.h"
#include "gui/PlayBackWidget.h"
#include "gui/TimeLineWidget.h"
#include "gui/TimeLineEditorWidget.h"
#include "gui/prop/EasingPickWidget.h" // gui::prop::paintEasingCurve (the tile glyph)
#include "gui/prop/prop_Items.h"
#include "gui/theme/Colors.h"
#include "core/KeyEasing.h"
#include "core/TimeKey.h"
#include "ctrl/TimeLineEditor.h"
#include "ctrl/graph/graph_Channel.h"
#include "ctrl/graph/graph_Curve.h"
#include "ctrl/time/time_Scaler.h"
#include <QAbstractButton>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QSet>
#ifdef AE_ENABLE_IMGUI
#include "gui/GraphPanelWidget.h"
#include <QTabBar>
#endif
#include <algorithm>
#include <functional>

namespace {
// The header's geometry, all from the design's own numbers: a 40px bar, 8px of
// inset at both ends, 6px between the groups, and a 1x16 rule between two of them
// (the mock's .tl-head / .tl-hairline). The transport's own 28px/20px grid belongs
// to PlayBackWidget; the bar's other buttons use the same 28px box.
const int kHeaderHeight = 40;
const int kHeaderPad = 8;
const int kGroupGap = 6;
const int kHairlineHeight = 16;
// The same 28px box the stylesheet pins (playbackwidget.ssa) and the transport's
// own grid uses. setFixedSize, not the QSS pin alone: a layout takes the size hint
// a restyle rewrites, while a fixed size is min = max = 28.
const int kButtonSize = 28;
// The easing chip's reserved width. Fixed on purpose: the chip's content changes
// with the key selection, and a control that resizes re-flows the bar beside it.
const int kEaseSlotWidth = 196;
// The bar's TEXT button (the channels trigger): the mock's .gp-chbtn numbers -
// 10px text, 6px of side padding, a 4px radius and a 28px box (the bar's own
// grid). The quick easings are glyph buttons now and carry no text.
const int kTextButtonSize = 10;
const int kTextButtonPadX = 6;
const int kTextButtonRadius = 4;
// The quick easings: the glyph's 18px box inside the bar's 28px button and the 2px
// between the four.
const int kQuickEaseGlyphSize = 18;
const int kQuickEaseGap = 2;
// The proportional toggle's glyph is larger than an easing glyph: it carries three
// marks (anchor / ring / anchor), and at 18px the side key bodies read as dust.
const int kProportionalGlyphSize = 22;

// The Auto preview context the quick-easing Auto button draws with is the SAME
// one the picker's Auto tile uses - gui::prop::autoPreviewContext() (defined in
// EasingPickWidget.cpp) - so the two images cannot be two different curves. A
// quick easing is a catalog image of the easing (for the Auto preset, of the
// automatic pair it applies to the segment after each selected key), not a
// preview of the selected keys (whose neighbours differ per selection): with the
// real, empty context Auto's derived handles collapse to the identity and the
// button would read as Linear. The canonical pair makes the REAL derivation shape
// the canonical ease-in-out, and with aAutoHandles it also shows the control
// points.
#ifdef AE_ENABLE_IMGUI
// The tab strip: the mock's own numbers for a tab (.tl-tabs button - 11px text,
// 3px/8px of padding, a 4px radius) and the bar it sits in (a 2px gap between the
// two tabs, and the 1x16 rule the mock's border-right draws between the strip and
// the transport is the header's own Hairline widget, as for every other group).
// THE INK IS NOT THE MOCK'S (user's ruling, 2026-10-23: "the accent color in the
// Timeline/Graph segmented control is rough ... accent fill is too much there").
// The mock paints the current tab as a solid accent chip; here the two tabs are a
// SEGMENTED FIELD in the header's own vocabulary - the ease chip's recipe
// (recessed surface + hairline edge, prop::EasingChip::paintEvent) with the
// current segment raised out of it as a knob (raised fill + outline edge). The
// accent is left to the document's own marks (the playhead, the keys, the
// selection ring); chrome does not compete with them.
const int kTabFontSize = 11;
const int kTabPadX = 8;
// 1px less than the mock's 3px because the tab now carries a 1px border of its
// own on BOTH states (transparent when unselected), so its inked height is the
// same as before and selecting a tab cannot resize it.
const int kTabPadY = 2;
const int kTabRadius = 4;      // the knob
const int kTroughRadius = 6;   // the field the knob sits in
const int kTabMargin = 2;      // knob inset inside the field
const int kTabEdge = 1;        // border width, both states
#endif

// The mock's .tl-hairline: the 1x16 rule between two of the bar's groups. Painted
// rather than styled - QSS box rules do not render on plain widget surfaces (the
// same note as the splitter, the dock sash and the header's own bottom hairline).
class Hairline: public QWidget {
public:
    explicit Hairline(QWidget* aParent): QWidget(aParent) { this->setFixedSize(1, kHairlineHeight); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(this->rect(), theme::Colors::current().hairline);
    }
};

// A TEXT BUTTON in the bar's own vocabulary: the mock's .gp-chbtn - 10px text at the
// muted ink and `raised` under the pointer or while pressed. The channels trigger is
// its one user (the quick easings were the checkable ones and are glyph buttons now),
// so the checked/accent state it used to paint is gone. It paints from the LIVE tokens
// rather than from a stylesheet, because the header owns no sheet of its own
// (playbackwidget.ssa styles the transport, not this) - the same rule the tab strip
// follows. gui::IconButton, the bar's other button, is the glyph-hover pair and has no
// text.
class HeaderTextButton: public QAbstractButton {
public:
    explicit HeaderTextButton(QWidget* aParent): QAbstractButton(aParent) {
        // Keyboard reachable by TAB, never by a mouse click: a click must not leave a
        // focus outline behind (the rest of the app's bar buttons are not
        // mouse-focusable either). The focus ring is the keyboard affordance.
        this->setFocusPolicy(Qt::TabFocus);
        this->setFixedHeight(kButtonSize);
    }

    QSize sizeHint() const override {
        QFont font = this->font();
        font.setPixelSize(kTextButtonSize);
        const QFontMetrics metrics(font);
        const int width = metrics.horizontalAdvance(this->text()) + 2 * kTextButtonPadX;
        return QSize(std::max(kButtonSize, width), kButtonSize);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const theme::Colors& colors = theme::Colors::current();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF box = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        if (this->isEnabled() && (this->isDown() || this->underMouse())) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors.raised);
            painter.drawRoundedRect(box, kTextButtonRadius, kTextButtonRadius);
        }

        QFont font = this->font();
        font.setPixelSize(kTextButtonSize);
        painter.setFont(font);
        const QColor ink = !this->isEnabled() ? colors.textMutedDim
            : this->underMouse()              ? colors.text
                                              : colors.textMuted;
        painter.setPen(ink);
        painter.drawText(this->rect(), Qt::AlignCenter, this->text());

        if (this->hasFocus()) {
            painter.setPen(QPen(colors.accent, 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(box, kTextButtonRadius, kTextButtonRadius);
        }
    }

    void keyPressEvent(QKeyEvent* aEvent) override {
        // Return activates like Space: QAbstractButton only binds Space (and Select),
        // and a bar of buttons with no dialog around it has no auto-default machinery
        // to borrow.
        if (aEvent->key() == Qt::Key_Return || aEvent->key() == Qt::Key_Enter) {
            this->click();
            aEvent->accept();
            return;
        }
        QAbstractButton::keyPressEvent(aEvent);
    }
};

// A GLYPH BUTTON for one of the four quick easings: the same curve the easing
// picker paints on its catalog tiles, through gui::prop::paintEasingCurve, and
// no text at all. Auto opts into the control-point glyph exactly as its tile
// does, so it reads as "a bezier with automatic handles" instead of as another
// function plot.
//
// Inks, all from the LIVE tokens (a theme change is a repaint, nothing else):
//   * resting  - textMutedDim, the ink the picker's unselected tiles use;
//   * hover    - the panel's text ink over the `raised` fill (the picker's own
//                tile hover rule: color text, background raised);
//   * checked  - the panel's text ink (never the accent: the quick easings are a
//                TYPE shortcut, not the chip's own state);
//   * disabled - textDisabled (nothing is selected, so the button cannot act);
//   * focus    - a 1px text-ink ring, the bar's keyboard affordance.
//
// The BOX is fixed: a glyph has no text whose width could follow the state, and
// setFixedSize makes min = max = 28, so enabling, checking or hovering one of
// the four never re-measures the group (the bar's reserved-width rule).
class QuickEaseButton: public QAbstractButton {
public:
    QuickEaseButton(
        util::Easing::Type aType, bool aAutoPreset, QWidget* aParent
    ): QAbstractButton(aParent), mAutoPreset(aAutoPreset) {
        // The Auto preset is a Custom curve with BOTH sides on
        // kAutoPresetHandleMode (AutoClamped) - the same pair the picker's Auto
        // tile writes - and the other three are plain types.
        mParam.type = aAutoPreset ? util::Easing::Type_Custom : aType;
        if (aAutoPreset) {
            mParam.handleIn = util::Easing::kAutoPresetHandleMode;
            mParam.handleOut = util::Easing::kAutoPresetHandleMode;
        }
        mAuto = gui::prop::autoPreviewContext();
        // Keyboard reachable by TAB, never by a mouse click (see HeaderTextButton):
        // a click must not leave a focus outline on a glyph button.
        this->setFocusPolicy(Qt::TabFocus);
        this->setFixedSize(kButtonSize, kButtonSize);
        this->setCheckable(true);
    }

    QSize sizeHint() const override { return QSize(kButtonSize, kButtonSize); }

protected:
    void paintEvent(QPaintEvent*) override {
        const theme::Colors& colors = theme::Colors::current();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF box = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const bool hovered = this->isEnabled() && this->underMouse();
        if (hovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors.raised);
            painter.drawRoundedRect(box, kTextButtonRadius, kTextButtonRadius);
        }

        // The glyph: the bar's 18px icon box, centered in the 28px button.
        const QRectF glyph((this->width() - kQuickEaseGlyphSize) / 2.0,
                           (this->height() - kQuickEaseGlyphSize) / 2.0,
                           kQuickEaseGlyphSize, kQuickEaseGlyphSize);
        const QColor ink = !this->isEnabled() ? colors.textDisabled
            : this->isChecked() || hovered     ? colors.text
                                               : colors.textMutedDim;
        // Auto's derived control points and stems are part of its icon, as on the
        // picker's tile; every other type is a bare curve.
        gui::prop::paintEasingCurve(painter, glyph, mParam, ink, 1.5, &mAuto, mAutoPreset);

        if (this->hasFocus()) {
            painter.setPen(QPen(colors.text, 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(box, kTextButtonRadius, kTextButtonRadius);
        }
    }

    void keyPressEvent(QKeyEvent* aEvent) override {
        // Return activates like Space, the same reason HeaderTextButton binds it.
        if (aEvent->key() == Qt::Key_Return || aEvent->key() == Qt::Key_Enter) {
            this->click();
            aEvent->accept();
            return;
        }
        QAbstractButton::keyPressEvent(aEvent);
    }

private:
    util::Easing::Param mParam;
    util::Easing::AutoContext mAuto;
    bool mAutoPreset = false;
};

} // namespace

namespace gui {

// THE PROPORTIONAL TOGGLE. The glyph is a PROPORTIONAL KEY IN ITS CONTEXT, not just
// a circle: two solid key bodies (the absolute anchors on either side, the timeline's
// filled circles) joined by a short line to a hollow ring in the middle (the key that
// follows them). That is the relationship the flag means, and it cannot be read as an
// easing. The ring is
// always a ring; the TOGGLE STATE is the ink - textMutedDim when every selected key
// is absolute, text when every one is proportional, textMuted plus a dash through
// the ring when the set is mixed - which is the same checked/unchecked language the
// quick easings use. A filled-disc variant would invert the key bodies: a filled
// key is the ABSOLUTE one, so filling the glyph to mean "on" would say the opposite.
class ProportionalButton: public QAbstractButton {
public:
    enum class State { None, Mixed, All };

    explicit ProportionalButton(QWidget* aParent): QAbstractButton(aParent) {
        // TAB-only focus: a mouse click must not leave the keyboard ring behind.
        this->setFocusPolicy(Qt::TabFocus);
        this->setFixedSize(kButtonSize, kButtonSize);
        this->setCheckable(true); // the checked state is exposed to assistive technology
        this->setAccessibleName(tr("Proportional keys"));
    }

    State proportionalState() const { return mState; }

    void setProportionalState(State aState) {
        mState = aState;
        this->setChecked(aState == State::All);
        this->update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const theme::Colors& colors = theme::Colors::current();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF box = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const bool hovered = this->isEnabled() && this->underMouse();
        if (hovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors.raised);
            painter.drawRoundedRect(box, kTextButtonRadius, kTextButtonRadius);
        }

        const QColor ink = !this->isEnabled() ? colors.textDisabled
            : this->isChecked() || hovered     ? colors.text
            : mState == State::Mixed           ? colors.textMuted
                                               : colors.textMutedDim;

        // The glyph, in the bar's 22px icon box: ANCHOR -- KEY -- ANCHOR. Larger
        // than the 18px easing glyphs because it has three marks to carry.
        const QRectF glyph(
            (this->width() - kProportionalGlyphSize) / 2.0,
            (this->height() - kProportionalGlyphSize) / 2.0,
            kProportionalGlyphSize, kProportionalGlyphSize
        );
        const QPointF centre = glyph.center();
        const QPointF left(glyph.left() + 3.4, centre.y());
        const QPointF right(glyph.right() - 3.4, centre.y());
        constexpr qreal kAnchor = 2.4;
        constexpr qreal kRingOuter = 4.0;

        // The chain: absolute key to relative key, both sides.
        painter.setPen(QPen(ink, 1.2));
        painter.drawLine(left + QPointF(kAnchor, 0.0), QPointF(centre.x() - kRingOuter, centre.y()));
        painter.drawLine(QPointF(centre.x() + kRingOuter, centre.y()), right - QPointF(kAnchor, 0.0));

        // The absolute anchors: the timeline's filled key body, at the icon's scale.
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink);
        painter.drawEllipse(left, kAnchor, kAnchor);
        painter.drawEllipse(right, kAnchor, kAnchor);

        // The relative key: a hollow ring at the timeline's 2px body weight.
        painter.setPen(QPen(ink, 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(centre, kRingOuter - 1.0, kRingOuter - 1.0);

        if (mState == State::Mixed) {
            // A mixed set: the ring alone cannot say "some"; a short dash inside it
            // is the same cue the old toggle used.
            painter.setPen(QPen(ink, 1.2));
            painter.drawLine(QPointF(centre.x() - 1.5, centre.y()), QPointF(centre.x() + 1.5, centre.y()));
        }

        if (this->hasFocus()) {
            painter.setPen(QPen(colors.text, 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(box, kTextButtonRadius, kTextButtonRadius);
        }
    }

    void keyPressEvent(QKeyEvent* aEvent) override {
        if (aEvent->key() == Qt::Key_Return || aEvent->key() == Qt::Key_Enter) {
            this->click();
            aEvent->accept();
            return;
        }
        QAbstractButton::keyPressEvent(aEvent);
    }

private:
    State mState = State::None;
};


TimeLineHeader::TimeLineHeader(GUIResources& aResources, PlayBackWidget* aTransport, QWidget* aParent):
    QWidget(aParent),
    mGUIResources(aResources),
    mTransport(aTransport),
#ifdef AE_ENABLE_IMGUI
    mTabs(nullptr),
#endif
    mTimeLine(nullptr),
    mInner(nullptr),
    mSelectionSlot(),
    mProportionalConnection(),
    mProportionalButton(nullptr),
    mEaseSlot(nullptr),
    mEasing(),
    mQuickEases(),
#ifdef AE_ENABLE_IMGUI
    mChannelsButton(nullptr)
#endif
    {
    // The bar is one fixed 40px row; the surface below keeps its own geometry
    // whatever the bar holds.
    this->setFixedHeight(kHeaderHeight);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(kHeaderPad, 0, kHeaderPad, 0);
    lay->setSpacing(kGroupGap);

#ifdef AE_ENABLE_IMGUI
    // The dock's views, as the bar's FIRST group (the mock's .tl-tabs): the tab
    // names the page, so the Graph page carries no title and no header row of its
    // own - the panel is one component on two floors (design section 5). The control
    // is a real QTabBar, the application's own tab control (gui::ProjectTabBar is one
    // too): keyboard left/right, hover and the selected state all come from Qt, and
    // the mock's border-right between the strip and the transport is the header's own
    // rule, like every other group's.
    mTabs = new QTabBar(this);
    mTabs->setDrawBase(false);
    mTabs->setExpanding(false);
    mTabs->setUsesScrollButtons(false);
    mTabs->setFocusPolicy(Qt::NoFocus);
    mTabs->setMouseTracking(true);
    mTabs->addTab(tr("Timeline"));
    mTabs->addTab(tr("Graph"));
    // The tab order IS Surface's own (Timeline, Graph): onTabChanged maps an index
    // back by position, so the two cannot drift.
    mTabs->setCurrentIndex(static_cast<int>(Surface::Timeline));
    this->refreshTabStyle();
    connect(mTabs, &QTabBar::currentChanged, this, &TimeLineHeader::onTabChanged);
    lay->addWidget(mTabs, 0, Qt::AlignVCenter);
    this->addHairline(*lay);
#endif

    // The transport, moved out of its old column: a fixed-size group, centered in
    // the bar.
    lay->addWidget(mTransport, 0, Qt::AlignVCenter);

    // The mock's rule between the transport and the chip group: the frame readout
    // used to sit between them and is gone (the playhead pill names the current frame
    // and the ruler the end frame), so this is the mock's own .tl-hairline - one
    // group's end, the next one's start (here: the quick easings, which precede the
    // chip's slot).
    this->addHairline(*lay);

    // THE FOUR QUICK EASINGS, TO THE LEFT OF THE CHIP: the picker's four common
    // tiles as one click each, on the same selection the chip names. They are a
    // SHORTCUT (three set the type; the Auto preset makes the segment after each
    // selected key automatic), and the range and the blend stay the key's own,
    // exactly as the picker's tiles leave them - and they read back which of the
    // four the selection agrees on. Added to the row BEFORE the chip's slot, so the
    // local order is transport | quick easings | chip (the bar reads the shortcut
    // first and then the control that names the selection).
    //
    // The four entries, in the picker catalog's own display head (its first tile
    // row). Their names come from util::Easing::getParamName - the Param's
    // user-facing name, which for a type is its getTypeName and for the automatic
    // preset is the handle pair it writes ("Auto Clamped"). That is the same
    // function the chip and the picker's tiles read, so the bar cannot introduce
    // a name of its own. getParamName is already translated (its "EasingName"
    // context), so its result is used as-is and must not be wrapped in tr()
    // again.
    //
    // WIDTH IS RESERVED, NOT MEASURED: each button is a fixed 28x28 box
    // (QuickEaseButton::setFixedSize) whose size hint never depends on the
    // enabled/checked/hover state, and the glyph itself is a fixed 18px box. The
    // group therefore contributes one constant width to this layout, and a
    // selection change cannot re-flow the bar or resize the dock.
    const struct QuickDef {
        util::Easing::Type type;
        bool autoPreset;
    } quickDefs[] = {
        { util::Easing::Type_Linear, false },
        { util::Easing::Type_Floor, false },
        { util::Easing::Type_Round, false },
        // The Auto preset: Custom with both handle sides Auto (the glyph's
        // derived pair), not a retired easing type.
        { util::Easing::Type_Custom, true },
    };
    auto* quickBox = new QWidget(this);
    auto* quickLay = new QHBoxLayout(quickBox);
    quickLay->setContentsMargins(0, 0, 0, 0);
    quickLay->setSpacing(kQuickEaseGap);
    for (const QuickDef& quick : quickDefs) {
        auto* button = new QuickEaseButton(quick.type, quick.autoPreset, quickBox);
        // The SAME user-facing name the chip derives from the modes: an
        // ordinary quick easing is its type name, the preset is the two handle
        // sides it writes (Auto Clamped).
        util::Easing::Param quickParam;
        quickParam.type = quick.autoPreset ? util::Easing::Type_Custom : quick.type;
        if (quick.autoPreset) {
            quickParam.handleIn = util::Easing::kAutoPresetHandleMode;
            quickParam.handleOut = util::Easing::kAutoPresetHandleMode;
        }
        const QString name = util::Easing::getParamName(quickParam);
        // A glyph-only control has no text for assistive technology to read, so
        // the easing's name IS its accessible text. THE AUTO PRESET'S ACTION IS THE
        // SEGMENT AFTER THE KEY, not a pair on one key: it makes that segment
        // automatic (the key's type and outgoing side, the next key's incoming side -
        // see ctrl::TimeLineEditor::assignSelectionAutoHandles), so its ACCESSIBLE
        // NAME carries that longer action. The tooltip stays one recipe for all four
        // (the pointer reads the same sentence); only the screen reader needs the
        // segment explanation.
        if (quick.autoPreset) {
            const QString action = tr("Make the segment after each selected key automatic");
            button->setAccessibleName(tr("%1 (%2)").arg(action, name));
        } else {
            button->setAccessibleName(name);
        }
        // ONE TOOLTIP RECIPE for the four: the pointer reads the same sentence
        // whether the entry is a type or the Auto preset. The accessible name keeps
        // the preset's longer action wording (a screen reader needs it; a tooltip
        // does not explain the segment).
        button->setToolTip(tr("Set the selected keys' easing to %1").arg(name));
        button->setEnabled(false); // until a selection names a key that carries an easing
        quickLay->addWidget(button);
        mQuickEases.push_back({button, quick.type, quick.autoPreset});
        connect(button, &QAbstractButton::clicked, this, [this, quick]() {
            this->onQuickEasingClicked(quick.type, quick.autoPreset);
        });
    }
    lay->addWidget(quickBox, 0, Qt::AlignVCenter);

    // The easing chip's slot: the reserved width, with a zero-margin layout so the
    // chip fills it without carrying the geometry (see easingChipSlot()). Fixed on
    // purpose - the chip's content changes with the selection, and the bar beside
    // it must not.
    mEaseSlot = new QWidget(this);
    mEaseSlot->setFixedWidth(kEaseSlotWidth);
    mEaseSlot->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* easeLay = new QHBoxLayout(mEaseSlot);
    easeLay->setContentsMargins(0, 0, 0, 0);
    easeLay->setSpacing(0);
    lay->addWidget(mEaseSlot, 0, Qt::AlignVCenter);

    // The easing chip itself: the picker's only home (design section 2). It is
    // built by its own control (prop::EasingItem, which owns the one picker
    // popover) and placed here - the slot is the width, the control is the widget.
    // The chip names the timeline's key selection, which setTimeLineWidget wires.
    mEasing.reset(new prop::EasingItem(mEaseSlot, &aResources));
    easeLay->addWidget(mEasing->itemWidget());
    mEasing->chip()->connect(mEasing->chip(), &QAbstractButton::clicked, this,
                             [this]() { this->onEasingChipClicked(); });

    // The mock's .tl-spacer: the key-property and channels controls are the bar's
    // right end, whatever the chip's content and the quick easings' widths do.
    lay->addStretch(1);

    // THE PROPORTIONAL TOGGLE, on the right and NOT inside the easing group: it
    // changes a key's relative flag, which is a different property from its easing.
    // Its glyph is the relationship itself (two absolute anchors joined to a ring in
    // the middle); the checked state is its ink and the mixed state adds a dash.
    mProportionalButton = new ProportionalButton(this);
    mProportionalButton->setProportionalState(ProportionalButton::State::None);
    mProportionalButton->setEnabled(false);
    lay->addWidget(mProportionalButton, 0, Qt::AlignVCenter);
    connect(mProportionalButton, &QAbstractButton::clicked, this, [this]() {
        this->onProportionalClicked();
    });

#ifdef AE_ENABLE_IMGUI
    // THE CHANNELS TRIGGER: the panel's ONE control, in the dock's header (the mock's
    // .tl-chans - a view action, so it sits with the view controls and not in the
    // panel's own ruler band). Hidden while the Timeline tab is the dock's view, since
    // there is no panel to control then (see onTabChanged) - but its BOX stays in the
    // layout. The dock is sized from this bar's minimum width, and the trigger is the
    // one item whose visibility changes with the tab, so a hidden trigger used to take
    // its box plus its group gap out of that minimum and move the dock (and the tree
    // splitter) on every tab switch.
    mChannelsButton = new HeaderTextButton(this);
    mChannelsButton->setToolTip(tr("Which channels are drawn"));
    // The box is FIXED, not the live label width: the trigger's words follow the
    // panel's rows ("6 channels" / "4 of 6"), and a count change must not re-flow the
    // bar any more than a tab change may. The width is the widest three-digit label,
    // measured in the button's own 10px text (the same rule HeaderTextButton's
    // sizeHint uses) plus its 6px side padding.
    QFont channelsFont = mChannelsButton->font();
    channelsFont.setPixelSize(kTextButtonSize);
    const QFontMetrics channelsMetrics(channelsFont);
    const int channelsWidth =
        std::max(channelsMetrics.horizontalAdvance(tr("%n channel(s)", nullptr, 999)),
                 channelsMetrics.horizontalAdvance(tr("%1 of %2").arg(999).arg(999)))
        + 2 * kTextButtonPadX;
    mChannelsButton->setFixedWidth(std::max(kButtonSize, channelsWidth));
    // ...and the fixed box is reserved while the button is hidden: QWidgetItem::
    // isEmpty() stays false for a hidden widget whose policy retains its size, so the
    // box layout keeps the item's size hint (the fixed width and height) as its own -
    // hiding the trigger now changes nothing in the row's minimum width.
    QSizePolicy channelsPolicy = mChannelsButton->sizePolicy();
    channelsPolicy.setRetainSizeWhenHidden(true);
    mChannelsButton->setSizePolicy(channelsPolicy);
    mChannelsButton->setVisible(false);
    connect(mChannelsButton, &QAbstractButton::clicked, this, [this]() { this->onChannelsClicked(); });
    lay->addWidget(mChannelsButton, 0, Qt::AlignVCenter);
#endif

    // The bar's right end is the channels trigger (on the Graph tab); the zoom
    // group that used to hang here was redundant - the timeline's wheel and its
    // scroll bar own zooming and fitting - so the spacer above is the last flexible
    // item before it.

    // The glyphs are tinted per theme, so they are re-read on a theme change - the
    // same route the transport takes for its own buttons.
    mGUIResources.onThemeChanged.connect(this, &TimeLineHeader::onThemeUpdated);
    this->refreshFrame();
}

TimeLineHeader::~TimeLineHeader() {
    // The surface holds the control's callbacks (they capture this widget), so it
    // must forget the control before either goes away. This widget is destroyed
    // before the timeline in every construction order the dock uses, and the
    // clearance makes it independent of that order.
    if (mInner) {
        mInner->setEasingItemHost(nullptr);
        if (mInner->editor() && mSelectionSlot.isValid())
            mInner->editor()->selection().onChanged.disconnect(mSelectionSlot);
        if (mProportionalConnection)
            disconnect(mProportionalConnection);
    }
}

void TimeLineHeader::setTimeLineWidget(TimeLineWidget* aTimeLine) {
    if (mTimeLine == aTimeLine)
        return;
    if (mInner) {
        mInner->setEasingItemHost(nullptr);
        if (mInner->editor() && mSelectionSlot.isValid())
            mInner->editor()->selection().onChanged.disconnect(mSelectionSlot);
        if (mProportionalConnection)
            disconnect(mProportionalConnection);
    }
    mSelectionSlot = util::SlotId();
    if (mProportionalConnection)
        disconnect(mProportionalConnection);

    mTimeLine = aTimeLine;

    // The surface widget inside the timeline: it owns the ONE easing picker and
    // pushes the chip's state to the control (setEasingItemHost). It is a direct
    // child, so this is a one-time lookup, not a search per refresh.
    mInner = mTimeLine
                 ? mTimeLine->findChild<TimeLineEditorWidget*>(QString(), Qt::FindDirectChildrenOnly)
                 : nullptr;
    if (mInner) {
        this->bindEasingHost();
        // The live selection's own change notification: whichever view moved the
        // selection (a click on the lanes, a marquee, a right-click's temporary loan,
        // the graph panel), this is what re-reads the quick easings. The chip is pushed
        // from the same source (TimeLineEditorWidget::pushEasingChipState).
        if (mInner->editor())
            mSelectionSlot =
                mInner->editor()->selection().onChanged.connect(this, &TimeLineHeader::refreshQuickEasings);
        // The flags live on the keys, not on the selection: the menu's toggle (or
        // any other edit) reports through this signal, and the button re-reads.
        mProportionalConnection = connect(
            mInner, &TimeLineEditorWidget::keyProportionalChanged,
            this, &TimeLineHeader::refreshProportional
        );
    }

    this->refreshFrame();
}

void TimeLineHeader::bindEasingHost() {
    if (!mInner)
        return;

    // The chip and the quick easings read ONE state (the timeline's live selection).
    // The chip is pushed its state by the surface (setEasingItemHost wires the
    // callbacks that write the document); those callbacks capture the surface, so
    // binding the host is what this bar has to do - and the picker's COMMIT is the one
    // easing write that does not move the selection, so the bar's own re-read is
    // composed with the host's callback rather than left to a signal that never fires.
    mInner->setEasingItemHost(mEasing.data());
    const std::function<void(util::Easing::Param)> applied = mEasing->onEasingApplied;
    mEasing->onEasingApplied = [this, applied](util::Easing::Param aEasing) {
        if (applied)
            applied(aEasing);
        this->refreshQuickEasings();
    };
    const std::function<void()> closed = mEasing->onPickerClosed;
    mEasing->onPickerClosed = [this, closed]() {
        if (closed)
            closed();
        this->refreshQuickEasings();
    };
}

void TimeLineHeader::onEasingChipClicked() {
    emit easingChipActivated();
    if (!mInner || !mTimeLine)
        return;
    ctrl::TimeLineEditor* editor = mTimeLine->editor();
    if (!editor)
        return;

    // The keys the chip names are the editor's live selection, which is also what
    // the picker then edits: the chip is not a label about the selection, it is its
    // control. The popover opens under the chip (the mock's anchoring) through the
    // ONE entry point the key menu uses (design section 2).
    core::TimeLineEvent targets;
    if (!editor->retrieveSelectionTargets(targets))
        return;
    const QWidget* chip = mEasing->itemWidget();
    mInner->openEasingPicker(targets, chip->mapToGlobal(QPoint(0, chip->height())));
}

void TimeLineHeader::onQuickEasingClicked(util::Easing::Type aType, bool aAutoPreset) {
    // THE SAME EDIT AS THE PICKER'S TILE, through the surface that owns the keys:
    // the timeline's selection is the set both the picker and the quick easings
    // act on, and TimeLineEditorWidget::applyEasingToSelection writes every
    // component the selection names as ONE undo step. It must NOT go through the
    // graph panel's editor: that action walks graph CHANNELS, and a Pose or FFD
    // key - which carries one shared easing but publishes no channel - was
    // silently skipped there, so the quick buttons never acted on (or lit for)
    // those keys. This route covers every key type that carries an easing.
    if (mInner) {
        if (aAutoPreset)
            mInner->applyAutoHandlesToSelection();
        else
            mInner->applyEasingToSelection(aType);
    }

    // The document's own notification re-reads the chip and the surfaces; the BAR's
    // state is re-read here, because the selection itself did not move.
    this->refreshQuickEasings();
}

void TimeLineHeader::refreshQuickEasings() {
    // WHICH OF THE FOUR IS ACTIVE, and whether any of them can act at all, from
    // the timeline's own live selection: every selected key that carries an
    // easing votes for its easing. Pose and FFD vote with their single shared
    // param (component 0): they have no graph channel but their easing shapes the
    // interval playback plays, so the buttons must be able to act on them.
    // Component 0 of an Image key is readable too, but the image offset is
    // step-held - playback never consults the param - so an image key does not
    // vote (no quick easing could change a played sample). The Auto button votes
    // for the PRESET - Custom with both sides Auto - because automatic handles
    // are modes now, not a type. Nothing to vote with - no selection, or only
    // keys of types with no easing - leaves every button disabled; a split vote
    // lights none of them.
    ctrl::TimeLineEditor* editor = mTimeLine ? mTimeLine->editor() : nullptr;
    bool votable = false;
    bool mixedType = false;
    bool mixedPreset = false;
    util::Easing::Type active = util::Easing::Type_TERM;
    bool presetActive = false;

    if (editor) {
        for (const ctrl::time::Selection::Item& item : editor->selection().items()) {
            if (!item.key)
                continue;
            core::TimeKeyPos pos = item.target.pos;
            core::TimeKey* key = pos.key();
            if (!key)
                continue; // a stale entry names a key the document no longer holds
            // The stored component count, not the graph channel count: Pose/FFD
            // answer 1 here and 0 there.
            const int stored = core::easingStorageCount(pos.type());
            const int component =
                (item.component == ctrl::time::Selection::kKeyLevel) ? 0 : item.component;
            if (stored <= 0 || component < 0 || stored <= component)
                continue; // a type that carries no easing, or no such component
            const util::Easing::Param* param =
                ctrl::graph::Curve::easingOf(*key, pos.type(), component);
            if (!param)
                continue; // bone/mesh
            if (!ctrl::graph::Channel(pos.type(), 0).isInterpolated())
                continue; // step-held (the image offset): its param is never played
            const bool preset = param->type == util::Easing::Type_Custom
                && param->handleIn == util::Easing::kAutoPresetHandleMode
                && param->handleOut == util::Easing::kAutoPresetHandleMode;
            if (!votable) {
                votable = true;
                active = param->type;
                presetActive = preset;
            } else {
                if (active != param->type)
                    mixedType = true;
                if (presetActive != preset)
                    mixedPreset = true;
            }
        }
    }

    for (const QuickEase& quick : mQuickEases) {
        quick.button->setEnabled(votable);
        quick.button->setChecked(
            votable
            && (quick.autoPreset ? (!mixedPreset && presetActive)
                                 : (!mixedType && active == quick.type))
        );
    }

    // The proportional control reads the SAME selection (the selection's onChanged
    // is one of its two update routes; the other is the key flags' own signal).
    this->refreshProportional();
}

void TimeLineHeader::refreshProportional() {
    if (!mProportionalButton)
        return;

    ctrl::TimeLineEditor* editor = mTimeLine ? mTimeLine->editor() : nullptr;
    QSet<core::TimeKey*> keys;
    int proportional = 0;
    if (editor) {
        for (const ctrl::time::Selection::Item& item : editor->selection().items()) {
            // The selection's stored key pointer is only ever compared; resolve the
            // key from the document, as the chip and quick easings do, and count
            // each key once (its two markers are one flag).
            core::TimeKeyPos pos = item.target.pos;
            core::TimeKey* key = pos.key();
            if (!key || key->isDefaultKey() || keys.contains(key))
                continue;
            keys.insert(key);
            if (key->isProportional())
                ++proportional;
        }
    }

    const int total = keys.size();
    const ProportionalButton::State state =
        total == 0 ? ProportionalButton::State::None
        : proportional == total ? ProportionalButton::State::All
        : proportional == 0     ? ProportionalButton::State::None
                                : ProportionalButton::State::Mixed;
    mProportionalButton->setProportionalState(state);
    mProportionalButton->setEnabled(total > 0);

    const QString action = state == ProportionalButton::State::All
        ? tr("Make the selected keys absolute")
        : tr("Make the selected keys proportional");
    mProportionalButton->setToolTip(
        total > 0 ? action : tr("Select keys to make them proportional")
    );
    mProportionalButton->setAccessibleDescription(
        total == 0 ? tr("No keys selected")
        : state == ProportionalButton::State::All ? tr("The selected keys are proportional")
        : state == ProportionalButton::State::Mixed ? tr("Some selected keys are proportional")
                                                   : tr("The selected keys are absolute")
    );
}

void TimeLineHeader::onProportionalClicked() {
    if (!mInner || !mProportionalButton)
        return;

    // A mixed or absolute set becomes proportional; an all-proportional set becomes
    // absolute. The surface pushes the edit (one undo step) and reports it, which
    // re-reads this control through keyProportionalChanged; the explicit refresh
    // also covers a refused edit (no key to act on).
    const bool enable = mProportionalButton->proportionalState() != ProportionalButton::State::All;
    mInner->setSelectionProportional(enable);
    this->refreshProportional();
}

void TimeLineHeader::addHairline(QHBoxLayout& aLayout) {
    auto* hairline = new Hairline(this);
    aLayout.addWidget(hairline, 0, Qt::AlignVCenter);
}

void TimeLineHeader::refreshFrame() {
    // The frame readout this used to re-read is gone (the playhead pill and the ruler
    // name both numbers), so what is left to re-read is the state the dock's two calls
    // still owe the bar: the quick easings' availability/active type (which follows the
    // timeline's editor, and the editor changes with the project) and, in a build with
    // the panel, the channels button's own words.
    this->refreshQuickEasings();
#ifdef AE_ENABLE_IMGUI
    GraphPanelWidget* panel = this->graphPanelWidget();
    this->updateChannelsButton(panel ? panel->channelRows() : QVector<PanelChannelRow>());
#endif
}

#ifdef AE_ENABLE_IMGUI
GraphPanelWidget* TimeLineHeader::graphPanelWidget() const {
    // The dock's Graph page, through the WINDOW: the header is constructed with a
    // GUIResources, a transport and a parent, and the dock's body is a sibling of this
    // bar - the same route gui::GraphPanelWidget takes to reach the application's
    // menus (there is exactly one page per window).
    QWidget* top = this->window();
    return top ? top->findChild<GraphPanelWidget*>() : nullptr;
}

void TimeLineHeader::connectChannels() {
    if (mChannelsConnected)
        return;
    GraphPanelWidget* panel = this->graphPanelWidget();
    if (!panel)
        return; // the page is not built yet; the next update asks again
    connect(panel, &GraphPanelWidget::channelsChanged, this,
            [this](const QVector<PanelChannelRow>& aRows) { this->updateChannelsButton(aRows); });
    mChannelsConnected = true;
}

void TimeLineHeader::updateChannelsButton(const QVector<PanelChannelRow>& aRows) {
    if (!mChannelsButton)
        return;
    this->connectChannels();

    // The button's words are its own: the total while every channel is shown, "shown of
    // total" once one is hidden - the count IS the state the button changes. The panel no
    // longer publishes this control or its text (the button is a header control now). No
    // rows at all means no list to open, so the control is disabled rather than opening
    // an empty popup.
    int shown = 0;
    for (const PanelChannelRow& row : aRows) {
        if (row.visible)
            ++shown;
    }
    const int total = aRows.size();
    mChannelsButton->setEnabled(total > 0);
    if (total <= 0)
        mChannelsButton->setText(tr("%n channel(s)", nullptr, 0));
    else if (shown == total)
        mChannelsButton->setText(tr("%n channel(s)", nullptr, total));
    else
        mChannelsButton->setText(tr("%1 of %2").arg(shown).arg(total));
    mChannelsButton->updateGeometry();
}

void TimeLineHeader::onChannelsClicked() {
    MainDisplayWidget* display = this->window() ? this->window()->findChild<MainDisplayWidget*>() : nullptr;
    GraphPanelWidget* panel = this->graphPanelWidget();
    if (!display || !panel)
        return;

    // The rows are the page's own model rows, built on demand (the page may not have
    // painted since the tab switched): one read for the button's words and one for the
    // list, so the two cannot show different counts.
    const QVector<PanelChannelRow> rows = panel->channelRows();
    this->updateChannelsButton(rows);
    if (rows.isEmpty())
        return; // no channels: no list to open

    // ANCHORED UNDER THE BUTTON, RIGHT-ALIGNED TO IT (the mock's .tl-chanpop): the
    // anchor is the button's bottom-right corner, mapped into the display's own pixels
    // because that is the object that owns the popup and maps it to the screen. The
    // two widgets are SIBLINGS (the dock's bar and the viewport), not parent and child,
    // so the mapping goes through GLOBAL coordinates - QWidget::mapTo/mapFrom assume
    // the target is an ancestor.
    const QPoint anchorGlobal =
        mChannelsButton->mapToGlobal(QPoint(mChannelsButton->width(), mChannelsButton->height()));
    display->showGraphChannelMenu(rows, display->mapFromGlobal(anchorGlobal));
}

void TimeLineHeader::onTabChanged(int aIndex) {
    // The tab order is Surface's own (see the constructor), so an index maps back by
    // position. An index Qt has no tab for is no view at all: nothing is reported,
    // and the dock's body stays where it is.
    if (aIndex < static_cast<int>(Surface::Timeline) || static_cast<int>(Surface::Graph) < aIndex)
        return;

    // The channels control belongs to the Graph page (there is no panel on the
    // Timeline tab), so it comes and goes with the tab, and its words are re-read as
    // it appears: the count is the panel's own and a key edit beside the dock changed
    // it while the tab was away.
    if (mChannelsButton) {
        const bool graph = static_cast<Surface>(aIndex) == Surface::Graph;
        if (graph && !mChannelsButton->isVisible()) {
            GraphPanelWidget* panel = this->graphPanelWidget();
            this->updateChannelsButton(panel ? panel->channelRows() : QVector<PanelChannelRow>());
        }
        mChannelsButton->setVisible(graph);
    }

    emit surfaceChanged(static_cast<Surface>(aIndex));
}

void TimeLineHeader::refreshTabStyle() {
    // The strip's own ink, built from the application's LIVE tokens rather than from
    // a stylesheet asset (the header owns none: playbackwidget.ssa styles the bar's
    // buttons, not a QTabBar, and this bar is not the place to add one).
    //
    // A SEGMENTED FIELD, in the header's own vocabulary - the easing chip beside it
    // is the same object (prop::EasingChip::paintEvent: `recessed` surface with a
    // `hairline` edge). The current segment is the KNOB raised out of that field:
    // `raised` fill and an `outline` edge. Three states, three steps, no accent:
    //   muted label at rest -> `text` label under the pointer -> the knob.
    // NO FILL ON HOVER, deliberately: `raised` IS the knob's fill in both themes (the
    // light table's `raised` and `hairline` are the same column, and so are the dark
    // table's), so a hover fill would paint a second knob and the pointer could not be
    // told from the selection. The label's own brightness is the hover feedback, as it
    // is on the bar's icon buttons.
    //
    // WHY NOT THE MOCK'S ACCENT CHIP. The mock (.tl-tabs button.on) paints the current
    // tab as a solid accent fill; at 11px in a 40px bar beside a row of muted icon
    // buttons that chip was the loudest thing in the dock, and the accent belongs to
    // the document's own marks - the playhead pill, the keys, the selection ring - not
    // to the chrome that names which page is up. Tokens are substituted by name rather
    // than by %N because QString::arg stops being readable (and %1 starts colliding
    // with %10) once a sheet carries this many.
    const theme::Colors& colors = theme::Colors::current();
    QString sheet = QStringLiteral(
        "QTabBar { background: @field@; border: @edge@px solid @fieldEdge@;"
        " border-radius: @fieldRadius@px; }"
        "QTabBar::tab { color: @rest@; background: transparent;"
        " border: @edge@px solid transparent; border-radius: @knobRadius@px;"
        " padding: @padY@px @padX@px; margin: @knobInset@px; font-size: @fontSize@px; }"
        "QTabBar::tab:selected { color: @on@; background: @knob@;"
        " border: @edge@px solid @knobEdge@; }"
        "QTabBar::tab:hover:!selected { color: @on@; }");
    sheet.replace(QLatin1String("@field@"), colors.recessed.name());
    sheet.replace(QLatin1String("@fieldEdge@"), colors.hairline.name());
    sheet.replace(QLatin1String("@fieldRadius@"), QString::number(kTroughRadius));
    sheet.replace(QLatin1String("@rest@"), colors.textMuted.name());
    sheet.replace(QLatin1String("@on@"), colors.text.name());
    sheet.replace(QLatin1String("@knob@"), colors.raised.name());
    sheet.replace(QLatin1String("@knobEdge@"), colors.outline.name());
    sheet.replace(QLatin1String("@knobRadius@"), QString::number(kTabRadius));
    sheet.replace(QLatin1String("@knobInset@"), QString::number(kTabMargin));
    sheet.replace(QLatin1String("@edge@"), QString::number(kTabEdge));
    sheet.replace(QLatin1String("@padY@"), QString::number(kTabPadY));
    sheet.replace(QLatin1String("@padX@"), QString::number(kTabPadX));
    sheet.replace(QLatin1String("@fontSize@"), QString::number(kTabFontSize));
    mTabs->setStyleSheet(sheet);
    mTabs->update();
}
#endif

void TimeLineHeader::onThemeUpdated(theme::Theme& aTheme) {
    this->setStyleSheet(aTheme.loadStylesheet("playbackwidget.ssa"));
#ifdef AE_ENABLE_IMGUI
    // The strip's ink is built from the tokens, not from a sheet: re-build it here so
    // an accent change (which re-applies the app's palette) re-inks the tabs too.
    this->refreshTabStyle();
#endif
    // The chip paints from the live palette, but its caret pixmap is tinted through
    // the icon pipeline and must be re-loaded for the new appearance.
    mEasing->refreshIcon();
    // The bar's buttons - the channels text button and the quick easings' glyphs -
    // paint from the live tokens too, so a theme change is a repaint and nothing else.
    for (const QuickEase& quick : mQuickEases)
        quick.button->update();
#ifdef AE_ENABLE_IMGUI
    if (mChannelsButton)
        mChannelsButton->update();
#endif
    this->update();
}

void TimeLineHeader::paintEvent(QPaintEvent*) {
    // The bar's own section separator: the 1px hairline on its bottom edge, which is
    // what separates the header from the ruler below it. Painted here because QSS box
    // rules do not render on plain widget surfaces (the transport used to paint its
    // own left-edge hairline in this same place).
    QPainter painter(this);
    painter.fillRect(0, this->height() - 1, this->width(), 1, theme::Colors::current().hairline);
}

} // namespace gui
