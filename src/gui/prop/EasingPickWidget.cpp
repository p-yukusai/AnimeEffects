#include <QIcon>
#include "gui/prop/EasingPickWidget.h"
#include <QApplication>
#include <QClipboard>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QAbstractSpinBox>
#include <QLineEdit>
#include <QScreen>
#include <QWindow>
#include <QStyleOptionSlider>
#include <functional>
#include <QLinearGradient>
#include "gui/AppStyle.h"
#include "gui/PopupWindow.h"
#include "gui/prop/bezierCurveEditor.h"
#include <cmath>

#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QComboBox>
#include <QSignalBlocker>
#include "gui/theme/Colors.h"
#include "gui/theme/Icons.h"

namespace gui {
namespace prop {

namespace {

// Layout constants (px) measured from the reference geometry dump. These are
// the single source of truth for the geometry verification.
constexpr int kPopoverWidth = 300;
constexpr int kRootMargin = 1;
constexpr int kHeaderH = 40; // on the 4px grid (was 36/flex)
constexpr int kSegPad = 2;
constexpr int kSegGap = 2;
constexpr int kSegBtnH = 24; // clean grid value (was 22); the mock's was ~25 flex
constexpr int kBodyMarginTop = 9, kBodyMarginBottom = 8, kBodyMarginLr = 12;
constexpr int kBodyGapSegPipe = 8;
constexpr int kPipelineH = 3;
constexpr int kTile = 32;      // on the 4px grid (was 30); fills the row, leaves ~9px
constexpr int kTileGlyph = 28; // on the 4px grid (was 26), fits in the 32px tile
constexpr int kTileGap = 4;
constexpr int kVsepW = 9; // thin hairline separator (1px line + 4px margins), not a tile slot
constexpr int kBeGlyphW = 18, kBeGlyphH = 12;
constexpr int kSliderHead = 18; // knob (14px) + 2px margin top/bottom so its top edge isn't clipped

// value-axis headroom (mirrors the mock's VMIN/VMAX)
constexpr double kVMIN = -0.35, kVMAX = 1.35;

// Picker tile catalog in display order, keyed by position, not enum value. The
// head is the three non-curve entries - Linear, Floor, Round - plus the AUTO
// PRESET, which is not a type any more: it draws a Custom curve with BOTH sides
// Auto (Blender's automatic handles) and clicking it writes those modes. The
// ACTION is segment-level: automatic handles apply to the ONE segment AFTER each
// selected key, so the host that owns the keys writes that key's type and
// outgoing side plus the next key's incoming side (see
// ctrl::TimeLineEditor::assignSelectionAutoHandles). The hairline separator
// closes that head; the analytic family then follows in the table's order. The
// type entries are asserted equal to the display table's non-Custom entries
// position by position by tests/picker_probe.cpp #10; the preset is the one
// entry that is a MODE pair rather than a Type.
struct TileDef {
    util::Easing::Type type;
    bool autoPreset;
};
const TileDef kTileOrder[] = {
    { util::Easing::Type_Linear, false }, { util::Easing::Type_Floor, false },
    { util::Easing::Type_Round, false },  { util::Easing::Type_Custom, true },
    { util::Easing::Type_Cubic, false },  { util::Easing::Type_Sine, false },
    { util::Easing::Type_Quad, false },   { util::Easing::Type_Quart, false },
    { util::Easing::Type_Quint, false },  { util::Easing::Type_Expo, false },
    { util::Easing::Type_Circ, false },   { util::Easing::Type_Back, false },
    { util::Easing::Type_Elastic, false }, { util::Easing::Type_Bounce, false },
};
constexpr int kTileOrderCount = int(sizeof(kTileOrder) / sizeof(kTileOrder[0]));

// THE Auto preview context (see the header): one segment wide, a neighbour one
// segment away on each side at the segment's own end values, both sides Auto. The
// REAL rule - Easing::controlPoint - turns it into the canonical ease-in-out, so
// the shape follows the derivation rather than a hardcoded path. It is not
// constexpr-inline in the header on purpose: ONE definition, used by the tile, the
// header's quick button and the tests.

// Which context a glyph draws with. The catalog's Auto preset tile always uses
// the canonical pair above; every other glyph (the blend thumbnails, the motion
// preview, the timeline's chip through prop::EasingItem) draws the real
// AutoContext of the keys it describes, so a preview stays honest.
const util::Easing::AutoContext& glyphAutoContext(bool aAutoPreset,
                                                  const util::Easing::AutoContext& aShown) {
    // One canonical pair, defined once (see autoPreviewContext).
    static const util::Easing::AutoContext kPreset = gui::prop::autoPreviewContext();
    return aAutoPreset ? kPreset : aShown;
}

QString tileName(int aIdx) { return QString::fromLatin1("tile-%1").arg(aIdx); }

util::Easing::Param mkParam(util::Easing::Type aType, util::Easing::Range aRange,
                            float aWeight) {
    util::Easing::Param p;
    p.type = aType;
    p.range = aRange;
    p.weight = aWeight;
    return p;
}

// The blend slider is now a plain self-driving QWidget (BlendControl, defined
// below at file scope) — the SAME mechanism as the bezier editor. See the
// BlendControl definition and the comment just above it.

QString rgba(QColor c, double aAlpha) {
    c.setAlphaF(aAlpha);
    return c.name(QColor::HexArgb);
}

const theme::Colors& C() { return theme::Colors::current(); }

// Close ✕ — the phosphor "x" glyph, loaded as the tinted theme icon
// (theme::iconDir()/x.svg, muted at rest, x_hover.svg/text on hover). Reads
// #popover-close QSS for the hover raised background. Rendered at 14px centered
// in the 22px button.
class CloseGlyph : public QToolButton {
public:
    explicit CloseGlyph(QWidget* aParent = nullptr)
        : QToolButton(aParent) {
        setObjectName("popover-close");
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(22, 22);
        setIconSize(QSize(14, 14));
        setAutoRaise(true);
        setText(QString()); // icon glyph
        updateIcon(false);
    }

protected:
    void enterEvent(QEnterEvent* aEvent) override {
        QToolButton::enterEvent(aEvent);
        updateIcon(true);
    }
    void leaveEvent(QEvent* aEvent) override {
        QToolButton::leaveEvent(aEvent);
        updateIcon(false);
    }

private:
    void updateIcon(bool aHover) {
        // x.svg (muted) at rest, x_hover.svg (text) on hover.
        const QString stem = aHover ? QStringLiteral("x_hover") : QStringLiteral("x");
        setIcon(QIcon(theme::iconDir() + "/" + stem + ".svg"));
    }
};

// Animation preview gauge (.be-strip in the mock): an 18px strip to the right
// of the bezier editor showing the live motion value on the value axis — a
// dot (ball) that moves as the preview animates (vertical track + accent ball).
class PreviewGauge : public QWidget {
public:
    explicit PreviewGauge(QWidget* aParent = nullptr) : QWidget(aParent) {
        setObjectName("be-strip");
        setFixedWidth(18);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    }
    void setValue(float v) { mValue = v; update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter pt(this);
        pt.setRenderHint(QPainter::Antialiasing, true);
        const theme::Colors& c = theme::Colors::current();
        // Mock .be-y-dot: an 8px white dot pinned to the strip's right edge
        // (right:0), top = (1.5 - v)/2 * height — the SAME value axis as the
        // bezier stage (-0.5..1.5). No track line.
        constexpr double kVMIN = -0.5, kVMAX = 1.5;
        const double v = std::clamp((double)mValue, kVMIN, kVMAX);
        const qreal top = (kVMAX - v) / (kVMAX - kVMIN) * height();
        pt.setPen(Qt::NoPen);
        pt.setBrush(c.text); // --text (near-white), not accent-bright
        pt.drawEllipse(QPointF(width() - 5.0, top + 5.0), 5.0, 5.0);
    }
private:
    float mValue = 0.5f;
};

} // namespace

// Declared in the header (gui::prop::autoPreviewContext) and DEFINED HERE, outside the
// file's anonymous namespace: a definition inside it would be a second, file-local
// overload, and every qualified call would then be ambiguous (measured: "call to
// autoPreviewContext is ambiguous", and the qualified ones link-failed against this
// one's local name).
util::Easing::AutoContext autoPreviewContext() {
    util::Easing::AutoContext ctx;
    // A symmetric neighbourhood whose CENTRAL differences are zero at both ends
    // of the drawn segment: the keys before and after sit at the opposite end
    // value, so the segment leaves and arrives flat. That is the canonical
    // automatic ease-in-out, (1/3, 0) / (2/3, 1), produced by the real
    // derivation rather than hardcoded.
    ctx.prev = { true, -1.0f, 1.0f };
    ctx.next = { true, 2.0f, 0.0f };
    ctx.inMode = util::Easing::kAutoPresetHandleMode;
    return ctx;
}

// BlendControl — the blending weight slider, drawn and driven by this widget
// itself rather than by QSlider. It gates its drag on its own mDown flag, not
// on event->buttons(): Qt's popup path re-targets an outside move with
// buttons() = NoButton (dropping the held LeftButton), so a buttons() check
// would never fire once the cursor leaves the popup and the drag would freeze.
// It self-paints and self-drives (no QSlider state machine, no grabMouse, no
// activateWindow), matching the bezier editor's drag mechanism.
class BlendControl : public QWidget {
public:
    explicit BlendControl(QWidget* aParent = nullptr) : QWidget(aParent) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMouseTracking(true);
    }
    void setRange(int aMin, int aMax) { mMin = aMin; mMax = aMax; }
    int value() const { return mValue; }
    void setValue(int v) {
        v = qBound(mMin, v, mMax);
        if (v == mValue) return;
        mValue = v;
        if (onValueChanged) onValueChanged(v);
        update();
    }
    bool isDown() const { return mDown; }

    std::function<void(int)> onValueChanged; // live change (chip / preview)
    std::function<void()> onPressed;         // begin a coalesced gesture
    std::function<void()> onReleased;        // end a coalesced gesture

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter pt(this);
        pt.setRenderHint(QPainter::Antialiasing, true);
        const theme::Colors& c = theme::Colors::current();
        const QRectF rc = QRectF(contentsRect());
        const qreal trackY = rc.center().y() - kTrackThickness / 2.0;
        const QRectF track(rc.left(), trackY, rc.width(), kTrackThickness);
        const qreal tr = kTrackThickness / 2.0;

        pt.setPen(Qt::NoPen);
        // 4px recessed -> accent-bright gradient track (the mock's range input).
        QLinearGradient grad(track.topLeft(), track.topRight());
        grad.setColorAt(0.0, c.recessed);
        grad.setColorAt(1.0, c.accentBright);
        pt.setBrush(grad);
        pt.drawRoundedRect(track, tr, tr);

