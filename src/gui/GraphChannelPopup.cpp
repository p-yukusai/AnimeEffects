#include "gui/GraphChannelPopup.h"

#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <algorithm>

#include "gui/theme/Colors.h"

namespace {

// THE DESIGN'S OWN NUMBERS for the list (mock.css: .gp-chanpop / .gp-chlayer /
// .gp-chrow / .gp-chrow .gw / .gp-chname), in the app's tokens:
//
//   .gp-chanpop { padding: 6px 0; border-radius: 6px; min-width: 150px;
//                 box-shadow: 0 10px 24px rgba(0,0,0,.5) }
//   .gp-chlayer { font-size: 10px; color: var(--text-muted); padding: 4px 10px 2px }
//   .gp-chrow   { height: 22px; gap: 8px; padding: 0 10px }
//   .gp-chrow .gw { width: 12px; height: 12px; border-radius: 3px }
//   .gp-chname  { font-size: 11px; color: var(--text) }
//
// The row's padding (10px) and the heading's padding (10px) are the SAME number on
// purpose: the title lines up with the SWATCH column, not with the names.
const int kBodyPadY = 6;      // .gp-chanpop's 6px 0
const int kRowHeight = 22;    // .gp-chrow
const int kRowPadX = 10;      // .gp-chrow / .gp-chlayer, one number for both
const int kSwatchSide = 12;   // .gp-chrow .gw
const int kSwatchRadius = 3;
const int kSwatchGap = 8;     // .gp-chrow's gap
const int kBodyRadius = 6;    // .gp-chanpop's border-radius
const int kMinWidth = 150;    // .gp-chanpop's min-width
const int kLayerPadTop = 6;
const int kLayerPadBottom = 3;
const int kLayerGap = 6;      // air above a new layer section
const int kRowFontSize = 11;  // .gp-chname
const int kLayerFontSize = 10;
// The shadow's own margin inside this widget (the body does not fill the window):
// .gp-chanpop's 0 10px 24px, approximated by a few rounded layers because a
// QGraphicsDropShadowEffect on a translucent top-level popup costs a render target
// for one 150x300 box.
const int kShadow = 8;
const int kShadowOffsetY = 4;
const int kShadowLayers = 6;

} // namespace

