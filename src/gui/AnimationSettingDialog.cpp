#include "gui/AnimationSettingDialog.h"
#include "gui/qml/AnimationSettingViewModel.h"
#include "gui/qml/QmlTheme.h"
#include <QBitmap>
#include <QApplication>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QQuickWidget>
#include <QQmlContext>
#include <QScreen>
#include <QSurfaceFormat>
#include <QUrl>
#include <QVBoxLayout>

namespace gui {

namespace {
// Dialog size and corner radius, exposed to the QML scene as context properties
// so the C++ window mask and the QML rounded body share one source of truth.
constexpr int kDialogWidth = 340;
constexpr int kDialogHeight = 176;
constexpr qreal kDialogRadius = 8.0;

// QQuickWidget subclass that restores the held LeftButton on a mouse move that
// Qt's popup path re-targeted with buttons()=NoButton. The QQuickMouseArea's C++
// move handler only treats a move as a drag when buttons() includes an accepted
// button, so a NoButton move silently stops a QML scrub. When the global mouse
// still holds the left button (the drag is genuinely active), re-stamp it so the
// QML scene keeps scrubbing — the direct-delivery counterpart to
// AnimationSettingDialog::mouseMoveEvent (which forwards redirected moves here).
class ScrubQuickWidget : public QQuickWidget {
public:
    using QQuickWidget::QQuickWidget;
protected:
    void mouseMoveEvent(QMouseEvent* aEvent) override {
        if (!(aEvent->buttons() & Qt::LeftButton)
            && (QGuiApplication::mouseButtons() & Qt::LeftButton)) {
            QMouseEvent f(aEvent->type(), aEvent->position(), aEvent->scenePosition(),
                          aEvent->globalPosition(), aEvent->button(), Qt::LeftButton,
                          aEvent->modifiers(), aEvent->source());
            QQuickWidget::mouseMoveEvent(&f);
            return;
        }
        QQuickWidget::mouseMoveEvent(aEvent);
    }
};
} // namespace

AnimationSettingDialog::AnimationSettingDialog(QWidget* aParent)
    : QWidget(aParent) {
    // A floating popup, ala EasingPickWidget. The shell is deliberately thin:
    // the visual chrome (rounded body, border, header + close) is drawn by the
    // Qt Quick scene. This widget just provides the transparent top-level
    // backing, an alpha channel, a transparent scene clear color, and a rounded
    // window mask so the transparent corners pass clicks through. Frameless is
    // redundant for a Qt::Popup, but on some platforms (Windows) the translucent
    // popup needs it to composite the rounded corners correctly.
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);

    // The model starts at neutral values; showAt() sets the real project state
    // on every open (this is a pure view — it never holds a project reference).
    // The placeholder must satisfy the model's fps >= 1 invariant (the model
    // clamps writers to >= 1 and asserts a valid fps), even though it is
    // overwritten before the scene is shown.
    mModel = new AnimationSettingViewModel(0, 0, 1, false, this);
    mTheme = new QmlTheme(this);

    mQuick = new ScrubQuickWidget(this);
    // Request an alpha channel so the transparent clear color actually shows the
    // desktop through the rounded corners (must be set before the scene renders).
    QSurfaceFormat fmt;
    fmt.setAlphaBufferSize(8);
    mQuick->setFormat(fmt);
    mQuick->setResizeMode(QQuickWidget::SizeRootObjectToView);
    mQuick->setClearColor(Qt::transparent);
    mQuick->rootContext()->setContextProperty("model", mModel);
    mQuick->rootContext()->setContextProperty("theme", mTheme);
    mQuick->rootContext()->setContextProperty("dialogWidth", kDialogWidth);
    mQuick->rootContext()->setContextProperty("dialogHeight", kDialogHeight);
    mQuick->rootContext()->setContextProperty("dialogRadius", kDialogRadius);
    // Expose the host so the QML scrub fields can mark a scrub as
    // beginScrub()/endScrub(); the dialog then forwards the popup's re-targeted
    // moves to the scene (see mouseMoveEvent). NOT a grabMouse. Set before the
    // source loads, or the QML binding evaluates scrubGrab as undefined.
    mQuick->rootContext()->setContextProperty("scrubGrab", this);
    mQuick->setSource(QUrl(QStringLiteral("qrc:/AnimeEffects/Dialogs/qml/AnimationSettingDialog.qml")));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(mQuick);

    setFixedSize(kDialogWidth, kDialogHeight);

    // Rounded mask: clips the window so the (transparent) corner cut is
    // click-through and the compositor knows the shape.
    QBitmap mask(size());
    mask.fill(Qt::color0);
    QPainter mp(&mask);
    mp.setRenderHint(QPainter::Antialiasing, true);
    mp.setBrush(Qt::color1);
    mp.setPen(Qt::NoPen);
    mp.drawRoundedRect(QRect(QPoint(0, 0), size()), kDialogRadius, kDialogRadius);
    setMask(mask);

    // The QML close button calls model.reject(); dismiss the popup.
    connect(mModel, &AnimationSettingViewModel::rejected, this, &QWidget::hide);