        // 14px round knob: white fill + 2px accent-bright border (border-box).
        const QPointF kp(headCenter(), rc.center().y());
        pt.setBrush(c.text);
        pt.drawEllipse(kp, kHeadRadius - 1.0, kHeadRadius - 1.0);
        QPen pen(c.accentBright);
        pen.setWidthF(2.0);
        pt.setPen(pen);
        pt.setBrush(Qt::NoBrush);
        pt.drawEllipse(kp, kHeadRadius - 1.0, kHeadRadius - 1.0);
    }
    void mousePressEvent(QMouseEvent* aEvent) override {
        if (aEvent->button() == Qt::LeftButton) {
            mDown = true;
            if (onPressed) onPressed();
            setValue(valueFromPixel(aEvent->position().toPoint().x()));
            aEvent->accept();
            return;
        }
        QWidget::mousePressEvent(aEvent);
    }
    void mouseMoveEvent(QMouseEvent* aEvent) override {
        // Gate on mDown ONLY — the same as the bezier editor's m_dragging.
        // Qt's popup path re-targets an outside move with buttons() = NoButton
        // (it drops the held LeftButton for re-delivered moves), so requiring
        // buttons() & LeftButton here would never fire once the cursor leaves
        // the popup and the drag freezes at the leave value. mDown is the
        // authoritative "we are dragging" state.
        if (mDown) {
            setValue(valueFromPixel(aEvent->position().toPoint().x()));
            aEvent->accept();
            return;
        }
        QWidget::mouseMoveEvent(aEvent);
    }
    void mouseReleaseEvent(QMouseEvent* aEvent) override {
        if (mDown) {
            mDown = false;
            setValue(valueFromPixel(aEvent->position().toPoint().x()));
            if (onReleased) onReleased();
            aEvent->accept();
            return;
        }
        QWidget::mouseReleaseEvent(aEvent);
    }
    void hideEvent(QHideEvent* aEvent) override {
        // The popup can close mid-drag (e.g. the release is swallowed when it
        // happens outside the popup / other Qt windows), so the release that
        // would clear mDown never arrives. Cancel the drag state on hide so the
        // control doesn't think it's still dragging when the popup reopens.
        mDown = false;
        QWidget::hideEvent(aEvent);
    }

private:
    static constexpr int kTrackThickness = 4; // == AppStyle track
    static constexpr int kHeadDiameter = 14;  // == AppStyle knob (2*7)
    static constexpr qreal kHeadRadius = 7.0;
    int mMin = 0, mMax = 100;
    int mValue = 100;
    bool mDown = false;

    qreal headCenter() const {
        const QRectF rc = QRectF(contentsRect());
        const qreal span = qMax<qreal>(1.0, rc.width() - kHeadDiameter);
        const qreal f = mMax > mMin ? qreal(mValue - mMin) / qreal(mMax - mMin) : 0.0;
        return rc.left() + kHeadRadius + f * span;
    }
    int valueFromPixel(int aX) const {
        const QRectF rc = QRectF(contentsRect());
        const qreal span = qMax<qreal>(1.0, rc.width() - kHeadDiameter);
        const qreal f = qBound(0.0, (aX - (rc.left() + kHeadRadius)) / span, 1.0);
        return mMin + qRound(f * qreal(mMax - mMin));
    }
};

// ---------------------------------------------------------------------------
// FlowLayout — minimal flex-wrap: place left->right, wrap when the row fills,
// uniform gap. Reproduces the mock's flex-wrap catalog (trow) so it wraps at
// the same point.
// ---------------------------------------------------------------------------
class FlowLayout : public QLayout {
public:
    FlowLayout(QWidget* aParent, int aGap) : QLayout(aParent), mGap(aGap) {
        setContentsMargins(0, 0, 0, 0);
    }
    ~FlowLayout() {
        while (QLayoutItem* it = takeAt(0)) delete it;
    }
    void addItem(QLayoutItem* aItem) override { mItems.append(aItem); }
    int count() const override { return mItems.size(); }
    QLayoutItem* itemAt(int aIndex) const override {
        return (aIndex < 0 || aIndex >= mItems.size()) ? nullptr : mItems.at(aIndex);
    }
    QLayoutItem* takeAt(int aIndex) override {
        if (aIndex < 0 || aIndex >= mItems.size()) return nullptr;
        return mItems.takeAt(aIndex);
    }
    Qt::Orientations expandingDirections() const override { return {}; }
    QSize sizeHint() const override {
        return QSize(200, doLayout(QRect(0, 0, 200, 0), true));
    }
    QSize minimumSize() const override { return QSize(0, 0); }
    int heightForWidth(int aWidth) const override {
        return doLayout(QRect(0, 0, aWidth, 0), true);
    }
    bool hasHeightForWidth() const override { return true; }
    void setGeometry(const QRect& aRect) override {
        QLayout::setGeometry(aRect);
        doLayout(aRect, false);
    }

private:
    int doLayout(const QRect& aRect, bool aTestOnly) const {
        int x = aRect.x(), y = aRect.y(), rowH = 0;
        for (QLayoutItem* it : mItems) {
            if (it->isEmpty()) continue;
            const QSize sz = it->sizeHint();
            if (x + sz.width() > aRect.right() + 1 && x > aRect.x()) {
                x = aRect.x();
                y += rowH + mGap;
                rowH = 0;
            }
            if (!aTestOnly) it->setGeometry(QRect(QPoint(x, y), sz));
            x += sz.width() + mGap;
            rowH = qMax(rowH, sz.height());
        }
        return y + rowH - aRect.y();
    }

    QList<QLayoutItem*> mItems;
    int mGap;
};

// ---------------------------------------------------------------------------
// paintEasingCurve + CurveGlyph
// ---------------------------------------------------------------------------
void paintEasingCurve(QPainter& p, const QRectF& aRect,
                      const util::Easing::Param& aParam,
                      const QColor& aColor, qreal aStrokeWidth,
                      const util::Easing::AutoContext* aAuto,
                      bool aAutoHandles) {
    const qreal w = aRect.width(), h = aRect.height();
    if (w <= 1 || h <= 1) return;

    // Reference (curveD in mock.js) computes the path in a 100x100 viewBox
    // with pad=2 and value axis [kVMIN,kVMAX], then non-uniformly scales to
    // the target rect via preserveAspectRatio="none". Reproduce that scaling.
    constexpr double VW = 100.0, VH = 100.0;
    const double pad = 2.0, span = VH - 2.0 * pad;
    const double x0 = pad, x1 = VW - pad;
    auto toScreen = [&](double vx, double vy) {
        return QPointF(aRect.x() + vx * w / VW, aRect.y() + vy * h / VH);
    };

    QPen pen(aColor);
    pen.setWidthF(aStrokeWidth);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);

    if (aParam.type == util::Easing::Type_Floor) {
        const double y0 = pad + (kVMAX - 0.0) / (kVMAX - kVMIN) * span;
        const double y1 = pad + (kVMAX - 1.0) / (kVMAX - kVMIN) * span;
        QPainterPath path;
        path.moveTo(toScreen(x0, y0));
        path.lineTo(toScreen(x1, y0));
        path.lineTo(toScreen(x1, y1));
        p.drawPath(path);
        return;
    }

    // THE AUTO ICON SHOWS THE PRESET, NOT ONE KEY'S LUCK. With no neighbouring
    // keys the derived handles are the identity, so an automatic pair would draw
    // Linear's straight line - the glyph the user could not tell apart from
    // Linear's. A menu entry names an EASING (every other tile draws its canonical
    // shape), so an automatic entry with no neighbours draws the characteristic
    // curve: a segment whose BOTH sides continue (neighbours outside the value
    // range), which is the flat-at-both-ends shape an automatic handle produces.
    const bool derivedSides = aParam.type == util::Easing::Type_Custom
        && (util::Easing::isDerived(aParam.handleOut) || util::Easing::isDerived(aAuto ? aAuto->inMode
                                                                                       : util::Easing::HandleMode_Aligned));
    const util::Easing::AutoContext emptyAuto;
    const util::Easing::AutoContext presetAuto = gui::prop::autoPreviewContext();
    const bool noNeighbours = !(aAuto && aAuto->prev.valid) && !(aAuto && aAuto->next.valid);
    const util::Easing::AutoContext& autoCtx =
        (aAutoHandles && derivedSides && noNeighbours) ? presetAuto
                                                       : (aAuto ? *aAuto : emptyAuto);

    const int N = 64;
    QPainterPath path;
    bool first = true;
    for (int i = 0; i <= N; ++i) {
        const double x = (double)i / N;
        // The 6-argument form: it reads the stored pair for a Custom curve whose
        // sides are both stored, and derives each side by its own mode else.
        const double y = util::Easing::calculate(aParam, autoCtx, x, 0.0f, 1.0f, 1.0f);
        const double vx = x0 + x * (x1 - x0);
        const double vy = pad + (kVMAX - y) / (kVMAX - kVMIN) * span;
        const QPointF s = toScreen(vx, vy);
        if (first) { path.moveTo(s); first = false; }
        else path.lineTo(s);
    }
    p.drawPath(path);

    // ---- the automatic-handle glyph ----------------------------------------
    // The catalog tile (and a header button) draws an automatic easing as what
    // it IS: a bezier whose control points were derived from the keys, with
    // those points visible as detached dots joined to the endpoints by short
    // stems. The flag is opt-in and only a Custom curve with a derived side
    // reads it, so every other glyph - and every preview that leaves the flag
    // off - paints exactly the curve it painted before. The dot radius and stem
    // width scale with the rect, so the same call reads at the 28px tile and at
    // a 16-18px button.
    if (aAutoHandles && derivedSides) {
        // The curve is sampled over one segment (d = 1), so derive its control
        // points in that same span. A stored side returns its stored point.
        const QPointF c1 = util::Easing::controlPoint(aParam, autoCtx.inMode, autoCtx, 1.0f, 0);
        const QPointF c2 = util::Easing::controlPoint(aParam, autoCtx.inMode, autoCtx, 1.0f, 1);
        auto toValueSpace = [&](double aX, double aY) {
            return toScreen(x0 + aX * (x1 - x0),
                            pad + (kVMAX - aY) / (kVMAX - kVMIN) * span);
        };
        const QPointF from = toValueSpace(0.0, 0.0);
        const QPointF to = toValueSpace(1.0, 1.0);
        const QPointF h1 = toValueSpace(c1.x(), c1.y());
        const QPointF h2 = toValueSpace(c2.x(), c2.y());

        QPen stemPen(aColor);
        stemPen.setWidthF(qMax(0.9, aStrokeWidth * 0.75));
        stemPen.setCapStyle(Qt::RoundCap);
        p.setPen(stemPen);
        p.drawLine(from, h1);
        p.drawLine(to, h2);

        // Sized to READ at a 16-18px button: the old 6% floor drew a ~1px speck
        // that disappeared into the stroke beside it.
        const qreal dotR = qBound(1.6, qMin(w, h) * 0.085, 2.4);
        p.setPen(Qt::NoPen);
        p.setBrush(aColor);
        p.drawEllipse(h1, dotR, dotR);
        p.drawEllipse(h2, dotR, dotR);
        p.setBrush(Qt::NoBrush);
    }
}

CurveGlyph::CurveGlyph(QWidget* aParent) : QWidget(aParent) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}
void CurveGlyph::setParam(const util::Easing::Param& aParam) { mParam = aParam; update(); }
void CurveGlyph::setAutoContext(const util::Easing::AutoContext& aContext) {
    mAuto = aContext;
    update();
}
void CurveGlyph::setGlyphColor(const QColor& aColor) { mColor = aColor; update(); }
void CurveGlyph::setStrokeWidth(double aWidth) { mStrokeWidth = aWidth; update(); }
void CurveGlyph::setAutoHandlesVisible(bool aVisible) { mAutoHandles = aVisible; update(); }