namespace gui {

GraphChannelPopup::GraphChannelPopup(QWidget* aParent):
    QWidget(aParent),
    mRows(),
    mEntries(),
    mHover(-1),
    mCurrent(-1),
    mBody() {
    // A POPUP, not a menu: Qt holds the mouse and the keyboard while it is up and a
    // click outside closes it, which is the behaviour the QMenu had and the behaviour
    // the row list wants. The translucent backing is what lets the rounded body and
    // its shadow composite over the dock underneath (the same flags the easing picker
    // uses, see prop::EasingItem).
    this->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    this->setAttribute(Qt::WA_TranslucentBackground, true);
    // THE BODY IS PAINTED HERE, so nothing else may fill the window: the application's
    // window stylesheet has a generic QWidget background rule, and a styled background
    // drawn behind this list would square off the rounded corners. An own, transparent
    // rule beats the inherited one, and the system background is off as well (the same
    // reason prop::EasingPickWidget leaves WA_StyledBackground alone).
    this->setObjectName(QStringLiteral("graphChannelPopup"));
    this->setStyleSheet(QStringLiteral("#graphChannelPopup { background: transparent; border: none; }"));
    this->setAttribute(Qt::WA_NoSystemBackground, true);
    this->setAutoFillBackground(false);
    // The keyboard is the list's while it is up: the rows are not child widgets (the
    // whole list is painted here), so the popup itself is the focus object and the
    // arrow keys/Return below are how a keyboard user reaches every row.
    this->setFocusPolicy(Qt::StrongFocus);
    this->setMouseTracking(true);
    this->setAccessibleName(tr("Channels"));
}

GraphChannelPopup::~GraphChannelPopup() = default;

void GraphChannelPopup::setRows(const QVector<PanelChannelRow>& aRows) {
    mRows = aRows;
    mHover = -1;
    mCurrent = mRows.isEmpty() ? -1 : 0;
    mKeyboardCursor = false;
    // The layout is the OPEN's (openAt calls rebuildLayout): a hidden list has no size
    // to build, and every open hands the rows it will list.
}

void GraphChannelPopup::rebuildLayout() {
    QFont rowFont = this->font();
    rowFont.setPixelSize(kRowFontSize);
    QFont layerFont = this->font();
    layerFont.setPixelSize(kLayerFontSize);
    const QFontMetrics rowMetrics(rowFont);
    const QFontMetrics layerMetrics(layerFont);

    mEntries.clear();
    int width = kMinWidth;
    int contentHeight = 0;
    QString lastLayer;
    for (int i = 0; i < mRows.size(); ++i) {
        const PanelChannelRow& row = mRows.at(i);

        // A LAYER is a SECTION: its own small, muted heading, once per contiguous
        // block of that layer. There is NO group heading: the rows are already
        // self-describing ("Move X", "Move Y", "Rotate", "Opacity"), and a second
        // "Move [X][Y]" title above them only raised the question of where it ended.
        // Rows are NOT additionally indented: their swatch already sets the label in,
        // and the swatch aligns with the layer heading's left edge.
        if (row.layer != lastLayer) {
            lastLayer = row.layer;
            Entry entry;
            entry.kind = EntryKind::Layer;
            entry.text = row.layer;
            entry.rect = QRect(
                0, contentHeight, kMinWidth,
                (mEntries.isEmpty() ? 0 : kLayerGap) + kLayerPadTop + layerMetrics.height() + kLayerPadBottom
            );
            contentHeight += entry.rect.height();
            mEntries.append(entry);
            width = std::max(width, kRowPadX + layerMetrics.horizontalAdvance(row.layer) + kRowPadX);
        }

        Entry entry;
        entry.kind = EntryKind::Row;
        entry.row = i;
        entry.text = row.name;
        entry.rect = QRect(0, contentHeight, kMinWidth, kRowHeight);
        contentHeight += entry.rect.height();
        mEntries.append(entry);
        width = std::max(
            width, kRowPadX + kSwatchSide + kSwatchGap + rowMetrics.horizontalAdvance(row.name)
                + kRowPadX
        );
    }

    // The boxes, in THIS widget's pixels: the body is inset by the shadow's margin,
    // and the entries keep the x they will be DRAWN at (the body's left edge plus the
    // row padding) so the paint and the hit test cannot disagree.
    mBody = QRect(kShadow, kShadow, width, contentHeight + 2 * kBodyPadY);
    for (Entry& entry : mEntries) {
        entry.rect.moveLeft(mBody.left());
        entry.rect.moveTop(entry.rect.top() + mBody.top() + kBodyPadY);
        entry.rect.setWidth(mBody.width());
    }

    this->resize(mBody.width() + 2 * kShadow, mBody.height() + 2 * kShadow);
}

int GraphChannelPopup::rowAt(const QPoint& aPos) const {
    for (const Entry& entry : mEntries) {
        if (entry.kind == EntryKind::Row && entry.rect.contains(aPos))
            return entry.row;
    }
    return -1;
}

void GraphChannelPopup::setHover(int aIndex) {
    if (mHover == aIndex)
        return;
    mHover = aIndex;
    this->update();
}

void GraphChannelPopup::moveCurrent(int aStep) {
    if (mRows.isEmpty())
        return;
    mKeyboardCursor = true; // a key moved the cursor: the row now shows the keyboard is here
    int index = mCurrent;
    for (int guard = 0; guard < mRows.size(); ++guard) {
        index += aStep;
        if (index < 0)
            index = mRows.size() - 1;
        else if (index >= mRows.size())
            index = 0;
        if (index != mCurrent) {
            mCurrent = index;
            break;
        }
    }
    this->update();
}

void GraphChannelPopup::activateCurrent() {
    if (mCurrent >= 0 && mCurrent < mRows.size()) {
        mKeyboardCursor = true; // the activated row was the keyboard's target
        this->update();
        this->toggleRow(mCurrent);
    }
}

void GraphChannelPopup::toggleRow(int aIndex) {
    if (aIndex < 0 || aIndex >= mRows.size())
        return;

    // The whole row toggles its channel's visibility, and the swatch IS the state
    // (filled while shown, hollow while hidden): this list flips its own copy first,
    // so the row the pointer just hit is drawn in its new state even when the editor
    // write is refused (a node the document dropped).
    mRows[aIndex].visible = !mRows[aIndex].visible;
    this->update();

    if (onRowToggled && !onRowToggled(mRows.at(aIndex)))
        this->hide(); // the row no longer names a keyed channel: the list is stale
}

void GraphChannelPopup::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    this->paintBody(painter);
    for (int i = 0; i < mEntries.size(); ++i)
        this->paintEntry(painter, i);
}