    // Forward each model commit out as a single changed() signal; the caller
    // reads the current state and applies it live.
    connect(mModel, &AnimationSettingViewModel::fpsChanged, this, [this] { emit changed(); });
    connect(mModel, &AnimationSettingViewModel::frameStartChanged, this, [this] { emit changed(); });
    connect(mModel, &AnimationSettingViewModel::frameEndChanged, this, [this] { emit changed(); });
    connect(mModel, &AnimationSettingViewModel::loopChanged, this, [this] { emit changed(); });
}

AnimationSettingDialog::~AnimationSettingDialog() {
    // Tear down the QML scene before the model/theme context QObjects die, so
    // the scene's binding teardown never re-reads a destroyed context property.
    delete mQuick;
    mQuick = nullptr;
}

bool AnimationSettingDialog::isLoopChecked() const { return mModel->loop(); }

util::Range AnimationSettingDialog::frameRange() const {
    return util::Range(mModel->frameStart(), mModel->frameEnd());
}

int AnimationSettingDialog::fps() const { return mModel->fps(); }

void AnimationSettingDialog::beginScrub() {
    mScrubActive = true;
    mModel->beginGesture();
}

void AnimationSettingDialog::endScrub() {
    mScrubActive = false;
    mModel->endGesture();
}

void AnimationSettingDialog::mouseMoveEvent(QMouseEvent* aEvent) {
    // Qt's popup path re-targets a move that lands outside the popup with
    // buttons() = NoButton (it drops the held LeftButton). The QQuickMouseArea's
    // C++ move handler only treats a move as a drag move when buttons() includes
    // an accepted button, so a NoButton move silently stops a QML scrub. While a
    // scrub is active, re-deliver the move to the QQuickWidget with the LeftButton
    // restored so the scene sees the drag continue (the QML space-conversions are
    // done by the widget). This is the same idea as
    // EasingPickWidget::forwardBlendCursorEvent.
    if (mScrubActive && (aEvent->buttons() & Qt::LeftButton) == 0
        && (QGuiApplication::mouseButtons() & Qt::LeftButton)) {
        const QPoint local = mQuick->mapFromGlobal(aEvent->globalPosition().toPoint());
        QMouseEvent f(aEvent->type(), local, aEvent->scenePosition(), aEvent->globalPosition(),
                      aEvent->button(), Qt::LeftButton, aEvent->modifiers(), aEvent->source());
        QApplication::sendEvent(mQuick, &f);
        aEvent->accept();
        return;
    }
    QWidget::mouseMoveEvent(aEvent);
}

void AnimationSettingDialog::mouseReleaseEvent(QMouseEvent* aEvent) {
    if (mScrubActive && aEvent->button() == Qt::LeftButton) {
        const QPoint local = mQuick->mapFromGlobal(aEvent->globalPosition().toPoint());
        QMouseEvent f(aEvent->type(), local, aEvent->scenePosition(), aEvent->globalPosition(),
                      Qt::LeftButton, Qt::NoButton, aEvent->modifiers(), aEvent->source());
        QApplication::sendEvent(mQuick, &f);
        endScrub();
        aEvent->accept();
        return;
    }
    QWidget::mouseReleaseEvent(aEvent);
}

void AnimationSettingDialog::hideEvent(QHideEvent* aEvent) {
    // The popup can close on a release that lands outside it, so the release
    // that ends a scrub never arrives; reset the scrub/gesture either way (the
    // QML MouseArea's own onCanceled also resets its scrubbing state).
    mScrubActive = false;
    mModel->endGesture();
    // Commit the whole session as one change (the owner pushes a single undo
    // command; it no-ops if nothing changed since open).
    emit applied();
    QWidget::hideEvent(aEvent);
}

void AnimationSettingDialog::showAt(const QPoint& aAnchor, const util::Range& aRange, int aFps, bool aLoop) {
    // Reset the working state and drop the session history so a reused popup
    // reflects the current values on each open (the caller has already captured
    // this state as the undo baseline, so the resulting changed() live-apply is
    // a no-op).
    mModel->reset(aRange.min(), aRange.max(), aFps, aLoop);
    // A reused scene may have cached the theme on a prior open; re-notify so the
    // QML re-reads the current palette (reflecting a theme change made between
    // openings).
    mTheme->refresh();

    // Position just below the anchor, clamped to the screen.
    QPoint topLeft = aAnchor;
    const QScreen* scr = screen();
    if (!scr) scr = QGuiApplication::primaryScreen();
    const QRect avail = scr ? scr->availableGeometry() : QRect();
    if (!avail.isNull()) {
        if (topLeft.x() + width() > avail.right() + 1)
            topLeft.setX(avail.right() + 1 - width());
        if (topLeft.x() < avail.left())
            topLeft.setX(avail.left());
        if (topLeft.y() + height() > avail.bottom() + 1)
            topLeft.setY(avail.bottom() + 1 - height());
        if (topLeft.y() < avail.top())
            topLeft.setY(avail.top());
    }
    move(topLeft);
    show();
    raise();
}

} // namespace gui