void CurveGlyph::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    paintEasingCurve(p, QRectF(0, 0, width(), height()), mParam,
                     mColor.isValid() ? mColor : palette().color(QPalette::WindowText),
                     mStrokeWidth, &mAuto, mAutoHandles);
}

// ---------------------------------------------------------------------------
// EasingPickWidget
// ---------------------------------------------------------------------------
EasingPickWidget::~EasingPickWidget() {
    // The slider's proxy style is owned by the slider widget itself
    // (QWidget::setStyle transfers ownership); it is cleaned up when the
    // child slider is destroyed during ~QWidget. Do NOT delete it here: the
    // base destructor runs after this body and the slider would be painted
    // with a dangling style.
}

EasingPickWidget::EasingPickWidget(const util::Easing::Param& aInitial, QWidget* aParent)
    : QFrame(aParent), mDraft(aInitial), mApplied(aInitial) {
    setObjectName("popover");
    setFixedWidth(kPopoverWidth);
    // Rounded corners need a real-alpha window: a Qt::Popup (top-level) plus
    // a translucent backing, with FramelessWindowHint so the backing is a
    // composited alpha surface rather than an opaque square (on X11/Wayland
    // the compositor handles the alpha; on Windows this flag is what makes Qt
    // layer the window at all, so the alpha is honoured — an unlayered window
    // drops it and paints the corner cut black). Shaping is done either by the
    // platform (Windows 11 22H2+, see showEvent) or by the paintEvent mask.
    // Setting the flags here means the widget
    // is also a correct popup when created standalone (tests), independent of
    // the owner also setting them (setting twice is a no-op).
    if (!testAttribute(Qt::WA_TranslucentBackground))
        setAttribute(Qt::WA_TranslucentBackground, true);
    if (!(windowFlags() & Qt::FramelessWindowHint))
        setWindowFlag(Qt::FramelessWindowHint, true);
    // WA_StyledBackground would fill the backing (black corners); the rounded
    // body is painted in paintEvent instead. Leave it off.

    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(kRootMargin, kRootMargin, kRootMargin, kRootMargin);
    root->setSpacing(0);
    root->addWidget(buildHeader());
    mBody = buildBody();
    root->addWidget(mBody);

    // Live motion preview: animate the pipeline fill with the current easing.
    // mAnimTimer was declared but never created/connected — that's why the
    // preview was frozen. Create it here and start it in showEvent.
    mAnimTimer = new QTimer(this);
    mAnimTimer->setInterval(16); // ~60fps
    connect(mAnimTimer, &QTimer::timeout, this, &EasingPickWidget::animStep);

    // Session undo/redo (per-step inside the popup). A discrete edit records one
    // entry as it happens; a continuous gesture (blend slider, bezier handle, one
    // typed spin value) records ONE entry when it finishes, so undo steps back a
    // whole gesture, not each tick. The blend slider's gesture start/stop arrive
    // through BlendControl callbacks (onPressed / onReleased) wired in buildBlend.
    // The bezier editor mutates the draft while a tangent handle is dragged;
    // beginGesture snapshots the pre-drag curve, endGesture folds the drag into
    // one history entry. Every pointer move reports its step (curveDragged), so
    // the model and every view follow the handle live - through ONE owner-held
    // command (liveEditStarted/liveEditEnded), which is what keeps the whole
    // drag a single undo entry in the DOCUMENT's history too.
    // The session spans the POPUP, not the gesture: the owner's one command stays
    // open until the popup closes (the rule the blend slider's close-time commit
    // already follows), so two drags in one open are still one undo entry. Ending
    // it at a release would make every drag its own entry.
    if (mBezierEditor) {
        connect(mBezierEditor, &BezierCurveEditor::dragStarted, this, &EasingPickWidget::beginGesture);
        connect(mBezierEditor, &BezierCurveEditor::dragStarted, this, &EasingPickWidget::beginLiveEdit);
        connect(mBezierEditor, &BezierCurveEditor::curveDragged, this, &EasingPickWidget::onBezierDragStep);
        connect(mBezierEditor, &BezierCurveEditor::editingFinished, this, &EasingPickWidget::endGesture);
    }

    applyAppearance();
    refresh();

    // Clicking a non-text child (the curve editor, a label, the background)
    // must release the active spinbox's edit focus. Those widgets are
    // Qt::NoFocus, so a plain click there never hands focus off the field.
    // Watch every widget so any press that isn't on a text editor clears it.
    installEventFilter(this);
    for (QWidget* w : findChildren<QWidget*>())
        w->installEventFilter(this);
}

void EasingPickWidget::applyAppearance() {
    const theme::Colors& c = C();
    // When mHideText is set, every textual glyph is painted fully transparent
    // (alpha 0) — each widget's box, background and border stay identical, so
    // the layout is bit-for-bit unchanged. Graphics that are drawn directly
    // (curve glyphs, bezier editor, gauge, close ✕, slider) use their own
    // explicit colors and are unaffected by these QSS color rules.
    // Emit opaque text as #RRGGBB and hidden text as the #AARRGGBB zero-alpha
    // form (QColor::name() alone would drop the alpha and paint opaque black).
    const auto textCol = [](bool aHide, const QColor& aC) {
        return aHide ? QStringLiteral("#00000000") : aC.name();
    };
    const QString text = textCol(mHideText, c.text);
    // The mock's muted glyphs/labels are --text-muted (#737373), but the dark
    // theme reads textMuted as neutral-400 (#a3a3a3, brighter). Use the dim
    // token so the picker's muted text matches the mock.
    const QString muted = textCol(mHideText, c.textMutedDim);
    const QString bright = textCol(mHideText, c.accentBright);
    const QString accentText = textCol(mHideText, c.accentText);
    QString q;
    q += "QFrame#popover { background: " + c.base.name() + "; }\n";
    q += "QWidget#popover-head { border-bottom: 1px solid " + c.hairline.name() + "; }\n";
    q += "QLabel#popover-title { font-size: 12px; font-weight: 600; color: " + text + "; }\n";
    q += "QToolButton#popover-close { background: transparent; border: 0; border-radius: 4px; color: " + muted + "; font-size: 12px; }\n";
    q += "QToolButton#popover-close:hover { color: " + text + "; background: " + c.raised.name() + "; }\n";
    q += "QToolButton[easingRole=\"seg\"] { background: transparent; border: 0; border-radius: 4px; color: " + muted + "; font-size: 11px; padding: 0 4px; }\n";
    q += "QToolButton[easingRole=\"seg\"]:hover { color: " + text + "; background: " + c.raised.name() + "; }\n";
    q += "QToolButton[easingRole=\"seg\"]:checked { background: " + c.active.name() + "; color: " + text + "; }\n";
    q += "QToolButton[easingRole=\"tile\"] { background: transparent; border: 0; border-radius: 5px; color: " + muted + "; }\n";
    q += "QToolButton[easingRole=\"tile\"]:hover { background: " + c.raised.name() + "; color: " + text + "; }\n";
    q += "QToolButton[easingRole=\"tile\"]:checked { background: " + rgba(c.accent, 0.22) + "; color: " + bright + "; }\n";
    q += "QPushButton[role=\"bact\"] { background: " + c.raised.name() + "; border: 0; border-radius: 4px; color: " + muted + "; font-size: 10px; padding: 5px; }\n";
    q += "QPushButton[role=\"bact\"]:hover { background: " + c.raisedHover.name() + "; color: " + text + "; }\n";
    // The value column of a labelled field (.fi), for the BEZIER SPINS only.
    // A combo is not given a field role of its own: the two per-side mode combos
    // are built as ordinary QComboBox, exactly like the property panel's
    // ComboItem, and styled by the app's OWN combo rule below. There is no
    // per-widget stylesheet on either combo.
    const QString field =
        "QLabel[role=\"fl\"] { font-size: 9px; color: " + muted + "; }\n"
        "QDoubleSpinBox[role=\"fi\"] { background: " + c.raised.name()
        + "; border: 1px solid " + c.raised.name() + "; border-radius: 4px; color: " + text
        + "; font-size: 11px; padding: 3px 5px; }\n"
        "QDoubleSpinBox[role=\"fi\"]:hover { border-color: " + c.hover.name() + "; }\n"
        "QDoubleSpinBox[role=\"fi\"]:focus { border-color: " + c.focus.name() + "; }\n"
        "QDoubleSpinBox[role=\"fi\"]:hover:pressed { background: " + c.recessed.name() + "; }\n";
    q += field;
    // THE PROPERTY PANEL'S COMBO, verbatim in behaviour: propertywidget.ssa's
    // shared field box (raised at rest, a geometry-reserving 1px border that only
    // recolours on hover/focus) plus its caret-down drop-down. It is selected by
    // the CLASS alone, so a QComboBox anywhere in this popover (there are exactly
    // the two mode combos) is the same control the property rows use - no role
    // attribute, no object name, no per-combo rule. The font is deliberately
    // inherited (the app font), as it is in the property panel.
    q += "QComboBox { background-color: " + c.raised.name()
        + "; padding-top: 1px; padding-bottom: 1px; padding-left: 4px; padding-right: 4px;"
        " border: 1px solid " + c.raised.name() + "; border-radius: 4px; color: " + text
        + "; min-height: 1.25em; }\n";
    q += "QComboBox:hover { border-color: " + c.hover.name() + "; }\n";
    q += "QComboBox:focus { border-color: " + c.focus.name() + "; }\n";
    q += "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: right;"
        " width: 1em; height: 1em; border: 0; background: transparent; }\n";
    q += "QComboBox::down-arrow { image: url(" + theme::iconDir()
        + "/caret-down.svg); width: 0.625em; height: 0.625em; }\n";
    setStyleSheet(q);
}

void EasingPickWidget::setHideText(bool aHide) {
    if (mHideText == aHide) return;
    mHideText = aHide;
    // The title and blend-value labels carry their own inline stylesheet, so
    // re-apply them with a transparent (or normal) text glyph to match. Use the
    // HexArgb transparent form (the zero-alpha "#00000000").
    const QString tcol = mHideText ? QStringLiteral("#00000000") : C().text.name();
    const QString mcol = mHideText ? QStringLiteral("#00000000") : C().textMutedDim.name();
    if (mTitle)
        mTitle->setStyleSheet("font-size:12px;font-weight:600;color:" + tcol);
    if (mBlendVal)
        mBlendVal->setStyleSheet("font-size:10px;color:" + mcol);
    applyAppearance();
    refresh();
}

QWidget* EasingPickWidget::buildHeader() {
    QWidget* head = new QWidget(this);
    mHead = head;
    head->setObjectName("popover-head");
    head->setFixedHeight(kHeaderH);
    head->setAttribute(Qt::WA_StyledBackground, true);
    QHBoxLayout* l = new QHBoxLayout(head);
    l->setContentsMargins(12, 0, 8, 0);
    l->setSpacing(6);
    // The header names the DRAFT'S Param, not its interpolation type: a Custom
    // curve is named by its two handle modes, so the Auto preset reads "Auto
    // Clamped" here - never the type name "Custom". refresh() keeps it in step.
    mTitle = new QLabel(util::Easing::getParamName(mDraft), head);
    mTitle->setObjectName("popover-title");
    mTitle->setStyleSheet("font-size:12px;font-weight:600;color:" + C().text.name());
    l->addWidget(mTitle);
    l->addStretch(1);
    // CloseGlyph draws a thin X matching the mock's SVG glyph (SetText("✕")
    // Unicode was ~5px and off-center). Uses the QSS #popover-close rules for
    // the hover background.
    CloseGlyph* close = new CloseGlyph(head);
    close->setToolTip(tr("Close"));
    connect(close, &QToolButton::clicked, this, [this] { emit dismissed(); });
    l->addWidget(close);
    return head;
}