void GraphChannelPopup::paintBody(QPainter& aPainter) const {
    const theme::Colors& colors = theme::Colors::current();

    // The shadow: a few rounded layers from the body outwards, offset DOWN (the
    // design's 0 10px 24px), each weaker than the one inside it.
    const QRectF body(mBody);
    for (int layer = kShadowLayers; layer >= 1; --layer) {
        const qreal grow = qreal(kShadow) * qreal(layer) / qreal(kShadowLayers);
        QColor shade(0, 0, 0);
        shade.setAlphaF(0.06 * (1.0 - double(layer) / double(kShadowLayers + 1)));
        aPainter.setPen(Qt::NoPen);
        aPainter.setBrush(shade);
        aPainter.drawRoundedRect(
            body.adjusted(-grow, -grow + kShadowOffsetY * grow / kShadow, grow, grow + kShadowOffsetY * grow / kShadow),
            kBodyRadius + grow, kBodyRadius + grow
        );
    }

    // THE BODY: the app's sunken floater surface, edged with the floater edge (the
    // user's ruling - the app's own body reads better than the mockup's).
    aPainter.setPen(QPen(colors.floaterEdge, 1.0));
    aPainter.setBrush(colors.floaterBody);
    aPainter.drawRoundedRect(body.adjusted(0.5, 0.5, -0.5, -0.5), kBodyRadius, kBodyRadius);
}

void GraphChannelPopup::paintEntry(QPainter& aPainter, int aIndex) const {
    const theme::Colors& colors = theme::Colors::current();
    const Entry& entry = mEntries.at(aIndex);

    if (entry.kind == EntryKind::Layer) {
        // Hierarchy is TYPE and whitespace, not a dot or a group title: the layer is
        // a small muted section label, and the self-describing rows follow it.
        QFont font = this->font();
        font.setPixelSize(kLayerFontSize);
        aPainter.setFont(font);
        aPainter.setPen(colors.textMuted);
        const QRect textBox(
            mBody.left() + kRowPadX, entry.rect.top() + kLayerPadTop,
            mBody.width() - 2 * kRowPadX, entry.rect.height() - kLayerPadTop - kLayerPadBottom
        );
        aPainter.drawText(textBox, Qt::AlignLeft | Qt::AlignVCenter, entry.text);
        return;
    }

    const PanelChannelRow& row = mRows.at(entry.row);

    // The row's own ink, clipped to the body so the hover fill cannot square off the
    // rounded corners. The pointer's mark is the raised fill alone (a popup that
    // re-strokes the row the mouse is on reads as a selected item, which this list
    // deliberately does not have). A KEYBOARD cursor is marked by the raised fill plus
    // a 1px INNER line in the neutral floater edge - the surface's own border ink, never
    // a hue. It cannot be mistaken for a selected channel: the list has no selection,
    // the only channel state it paints is the swatch's hue (filled = shown, hollow =
    // hidden). The sticky focus is NOT marked here: it is the panel's tangent-handle
    // state, and a bold row that survived the pointer read as a random emphasis. The
    // line therefore means "the keyboard is here", not "this channel is selected".
    aPainter.save();
    QPainterPath clip;
    clip.addRoundedRect(QRectF(mBody), kBodyRadius, kBodyRadius);
    aPainter.setClipPath(clip);

    const bool keyboardCurrent = entry.row == mCurrent && mKeyboardCursor;
    if (entry.row == mHover || keyboardCurrent) {
        aPainter.setPen(Qt::NoPen);
        aPainter.setBrush(colors.raised);
        aPainter.drawRect(QRect(mBody.left(), entry.rect.top(), mBody.width(), entry.rect.height()));
    }
    if (keyboardCurrent) {
        aPainter.setPen(QPen(colors.floaterEdge, 1.0));
        aPainter.setBrush(Qt::NoBrush);
        // Inset one pixel: the ring is the ROW's, not the body's own edge.
        aPainter.drawRect(
            QRect(mBody.left() + 1, entry.rect.top() + 1, mBody.width() - 3, entry.rect.height() - 3)
        );
    }

    // THE SWATCH is the row's colour key: filled with the channel's hue while it is
    // shown, the same hue as a 1px outline while it is hidden (mock.css's
    // .gp-chrow.off .gw). No second control sits beside it - the row is the toggle.
    const QRectF swatch(
        qreal(mBody.left() + kRowPadX), qreal(entry.rect.center().y()) - kSwatchSide / 2.0,
        qreal(kSwatchSide), qreal(kSwatchSide)
    );
    aPainter.setPen(row.visible ? QPen(Qt::NoPen) : QPen(row.color, 1.0));
    aPainter.setBrush(row.visible ? QBrush(row.color) : QBrush(Qt::NoBrush));
    aPainter.drawRoundedRect(
        swatch.adjusted(0.5, 0.5, -0.5, -0.5), kSwatchRadius, kSwatchRadius
    );

    QFont font = this->font();
    font.setPixelSize(kRowFontSize);
    // NO BOLD FOR THE STICKY FOCUS. The focused channel is shown by the panel's
    // tangent handles; a bold row that stayed after the pointer left read as a
    // random emphasized item. The list's only row mark is the hover/keyboard fill.
    aPainter.setFont(font);
    aPainter.setPen(row.visible ? colors.text : colors.textMuted);
    const QRect textBox(
        mBody.left() + kRowPadX + kSwatchSide + kSwatchGap, entry.rect.top(),
        mBody.width() - (kRowPadX + kSwatchSide + kSwatchGap) - kRowPadX, entry.rect.height()
    );
    aPainter.drawText(textBox, Qt::AlignLeft | Qt::AlignVCenter, entry.text);

    aPainter.restore();
}