QWidget* EasingPickWidget::buildBody() {
    QWidget* body = new QWidget(this);
    mBody = body;
    body->setObjectName("popover-body");
    QVBoxLayout* l = new QVBoxLayout(body);
    l->setContentsMargins(kBodyMarginLr, kBodyMarginTop, kBodyMarginLr, kBodyMarginBottom);
    // Uniform 8px body spacing so that a hidden widget (the motion-preview bar
    // and the normal panel in Custom mode) collapses away entirely, matching
    // the reference mock where the hidden pipeline's margin disappears. The
    // seg<->pipeline and pipeline<->trow gaps are both 8px, so a single
    // spacing value reproduces the normal-mode layout exactly.
    l->setSpacing(kBodyGapSegPipe);

    l->addWidget(buildSeg((util::Easing::Range)mDraft.range));
    l->addWidget(buildPipeline());

    mNormalPanel = new QWidget(body);
    // The popup is fixed to a per-mode height (kNormalHeight). Make this panel
    // fill the body's leftover height so the tile grid sits just below the
    // header and the blend slider is pushed to the bottom (end-aligned), with
    // the surplus as a gap between them — instead of the blend getting centered
    // in the middle of a stretched panel.
    mNormalPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    QVBoxLayout* n = new QVBoxLayout(mNormalPanel);
    n->setContentsMargins(0, 0, 0, 0);
    n->setSpacing(0);
    n->addWidget(buildTrow());
    n->addStretch(1);   // surplus space goes between the tiles and the blend
    n->addWidget(buildBlend());
    l->addWidget(mNormalPanel);

    mBezierPanel = buildBezierPanel();
    mBezierPanel->setVisible(false);
    mBezierPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    l->addWidget(mBezierPanel);

    return body;
}

QWidget* EasingPickWidget::buildSeg(util::Easing::Range) {
    QFrame* seg = new QFrame(this);
    seg->setObjectName("seg");
    seg->setAttribute(Qt::WA_StyledBackground, true);
    // Scope to #seg so the selector-less rule doesn't cascade `background:
    // recessed` onto the child buttons (which would override their :checked
    // active fill).
    seg->setStyleSheet("#seg { background:" + C().recessed.name() + ";border-radius:6px; }");
    mSegLayout = new QGridLayout(seg);
    mSegLayout->setContentsMargins(kSegPad, kSegPad, kSegPad, kSegPad);
    mSegLayout->setSpacing(kSegGap);
    for (int i = 0; i < 4; ++i) mSegLayout->setColumnStretch(i, 1);

    const char* labels[4] = { "In", "Out", "In\u2013Out", "Custom" };
    const char* names[4] = { "seg-r-0", "seg-r-1", "seg-r-2", "seg-r-custom" };
    for (int i = 0; i < 4; ++i) {
        QToolButton* b = new QToolButton(seg);
        b->setObjectName(names[i]);
        b->setText(QString::fromUtf8(labels[i]));
        b->setCheckable(true);
        b->setProperty("easingRole", "seg");
        b->setFixedHeight(kSegBtnH);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setCursor(Qt::PointingHandCursor);
        mSegBtns.append(b);
        mSegLayout->addWidget(b, 0, i);
    }
    connect(mSegBtns[0], &QToolButton::clicked, this, [this] { setRange(util::Easing::Range_In); });
    connect(mSegBtns[1], &QToolButton::clicked, this, [this] { setRange(util::Easing::Range_Out); });
    connect(mSegBtns[2], &QToolButton::clicked, this, [this] { setRange(util::Easing::Range_InOut); });
    connect(mSegBtns[3], &QToolButton::clicked, this, [this] { setCustom(); });
    return seg;
}

QWidget* EasingPickWidget::buildPipeline() {
    QWidget* pipe = new QWidget(this);
    mPipeline = pipe;
    pipe->setObjectName("pipeline");
    pipe->setFixedHeight(kPipelineH);
    pipe->setStyleSheet("background:" + C().raised.name() + ";border-radius:2px;");
    QVBoxLayout* l = new QVBoxLayout(pipe);
    l->setContentsMargins(0, 0, 0, 0);
    mPipelineFill = new QWidget(pipe);
    mPipelineFill->setObjectName("pipeline-fill");
    mPipelineFill->setFixedHeight(kPipelineH);
    mPipelineFill->setFixedWidth(137); // 50% of the 274px pipeline (fixed-width popover)
    mPipelineFill->setStyleSheet("background:" + C().accent.name() + ";border-radius:2px;");
    l->addWidget(mPipelineFill, 0, Qt::AlignLeft);
    return pipe;
}

QWidget* EasingPickWidget::buildTrow() {
    mTrow = new QWidget(this);
    mTrow->setObjectName("trow");
    FlowLayout* flow = new FlowLayout(mTrow, kTileGap);
    for (int i = 0; i < kTileOrderCount; ++i) {
        QToolButton* tile = makeTileAt(i, kTileOrder[i].type, kTileOrder[i].autoPreset);
        tile->setObjectName(tileName(i));
        flow->addWidget(tile);
        mTileBtns.append(tile);
        if (i == 3) {
            // The hairline closes the derived-easing cluster: the catalog's head
            // is Linear, Floor, Round, Auto preset (kTileOrder), so the
            // separator lands AFTER the preset and the first tile row is exactly
            // those four with the curve family following.
            QFrame* sep = new QFrame(mTrow);
            sep->setObjectName("vsep");
            sep->setFixedSize(kVsepW, kTile);
            QHBoxLayout* hl = new QHBoxLayout(sep);
            hl->setContentsMargins(4, 0, 4, 0);
            QWidget* line = new QWidget(sep);
            line->setFixedSize(1, kTile);
            line->setStyleSheet("background:" + C().hairline.name() + ";");
            hl->addWidget(line);
            flow->addWidget(sep);
        }
    }
    return mTrow;
}

QToolButton* EasingPickWidget::makeTileAt(int aIndex, util::Easing::Type aType, bool aAutoPreset) {
    const TileDef def{ aType, aAutoPreset };
    QToolButton* tile = new QToolButton(mTrow);
    tile->setFixedSize(kTile, kTile);
    tile->setCursor(Qt::PointingHandCursor);
    tile->setCheckable(true);
    tile->setAutoRaise(true);
    tile->setProperty("easingRole", "tile");
    QVBoxLayout* vl = new QVBoxLayout(tile);
    vl->setContentsMargins(0, 0, 0, 0); // center the glyph via flex alignment, not padding
    CurveGlyph* g = new CurveGlyph(tile);
    g->setObjectName("t-glyph");
    g->setFixedSize(kTileGlyph, kTileGlyph);
    util::Easing::Param glyph = mkParam(def.type, mDraft.range, mDraft.weight);
    if (def.autoPreset) {
        glyph.type = util::Easing::Type_Custom;
        glyph.handleIn = util::Easing::kAutoPresetHandleMode;
        glyph.handleOut = util::Easing::kAutoPresetHandleMode;
    }
    g->setParam(glyph);
    // The Auto preset tile draws the canonical automatic-handle pair, not the
    // edited key's neighbours (a tile is a catalog image; see autoPreviewContext).
    g->setAutoContext(glyphAutoContext(def.autoPreset, mAuto));
    // The preset's catalog image must SAY it is a bezier with derived handles:
    // draw the two control points and their stems. No other tile (nor the blend
    // thumbnails) opts in, so their glyphs are unchanged.
    g->setAutoHandlesVisible(def.autoPreset);
    g->setGlyphColor(C().textMutedDim);
    vl->addWidget(g, 0, Qt::AlignCenter);
    // The tile's NAME is the user-facing name of the Param it writes, from the
    // same function the chip and the header read: an ordinary type tile is its
    // easing name, and the mode preset is named by its two handle sides ("Auto
    // Clamped" for the clamped pair this tile creates). A glyph-only button has
    // no text, so this is also its accessible text.
    //
    // THE PRESET'S NAME SAYS WHAT THE CLICK DOES, not only the pair it stores:
    // automatic handles apply to one SEGMENT (a tangent pair belongs to a
    // segment), so the action makes the segment AFTER each selected key automatic
    // - that key's type and outgoing side plus the next key's incoming side, in
    // ONE undo step. A LAST key (or the only key) has no segment after it YET,
    // so it writes its OWN pair instead - type Custom and BOTH sides automatic -
    // and the segments that appear when a neighbour is added derive on both
    // sides. A screen-reader user gets the same words the tooltip shows.
    const QString tileLabel = util::Easing::getParamName(glyph);
    if (def.autoPreset) {
        const QString action = tr("Make the segment after the selected key automatic");
        tile->setAccessibleName(tr("%1 (%2)").arg(action, tileLabel));
        tile->setToolTip(tr("%1 - %2 handles").arg(action, tileLabel));
    } else {
        tile->setAccessibleName(tileLabel);
        tile->setToolTip(tr("Set the easing to %1").arg(tileLabel));
    }
    connect(tile, &QToolButton::clicked, this, [this, def] {
        if (def.autoPreset)
            this->setAutoHandlesPreset();
        else
            this->setType(def.type);
    });
    Q_UNUSED(aIndex);
    return tile;
}

QWidget* EasingPickWidget::buildBlend() {
    QWidget* wrap = new QWidget(this);
    wrap->setObjectName("blend-wrap");
    QHBoxLayout* l = new QHBoxLayout(wrap);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(8);

    mBlendLinear = new CurveGlyph(wrap);
    mBlendLinear->setObjectName("be-glyph-linear");
    mBlendLinear->setFixedSize(kBeGlyphW, kBeGlyphH);
    mBlendLinear->setParam(mkParam(util::Easing::Type_Linear, util::Easing::Range_InOut, 1.0f));
    mBlendLinear->setGlyphColor(C().textMutedDim);
    l->addWidget(mBlendLinear);

    mBlendInput = new BlendControl(wrap);
    mBlendInput->setObjectName("blend-input");
    mBlendInput->setRange(0, 100);
    mBlendInput->setValue(100);
    mBlendInput->setFixedHeight(kSliderHead); // >= 2*knobRadius so the knob isn't clipped
    // BlendControl paints itself (here, not via AppStyle) and drives its own
    // drag, matching the bezier editor's mechanism. It reports a live value
    // change, and gesture start/stop, through std::function callbacks.
    mBlendInput->onValueChanged = [this](int v) { setWeight(v / 100.0f); };
    mBlendInput->onPressed = [this] { beginGesture(); };
    mBlendInput->onReleased = [this] { endGesture(); };
    mBlendInput->setCursor(Qt::PointingHandCursor);
    mBlendInput->setFocusPolicy(Qt::NoFocus);
    l->addWidget(mBlendInput, 1);

    mBlendCurve = new CurveGlyph(wrap);
    mBlendCurve->setObjectName("be-glyph-curve");
    mBlendCurve->setFixedSize(kBeGlyphW, kBeGlyphH);
    mBlendCurve->setParam(mkParam(util::Easing::Type_Cubic, util::Easing::Range_InOut, 1.0f));
    mBlendCurve->setGlyphColor(C().textMutedDim);
    l->addWidget(mBlendCurve);

    mBlendVal = new QLabel(wrap);
    mBlendVal->setObjectName("blend-val");
    mBlendVal->setFixedWidth(30);
    mBlendVal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    mBlendVal->setStyleSheet("font-size:10px;color:" + C().textMutedDim.name() + ";");
    l->addWidget(mBlendVal);
    return wrap;
}

QWidget* EasingPickWidget::buildBezierPanel() {
    QWidget* panel = new QWidget(mBody);
    panel->setObjectName("bezier-panel");
    QVBoxLayout* l = new QVBoxLayout(panel);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(8);

    // The draggable curve editor (reuses the engine's editor; it is
    // authoritative: paintEvent syncs m_points <-> bezier <-> spins).
    mBezierEditor = new BezierCurveEditor(panel, &mDraft.cubicBezier, {}, &mBezierProgress);
    mBezierEditor->setObjectName("bezier-stage");
    // The mock's stage is a 1:1 square (the curve SVG's intrinsic aspect), so
    // pin the editor to its OWN width (274 body content − 18px gauge = 256),
    // not the full body width — a 256x256 square like the reference.
    mBezierEditor->setMinimumHeight(256);
    mBezierEditor->setMaximumHeight(256);
    mBezierEditor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // be-root: the graph is flex:1 (NOT full width) + the animation gauge to
    // its right, per the mock (.be-root{display:flex} .be-strip{width:18px}).
    QWidget* beroot = new QWidget(panel);
    beroot->setObjectName("be-root");
    QHBoxLayout* bel = new QHBoxLayout(beroot);
    bel->setContentsMargins(0, 0, 0, 0);
    bel->setSpacing(0);
    bel->addWidget(mBezierEditor, 1);
    mBezierGauge = new PreviewGauge(beroot);
    bel->addWidget(mBezierGauge);
    l->addWidget(beroot);

    // x1 y1 x2 y2 spins, wired so a typed edit lands in the draft bezier.
    const char* names[4] = { "x1", "y1", "x2", "y2" };
    QHBoxLayout* spins = new QHBoxLayout();
    spins->setSpacing(5);
    for (int i = 0; i < 4; ++i) {
        // bfield: a label (.fl) above the input (.fi), per the mock.
        QWidget* field = new QWidget(panel);
        field->setProperty("role", "bfield");
        QVBoxLayout* f = new QVBoxLayout(field);
        f->setContentsMargins(0, 0, 0, 0);
        f->setSpacing(2);
        QLabel* lab = new QLabel(QLatin1String(names[i]), field);
        lab->setProperty("role", "fl");
        lab->setFixedHeight(12);
        f->addWidget(lab);
        QDoubleSpinBox* s = new QDoubleSpinBox(field);
        s->setObjectName(QString::fromLatin1("bezier-") + QLatin1String(names[i]));
        s->setProperty("role", "fi");
        s->setLocale(QLocale::c());
        s->setRange(-2.0, 2.0);
        s->setSingleStep(0.01);
        s->setDecimals(2);
        s->setButtonSymbols(QAbstractSpinBox::NoButtons);
        s->setFixedHeight(24);
        f->addWidget(s);
        spins->addWidget(field);
        mBezierSpins.append(s);
        QDoubleSpinBox::connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                                this, [this, i](double v) { onBezierSpin(i, v); });
        // A typed number is ONE gesture: every keystroke/step streams a step into
        // the owner's open command (see onBezierSpin), and finishing the edit folds
        // the popup's own draft history into one entry - so a five-digit entry
        // cannot leave five undo entries. The command itself stays open until the
        // popup closes, like every other streamed gesture's.
        QDoubleSpinBox::connect(s, &QAbstractSpinBox::editingFinished,
                                this, [this] { endGesture(); });
    }
    l->addLayout(spins);

    // THE PER-SIDE HANDLE MODES. Blender's five, one combo per side: the
    // outgoing side (this param's handleOut) and the incoming side (its
    // handleIn). They are separate controls because the two sides derive
    // independently - dragging one side is what frees it, and the other keeps
    // deriving.
    //
    // Each is a labelled field in the picker's own row geometry - a role=bfield
    // box with its role=fl label above a 24px value, 5px between the columns, as
    // the x1/y1/x2/y2 row above - but the value control is the property panel's
    // OWN control: a plain QComboBox with ComboItem's properties and no local
    // stylesheet, selected by class by the QComboBox rule in applyAppearance
    // (propertywidget.ssa's field standard). Nothing here styles a combo
    // individually.
    {
        QWidget* modes = new QWidget(panel);
        modes->setObjectName("handle-modes");
        QHBoxLayout* ml = new QHBoxLayout(modes);
        ml->setContentsMargins(0, 0, 0, 0);
        ml->setSpacing(5);
        const struct {
            bool incoming;
            const char* label;
            const char* name;
        } defs[] = {
            { true, QT_TR_NOOP("In"), "handle-in" },
            { false, QT_TR_NOOP("Out"), "handle-out" },
        };
        for (const auto& def : defs) {
            QWidget* field = new QWidget(modes);
            field->setProperty("role", "bfield");
            QVBoxLayout* f = new QVBoxLayout(field);
            f->setContentsMargins(0, 0, 0, 0);
            f->setSpacing(2);
            QLabel* lab = new QLabel(tr(def.label), field);
            lab->setProperty("role", "fl");
            lab->setFixedHeight(12);
            f->addWidget(lab);
            // The property panel's ComboItem, exactly: a plain QComboBox that
            // fills the field column, no role property, no local stylesheet and
            // NO fixed height (ComboItem sets none either - its 24px field box is
            // the font + propertywidget.ssa field rule, which is shared here).
            // The popover's own QComboBox rule (see applyAppearance) is that same
            // propertywidget.ssa rule. The object name is only the harness's
            // handle on the control; it is not a style or construction property.
            QComboBox* box = new QComboBox(field);
            box->setObjectName(QString::fromLatin1(def.name));
            box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            for (int m = 0; m < util::Easing::HandleMode_TERM; ++m) {
                const auto mode = static_cast<util::Easing::HandleMode>(m);
                box->addItem(util::Easing::getHandleModeName(mode), m);
            }
            const bool incoming = def.incoming;
            connect(box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, box, incoming](int) {
                this->setHandleMode(incoming, static_cast<util::Easing::HandleMode>(box->currentData().toInt()));
            });
            f->addWidget(box);
            ml->addWidget(field, 1);
            (incoming ? mHandleIn : mHandleOut) = box;
        }
        l->addWidget(modes);
    }

    // These spins are fed by the editor's paintEvent (which writes the
    // clamped values back); give it the vector so it can update them.
    mBezierEditor->spinBoxes = mBezierSpins;

    // Copy / Paste control values.
    QHBoxLayout* acts = new QHBoxLayout();
    acts->setSpacing(6);
    QPushButton* copy = new QPushButton(tr("Copy"), panel);
    copy->setObjectName("bezier-copy");
    copy->setProperty("role", "bact");
    QPushButton* paste = new QPushButton(tr("Paste"), panel);
    paste->setObjectName("bezier-paste");
    paste->setProperty("role", "bact");
    connect(copy, &QPushButton::clicked, this, [this] {
        const util::Easing::CubicBezier& b = mDraft.cubicBezier;
        QGuiApplication::clipboard()->setText(
            QStringLiteral("%1,%2,%3,%4").arg(b.x1).arg(b.y1).arg(b.x2).arg(b.y2));
    });
    connect(paste, &QPushButton::clicked, this, [this] {
        const QStringList parts = QGuiApplication::clipboard()->text().split(',');
        if (parts.size() != 4) return;
        bool ok[4] = { false, false, false, false };
        const double v[4] = { parts[0].toDouble(&ok[0]), parts[1].toDouble(&ok[1]),
                              parts[2].toDouble(&ok[2]), parts[3].toDouble(&ok[3]) };
        if (!(ok[0] && ok[1] && ok[2] && ok[3])) return;
        const util::Easing::Param old = mDraft;
        mDraft.cubicBezier = { (float)v[0], (float)v[1], (float)v[2], (float)v[3] };
        if (mDraft == old) return;
        // A pasted control point is a STORED value: any side it changed and that
        // derived its point must read it from now on (the drag's own rule).
        if (freeEditedSides(old.cubicBezier))
            refreshTiles(); // a freed side may stop the draft reading as the preset
        pushState(old); // paste is a discrete edit
        renderBezier();
        emit changed(mDraft);
        commitDiscrete(); // a discrete edit applies now, not on close
    });
    acts->addWidget(copy);
    acts->addWidget(paste);
    l->addLayout(acts);

    // The popup is fixed to kCustomHeight (taller than the bezier panel's
    // natural height), and the bezier content should NOT stretch to fill — it
    // stays at its natural size (editor 256 + spins + actions) with the surplus
    // as a gap at the bottom. The panel itself is Expanding (reaches the bottom
    // of the popup); this stretch keeps its content packed at the top.
    l->addStretch(1);
    return panel;
}

bool EasingPickWidget::freeEditedSides(const util::Easing::CubicBezier& aBefore) {
    if (mDraft.type != util::Easing::Type_Custom)
        return false; // the modes are inert on an analytic type: nothing to free
    bool freed = false;
    // x1/y1 LEAVES the segment's from key: it is this param's handleOut.
    if ((mDraft.cubicBezier.x1 != aBefore.x1 || mDraft.cubicBezier.y1 != aBefore.y1)
        && util::Easing::isDerived(mDraft.handleOut)) {
        mDraft.handleOut = util::Easing::HandleMode_Free;
        freed = true;
    }
    // x2/y2 ARRIVES at the segment's to key: it is this param's handleIn.
    if ((mDraft.cubicBezier.x2 != aBefore.x2 || mDraft.cubicBezier.y2 != aBefore.y2)
        && util::Easing::isDerived(mDraft.handleIn)) {
        mDraft.handleIn = util::Easing::HandleMode_Free;
        freed = true;
    }
    return freed;
}

void EasingPickWidget::onBezierSpin(int aIndex, double aValue) {
    const util::Easing::Param old = mDraft;
    // The first keystroke/step of this spin is the START of a gesture: snapshot
    // before mutating, so the popup's own undo folds the whole entry into one
    // step (endGesture runs on editing-finished) and the streamed session is
    // joined once, not once per digit. A gesture is per EDIT; the streamed session
    // spans the popup, so it is a different flag.
    const bool firstStep = !mGestureActive;
    if (firstStep) beginGesture();
    util::Easing::CubicBezier& b = mDraft.cubicBezier;
    if (aIndex == 0) b.x1 = (float)aValue;
    else if (aIndex == 1) b.y1 = (float)aValue;
    else if (aIndex == 2) b.x2 = (float)aValue;
    else b.y2 = (float)aValue;
    if (mDraft == old) {
        // A keystroke that did not produce a new value (an intermediate "-" or a
        // repeated digit) must not count as the gesture's first step.
        if (firstStep) { mGestureActive = false; }
        return;
    }
    // The typed side becomes stored if it was derived; the other side is untouched.
    if (freeEditedSides(old.cubicBezier)) {
        // A freed side stops the draft reading as the Auto preset, so the catalog
        // tile that was checked must be unchecked now (it is the state the popup
        // reopens the catalog on).
        refreshHandleModes();
        refreshTiles();
    }
    // Sync the handle points from the new bezier so the paint event's
    // write-back keeps it (otherwise it reverts to a stale control point).
    if (mBezierEditor) { mBezierEditor->syncFromBezier(); mBezierEditor->update(); }
    emit changed(mDraft);
    mApplied = mDraft;   // the owner's session writes the model on this step
    beginLiveEdit();     // opens the owner's ONE command on the first step
    emit liveEdited(mDraft);
}