void GraphChannelPopup::mouseMoveEvent(QMouseEvent* aEvent) {
    const int hovered = this->rowAt(aEvent->position().toPoint());
    const int previous = mHover;
    this->setHover(hovered);
    // The channel is focused when the pointer ENTERS a row (or moves between rows),
    // not on every move within one: a focus write and a panel repaint per pixel is
    // exactly what the hover rule must not cost.
    if (hovered >= 0 && hovered != previous) {
        // The row the pointer names is also Space/Return's target, but the pointer's
        // mark is the RAISED FILL - the keyboard's inner ring is cleared here and only
        // reappears when a key moves or activates the cursor.
        mCurrent = hovered;
        mKeyboardCursor = false;
        this->update();
        if (onRowHovered)
            onRowHovered(mRows.at(hovered)); // a row focuses its channel (keep the QMenu's behaviour)
    }
    aEvent->accept();
}

void GraphChannelPopup::mouseReleaseEvent(QMouseEvent* aEvent) {
    if (aEvent->button() == Qt::LeftButton) {
        const int hit = this->rowAt(aEvent->position().toPoint());
        if (hit >= 0)
            this->toggleRow(hit);
        aEvent->accept();
        return;
    }
    QWidget::mouseReleaseEvent(aEvent);
}

void GraphChannelPopup::keyPressEvent(QKeyEvent* aEvent) {
    switch (aEvent->key()) {
    case Qt::Key_Escape:
        this->hide();
        aEvent->accept();
        return;
    case Qt::Key_Up:
        this->moveCurrent(-1);
        aEvent->accept();
        return;
    case Qt::Key_Down:
        this->moveCurrent(1);
        aEvent->accept();
        return;
    case Qt::Key_Home:
        if (!mRows.isEmpty()) {
            mCurrent = 0;
            mKeyboardCursor = true;
        }
        this->update();
        aEvent->accept();
        return;
    case Qt::Key_End:
        if (!mRows.isEmpty()) {
            mCurrent = mRows.size() - 1;
            mKeyboardCursor = true;
        }
        this->update();
        aEvent->accept();
        return;
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        this->activateCurrent();
        aEvent->accept();
        return;
    case Qt::Key_Tab:
        this->moveCurrent((aEvent->modifiers() & Qt::ShiftModifier) ? -1 : 1);
        aEvent->accept();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(aEvent);
}

void GraphChannelPopup::leaveEvent(QEvent* aEvent) {
    this->setHover(-1);
    QWidget::leaveEvent(aEvent);
}

void GraphChannelPopup::openAt(const QPoint& aGlobalBottomRight) {
    this->rebuildLayout();
    mHover = -1;
    mCurrent = mRows.isEmpty() ? -1 : 0;
    mKeyboardCursor = false; // the pointer opened the list: no keyboard mark until a key is used

    // UNDER the anchor, RIGHT-ALIGNED to it: the body's right edge is the anchor's x,
    // and the body's top sits a few pixels below it (the mock's .tl-chanpop, which the
    // dock header hosts). The shadow's own margin travels with the body.
    QPoint topLeft(aGlobalBottomRight.x() - (mBody.left() + mBody.width()), aGlobalBottomRight.y() + 4 - mBody.top());

    // Clamped to the screen the anchor is on, so the list stays whole beside a window
    // edge (a header button sits at the dock's right end, which is exactly where a
    // popup can hang off).
    QScreen* screen = QGuiApplication::screenAt(aGlobalBottomRight);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect available = screen->availableGeometry();
        topLeft.setX(std::max(available.left() - kShadow, std::min(topLeft.x(), available.right() + kShadow - this->width())));
        topLeft.setY(std::max(available.top() - kShadow, std::min(topLeft.y(), available.bottom() + kShadow - this->height())));
    }

    this->move(topLeft);
    this->show();
    this->raise();
    this->setFocus(Qt::PopupFocusReason);
    this->update();
}

} // namespace gui