void EasingPickWidget::onBezierDragStep() {
    // The editor has already written the moved control point into the draft
    // (BezierCurveEditor::mouseMoveEvent -> commitPoints). One pointer move is
    // one STEP: report it and let the owner rewrite its open command, so the
    // document, the graph and every view follow the handle NOW - not on release,
    // and not on close. The side the drag stored is freed here (not at release)
    // for the same reason: the derived side would otherwise ignore the write.
    if (freeEditedSides(mGestureStart.cubicBezier)) {
        refreshHandleModes(); // title + mode combos say "Free" while dragging
        refreshTiles();       // …and the Auto tile stops reading as current
    }
    emit changed(mDraft);
    mApplied = mDraft; // the model holds this step; close must not re-commit it
    beginLiveEdit();   // defensive: the press opened it already
    emit liveEdited(mDraft);
}

void EasingPickWidget::beginLiveEdit() {
    if (mLiveEditActive) return;
    mLiveEditActive = true;
    emit liveEditStarted();
}

void EasingPickWidget::endLiveEdit() {
    if (!mLiveEditActive) return;
    mLiveEditActive = false;
    // The owner closes its command here (the popup's close is the one undo entry
    // for the session); the popup's own draft history was folded by endGesture.
    emit liveEditEnded();
}

// ---- interactions ----------------------------------------------------------
void EasingPickWidget::setType(util::Easing::Type aType) {
    const util::Easing::Param old = mDraft;
    mDraft.type = aType;
    // A TYPE tile leaves the Custom panel (the only one that can be forced open).
    mForceBezierPanel = false;
    if (mDraft == old) return;
    pushState(old); // discrete edit: one undo step
    refresh();
    emit changed(mDraft);
    commitDiscrete();
}
void EasingPickWidget::setRange(util::Easing::Range aRange) {
    const util::Easing::Param old = mDraft;
    if (mDraft.type == util::Easing::Type_Custom)
        mDraft.type = util::Easing::Type_Cubic;
    mForceBezierPanel = false;
    mDraft.range = aRange;
    if (mDraft == old) return;
    pushState(old);
    refresh();
    emit changed(mDraft);
    commitDiscrete();
}
void EasingPickWidget::setWeight(float aWeight) {
    const util::Easing::Param old = mDraft;
    mDraft.weight = aWeight;
    if (mDraft == old) return;
    refresh();
    // Blend is a continuous control: report the live value (chip / preview) but
    // do NOT commit; hideEvent writes the final draft once, so the whole drag is
    // one undo step.
    emit changed(mDraft);
}
void EasingPickWidget::setCustom() {
    const util::Easing::Param old = mDraft;
    // An explicit click on the Custom segment OPENS THE BEZIER PANEL, whatever the
    // draft is - including the Auto preset, whose pair already makes the type
    // Custom. Without this flag that click could neither change the model nor
    // reach the controls (the preset is routed to the catalog; see refresh).
    mForceBezierPanel = true;
    mDraft.type = util::Easing::Type_Custom;
    // 'changed' is the SIGNAL: a local of that name shadows it and the emit below
    // becomes a call on a bool (measured: clang reports "called object type 'bool'").
    const bool didChange = !(mDraft == old);
    if (didChange)
        pushState(old);
    refresh();
    renderBezier();
    if (didChange) {
        emit changed(mDraft);
        commitDiscrete();
    }
}

void EasingPickWidget::setAutoHandlesPreset() {
    const util::Easing::Param old = mDraft;
    // The preset is the handle MODES, not a type: a Custom curve (the only kind
    // whose controls the modes shape) with both sides deriving. It writes the
    // CLAMPED automatic pair (kAutoPresetHandleMode, the retired Auto type's
    // behaviour) - unclamped Auto overshoots by construction and is a deliberate
    // per-side pick in the mode combos, not the preset.
    mForceBezierPanel = false; // the preset is a CATALOG state (its tile stays visible)
    mDraft.type = util::Easing::Type_Custom;
    mDraft.handleIn = util::Easing::kAutoPresetHandleMode;
    mDraft.handleOut = util::Easing::kAutoPresetHandleMode;
    if (mDraft == old) {
        // Already the preset: the popover must SHOW the catalog with the tile
        // checked (a draft reached here from the bezier panel by the combos would
        // otherwise stay on the bezier panel). The ACTION is still committed: the
        // preset makes the segment AFTER each selected key automatic, so a key
        // that is already the pair may still have a neighbour whose incoming side
        // is not automatic (exactly what an edit made before the segment rule
        // existed leaves behind). The host writes nothing when the whole rule is
        // already satisfied.
        refresh();
        emit applied(mDraft);
        return;
    }
    pushState(old);
    refresh();
    renderBezier();
    emit changed(mDraft);
    commitDiscrete();
}

void EasingPickWidget::setHandleMode(bool aIncoming, util::Easing::HandleMode aMode) {
    if (aMode < 0 || util::Easing::HandleMode_TERM <= aMode)
        return;
    const util::Easing::Param old = mDraft;
    util::Easing::HandleMode& mode = aIncoming ? mDraft.handleIn : mDraft.handleOut;
    if (mode == aMode) return;
    mode = aMode;
    // A mode only shapes a Custom curve; the combos live in that panel, so the
    // type is already Custom there. Programmatic callers get the same rule: a
    // mode written onto an analytic type is inert by design (the type's formula
    // is its shape), not silently switched underneath them.
    pushState(old);
    refresh();
    renderBezier();
    emit changed(mDraft);
    commitDiscrete(); // a mode change applies at once, popup still open
}
void EasingPickWidget::setValue(const util::Easing::Param& aParam) {
    // Programmatic load (a new key's easing, or the open measurement dance):
    // reset the draft, the committed baseline, and the session history as one
    // clean slate so nothing is emitted or committed. A streamed session also
    // ends here: a loaded draft is not a step of the edit it replaced, so the
    // owner must close that command rather than rewrite it with this value.
    endLiveEdit();
    mDraft = aParam;
    mApplied = aParam;
    mUndo.clear();
    mRedo.clear();
    mGestureActive = false;
    mForceBezierPanel = false; // a loaded draft opens where its own state routes
    refresh();
}

void EasingPickWidget::setAutoContext(const util::Easing::AutoContext& aContext) {
    // The neighbour keys, not the draft, changed: re-render the curves the popup
    // derives from them (the motion preview on its next tick, and the blend
    // thumbnails) so an Auto draft is never shown as a shape no key has. The
    // picker's Auto TILE is exempt by design: it is a catalog image and draws the
    // canonical automatic-handle pair (see autoPreviewContext).
    mAuto = aContext;
    refreshTiles();
}

bool EasingPickWidget::setShownValue(
    const util::Easing::Param& aParam, const util::Easing::AutoContext& aAutoContext
) {
    // A CLOSED POPUP HAS NOTHING TO KEEP IN STEP, and A GESTURE IN THE POPUP WINS: the
    // value the user is dragging is the truth until the gesture ends, and the owner
    // pushes the document's state again when it does (see isEditing). Both answers are
    // "not adopted", so a caller can tell them from an adoption without inspecting
    // this widget's state.
    if (!this->isVisible() || isEditing())
        return false;

    // The same state it already holds: adopting it would be a refresh for a change that
    // did not happen (every document notification reaches this).
    if (mDraft == aParam && mAuto == aAutoContext)
        return false;

    // THE WHOLE SHOWN STATE, loaded the way a programmatic load is (see setValue) but
    // WITHOUT closing a session: an external change is not the end of the user's edit,
    // and this is not an edit of ours to commit. The session history describes the
    // draft this replaced, so it goes with it. An externally re-read value is also
    // already in the model (that is where it came from), so it is the baseline a
    // close-time commit compares against - nothing is re-committed.
    mAuto = aAutoContext;
    mDraft = aParam;
    mApplied = aParam;
    mUndo.clear();
    mRedo.clear();
    // mForceBezierPanel is deliberately LEFT ALONE: the panel the user is looking at is
    // their choice, and a graph edit must not jump the popup's tab under them.
    refresh();
    return true;
}

// ---- commit ---------------------------------------------------------------
void EasingPickWidget::commitDiscrete() {
    // A DISCRETE edit (tile / segment / custom / mode combo / paste) is a finished
    // change: commit it to the model now, so the keys and every view that reads
    // them update while the popup is still open. A STREAMED gesture (bezier drag /
    // typed spin) does not come through here - its steps already wrote the model
    // through the owner's open command - and the blend slider keeps the
    // one-undo-on-close rule (hideEvent commits its final draft once). mApplied is
    // the baseline, so an edit that wrote nothing (or a second commit of the same
    // draft) emits nothing and close cannot re-commit it.
    if (mDraft == mApplied)
        return;
    mApplied = mDraft;
    emit applied(mDraft);
}

// ---- session undo/redo -----------------------------------------------------
void EasingPickWidget::pushState(const util::Easing::Param& aState) {
    mUndo.push_back(aState);
    mRedo.clear();
}

void EasingPickWidget::beginGesture() {
    // Snapshot the pre-gesture draft so endGesture can fold the whole gesture into
    // ONE history entry: a handle drag, a blend drag, or one typed spin value.
    // Do not record yet: a press-without-move must not leave a history entry.
    mGestureStart = mDraft;
    mGestureActive = true;
}

void EasingPickWidget::endGesture() {
    if (!mGestureActive) return;
    mGestureActive = false;
    // A bezier drag wrote stored control points: free whichever side it moved if
    // that side derived its point (the canvas drag's own rule; see the header).
    // The streamed steps already freed it; this is the same rule at the finish, so
    // a gesture whose only step was the last one cannot leave it derived.
    const bool freed = freeEditedSides(mGestureStart.cubicBezier);
    if (mDraft != mGestureStart) pushState(mGestureStart); // one entry per gesture
    if (freed)
        refresh(); // the mode combos and panel must show the freed side
    emit changed(mDraft); // ensure derived views see the final draft
}

void EasingPickWidget::undo() {
    if (mUndo.isEmpty()) return;
    mRedo.push_back(mDraft);
    mDraft = mUndo.takeLast();
    refresh();
    emit changed(mDraft);
    // During a streamed session the model must follow the popup's own history step
    // too; it is a step of the SAME command, so no extra undo entry appears.
    if (mLiveEditActive) {
        mApplied = mDraft;
        emit liveEdited(mDraft);
    }
}

void EasingPickWidget::redo() {
    if (mRedo.isEmpty()) return;
    mUndo.push_back(mDraft);
    mDraft = mRedo.takeLast();
    refresh();
    emit changed(mDraft);
    if (mLiveEditActive) {
        mApplied = mDraft;
        emit liveEdited(mDraft);
    }
}

// ---- render -----------------------------------------------------------------
void EasingPickWidget::refresh() {
    // THE ROUTING. Custom means the bezier panel - except when the draft IS the
    // Auto preset, which is a catalog state: its tile must remain visible and
    // checked, otherwise a preset selected by the tile (or loaded from a key)
    // lands on a panel where the tile cannot be seen and the popover cannot say
    // the preset is current. The Custom segment click sets mForceBezierPanel, so
    // the bezier controls stay reachable on a preset without changing the pair.
    const bool autoPreset = mDraft.type == util::Easing::Type_Custom
        && mDraft.handleIn == util::Easing::kAutoPresetHandleMode
        && mDraft.handleOut == util::Easing::kAutoPresetHandleMode;
    const bool inBezier =
        mDraft.type == util::Easing::Type_Custom && (mForceBezierPanel || !autoPreset);
    mAnimT = 0.0; // restart the motion preview on any change
    // The header names the Param the same way the chip and every tile do (one
    // vocabulary; a Custom curve by its handle pair, never "Custom").
    if (mTitle)
        mTitle->setText(util::Easing::getParamName(mDraft));
    refreshSeg();
    mNormalPanel->setVisible(!inBezier);
    mBezierPanel->setVisible(inBezier);
    // In Custom mode the top motion-preview bar is hidden (the animated value
    // rides the gauge beside the curve instead), matching the reference mock.
    if (mPipeline) mPipeline->setVisible(!inBezier);
    // Run the tiles even while the Custom panel hides them: their checked state
    // is the draft's, and a stale check left on another tile would be the truth
    // the moment the catalog is shown again. (The Auto preset itself switches to
    // Custom, so this is exactly the path that used to leave two tiles checked.)
    refreshTiles();
    if (inBezier) renderBezier();
    updateBlend();
    // Pin the popup's height to the known per-mode height. The popup is a
    // top-level Qt::Popup with a fixed width; its height is deterministic
    // (kNormalHeight / kCustomHeight), so we set it explicitly instead of
    // measuring a racy just-shown geometry() (which oscillated 478/510) or a
    // heightForWidth that returns -1 in Custom. A fixed per-mode height plus
    // reserving the tallest at open means a mode switch only grows/shrinks the
    // popup in place — it never relocates.
    setFixedHeight(inBezier ? kCustomHeight : kNormalHeight);
    // Defer one event so the layout settles, then re-assert the anchored
    // top-left (drift protection). The height itself is already pinned above.
    QTimer::singleShot(0, this, &EasingPickWidget::refit);
}

void EasingPickWidget::refreshSeg() {
    const bool inBezier = mDraft.type == util::Easing::Type_Custom;
    for (int i = 0; i < 4; ++i) {
        const bool on = (i == 3) ? inBezier : (!inBezier && i == (int)mDraft.range);
        mSegBtns[i]->setChecked(on); // QSS drives hover/checked styling
    }
}

void EasingPickWidget::refreshTiles() {
    for (int i = 0; i < mTileBtns.size(); ++i) {
        QToolButton* tile = mTileBtns[i];
        const TileDef& def = kTileOrder[i];
        const bool on = def.autoPreset
            ? (mDraft.type == util::Easing::Type_Custom
               && mDraft.handleIn == util::Easing::kAutoPresetHandleMode
               && mDraft.handleOut == util::Easing::kAutoPresetHandleMode)
            : (mDraft.type == def.type);
        tile->setChecked(on);
        if (CurveGlyph* g = tile->findChild<CurveGlyph*>()) {
            // The selected tile's curve is accentBright (purple) on the
            // accent-tinted fill; the rest are muted. Set it explicitly here
            // rather than relying on the QSS `color` reaching the glyph's
            // palette (it does not repaint on check-state change alone).
            g->setGlyphColor(on ? C().accentBright : C().textMutedDim);
            util::Easing::Param glyph = mkParam(def.type, mDraft.range, mDraft.weight);
            if (def.autoPreset) {
                glyph.type = util::Easing::Type_Custom;
                glyph.handleIn = util::Easing::kAutoPresetHandleMode;
                glyph.handleOut = util::Easing::kAutoPresetHandleMode;
            }
            g->setParam(glyph);
            // The preset keeps its canonical catalog pair here too, so this
            // refresh cannot turn its tile back into a straight line.
            g->setAutoContext(glyphAutoContext(def.autoPreset, mAuto));
        }
    }
}

void EasingPickWidget::updateBlend() {
    mBlendInput->setValue(qRound(mDraft.weight * 100));
    mBlendVal->setText(QString::number(qRound(mDraft.weight * 100)) + "%");
}

void EasingPickWidget::refreshHandleModes() {
    // The header names the Param the same way the chip and every tile do (one
    // vocabulary; a Custom curve by its handle pair, never "Custom").
    if (mTitle)
        mTitle->setText(util::Easing::getParamName(mDraft));
    // The two mode combos show the draft's own sides; a programmatic change (the
    // Auto preset, a paste, setValue, a drag freeing a derived side) must move
    // them without re-emitting.
    if (mHandleIn) {
        const QSignalBlocker block(mHandleIn);
        mHandleIn->setCurrentIndex(int(mDraft.handleIn));
    }
    if (mHandleOut) {
        const QSignalBlocker block(mHandleOut);
        mHandleOut->setCurrentIndex(int(mDraft.handleOut));
    }
}

void EasingPickWidget::renderBezier() {
    // The editor is authoritative: paintEvent reads the draft bezier, clamps
    // it, and writes the values back to bezier + the spins. Sync the handle
    // points first so a programmatic bezier change (spin/paste/setValue) is
    // not reverted by the paint event's write-back.
    if (mBezierEditor) { mBezierEditor->syncFromBezier(); mBezierEditor->update(); }
    refreshHandleModes();
}

void EasingPickWidget::resizeEvent(QResizeEvent* aEvent) {
    QFrame::resizeEvent(aEvent);
}
void EasingPickWidget::moveEvent(QMoveEvent* aEvent) {
    QFrame::moveEvent(aEvent);
    // moveEvent() fires on EVERY move of the popup: the open placement, the
    // anchor re-assert on refit, or a nudge by the WM/compositor to keep a
    // growing popup on-screen. The popup size is pinned per-mode and the anchor
    // is re-asserted on every refit, so any relocation here would be a bug; the
    // picker_pos_probe gate asserts the left edge never drifts.
}
bool EasingPickWidget::eventFilter(QObject* aWatched, QEvent* aEvent) {
    // Session undo/redo: Ctrl+Z / Ctrl+Shift+Z anywhere in the popup (including
    // while a spinbox has focus) steps the draft history. The app's own Ctrl+Z
    // is an ApplicationShortcut that does not fire while this popup (a
    // Qt::Popup) is open, so we own the keys here; the filter runs before the
    // focused widget handles them, so it also beats a focused spinbox (whose
    // own text-undo we deliberately pre-empt).
    if (aEvent->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(aEvent);
        const bool ctrl = (ke->modifiers() & Qt::ControlModifier) && !(ke->modifiers() & Qt::AltModifier);
        if (ctrl && ke->key() == Qt::Key_Z) {
            if (ke->modifiers() & Qt::ShiftModifier) redo();
            else undo();
            return true;
        }
    }
    if (aEvent->type() == QEvent::MouseButtonPress) {
        // A press on a spinbox lands on the spinbox or its internal line edit;
        // either way it's a click on the field and must keep focus.
        if (qobject_cast<QAbstractSpinBox*>(aWatched))
            return QFrame::eventFilter(aWatched, aEvent);
        if (auto* edit = qobject_cast<QLineEdit*>(aWatched))
            if (qobject_cast<QAbstractSpinBox*>(edit->parentWidget()))
                return QFrame::eventFilter(aWatched, aEvent);

        // Otherwise the click is on a non-text child (curve editor, label,
        // background, button) — release the active spinbox's edit focus. The
        // focused widget of a spinbox is its internal line edit, so walk up to
        // find the spinbox (if any) before clearing.
        if (auto* fw = QApplication::focusWidget()) {
            auto* fspin = qobject_cast<QAbstractSpinBox*>(fw);
            if (!fspin)
                fspin = qobject_cast<QAbstractSpinBox*>(fw->parentWidget());
            if (fspin)
                fw->clearFocus();
        }
    }
    return QFrame::eventFilter(aWatched, aEvent);
}

void EasingPickWidget::mouseMoveEvent(QMouseEvent* aEvent) {
    // Qt's popup path (QWidgetWindow::handleMouseEvent) re-targets a mouse move
    // to the popup itself once the cursor leaves the popup rect — dropping it
    // from the pressed child (the blend slider), so a drag "loses" the moment it
    // crosses the popup's edge. When the slider is mid-drag, forward the move to
    // it so the value keeps tracking outside. (The forwarded event is rebuilt in
    // the slider's local coords; the slider must not grab the mouse, see below.)
    forwardBlendCursorEvent(aEvent);
}

void EasingPickWidget::mouseReleaseEvent(QMouseEvent* aEvent) {
    // Same popup-path redirect also steals the release, which would leave the
    // slider "down" forever. Forward it so isSliderDown() resets cleanly.
    forwardBlendCursorEvent(aEvent);
}

void EasingPickWidget::forwardBlendCursorEvent(QMouseEvent* aEvent) {
    if (!mBlendInput || !mBlendInput->isDown()) {
        if (aEvent->type() == QEvent::MouseMove) QFrame::mouseMoveEvent(aEvent);
        else QFrame::mouseReleaseEvent(aEvent);
        return;
    }
    QMouseEvent f(aEvent->type(),
                  mBlendInput->mapFromGlobal(aEvent->globalPosition().toPoint()),
                  aEvent->scenePosition(), aEvent->globalPosition(),
                  aEvent->button(), aEvent->buttons(), aEvent->modifiers());
    QApplication::sendEvent(mBlendInput, &f);
}

void EasingPickWidget::showEvent(QShowEvent* aEvent) {
    QFrame::showEvent(aEvent);
    // The window flags are final by now, so this is the safe point to ask the
    // platform to round the popup (it forces the native window to exist); on
    // platforms that don't do it themselves this is a no-op and the paintEvent
    // mask does the shaping instead. See gui/PopupWindow.h.
    roundPopupWindow(this);
    if (mAnimTimer) mAnimTimer->start();
    // On reopen the popup keeps its previous (possibly tall Custom) height
    // while the content returns to the applied (non-Custom) state — re-fit.
    mAnimT = 0.0;
    QTimer::singleShot(0, this, &EasingPickWidget::refit);
}

void EasingPickWidget::hideEvent(QHideEvent* aEvent) {
    QFrame::hideEvent(aEvent);
    if (mAnimTimer) mAnimTimer->stop();
    // The anchor is only meaningful while the popup is shown. Clear it on hide
    // so a reopen's 0-opacity dance doesn't briefly re-assert a stale position
    // (openPopover re-establishes it at the end of the dance).
    mHasAnchor = false;

    // A streamed session (a handle drag / a typed spin) closes here: the popup's
    // close is the ONE undo entry for the session, the same close-time rule the
    // blend slider follows, so the owner's open command ends now. Its steps
    // already wrote the model, so the commit below has nothing left to do for it.
    // An interrupted gesture's own draft-history entry is folded here too.
    endLiveEdit();
    if (mGestureActive) endGesture();

    // Apply-on-close, for the DRAG remnant: discrete edits already committed as
    // they happened (commitDiscrete), and a streamed gesture applied every step,
    // so this normally writes only the final value of a blend-slider drag - one
    // undo step for the whole gesture. Only commit if the draft actually moved.
    // hideEvent fires for both the ✕ and an outside-click dismiss of the popup.
    if (mDraft != mApplied) {
        mApplied = mDraft;
        emit applied(mDraft);
    }

    // Discard the session history for the next open.
    mUndo.clear();
    mRedo.clear();
    mGestureActive = false;

    // And the popup is gone: whoever lent it a temporary selection ends it now
    // (after the commit above, so the commit still saw the keys it was for).
    emit closed();
}

void EasingPickWidget::animStep() {
    mAnimT = std::fmod(mAnimT + 0.007, 1.0);
    if (!mPipeline || !mPipelineFill) return;
    const float v = util::Easing::calculate(mDraft, mAuto, (float)mAnimT, 0.0f, 1.0f, 1.0f);
    const float f = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    mPipelineFill->setFixedWidth(qRound(f * mPipeline->width()));
    // Drive the Custom-view animation gauge (the ball's Y = the live value).
    if (mBezierGauge) static_cast<PreviewGauge*>(mBezierGauge)->setValue(v);
}

void EasingPickWidget::refit() {
    // The height is pinned by refresh() to the per-mode constant
    // (kNormalHeight / kCustomHeight); we do NOT re-derive it here from
    // heightForWidth, which returns -1 in Custom and is the source of the racy
    // 476/478/510 oscillation. refit only re-asserts the anchored top-left so
    // nothing (a compositor/WM nudge, or a resize) drifts the popup.
    mRefitAttempts = 0;
    if (mHasAnchor && isWindow())
        move(mAnchorPoint);
}

void EasingPickWidget::setAnchorPoint(const QPoint& aPoint) {
    mAnchorPoint = aPoint;
    mHasAnchor = true;
}

void EasingPickWidget::popUpAt(const QPoint& aGlobalAnchor, const QWidget* aAnchorWidget,
                               const util::Easing::Param& aShown,
                               const util::Easing::AutoContext& aAutoContext) {
    // 0-opacity first-frame trick: the Popup's live height is DPI-correct
    // only after its native window is realized (a hidden popup's
    // heightForWidth/sizeHint is stale — hidden 477 vs live 488 on this
    // host; user saw 407→489). Show the tall Custom at opacity 0 for one
    // frame so Qt sizes the native window live, then flip back to the
    // requested mode at opacity 1. At that point liveCustomHeight() ==
    // geometry().height() is authoritative — use it to reserve Custom
    // height once, nudge up if it would clip, and let Custom grow down in
    // place. (The popup opens flush at the anchor, gap 0.)
    //
    // The Auto context is installed before the first paint: the blend
    // thumbnails and the motion preview derive their curves from the neighbours
    // of the key this open names (the Auto tile draws the catalog's canonical
    // pair instead - a tile is an image, not a preview of the user's keys).
    setAutoContext(aAutoContext);

    const util::Easing::Param requested = aShown;
    const bool wasCustom = requested.type == util::Easing::Type_Custom;
    if (!wasCustom) {
        util::Easing::Param c = requested;
        c.type = util::Easing::Type_Custom;
        setValue(c);
    } else {
        setValue(requested);
    }
    const QScreen* scr = aAnchorWidget ? aAnchorWidget->screen() : nullptr;
    if (!scr) scr = QGuiApplication::primaryScreen();
    const QRect avail = scr ? scr->availableGeometry() : QRect();
    int left = aGlobalAnchor.x();
    if (!avail.isNull()) {
        const int w = width() > 0 ? width() : kPopoverWidth;
        if (left + w > avail.right() + 1) left = avail.right() + 1 - w;
        if (left < avail.left()) left = avail.left();
    }
    // Realize the native window at the tall Custom height (0 opacity).
    setWindowOpacity(0.0);
    // Treat the popup as a transient child of the app window. A popup with
    // no transient parent is left to the WM/compositor, which will nudge a
    // growing popup to keep it on-screen — that is the "menu nudges to the
    // right on mode switch" drift. On X11 the popup is still
    // override-redirect (unmanaged, so this is a no-op there); on Wayland it
    // stops the compositor relocating it.
    winId();
    if (auto* ph = windowHandle()) {
        if (auto* top = aAnchorWidget ? aAnchorWidget->window() : nullptr) {
            if (auto* wh = top->windowHandle())
                ph->setTransientParent(wh);
        }
    }
    move(left, aGlobalAnchor.y());
    show();
    raise();
    // Two ticks: the live geometry is realized by then. We are STILL in
    // Custom here, so reserve the Custom (tallest) height — reading it
    // after flipping back to the requested mode gave the normal height and
    // let the tall Custom panel clip off the bottom of the screen.
    QTimer::singleShot(0, this, [this, requested, wasCustom, avail, left, aGlobalAnchor]() {
        if (!this->isVisible()) return;
        QTimer::singleShot(0, this, [this, requested, wasCustom, avail, left, aGlobalAnchor]() {
            if (!this->isVisible()) return;
            // Reserve the tallest (Custom) height so the popup never needs
            // to be relocated when switching to Custom. The height is a
            // KNOWN constant, not a measured one — the previous measurement
            // (liveCustomHeight / heightForWidth / sizeHint) oscillated
            // 476/478/510 and was nowhere near the real ~489. Use the
            // deterministic constant and let the popup grow down in place.
            const int reserveH = kCustomHeight;
            int top = aGlobalAnchor.y();
            if (!avail.isNull() && reserveH > 0) {
                if (top + reserveH > avail.bottom() + 1)
                    top = qMax(avail.top(), avail.bottom() + 1 - reserveH);
            }
            // Now flip back to the requested mode (sizes the normal panel).
            if (!wasCustom) this->setValue(requested);
            QTimer::singleShot(0, this, [this, left, top]() {
                if (!this->isVisible()) return;
                // Pin the anchor once; every later refit re-asserts it so
                // the popup can never drift to the right on a mode switch.
                // top is reserved for the tallest (Custom) height, so
                // switching to Custom grows the popup down in place.
                this->move(left, top);
                this->setAnchorPoint(QPoint(left, top));
                this->setWindowOpacity(1.0);
            });
        });
    });
}

int EasingPickWidget::liveCustomHeight() const {
    if (!isVisible()) return -1;
    return geometry().height();
}

bool EasingPickWidget::verifyMaxHeightMatchesLive(QString* aMsg) const {
    if (!isVisible()) {
        if (aMsg) *aMsg = QStringLiteral("not visible");
        return false;
    }
    int hfw = heightForWidth(kPopoverWidth);
    int sh = sizeHint().height();
    int maxH = const_cast<EasingPickWidget*>(this)->maxContentHeight();
    int live = liveCustomHeight();
    QString msg = QStringLiteral("hfw=%1 sh=%2 maxH=%3 live=%4 diff=%5")
        .arg(hfw).arg(sh).arg(maxH).arg(live).arg(live - maxH);
    if (aMsg) *aMsg = msg;
    return qAbs(live - maxH) <= 1;
}

int EasingPickWidget::maxContentHeight() {
    // Cheap hidden estimate for reasoning/tests before the native window
    // exists. The authoritative live height is geometry().height() after show
    // (see liveCustomHeight and the 0-opacity first-frame trick in
    // EasingPickWidget::popUpAt). The estimate forces Custom visible, reads
    // sizeHint/heightForWidth, then restores the caller's mode; a hidden
    // popup's heightForWidth can be -1 or stale, so fall back to minimum.
    if (isVisible()) return geometry().height();
    const bool bezier = mDraft.type == util::Easing::Type_Custom;
    if (width() != kPopoverWidth) const_cast<EasingPickWidget*>(this)->setFixedWidth(kPopoverWidth);
    const_cast<EasingPickWidget*>(this)->ensurePolished();
    const_cast<EasingPickWidget*>(this)->mNormalPanel->setVisible(false);
    const_cast<EasingPickWidget*>(this)->mBezierPanel->setVisible(true);
    if (mPipeline) const_cast<EasingPickWidget*>(this)->mPipeline->setVisible(false);
    if (layout()) { const_cast<EasingPickWidget*>(this)->layout()->invalidate(); const_cast<EasingPickWidget*>(this)->layout()->activate(); }
    if (mBody && mBody->layout()) { mBody->layout()->invalidate(); mBody->layout()->activate(); }
    if (mBezierPanel && mBezierPanel->layout()) { mBezierPanel->layout()->invalidate(); mBezierPanel->layout()->activate(); }
    int hCustom = qMax(sizeHint().height(), heightForWidth(kPopoverWidth));
    if (hCustom <= 0) hCustom = minimumSizeHint().height();
    const_cast<EasingPickWidget*>(this)->mNormalPanel->setVisible(!bezier);
    const_cast<EasingPickWidget*>(this)->mBezierPanel->setVisible(bezier);
    if (mPipeline) const_cast<EasingPickWidget*>(this)->mPipeline->setVisible(!bezier);
    if (layout()) { const_cast<EasingPickWidget*>(this)->layout()->invalidate(); const_cast<EasingPickWidget*>(this)->layout()->activate(); }
    return hCustom > 0 ? hCustom : -1;
}

void EasingPickWidget::paintEvent(QPaintEvent* aEvent) {
    // Translucent rounded popup: the window backing is transparent (WA_Translucent-
    // Background), so we must paint the rounded body ourselves. Letting QFrame
    // paint first would fill the square backing. On opaque windows (tests/offscreen
    // before expose) the backing isn't translucent, so also install a rounded
    // window mask — it clips the corner cut reliably on every compositor
    // (including offscreen/platform mismatches that otherwise leave black corners).
    constexpr qreal kRadius = 8.0;
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        // Clear to transparent so the corner cut stays see-through.
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(rect(), Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath path;
        path.addRoundedRect(r, kRadius, kRadius);
        p.fillPath(path, C().base);
        QPen pen(C().raised);
        pen.setWidth(1);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
    // Paint children (header/body/tiles) over the rounded body.
    // Block recursion: QFrame::paintEvent would re-enter this override.
    {
        QPaintEvent ev(aEvent->rect());
        QFrame::paintEvent(&ev);
    }
    // Rounded window mask — cheap and compositor-independent, but only where the
    // platform does not round the popup itself: on Windows 11 22H2+ DWM rounds
    // the window (see showEvent), and a window region would prevent exactly that
    // (Microsoft lists "window regions" among the things that make a window
    // un-roundable). Elsewhere the mask rounds the corner cut reliably on every
    // compositor (including offscreen / platform mismatches). A single cached
    // mask, refreshed on size change.
    if (!usesSystemPopupRounding()) {
        static QSize sMaskSize;
        if (sMaskSize != size()) {
            sMaskSize = size();
            QBitmap mask(size());
            mask.fill(Qt::color0);
            QPainter mp(&mask);
            mp.setRenderHint(QPainter::Antialiasing, true);
            mp.setBrush(Qt::color1);
            mp.setPen(Qt::NoPen);
            mp.drawRoundedRect(QRect(QPoint(0, 0), size()), kRadius, kRadius);
            setMask(mask);
        }
    }
}

} // namespace prop
} // namespace gui
