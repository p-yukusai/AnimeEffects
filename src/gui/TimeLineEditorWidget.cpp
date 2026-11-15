#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QFontMetrics>
#include <QKeyEvent>
#include <algorithm>
#include <sstream>
#include "core/TimeKeyExpans.h"
#include "core/KeyEasing.h"
#include "core/KeyEasingAccess.h"
#include "core/ImageKey.h"
#include "core/StateKey.h"
#include "core/Serializer.h"
#include "util/StreamWriter.h"
#include "core/ChannelSpec.h"
#include "core/BlurKey.h"
#include "core/WaveKey.h"
#include "core/TimeFormat.h"
#include "cmnd/BasicCommands.h"
#include "ctrl/CmndName.h"
#include "ctrl/TimeLineUtil.h"
#include "ctrl/graph/graph_Curve.h"
#include "gui/TimeLineEditorWidget.h"

#include <QCanvasPainter>
#include "gui/TimeBodyCanvas.h"
#include "gui/MouseSetting.h"
#include "gui/PillInk.h"
#include "gui/prop/prop_Items.h"
#include "gui/prop/prop_KeyAccessor.h"
#include "qapplication.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QMimeData>
#include "gui/theme/Colors.h"

namespace gui {

namespace {
// The window's two fields, compared literally: ctrl::TimeLineEditor::PlayWindow is the
// timeline editor's own struct and carries no operator==. Used only to decide whether
// the playWindowChanged signal has something to announce.
bool samePlayWindow(
    const ctrl::TimeLineEditor::PlayWindow& aLhs, const ctrl::TimeLineEditor::PlayWindow& aRhs
) {
    return aLhs.start == aRhs.start && aLhs.end == aRhs.end;
}

// WHAT A TIMELINE KEY COMMAND ACTS ON. The timeline selects whole KEYS (a
// key-level entry), while the animation graph selects individual markers. A
// target carries only the component the hit test landed on, so a key-level
// entry is fanned out here over every channel key at that frame; a marker
// entry contributes exactly its component, and several marker entries for the
// same key contribute their union. Structural keys and non-track properties
// keep their single target. Stale targets (the key left the document while a
// menu was open) are dropped, not fanned out.
core::TimeLineEvent targetsForSelection(
    const core::TimeLineEvent& aTargets, const ctrl::time::Selection& aSelection
) {
    core::TimeLineEvent out;
    out.setType(aTargets.type());
    for (const core::TimeLineEvent::Target& target : aTargets.targets()) {
        if (!target.node || target.pos.isNull()) {
            continue;
        }
        core::TimeKeyPos targetPos = target.pos;
        core::TimeLine* line = targetPos.line();
        const core::TimeKeyType type = targetPos.type();
        const int frame = targetPos.index();
        // A whole-property target (-1) has no legacy map slot to "exist"; its
        // channels are resolved below. A component target still checks that its
        // key is where the menu named it.
        const bool propertyLevel =
            core::TimeLine::usesChannelTracks(type) && targetPos.component() < 0;
        if (!propertyLevel && !target.pos.isExist()) {
            continue;
        }

        bool wholeKey = false;
        QVector<int> namedComponents;
        for (const ctrl::time::Selection::Item& item : aSelection.items()) {
            if (item.target.node != target.node || item.target.pos.line() != line
                || item.target.pos.type() != type || item.target.pos.index() != frame) {
                continue;
            }
            if (item.component == ctrl::time::Selection::kKeyLevel) {
                wholeKey = true;
            } else if (!namedComponents.contains(item.component)) {
                namedComponents.push_back(item.component);
            }
        }

        if (wholeKey && core::TimeLine::usesChannelTracks(type)) {
            const core::PropertyTrack* prop = line->property(type);
            if (prop) {
                for (int c = 0; c < prop->channelCount(); ++c) {
                    if (line->hasChannelKey(type, c, frame)) {
                        out.pushTarget(*target.node, core::TimeKeyPos(*line, type, frame, c));
                    }
                }
                if (prop->state().keyAt(frame)) {
                    out.pushTarget(
                        *target.node,
                        core::TimeKeyPos(*line, type, frame, core::TimeKeyPos::kStateComponent)
                    );
                }
                continue;
            }
        }
        if (!namedComponents.isEmpty()) {
            for (int c : namedComponents) {
                if (line->hasChannelKey(type, c, frame)) {
                    out.pushTarget(*target.node, core::TimeKeyPos(*line, type, frame, c));
                }
            }
            continue;
        }
        out.pushTarget(*target.node, target.pos);
    }
    return out;
}
} // namespace

//-------------------------------------------------------------------------------------------------
TimeCursor::TimeCursor(QWidget* aParent):
    QWidget(aParent),
    mEdgeColor(theme::Colors::current().accentBright) {
    this->setAutoFillBackground(false);
    // The line is decoration: every press in its column belongs to the surface (and, over
    // the strip, to the ruler's own seek). It used to swallow them.
    this->setAttribute(Qt::WA_TransparentForMouseEvents, true);
}

void TimeCursor::paintEvent(QPaintEvent* aEvent) {
    (void)aEvent;
    // One opaque column - the strip's own line ink, which is what the playhead is.
    QPainter painter(this);
    painter.fillRect(this->rect(), mEdgeColor);
}

void TimeCursor::setCursorPos(const QPoint& aLineTopLeft, int aHeight) {
    this->setGeometry(aLineTopLeft.x(), aLineTopLeft.y(), 1, aHeight);
    update();
}

QColor TimeCursor::edgeColor() const { return mEdgeColor; }

void TimeCursor::setEdgeColor(const QColor& cursorEdgeColor) { mEdgeColor = cursorEdgeColor; }

//-------------------------------------------------------------------------------------------------
TimeLineEditorWidget::TimeLineEditorWidget(ViaPoint& aViaPoint, QWidget* aParent):
    QCanvasPainterWidget(aParent),
    mViaPoint(aViaPoint),
    mProject(),
    mTimeLineSlot(),
    mTreeRestructSlot(),
    mProjectAttrSlot(),
    mEditor(),
    mCamera(),
    mTimeCursor(this),
    mKeyCommandMap(*aViaPoint.keyCommandMap()),
    mCopyKey(),
    mPasteKey(),
    mDeleteKey(),
    mEasingHost(),
    mTargets(),
    mCopyTargets(),
    mPastePos(),
    mOnPasting(),
    mTimelineTheme() {
    // The Canvas host clears its texture to this every frame (a QWidget host shows the QSS
    // floor instead); updateTheme() re-reads it when the palette changes.
    setFillColor(theme::Colors::current().base);
    mTimeCursor.show();

    mEditor.reset(new ctrl::TimeLineEditor());
    this->setMouseTracking(true);
    // The surface is where Ctrl+A lives (see keyPressEvent): ClickFocus, not
    // StrongFocus, so a click in the lanes hands the keyboard to this widget (the
    // press still propagates to TimeLineWidget's gestures, because every mouse
    // handler here ignores) without adding a Tab stop beside the timeline's own.
    this->setFocusPolicy(Qt::ClickFocus);
    this->setContextMenuPolicy(Qt::CustomContextMenu);

    // THE CHIP FOLLOWS THE SELECTION, NOT THE EVENT LOOP. A selection another view
    // makes is announced by the set itself (ctrl::time::Selection::onChanged): the
    // animation graph panel writes the very same set (the panel shares it - see
    // ctrl::time::Selection and ctrl::GraphEditor::setSelectionSource), and the only
    // other announcement it used to have was a repaint request, which Qt DROPS while
    // this widget is hidden, collapsed or not yet laid out. The notification has no
    // such gap, so there is nothing left for a reconcile timer to cover - and one
    // source for one fact is the point (see pushEasingChipState). The push is
    // idempotent (prop::EasingChip ignores a state equal to the one it holds), so a
    // change the chip does not show costs one compare.
    mEditor->selection().onChanged.connect(this, &TimeLineEditorWidget::pushEasingChipState);
    // ...AND THE OPEN PICKER FOLLOWS THE SAME NOTIFICATION. A key selected in the
    // graph panel is the same set this surface reads, so the popup must re-read what
    // it shows for it - the other half of the chip's one-way sync (see
    // refreshOpenPicker; the picker itself refuses a re-read mid-gesture).
    mEditor->selection().onChanged.connect(this, &TimeLineEditorWidget::refreshOpenPicker);

    // The ruler strip: own paint and own input, over the track surface and
    // under the playhead (raised below so its pill overlays the window pills).
    mTimeRuler.reset(new TimeRuler(*this, this));
    mTimeRuler->setGeometry(0, 0, this->width(), ctrl::TimeLineEditor::kHeaderHeight);
    mTimeRuler->show();
    mTimeCursor.raise();
    this->connect(mTimeRuler.data(), &TimeRuler::frameChanged, this, &TimeLineEditorWidget::rulerFrameChanged);

    this->connect(this, &QWidget::customContextMenuRequested, this, &TimeLineEditorWidget::onContextMenuRequested);

    {
        mCopyKey = new QAction(tr("Copy"), this);
        mCopyKey->connect(mCopyKey, &QAction::triggered, this, &TimeLineEditorWidget::onCopyKeyTriggered);

        mPasteKey = new QAction(tr("Paste"), this);
        mPasteKey->connect(mPasteKey, &QAction::triggered, this, &TimeLineEditorWidget::onPasteKeyTriggered);

        mDeleteKey = new QAction(tr("Delete"), this);
        mDeleteKey->connect(mDeleteKey, &QAction::triggered, this, &TimeLineEditorWidget::onDeleteKeyTriggered);

        mCopyToClipboard = new QAction(tr("Copy to clipboard"), this);
        mCopyToClipboard->connect(
            mCopyToClipboard, &QAction::triggered, this, &TimeLineEditorWidget::onCopyCBTriggered
        );

        mSelectSpacing = new QAction(tr("Select spacing between keys"), this);
        mSelectSpacing->connect(mSelectSpacing, &QAction::triggered, [=] {onSelectSpacingTriggered();});
    }
    this->update();
    {
        auto key = mKeyCommandMap.get("Copy");
        if (key)
            key->addListener([=]() {
                mTargets = core::TimeLineEvent();
                mEditor->retrieveSelectionTargets(mTargets);
                mCopyKey->trigger();
            });
    }
    {
        auto key = mKeyCommandMap.get("Paste");
        if (key)
            key->addListener([=]() {
                if (!mCopyTargets.targets().isEmpty()) {
                    // Anchor keyboard paste at the playhead, not the mouse:
                    // QCursor::pos() is a screen coordinate, and mapped into
                    // this widget it only denotes a frame while the cursor
                    // actually hovers the timeline. Elsewhere (viewport,
                    // object tree) it maps to an arbitrary frame, so Ctrl+V
                    // looked like it pasted at random. The context menu keeps
                    // its own click-position anchor (onContextMenuRequested).
                    const auto& scale = mEditor->timeScale();
                    const int x =
                        scale.pixelWidth(mEditor->currentFrame().get()) + ctrl::TimeLineEditor::kTimeLineMargin;
                    mPastePos = QPoint(x, 0);
                    mPasteKey->trigger();
                }
            });
    }
    {
        auto key = mKeyCommandMap.get("Delete");
        if (key)
            key->addListener([=]() {
                mTargets = core::TimeLineEvent();
                if (mEditor->retrieveSelectionTargets(mTargets)) {
                    mDeleteKey->trigger();
                }
            });
    }
}

void TimeLineEditorWidget::setProject(core::Project* aProject) {
    // A session names params and a command stack that belong to the old document;
    // dropping its handles here keeps a step after the swap from rewriting them.
    endEasingEdit();
    if (mProject) {
        mProject->onTimeLineModified.disconnect(mTimeLineSlot);
        mProject->onTreeRestructured.disconnect(mTreeRestructSlot);
        mProject->onProjectAttributeModified.disconnect(mProjectAttrSlot);
    }

    if (aProject) {
        mProject = aProject->pointee();
        mTimeLineSlot = aProject->onTimeLineModified.connect(this, &TimeLineEditorWidget::onTimeLineModified);

        mTreeRestructSlot = aProject->onTreeRestructured.connect(this, &TimeLineEditorWidget::onTreeRestructured);

        mProjectAttrSlot =
            aProject->onProjectAttributeModified.connect(this, &TimeLineEditorWidget::onProjectAttributeModified);
    } else {
        mProject.reset();
    }

    const ctrl::TimeLineEditor::PlayWindow windowBefore = mEditor->playWindow();
    mEditor->setProject(aProject);
    // ctrl::TimeLineEditor::setProject clears the temporary window (it is a gesture on
    // one project's ruler); announce it so the graph's overlay goes with it.
    if (!samePlayWindow(windowBefore, mEditor->playWindow()))
        emit playWindowChanged();
    updateSize();
    // The project owns the ruler's content (frame range, fps, play window):
    // the surface repaint alone leaves the strip stale until the next
    // camera update.
    if (mTimeRuler)
        mTimeRuler->update();
    // The keys went with the old project too: ctrl::TimeLineEditor::setProject
    // clears the selection (clearRows), and the repaint above is what re-reads it
    // for the header's chip.
}

void TimeLineEditorWidget::setFrame(core::Frame aFrame) {
    mEditor->setFrame(aFrame);

    // particial rendering
    updateTimeCursorPos();
}

core::Frame TimeLineEditorWidget::currentFrame() const { return mEditor->currentFrame(); }

void TimeLineEditorWidget::updateTimeCursorPos() {
    if (mCamera) {
        auto pos = mEditor->currentTimeCursorPos();
        // THE PILL IS THE RULER'S (Model::playhead): the model suppresses the number under
        // it, so the strip repaints each tick anyway - which is what moves the pill.
        if (mTimeRuler)
            mTimeRuler->update();
        // The line is the surface's own decoration: pinned to the top (the ruler's line
        // row) and following the content horizontally.
        mTimeCursor.setEdgeColor(theme::Colors::current().accentBright);
        const QPoint viewportPos(mCamera->leftTopPos().toPoint().x() + pos.x(), frameRuler::kLineTop);
        mTimeCursor.setCursorPos(viewportPos, mCamera->screenHeight() - frameRuler::kLineTop);
    }
}

void TimeLineEditorWidget::updateCamera(const core::CameraInfo& aCamera) {
    mCamera = &aCamera;
    updateSize();
    updateTimeCursorPos();
    if (mTimeRuler)
        mTimeRuler->setCamera(&aCamera);
}

void TimeLineEditorWidget::resizeEvent(QResizeEvent* aEvent) {
    QWidget::resizeEvent(aEvent);
    if (mTimeRuler)
        mTimeRuler->setGeometry(0, 0, this->width(), ctrl::TimeLineEditor::kHeaderHeight);
}

void TimeLineEditorWidget::keyPressEvent(QKeyEvent* aEvent) {
    // Ctrl+A selects everything the timeline shows (see
    // ctrl::TimeLineEditor::selectAllKeys for the exact enumeration) and repaints
    // only when the set actually changed - a second Ctrl+A is not a repaint.
    // The modifiers must be EXACTLY Ctrl: Ctrl+Shift+A is a different binding, the
    // graph panel's own rule (GraphPanelWidget::panelKeyAction).
    if (aEvent->key() == Qt::Key_A && aEvent->modifiers() == Qt::ControlModifier) {
        if (mEditor->selectAllKeys())
            this->update();
        aEvent->accept();
        return;
    }
    // Everything else is not this surface's: the base ignores it, which is what
    // propagates it to TimeLineWidget's arrow-key handling and then to the
    // application's own key map.
    QWidget::keyPressEvent(aEvent);
}

void TimeLineEditorWidget::setRows(const gui::AnimationRows& aRows) {
    // A collapse/expand/reorder rebuilds the rows, not the document. Clear the ROWS only,
    // then drop just the selection entries whose layer the rebuilt rows no longer show (a
    // closed folder's summary still represents its descendants).
    mEditor->resetRows();

    for (const gui::AnimationRow& row : aRows) {
        // the document row is the tree's own header: a document is not a layer, so it has
        // no lane and pushes nothing (its height is still part of the content axis, which
        // is why `top` is the model's cumulative pixel offset and not a row index)
        if (!row.node || row.isDocument)
            continue;
        mEditor->pushRow(row.node, util::Range(row.top, row.bottom()), row.closedFolder);
    }

    mEditor->retainSelectionInRows();
    updateSize();
    this->update();
}

void TimeLineEditorWidget::updateLineSelection(core::ObjectNode* aRepresent) {
    mEditor->updateRowSelection(aRepresent);
    this->update();
}

bool TimeLineEditorWidget::updateCursor(const core::AbstractCursor& aCursor, Qt::KeyboardModifiers aModifiers) {
    const ctrl::TimeLineEditor::PlayWindow windowBefore = mEditor->playWindow();
    ctrl::TimeLineEditor::UpdateFlags flags = mEditor->updateCursor(aCursor, aModifiers);
    // THE WINDOW'S OWN ANNOUNCEMENT: the ruler's shift-drag sets, moves or collapses
    // the temporary playback window through this call, and it can do so without moving
    // the current frame (so rulerFrameChanged and ModFrame are both silent). The graph
    // panel draws the same window, so the GUI is told here, where the gesture lands.
    if (!samePlayWindow(windowBefore, mEditor->playWindow()))
        emit playWindowChanged();

    if (flags & ctrl::TimeLineEditor::UpdateFlag_ModView) {
        updateTimeCursorPos();
        // The repaint re-reads the selection for the header's chip (see
        // paintEvent): ModView is the editor's own "the selection and its
        // highlight changed" report, and every selection mutator sets it.
        this->update();
    }
    return (flags & ctrl::TimeLineEditor::UpdateFlag_ModFrame);
}

bool TimeLineEditorWidget::updateWheel(QWheelEvent* aEvent, int aMouseX, int& aPixelAfter) {
    const double oldSpacing = mEditor->pixelsPerFrame();
    const bool changed =
        mEditor->updateWheel(aEvent->angleDelta().y(), mViaPoint.mouseSetting().invertZoom);
    if (changed) {
        // Pixel-exact zoom anchor: the content point under the mouse stays put,
        // regardless of whether it lies inside the frame range.
        aPixelAfter = mEditor->anchorPixelAfterScale(aMouseX, oldSpacing);
        updateSize();
    } else {
        // Zoom clamped at floor/ceiling: keep the view exactly still instead of
        // re-deriving a scroll offset (frame<->pixel rounding would drift it).
        aPixelAfter = aMouseX;
    }
    return changed;
}

void TimeLineEditorWidget::updateSize() {
    // The widget is a viewport-fixed surface sized by TimeLineWidget; content
    // size changes only need a repaint (the paint translates by the camera).
    this->update();
}

void TimeLineEditorWidget::updateProjectAttribute() {
    const ctrl::TimeLineEditor::PlayWindow windowBefore = mEditor->playWindow();
    mEditor->updateProjectAttribute();
    // A frame-range change invalidates the temporary window (ctrl::TimeLineEditor's
    // rule), so the panel's overlay must be told the same thing the ruler is.
    if (!samePlayWindow(windowBefore, mEditor->playWindow()))
        emit playWindowChanged();
    updateTimeCursorPos();
    updateSize();
}

void TimeLineEditorWidget::updateTheme(theme::Theme& aTheme) {
    Q_UNUSED(aTheme) // TODO
    mTimelineTheme.reset();
    // The Canvas host clears its texture to this every frame; a QWidget host shows the QSS
    // floor instead. Re-read with the theme so a palette change reaches it.
    setFillColor(theme::Colors::current().base);
    if (mTimeRuler)
        mTimeRuler->updateTheme();
    updateTimeCursorPos();
}
QSize TimeLineEditorWidget::getEditorSize() const {
    return mEditor->modelSpaceSize();
}

int TimeLineEditorWidget::frameStartPixel() const {
    return mEditor->frameStartPixel();
}

int TimeLineEditorWidget::frameEndPixel() const {
    return mEditor->frameEndPixel();
}

void TimeLineEditorWidget::paint(QCanvasPainter* aPainter) {
    if (!mCamera)
        return;
    // The surface is viewport-fixed; the content translates by the view origin, exactly as the
    // QPainter host's paintEvent does.
    const QPoint origin = mCamera->leftTopPos().toPoint();
    const QRect cull = QRect(QPoint(0, 0), size()).translated(-origin);
    const ctrl::time::BodyScene scene = mEditor->bodyScene(*mCamera, mTimelineTheme, cull);

    aPainter->save();
    aPainter->translate(float(origin.x()), float(origin.y()));
    gui::timeBodyCanvas::paintLinesCanvas(*aPainter, scene.lines);
    gui::timeBodyCanvas::paintGuidesCanvas(*aPainter, scene.markers, ctrl::time::SelectionBox());
    for (const ctrl::time::KeyPass& pass : scene.keyPasses)
        gui::timeBodyCanvas::paintKeysCanvas(*aPainter, pass);
    gui::timeBodyCanvas::paintGuidesCanvas(*aPainter, {}, scene.selection);
    aPainter->restore();

    // The hairline under the ruler separates it from the track area (the QPainter host draws the
    // same row on the first track row, below the ruler widget).
    aPainter->setFillStyle(mTimelineTheme.rulerLineColor());
    aPainter->fillRect(QRectF(0, ctrl::TimeLineEditor::kHeaderHeight, width(), 1));

    pushEasingChipState();
}

void TimeLineEditorWidget::onTimeLineModified(core::TimeLineEvent& aEvent, bool aUndo) {
    if (!mOnPasting) {
        mCopyTargets = core::TimeLineEvent();
    }
    mEditor->updateKey();
    // Creating keys always selects its output: clear the prior selection and
    // keep only the keys a push/copy just created. An undo/redo that REMOVES
    // keys (aUndo=true) must not select a notification over keys that are no
    // longer in their lane — refresh() already dropped them, so only select on
    // a forward (exec/redo) creation.
    if (!aUndo &&
        (aEvent.type() == core::TimeLineEvent::Type_PushKey || aEvent.type() == core::TimeLineEvent::Type_CopyKey)) {
        mEditor->selectCreatedKeys(aEvent);
    }
    // A modification can move, drop or re-ease the keys under the selection (an
    // undo, a delete, or the picker's own commit). The repaint re-reads them for the
    // header's chip, and this push makes that independent of the paint: the document
    // signal arrives whether or not this widget is visible, so the chip names the
    // post-edit truth - and re-derives the Auto context from the document - even with
    // the dock's surface hidden.
    this->update();
    pushEasingChipState();
    // THE OPEN PICKER FOLLOWS THE DOCUMENT TOO. The graph's handle drag and its mode
    // changes modify the very key an open picker is showing, and this signal arrives
    // whether or not either page is painted - so the popup is re-read here, not on the
    // next time something happens to repaint it. The picker ignores the re-read while a
    // gesture is in progress in it (a drag there wins until it ends).
    refreshOpenPicker();
}

void TimeLineEditorWidget::onTreeRestructured(core::ObjectTreeEvent&, bool) { mCopyTargets = core::TimeLineEvent(); }

void TimeLineEditorWidget::onProjectAttributeModified(core::ProjectEvent&, bool) {
    mCopyTargets = core::TimeLineEvent();
}

void TimeLineEditorWidget::onContextMenuRequested(const QPoint& aPos) {
    // The ruler's context menu is reserved for the temporary playback
    // window: it shows only while a window is active, and only when the
    // click is on the ruler itself — the window lives there, so its clear
    // action sits next to it, and the track menu stays key-focused. The
    // ruler region is the editor's predicate, the same one the gesture
    // machinery uses.
    if (mEditor->isOnRuler(aPos.y())) {
        if (!mEditor->playWindow().active())
            return;
        QMenu menu(this);
        auto* clearWindow = menu.addAction(tr("Clear playback window"));
        clearWindow->connect(clearWindow, &QAction::triggered, [this]() {
            const bool wasActive = mEditor->playWindow().active();
            mEditor->clearPlayWindow();
            if (wasActive)
                emit playWindowChanged();
            this->update();
            // The window's band and pills are the ruler's paint; the surface
            // repaint alone would leave them on screen until the next move.
            if (mTimeRuler)
                mTimeRuler->update();
        });
        menu.exec(this->mapToGlobal(aPos));
        this->update();
        return;
    }

    QMenu menu(this);

    // customContextMenuRequested delivers surface coordinates, but every
    // editor query below runs in model space: rows are pushed there
    // (updateLinesRecursive shifts them by the view origin) and mouse
    // events land there through AbstractCursor's toWorldPos. Feeding raw
    // surface coordinates silently missed every key whenever the view was
    // scrolled — no highlight, no key actions — and anchored paste at a
    // shifted frame. The ruler check above stays in surface space on
    // purpose: the ruler strip is pinned to the widget top.
    const QPoint worldPos = mCamera ? mCamera->toWorldPos(QPointF(aPos)).toPoint() : aPos;

    mTargets = core::TimeLineEvent();
    const bool clickedOnKey = mEditor->selectKeysAt(mTargets, worldPos);
    if (!clickedOnKey) {
        // Not on a key: the current selection is what the menu acts on ("keys stay
        // operable from anywhere once selected"). It is empty when nothing is
        // selected at all, and buildKeyMenu reads that as the paste-only menu.
        mEditor->retrieveSelectionTargets(mTargets);
    }

    mPastePos = worldPos;

    // Both shapes of the menu are buildKeyMenu's, which is also the graph panel's
    // route into this menu (see openKeyMenu).
    buildKeyMenu(menu);
    menu.exec(this->mapToGlobal(aPos));
    // A right-click on an unselected key lent it a temporary selection for
    // this menu only; the previous selection is not restored, so drop the
    // temporary selection now that the menu is gone, then repaint so the
    // highlight clears (see ctrl::TimeLineEditor::endTemporarySelection).
    mEditor->endTemporarySelection();
    this->update();
}

//-------------------------------------------------------------------------------------------------
bool TimeLineEditorWidget::setSelectionProportional(bool aEnabled) {
    if (!mProject || !mEditor)
        return false;

    core::TimeLineEvent targets;
    if (!mEditor->retrieveSelectionTargets(targets) || targets.targets().isEmpty())
        return false;

    this->applyProportionalForTargets(targets, aEnabled);
    return true;
}

void TimeLineEditorWidget::applyProportionalForTargets(const core::TimeLineEvent& aTargets, bool aEnabled) {
    if (!mProject)
        return;

    ctrl::TimeLineUtil::setProportionalKeys(*mProject, aTargets, aEnabled);
    // The weights live on the keys, not on the selection, so the selection's own
    // signal cannot report this. The header's control (and anything else that
    // mirrors the flags) listens here.
    emit keyProportionalChanged();
}

//-------------------------------------------------------------------------------------------------
void TimeLineEditorWidget::buildKeyMenu(QMenu& aMenu) {
    // ONE BUILDER, TWO CALLERS: the timeline's own right-click above and the
    // animation graph panel's (openKeyMenu, reached through gui::TimeLineWidget and
    // gui::MainDisplayWidget). Both hand this the same state - mTargets, the whole
    // selection in the timeline's currency (one target per KEY), mCopyTargets (what
    // a paste would drop) and mPastePos (the frame it would land on) - and both get
    // the same items, the same labels and the same edit path as a result. There is
    // no second menu to keep in step.
    //
    // THE ITEMS ACT ON THE SET. Every action below reads mTargets - the selection the
    // caller set - and not the one key a press landed on, so with N keys selected an
    // activation is one edit over all N of them:
    //   * Copy: mCopyTargets = mTargets, the whole selection (plus the clipboard
    //     write when the user's "auto copy to clipboard" setting is on).
    //   * Copy to clipboard: the clipboard write over mCopyTargets, which the item
    //     above has just filled with the whole selection. A STANDALONE activation keeps
    //     whatever was copied last - onCopyCBTriggered's own long-standing rule, the
    //     same in the timeline's menu.
    //   * Paste: drops mCopyTargets at mPastePos, the frame this menu was opened on
    //     (the selection supplies the anchor here; what is written is the copied set).
    //   * Select spacing between keys: one dialog, then every target of mTargets moves.
    //   * Delete: one deleteCheckedKeys(mTargets) command over the whole set.
    // Easing is deliberately absent: it lives in the dock header's own chip
    // (openEasingPicker), so a menu entry would be a second route to one edit.
    // ACTIONS ARE INVARIANT VERBS. Unlike a count ("1 key"/"3 keys"), a command does
    // not change with the selection: "Copy", "Paste", "Delete" read the same for one
    // key or twenty and never inflect, which is the convention most software follows.
    // The count is the chip/ruler's job; the menu names the action.
    mPasteKey->setEnabled(mCopyTargets.hasAnyTargets());

    if (mTargets.targets().isEmpty()) {
        aMenu.addAction(mPasteKey); // no key involved: paste is all there is
        return;
    }

    aMenu.addAction(mCopyKey);
    aMenu.addAction(mCopyToClipboard);
    aMenu.addAction(mPasteKey);
    // Proportional toggles the whole selection through ONE call, which pushes one
    // undo step (ctrl::TimeLineUtil::setProportionalKeys). The check reflects the
    // set: all keys proportional reads checked, a mixed or absolute set unchecked -
    // activating then makes every selected key proportional. Weights are computed
    // from each key's current position against its current absolute neighbours, so
    // the action is a plain toggle of the set's state rather than a per-key edit.
    {
        bool allProportional = true;
        for (auto& target : mTargets.targets()) {
            const core::TimeKey* key = target.pos.key();
            if (!key || !key->isProportional()) {
                allProportional = false;
                break;
            }
        }
        aMenu.addSeparator();
        // NOT a checkable action: one QAction with a check mark makes QMenu reserve
        // the indicator column for EVERY item in the menu, which is the extra
        // left indent this menu had and the other context menus do not. The command
        // names the change instead ("Make proportional" / "Make absolute"), so no
        // state glyph is needed and the item aligns with Copy/Paste/Delete.
        QAction* proportional = aMenu.addAction(
            allProportional ? tr("Make absolute") : tr("Make proportional")
        );
        proportional->connect(proportional, &QAction::triggered, [this, allProportional]() {
            this->applyProportionalForTargets(mTargets, !allProportional);
        });
    }
    aMenu.addSeparator();

    // LINK / SEPARATE CHANNELS: a keyframe's grouping. A linked keyframe is a
    // property key (every graphable channel has a key there, all linked); a split
    // keyframe belongs to one channel. Every target must name one frame of one
    // multi-channel property. Linking requires the full channel set; separating
    // always works on a frame that has keys. One command/undo step.
    {
        core::ObjectNode* groupNode = nullptr;
        core::TimeLine* groupLine = nullptr;
        core::TimeKeyType groupType = core::TimeKeyType_TERM;
        int groupFrame = -1;
        bool groupable = !mTargets.targets().isEmpty();
        for (const core::TimeLineEvent::Target& target : mTargets.targets()) {
            if (!target.node || !target.pos.line()) {
                groupable = false;
                break;
            }
            core::TimeKeyPos targetPos = target.pos;
            const core::TimeKeyType type = targetPos.type();
            const int frame = targetPos.index();
            if (!core::TimeLine::usesChannelTracks(type) || core::propertyChannelCount(type) < 2) {
                groupable = false;
                break;
            }
            if (!groupNode) {
                groupNode = target.node;
                groupLine = targetPos.line();
                groupType = type;
                groupFrame = frame;
            } else if (target.node != groupNode || targetPos.line() != groupLine
                       || type != groupType || frame != groupFrame) {
                groupable = false;
                break;
            }
        }
        if (groupable && groupLine && groupNode && mProject) {
            const bool linked = groupLine->isFrameLinked(groupType, groupFrame);
            const bool canLink = groupLine->canLinkFrame(groupType, groupFrame);
            QAction* grouping = aMenu.addAction(
                linked ? tr("Separate Channels") : tr("Link Channels")
            );
            if (!linked) {
                grouping->setEnabled(canLink);
                if (!canLink) {
                    grouping->setText(tr("Link Channels (all channels need a key here)"));
                    grouping->setToolTip(
                        tr("Every channel must have a key at this frame before they can be linked.")
                    );
                }
            }
            grouping->connect(
                grouping, &QAction::triggered,
                [this, groupNode, groupType, groupFrame, linked]() {
                    ctrl::TimeLineUtil::setFrameLinked(
                        *mProject, *groupNode, groupType, groupFrame, !linked
                    );
                    emit channelGroupingChanged();
                    this->update();
                }
            );
            aMenu.addAction(mSelectSpacing);
            aMenu.addSeparator();
            aMenu.addAction(mDeleteKey);
            return;
        }
    }

    aMenu.addAction(mSelectSpacing);
    aMenu.addSeparator();
    aMenu.addAction(mDeleteKey);
}

// Type, frame, easing type, easing range, easing weight.
static void addVecToJson(QJsonObject* json, const QString& title, const QVector2D vec) {
    json->insert(title + "X", vec.x());
    json->insert(title + "Y", vec.y());
}

// Tf means Type & Frame in case you're wondering.
static void addTfToObj(QJsonObject* json, int keyType, const core::TimeKey* timeKey) {
    json->insert("Type", core::TimeLine::getTimeKeyName(static_cast<core::TimeKeyType>(keyType)));
    json->insert("Frame", timeKey->frame());
    // The flag travels, the weights and bound ids do NOT: the bounds may not exist
    // at the destination object, so the reader enables the key with empty weights
    // and the next projection resolves its bounds there.
    json->insert("Proportional", timeKey->isProportional());
}
template<typename keyData>
static void addStandardToObj(QJsonObject* json, keyData data, const int keyType, core::TimeKey* timeKey) {
    addTfToObj(json, keyType, timeKey);
    // ONE ENTRY PER COMPONENT, so a copy keeps each curve: a Move key's X and Y,
    // a Blur key's three, Pose/FFD's single shared one. The top-level eType/
    // eRange/eWeight stay component 0's, which is what a build without the
    // per-component reader understands; "eEasings" is the exact payload this
    // build reads (see ctrl::TimeLineEditor::easingToJson, the ONE description of
    // its fields, shared with the reader).
    const int stored = core::easingStorageCount(static_cast<core::TimeKeyType>(keyType));
    QJsonArray easings;
    for (int component = 0; component < stored; ++component) {
        const QJsonObject easing = ctrl::TimeLineEditor::easingToJson(data.easing(component));
        if (component == 0) {
            json->insert("eType", easing["eType"]);
            json->insert("eRange", easing["eRange"]);
            json->insert("eWeight", easing["eWeight"]);
        }
        easings.append(easing);
    }
    if (stored > 0)
        json->insert("eEasings", easings);
}

QJsonObject getKeyTypeSerialized(int keyType, core::TimeKey* timeKey, core::ObjectNode* node) {
    // Image has its own resource/grid clipboard payload (IDs must be resolved by
    // the destination tree); it is a StateKey but not a scalar state payload.
    if (!dynamic_cast<core::ImageKey*>(timeKey)) {
        if (auto* stateKey = dynamic_cast<core::StateKey*>(timeKey)) {
        QJsonObject key;
        addTfToObj(&key, keyType, timeKey);
        std::ostringstream bytes;
        util::StreamWriter writer(bytes);
        core::Serializer out(writer);
        if (!stateKey->serialize(out)) {
            return {};
        }
        const std::string data = bytes.str();
        key["StateData"] = QString::fromLatin1(
            QByteArray(data.data(), int(data.size())).toBase64()
        );
        return key;
        }
    }
    if (auto* channelKey = dynamic_cast<core::ChannelKey*>(timeKey)) {
        QJsonObject key;
        addTfToObj(&key, keyType, timeKey);
        key["Value"] = channelKey->value();
        key["Component"] = channelKey->component();
        key["Linked"] = channelKey->linked();
        const QJsonObject easing = ctrl::TimeLineEditor::easingToJson(channelKey->easing());
        key["eType"] = easing["eType"];
        key["eRange"] = easing["eRange"];
        key["eWeight"] = easing["eWeight"];
        QJsonArray easings;
        easings.append(easing);
        key["eEasings"] = easings;
        return key;
    }

    switch (keyType) {
    // General data for all keys: Type, Frame and Easing {type, range & weight}
    case core::TimeKeyType_Move: {
        core::MoveKey::Data data = dynamic_cast<const core::MoveKey*>(timeKey)->data();
        // Data types: pos(Vec2D), centroid(Vec2D), spline(int) //
        QJsonObject move;
        addStandardToObj(&move, data, keyType, timeKey);
        addVecToJson(&move, "Pos", data.pos());
        addVecToJson(&move, "Centre", data.centroid());
        move["Spline"] = core::MoveKey::getSplineName(data.spline());
        return move;
    }
    case core::TimeKeyType_Rotate: {
        auto data = dynamic_cast<const core::RotateKey*>(timeKey)->data();
        // Data types: Rotate (float) //
        QJsonObject rotate;
        addStandardToObj(&rotate, data, keyType, timeKey);
        rotate["Rotate"] = data.rotate();
        return rotate;
    } break;
    case core::TimeKeyType_Scale: {
        auto data = dynamic_cast<const core::ScaleKey*>(timeKey)->data();
        // Data types: Scale (vec2d) //
        QJsonObject scale;
        addStandardToObj(&scale, data, keyType, timeKey);
        addVecToJson(&scale, "Scale", data.scale());
        return scale;
    }
    case core::TimeKeyType_Depth: {
        auto data = dynamic_cast<const core::DepthKey*>(timeKey)->data();
        // Data types: Depth (float) //
        QJsonObject depth;
        addStandardToObj(&depth, data, keyType, timeKey);
        depth["Depth"] = data.depth();
        return depth;
    }
    case core::TimeKeyType_Opacity: {
        auto data = dynamic_cast<const core::OpacityKey*>(timeKey)->data();
        // Data types: Opacity (float) //
        QJsonObject opacity;
        addStandardToObj(&opacity, data, keyType, timeKey);
        opacity["Opacity"] = data.opacity();
        return opacity;
    }
    case core::TimeKeyType_Bone: {
        auto data = dynamic_cast<const core::BoneKey*>(timeKey)->data();
        /* Data types:
        LocalPos(vec2d), LocalAngle(float), Range(vec2d * 2),
        ---
        Shape[Valid(bool), SegmentStart(vec2d), SegmentDir(vec2d), Unit(vec2d),
        DirAngle(float), Length(float), Radius (vec2d * 2), RootBendAngle(float *2),
        TailBendAngle(float * 2), Bounding(vec2d), Polygon{PolyCount(int), Vertices(vec2d)}],
        ---
        WorldPos(vec2d), WorldAngle(float), Rotate(float),
        Children(Bones with same data type as previously described minus children of their own)
        */
        QJsonObject bones;
        addTfToObj(&bones, keyType, timeKey);
        QJsonArray boneArray;
        for (auto bone : data.topBones()) {
            QJsonObject topBone;
            topBone["Bone"] = bone->serializeToJson();
            boneArray.append(topBone);
        }
        bones["Bones"] = boneArray;
        return bones;
    }
    case core::TimeKeyType_Pose: {
        auto data = dynamic_cast<const core::PoseKey*>(timeKey)->data();
        // Data types: Same as the bone key.
        QJsonObject pose;
        // Pose carries ONE shared easing (kComponents == 1); write it like every
        // other key's so a copy keeps it.
        addStandardToObj(&pose, data, keyType, timeKey);
        QJsonArray boneArray;
        for (auto bone : data.topBones()) {
            QJsonObject topBone;
            topBone["Bone"] = bone->serializeToJson();
            boneArray.append(topBone);
        }
        pose["Poses"] = boneArray;
        return pose;
    }
    case core::TimeKeyType_Mesh: {
        auto data = dynamic_cast<const core::MeshKey*>(timeKey)->data();
        // Data for MeshKey: Origin Offset (vec2d), Vertex count (int), Vertices (vec2d), Edge count (int),
        // Edges (ints), Face count (int), Faces (ints)
        QJsonObject mesh;
        addTfToObj(&mesh, keyType, timeKey);
        mesh["Mesh"] = data.serializeToJson();
        return mesh;
    } break;
    case core::TimeKeyType_FFD: {
        auto data = dynamic_cast<const core::FFDKey*>(timeKey)->data();
        // Data for FFDKey: Vertex count (int), Vertex positions (vec3)
        QJsonObject ffd;
        addStandardToObj(&ffd, data, keyType, timeKey);
        ffd["FFD"] = dynamic_cast<const core::FFDKey*>(timeKey)->serializeToJson();
        // ⚠ IF THE IMAGE'S GRIDMESH DOESN'T MATCH THE TARGET'S THE DEFORMATION WON'T OCCUR ⚠ //
        /* We need the cell size to keep consistency with the vertex data, as the same image will
        always generate the same mesh for any given cell size, we also need to check if there is an
        image key close to our target to get the correct cellsize for the image being deformed. */
        if (node->timeLine()->map(core::TimeKeyType_Image).size() != 0) {
            int dataFrame = timeKey->frame();
            int closest = 0;
            int index = 0;
            for (auto nodeM : node->timeLine()->map(core::TimeKeyType_Image)) {
                if (nodeM->frame() == dataFrame || nodeM->frame() - dataFrame <= closest - dataFrame) {
                    closest = index;
                }
                index++;
            }
            ffd["cellSize"] =
                dynamic_cast<const core::ImageKey*>(node->timeLine()->timeKey(core::TimeKeyType_Image, closest))
                ?
                dynamic_cast<const core::ImageKey*>(node->timeLine()->timeKey(core::TimeKeyType_Image, closest))
                      ->data()
                      .gridMesh()
                      .cellSize() : 0;
        } else {
            ffd["cellSize"] = node->timeLine()->current().areaImageKey()->data().gridMesh().cellSize();
        }
        return ffd;
    }
    case core::TimeKeyType_Image: {
        auto data = dynamic_cast<const core::ImageKey*>(timeKey)->data();
        /* Data for ImageKey: Identifier(QString), BlendMode(QString), Offset(vec2d), GridMesh[Size(qsize),
        OriginOffset(vec2d), CellPx(int), IndexCount(int), Indices(uint32 * indexcount),
        VertexCount(int), Positions(vec3d), Offsets(vec3d), TexCoords(vec2d), Normals (vec3d),
        ConnectSize(size_t), XCMemSize(uint64), XCMemData (char*)]
        ---
        Regarding the correct identification of image resources, it is entirely possible for this method as currently
        implemented to not get the correct image because there is more than one resource with the same identifier.
        If this happens to you all I can say is git gud lmao.
        */
        QJsonObject img;
        addStandardToObj(&img, data, keyType, timeKey);
        img["Image"] = dynamic_cast<const core::ImageKey*>(timeKey)->serializeToJson();
        // qDebug() << QJsonDocument(img).toJson(QJsonDocument::Indented).toStdString().c_str();
        return img;
    }
    case core::TimeKeyType_HSV: {
        auto data = dynamic_cast<const core::HSVKey*>(timeKey)->data();
        // Data types: HSV (List<int>) //
        QJsonObject hsv;
        addStandardToObj(&hsv, data, keyType, timeKey);
        hsv["Hue"] = data.hsv()[0];
        hsv["Saturation"] = data.hsv()[1];
        hsv["Value"] = data.hsv()[2];
        hsv["Absolute"] = data.hsv()[3];
        return hsv;
    }
    case core::TimeKeyType_Blur: {
        auto data = dynamic_cast<const core::BlurKey*>(timeKey)->data();
        // Data types: Amount, BlurX, BlurY, Angle (floats) //
        QJsonObject blur;
        addStandardToObj(&blur, data, keyType, timeKey);
        blur["Amount"] = data.amount();
        blur["Directional"] = data.isDirectional();
        blur["BlurX"] = data.blurX();
        blur["BlurY"] = data.blurY();
        blur["Angle"] = data.angleDeg();
        return blur;
    }
    case core::TimeKeyType_Wave: {
        auto data = dynamic_cast<const core::WaveKey*>(timeKey)->data();
        // Data types: Strength, Wavelength, Direction, Phase, Mix (floats, the
        // five graph channels) and the step-held shape (Speed, Crest, the dual
        // wave's angle and scale, the deform mode's name).
        QJsonObject wave;
        addStandardToObj(&wave, data, keyType, timeKey);
        wave["Strength"] = data.strength();
        wave["Wavelength"] = data.wavelength();
        wave["Direction"] = data.angleDeg();
        wave["Phase"] = data.phase();
        wave["Mix"] = data.mix();
        wave["Speed"] = data.speed();
        wave["Crest"] = data.crest();
        wave["DualAngle"] = data.dualAngleDeg();
        wave["DualScale"] = data.dualScale();
        wave["Deform"] = data.deform() == core::WaveKey::Deform_Sway
            ? QStringLiteral("Sway") : QStringLiteral("Ripple");
        return wave;
    }
    default: {
        return {};
    }
    }
}

void TimeLineEditorWidget::onCopyKeyTriggered(bool) {
    mCopyTargets = mEditor ? targetsForSelection(mTargets, mEditor->selection()) : mTargets;
    QSettings settings;
    auto cbCopy = settings.value("generalsettings/keys/autocb");
    bool copyToCb = !cbCopy.isValid() || cbCopy.toBool();
    if (copyToCb) {
        onCopyCBTriggered(true);
    }
}

void TimeLineEditorWidget::onCopyCBTriggered(bool) {
    if (!mCopyTargets.hasAnyTargets()) {
        mCopyTargets = mTargets;
    }
    QJsonObject targets;
    QJsonArray keys;
    targets["TargetsSize"] = mCopyTargets.targets().size();
    qDebug() << mCopyTargets.targets().size();
    for (auto cTarget : mCopyTargets.targets()) {
        core::TimeKey* timeKey = cTarget.pos.key();
        if (!timeKey)
            continue;
        core::TimeKeyType keyType = timeKey->type();
        keys.append(getKeyTypeSerialized(keyType, timeKey, cTarget.node));
    }
    targets["Keys"] = keys;
    // There be IO dragons here
    // qDebug() << QJsonDocument(targets).toJson(QJsonDocument::Indented).toStdString().c_str();
    QClipboard* cb = QGuiApplication::clipboard();
    cb->setText(QJsonDocument(targets).toJson(QJsonDocument::Indented).toStdString().c_str());
}

void TimeLineEditorWidget::onPasteKeyTriggered(bool) {
    mOnPasting = true;
    if (!mEditor->pasteCopiedKeys(mCopyTargets, mPastePos)) {
        QMessageBox::warning(nullptr, tr("Operation Error"), tr("Failed to paste keys. "
        "\n(If you want to paste across objects copy the key to the clipboard and right-click and paste to the node instead)"));
    }
    mOnPasting = false;
}

// The header's quick-easing buttons, in the timeline's own currency: aNext maps
// EVERY selected component's live param to the value to write (a TYPE writes its
// type; the Auto preset writes the segments around each key - see below). Pose
// and FFD answer their one shared param at component 0, so they take the preset
// like any other key - their easing shapes the interval playback plays. Image is
// offered too: it stores a param (the chip reads it), and an edit to a step-held
// channel simply does not change a played sample.
bool TimeLineEditorWidget::applyEasingToSelection(const util::Easing::Type aType) {
    if (!mEditor)
        return false;
    // A TYPE shortcut, like the picker's tile (setType): each component keeps its
    // range, weight and stored bezier and takes the new type. Writing a whole
    // default Param here would reset an edited range/blend on the way past.
    return mEditor->assignSelectionEasing([aType](const util::Easing::Param& aCurrent) {
        util::Easing::Param value = aCurrent;
        value.type = aType;
        return value;
    });
}

bool TimeLineEditorWidget::applyAutoHandlesToSelection() {
    // The Auto PRESET makes the segment AFTER each selected key automatic (see
    // ctrl::TimeLineEditor::assignSelectionAutoHandles): that key's type and
    // outgoing side plus the next key's incoming side, in ONE undo step. The
    // previous key's param and the key's own incoming side are NOT written - they
    // shape the segment arriving at the key. A LAST key (or the only key) has no
    // segment leaving it YET, so its OWN pair is written instead - type Custom
    // and both sides automatic. The preset is the CLAMPED pair
    // (kAutoPresetHandleMode), never the retired Auto type; unclamped Auto is a
    // deliberate per-side pick in the picker's mode combo.
    if (!mEditor)
        return false;
    return mEditor->assignSelectionAutoHandles();
}

//-------------------------------------------------------------------------------------------------
bool TimeLineEditorWidget::beginEasingEdit(const QString& aName) {
    // ONE session at a time: a new gesture owns a new command.
    endEasingEdit();
    if (!mEditor || !mProject)
        return false;

    mEasingEditName = aName;
    mEasingEditOpen = true;

    // THE SELECTION -> PARAM MAP, the same rule assignSelectionEasing applies: a
    // whole-key entry (every timeline gesture) names every component the type
    // stores, a marker entry (the graph's selection) only its component, Pose/FFD
    // answer their one shared param and Bone/Mesh none. The key is re-resolved from
    // the document, so a stale selection entry contributes nothing.
    for (const ctrl::time::Selection::Item& item : mEditor->selection().items()) {
        core::TimeLineEvent::Target target = item.target;
        core::TimeKeyPos pos = target.pos;
        core::TimeKey* key = pos.key();
        if (!key || !target.node)
            continue;
        const int stored = core::easingStorageCount(pos.type());
        const bool wholeKey = (item.component == ctrl::time::Selection::kKeyLevel);
        if (!wholeKey && (item.component < 0 || stored <= item.component))
            continue;
        for (int component = 0; component < stored; ++component) {
            if (!wholeKey && component != item.component)
                continue;
            util::Easing::Param* param = core::keyEasingOf(*key, pos.type(), component);
            if (!param)
                continue;
            EasingEditTarget edit;
            edit.param = param;
            edit.node = target.node;
            edit.type = pos.type();
            edit.frame = core::Frame(key->frame());
            mEasingEdit.append(edit);
        }
    }
    return !mEasingEdit.isEmpty();
}

bool TimeLineEditorWidget::stepEasingEdit(const util::Easing::Param& aDraft) {
    if (!mEasingEditOpen || !mEditor || !mProject)
        return false;

    // The stack no longer tops the command this session pushed (an undo/redo, or
    // another gesture's edit took the modifiable link): rewriting it would corrupt
    // the history, so close the session and let this step open a fresh edit - the
    // app's "later commit opens a new edit" rule for a drag that outlived its
    // command.
    if (!mEasingEdit.isEmpty() && mEasingEdit.first().command && !mWrite.continues()) {
        const QString name = mEasingEditName;
        endEasingEdit();
        beginEasingEdit(name);
    }

    const bool firstStep = mEasingEdit.isEmpty() || !mEasingEdit.first().command;

    if (firstStep) {
        bool changes = false;
        for (const EasingEditTarget& edit : mEasingEdit) {
            if (edit.param && edit.node && *edit.param != aDraft) {
                changes = true;
                break;
            }
        }
        if (!changes)
            return false; // a press that has not moved: record nothing

        // ONE notification per KEY: the components of one key are one change to
        // its timeline. These targets are established for the WHOLE session by
        // RewriteAnnounce.begin() - later steps announce them and cannot change
        // them.
        core::TimeLineEvent targets;
        targets.setType(core::TimeLineEvent::Type_ChangeKeyValue);
        for (const EasingEditTarget& edit : mEasingEdit) {
            if (edit.node)
                targets.pushTarget(*edit.node, edit.type, edit.frame.get());
        }

        // Every param gets a command: the ones this step leaves alone start at the
        // value they already hold, so a later step can still rewrite them into the
        // SAME command. A command reads its undo value when it first runs, so the
        // pre-gesture state is what undo restores. RewriteAnnounce opens the one
        // macro, grabs the one notifier and announces this first step.
        mWrite.begin(*mProject, mProject->commandStack(), mEasingEditName, targets, [this, &aDraft] {
            cmnd::Base* marker = nullptr;
            for (EasingEditTarget& edit : mEasingEdit) {
                if (!edit.param || !edit.node)
                    continue;
                const util::Easing::Param next = (*edit.param == aDraft) ? *edit.param : aDraft;
                auto* command = new cmnd::ModifiableAssign<util::Easing::Param>(edit.param, next);
                mProject->commandStack().push(command);
                edit.command = command;
                marker = command;
            }
            return marker;
        });
        return true; // the macro's own notifier announced this first step
    }

    // REWRITE the commands with this step's value and announce the keys they
    // changed - RewriteAnnounce owns both halves, in that order and exactly once.
    return mWrite.step([this, &aDraft] {
        bool changes = false;
        for (EasingEditTarget& edit : mEasingEdit) {
            if (!edit.command || !edit.param || *edit.param == aDraft)
                continue;
            edit.command->modifyValue(aDraft);
            changes = true;
        }
        return changes;
    });
}

void TimeLineEditorWidget::endEasingEdit() {
    // The commands stay on the stack holding their last value; this drops only this
    // session's handle to them, so the next gesture opens a command of its own.
    mEasingEdit.clear();
    mEasingEditName.clear();
    mEasingEditOpen = false;
    mWrite.end();
}

bool targetsAreSeparated(QList<core::TimeLineEvent::Target> &targets){
    int frame = 0;
    bool hasFrame = false;
    bool isDifferent = false;
    for(auto target: targets){
        if(!hasFrame){ frame = target.pos.key()->frame(); hasFrame = true; }
        if(target.pos.key()->frame() != frame){ isDifferent = true; }
    }
    return isDifferent;
}
QList<QList<core::TimeLineEvent::Target>*> sortTargets(QList<core::TimeLineEvent::Target>* targets){
    QList<QMap<int, core::TimeKey*>> targetMap;
    QList<QList<core::TimeLineEvent::Target>*> targetLists;
    for(auto target: *targets){
        if(targetMap.empty()){
            targetMap.emplaceBack(target.pos.line()->map(target.pos.key()->type()));
            targetLists.emplace_back(new QList{target});
        }
        else{
            bool containsKey = false;
            // Peak programming right here
            for(const auto& map: targetMap){
                for(const auto& key: map){
                    if(key == target.pos.key()){ containsKey = true; }
                }
            }
            if(containsKey) { targetLists.back()->emplace_back(target); }
            else{
                targetMap.emplaceBack(target.pos.line()->map(target.pos.key()->type()));
                targetLists.emplaceBack(new QList{target});
            }
        }
    }
    for(auto target: targetLists){
        std::sort(target->begin(), target->end(), ctrl::TimeLineUtil::MoveFrameOfKey::greaterThan);
    }
    XC_ASSERT(targetMap.size() == targetLists.size());
    return targetLists;
}

static QString keyToString(const core::TimeKeyType key) {
    switch (key) {
    case core::TimeKeyType_Bone:
        return "Bone";
    case core::TimeKeyType_Mesh:
        return "Mesh";
    case core::TimeKeyType_Image:
        return "Image";
    case core::TimeKeyType_HSV:
        return "HSV";
    case core::TimeKeyType_Blur:
        return "Blur";
    case core::TimeKeyType_Wave:
        return "Wave";
    case core::TimeKeyType_FFD:
        return "FFD";
    case core::TimeKeyType_Depth:
        return "Depth";
    case core::TimeKeyType_Move:
        return "Move";
    case core::TimeKeyType_Opacity:
        return "Opacity";
    case core::TimeKeyType_Pose:
        return "Pose";
    case core::TimeKeyType_Rotate:
        return "Rotate";
    case core::TimeKeyType_Scale:
        return "Scale";
    default:
        return "Unknown";
    }
}

void TimeLineEditorWidget::onSelectSpacingTriggered() {
    if(!targetsAreSeparated(mTargets.targets())){
        QMessageBox msg;
        msg.setText(tr("A minimum of two keys in different frames are needed."));
        msg.setWindowTitle(tr("Not enough targets"));
        msg.setIcon(QMessageBox::Information);
        msg.setStandardButtons(QMessageBox::Ok);
        msg.exec();
        return;
    }
    auto *diag = new QDialog;
    diag->setObjectName("Dialog");
    diag->resize(400, 100);
    QSizePolicy sizePolicy(QSizePolicy::Policy::MinimumExpanding, QSizePolicy::Policy::MinimumExpanding);
    diag->setSizePolicy(sizePolicy);
    auto verticalLayout = new QVBoxLayout(diag);
    verticalLayout->setObjectName("verticalLayout");
    auto label = new QLabel(diag);
    label->setObjectName("label");
    QSizePolicy sizePolicy1(QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Maximum);
    label->setSizePolicy(sizePolicy1);
    verticalLayout->addWidget(label);

    auto frameSpacing = new QSpinBox(diag);
    // Spacing selects every Nth frame within the project's frame range: the
    // span, not the (possibly negative) end frame, bounds it.
    frameSpacing->setMaximum(qMax(1, mProject->attribute().frameRange().diff()));
    frameSpacing->setMinimum(1);
    frameSpacing->setValue(1);
    frameSpacing->setObjectName("frameSpacing");
    verticalLayout->addWidget(frameSpacing);

    auto *buttonBox = new QDialogButtonBox(diag);
    buttonBox->setObjectName("buttonBox");
    buttonBox->setOrientation(Qt::Horizontal);
    buttonBox->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonBox->setCenterButtons(true);
    verticalLayout->addWidget(buttonBox);

    diag->setWindowTitle(tr("Key spacing"));
    label->setText(
        R"(<html><head/><body><p align="center">)" + tr("Number of frames the selected keys should be spaced by:") +
        "</p></body></html>"
    );
    bool connected= connect(buttonBox, &QDialogButtonBox::accepted, this, [diag](){ diag->accept();});
    connected = connected && connect(buttonBox, &QDialogButtonBox::rejected, this, [diag](){ diag->reject();});
    if(!connected){
        diag->deleteLater();
        return;
    }
    QMetaObject::connectSlotsByName(diag);
    frameSpacing->setFocus();
    diag->exec();

    if(diag->result() == QDialog::Accepted){
        // We sort in reverse as to avoid most conflicts while moving keys
        auto targets = sortTargets(&mTargets.targets());
        int spacing = frameSpacing->value();
        // We ignore the first keyframe for each target, which are at the back in this case
        QList<core::TimeKey*> ignoredTargets;
        for(auto target: targets) { ignoredTargets.emplaceBack(target->back().pos.key()); }
        // We move the keys and check if the move was successful. Destinations
        // are unconstrained: the frame range does not gate the keying system,
        // so keys may be spaced past the last frame.
        QList<QPair<core::TimeLineEvent::Target, int>> conflicts;
        for(auto target: targets) {
            int tSize = static_cast<int>(target->size()) - 1;
            int initialFrame = target->back().pos.key()->frame();
            for(auto key : *target) {
                int frameAccumulation = spacing * tSize;
                if (!ignoredTargets.contains(key.pos.key())) {
                    int frame = key.pos.key()->frame();
                    int dest = initialFrame + frameAccumulation;
                    const core::TimeKeyType keyType = key.pos.type();
                    if(!key.pos.line()->move(keyType, frame, dest)) {
                        conflicts.emplace_back(key, dest);
                    }
                    else {
                        // Sigh...
                        void(true);
                    }
                    tSize -= 1;
                }
            }
        }
        if (!conflicts.empty()) {
            QMessageBox msg;
            QStringList errorLog;
            for (const auto& [key, frame] : conflicts) {
                errorLog.append(
                    tr("Key conflict detected for key type ") + keyToString(key.pos.type()) +
                    tr(" in node ") + key.node->name() + tr(" at frame ") + QString::number(frame));
            }
            msg.setWindowTitle(tr("Move error"));
            msg.setText(tr("Unable to move ") + QString::number(conflicts.size()) + tr(" key(s), due to the following reasons: "));
            msg.setDetailedText(errorLog.join("\n"));
            msg.exec();
        }
    }
    diag->deleteLater();
}

void TimeLineEditorWidget::onDeleteKeyTriggered(bool) {
    // THE MENU'S DELETE ACTS ON THE SET, BUT ONLY ON THE PART THE DOCUMENT STILL
    // HOLDS. A context menu names its keys when it OPENS, and a key can leave the
    // document while the menu is up (an undo from another view, a paste that
    // replaced one, the graph's own edit). ctrl::TimeLineEditor::deleteCheckedKeys
    // builds each remover with aOptional=true, which SKIPS a frame that holds no
    // key: a stale target therefore made the command do nothing at all while the
    // menu still closed as if it had acted, and a set that was entirely stale
    // pushed an EMPTY macro and cleared the selection.
    //
    // So the live targets are collected here, and an empty answer writes NOTHING -
    // not even the empty history entry - which is the same "collect before you
    // push" rule ctrl::GraphEditor::removeSelectedKey follows.
    if (!mEditor)
        return;

    core::TimeLineEvent live = targetsForSelection(mTargets, mEditor->selection());
    if (live.targets().isEmpty())
        return;

    mEditor->deleteCheckedKeys(live);
}

QColor TimeLineEditorWidget::headerContentColor() const { return mTimelineTheme.headerContentColor(); }

void TimeLineEditorWidget::setHeaderContentColor(const QColor& headerContentColor) {
    mTimelineTheme.setHeaderContentColor(headerContentColor);
}

QColor TimeLineEditorWidget::headerBackgroundColor() const { return mTimelineTheme.headerBackgroundColor(); }

void TimeLineEditorWidget::setHeaderBackgroundColor(const QColor& headerBackgroundColor) {
    mTimelineTheme.setHeaderBackgroundColor(headerBackgroundColor);
}

QColor TimeLineEditorWidget::rulerLineColor() const { return mTimelineTheme.rulerLineColor(); }

void TimeLineEditorWidget::setRulerLineColor(const QColor& rulerLineColor) {
    mTimelineTheme.setRulerLineColor(rulerLineColor);
}


QColor TimeLineEditorWidget::trackColor() const { return mTimelineTheme.trackColor(); }

void TimeLineEditorWidget::setTrackColor(const QColor& trackColor) { mTimelineTheme.setTrackColor(trackColor); }

QColor TimeLineEditorWidget::trackTextColor() const { return mTimelineTheme.trackTextColor(); }

void TimeLineEditorWidget::setTrackTextColor(const QColor& trackTextColor) {
    mTimelineTheme.setTrackTextColor(trackTextColor);
}

QColor TimeLineEditorWidget::trackEdgeColor() const { return mTimelineTheme.trackEdgeColor(); }

void TimeLineEditorWidget::setTrackEdgeColor(const QColor& trackEdgeColor) {
    mTimelineTheme.setTrackEdgeColor(trackEdgeColor);
}

QColor TimeLineEditorWidget::trackSelectColor() const { return mTimelineTheme.trackSelectColor(); }

void TimeLineEditorWidget::setTrackSelectColor(const QColor& trackSelectColor) {
    mTimelineTheme.setTrackSelectColor(trackSelectColor);
}

QColor TimeLineEditorWidget::trackSeperatorColor() const { return mTimelineTheme.trackSeperatorColor(); }

void TimeLineEditorWidget::setTrackSeperatorColor(const QColor& trackSeperatorColor) {
    mTimelineTheme.setTrackSeperatorColor(trackSeperatorColor);
}

//-------------------------------------------------------------------------------------------------
bool TimeLineEditorWidget::openKeyMenu(const core::TimeLineEvent::Target& aPressedKey, const QPoint& aGlobalPos) {
    // THE KEY MENU'S ENTRY POINT for a caller whose pointer is in its own space:
    // the animation graph panel hit-tests the key itself (ctrl::GraphEditor::
    // contextTargetAt, in the panel's pixels) and hands the triple over - it cannot
    // name a pixel in THIS widget's lane geometry, so
    // ctrl::TimeLineEditor::selectKeysAt's hit test is not reachable from it. What that hit test's rule decides is therefore decided
    // here, on the one shared selection, by key identity:
    ctrl::time::Selection& selection = mEditor->selection();

    if (!aPressedKey.node || !aPressedKey.pos.isExist())
        return false; // not a key the document holds (a stale key from the panel's last frame)

    // The pressed key is ALREADY selected: the menu is the SELECTION's, so it acts on
    // every selected key - that is the multi-key case, and nothing is lent or dropped.
    // Any other key replaces the selection for the menu's lifetime (the temporary loan
    // the timeline's own route makes: the pressed key shows as selected while the menu
    // is up, every action consumes that set, and the previous selection is not
    // restored).
    core::TimeKey* lentKey = nullptr;
    if (!selection.contains(aPressedKey)) {
        core::TimeLineEvent single;
        single.pushTarget(*aPressedKey.node, aPressedKey.pos);
        selection.set(single);
        lentKey = selection.items().isEmpty() ? nullptr : selection.items().front().key;
    }

    // WHAT THE ITEMS ACT ON, read AFTER the rule above: the whole selection, one
    // target per key - the set every item in buildKeyMenu works from. It is non-empty
    // by construction (the pressed key is a key the document holds, so the loan put it
    // in, and the other branch left the selection standing), which is what makes this
    // the key menu and never buildKeyMenu's paste-only shape.
    mTargets = core::TimeLineEvent();
    mEditor->retrieveSelectionTargets(mTargets);

    // THE PASTE ANCHOR: the pressed key's frame, in the timeline's own pixel currency -
    // the same composition the keyboard paste uses for the playhead
    // (TimeLineEditorWidget's Ctrl+V listener), so a paste from this menu lands where
    // the key the menu was opened on is.
    mPastePos = QPoint(
        mEditor->timeScale().pixelWidth(aPressedKey.pos.index()) + ctrl::TimeLineEditor::kTimeLineMargin, 0
    );

    QMenu menu(this);
    buildKeyMenu(menu);
    // exec(), not popup(): the whole point of this route is that the loan above is
    // spent by the menu and dropped when it closes - "after the menu" has to be a
    // line in this function. The panel's right-click is inside a press handler; a
    // nested event loop there is what the timeline's own context-menu handler runs
    // too (QMenu owns the interaction either way).
    menu.exec(aGlobalPos);

    // ...AND THE LOAN IS DROPPED, unless an action replaced the selection with
    // something else while the menu was up: paste selects the keys it created
    // (ctrl::TimeLineEditor::selectCreatedKeys) and delete takes its keys out of the
    // set, so only a selection that still names nothing but the lent key is the loan
    // left standing (ctrl::TimeLineEditor::endTemporarySelection is the same drop for
    // the timeline's own route, where the editor holds the flag this widget cannot set).
    if (lentKey) {
        const ctrl::time::Selection& after = mEditor->selection();
        bool stillLent = !after.empty();
        for (const ctrl::time::Selection::Item& item : after.items()) {
            if (item.key != lentKey) {
                stillLent = false;
                break;
            }
        }
        if (stillLent)
            mEditor->selection().clear();
    }

    this->update(); // the selection changed under this widget's own ink
    return true;
}

//-------------------------------------------------------------------------------------------------
void TimeLineEditorWidget::pushEasingChipState() {
    if (!mEasingHost)
        return;

    QVector<util::Easing::Param> values;
    util::Easing::AutoContext autoContext;
    QString scopeNote;
    if (mEditor) {
        const ctrl::time::Selection& selection = mEditor->selection();
        bool haveShown = false;
        for (int i = 0; i < selection.items().size(); ++i) {
            const ctrl::time::Selection::Item& item = selection.items()[i];
            // The key is resolved FROM THE DOCUMENT, never taken off the stored
            // pointer: a selection entry outlives the key it names (an undo, a
            // deletion, a project reload), and ctrl::time::Selection documents that
            // its stored key is only ever compared against map entries, never
            // dereferenced (ctrl::time/time_Selection.h). This runs on a PAINT path,
            // so a dereference here would be a use-after-free on a document change
            // this widget had not repainted yet. The (line, type, frame) triple is
            // the identity every other reader resolves through, this widget's own
            // gestures included.
            core::TimeLineEvent::Target target = item.target; // non-const: pos.line() writable
            core::TimeLine* line = target.pos.line();
            if (!line)
                continue;

            const QVector<int> named =
                selection.componentsOf(target, core::easingStorageCount(target.pos.type()));
            const int component = named.isEmpty() ? 0 : named.first();

            core::TimeKey* key = nullptr;
            if (core::TimeLine::usesChannelTracks(target.pos.type())) {
                key = line->channelKey(target.pos.type(), component, target.pos.index());
            } else {
                key = line->timeKey(target.pos.type(), target.pos.index());
            }
            if (!key)
                continue; // a stale entry names a key the document no longer holds
            // ONE EASING PER KEY for the CHIP'S DISPLAY. Each marker now carries
            // its own curve, so the value read is the component of the FIRST
            // entry naming this key; the WRITE path (ctrl::TimeLineEditor::assignSelectionEasing) is
            // per component and does not collapse them. Keeping one value per
            // key is what makes the chip's "N key(s)" count true.
            // THE COMPONENT THIS KEY SHOWS: the first one the selection names for
            // it (processor order - a single marker answers itself, a whole-key
            // entry answers 0). The same question openEasingPicker asks, so the chip
            // and the picker cannot open on two different curves.
            bool alreadyNamed = false;
            for (int j = 0; j < i; ++j) {
                const core::TimeLineEvent::Target& earlier = selection.items()[j].target;
                if (earlier.node == target.node && earlier.pos.type() == target.pos.type()
                    && earlier.pos.index() == target.pos.index()) {
                    alreadyNamed = true;
                    break;
                }
            }
            if (alreadyNamed)
                continue;
            const util::Easing::Param* value =
                ctrl::graph::Curve::easingOf(*key, target.pos.type(), component);
            if (!value)
                continue;
            values.push_back(*value);
            // The chip SHOWS the first key's easing (see prop::EasingChip), so that
            // is the one whose neighbours shape its glyph - the key's own Auto
            // context, which is a document fact (prop::autoContextOf).
            if (!haveShown) {
                autoContext = prop::autoContextOf(target, *key, component);
                // ...AND IF THAT KEY'S SELECTION SPANS SEVERAL COMPONENTS, SAY SO:
                // the chip paints one component's curve, while the picker's commit
                // writes every component the selection names. A whole-key entry
                // spans every component its type stores; a graph-panel selection
                // can name a subset (X but not Y). The tooltip names them, in the
                // same channel vocabulary the panel uses.
                if (named.size() > 1) {
                    QStringList names;
                    for (const int namedComponent : named) {
                        names << ctrl::graph::Channel(target.pos.type(), namedComponent).componentName();
                    }
                    scopeNote = names.join(QStringLiteral(", "));
                }
                haveShown = true;
            }
        }
    }
    mEasingHost->setSelection(values, autoContext, scopeNote);
}

//-------------------------------------------------------------------------------------------------
void TimeLineEditorWidget::setEasingItemHost(prop::EasingItem* aHost) {
    if (mEasingHost) {
        // The host is going away (or being replaced): its callbacks capture this
        // widget, so they must not outlive it. A session it left open also goes.
        mEasingHost->onEasingApplied = nullptr;
        mEasingHost->onEasingLiveBegin = nullptr;
        mEasingHost->onEasingLiveStep = nullptr;
        mEasingHost->onEasingLiveEnd = nullptr;
        mEasingHost->onPickerClosed = nullptr;
        endEasingEdit();
    }
    mEasingHost = aHost;
    if (!mEasingHost) {
        // No chip to keep in step. Nothing to tear down either: the selection's own
        // notification is what pushes it, and pushEasingChipState is a no-op while
        // no host is attached (so a selection made with no chip costs one test).
        return;
    }

    // The one commit path: the control hands every edit the picker commits here,
    // and this widget writes the keys the open named. The AUTO PRESET is not a
    // per-key type edit: it makes the segment AFTER each selected key automatic
    // (that key's type and outgoing side plus the next key's incoming side), so it
    // takes the segment route. The predicate is the same one the picker's tile
    // uses to show itself checked, so a manually-built mode pair that equals the
    // preset behaves like the preset.
    mEasingHost->onEasingApplied = [this](util::Easing::Param aEasing) {
        if (!mEditor)
            return;
        const bool autoPreset = aEasing.type == util::Easing::Type_Custom
            && aEasing.handleIn == util::Easing::kAutoPresetHandleMode
            && aEasing.handleOut == util::Easing::kAutoPresetHandleMode;
        if (autoPreset) {
            mEditor->assignSelectionAutoHandles();
            return;
        }
        mEditor->assignSelectionEasing([aEasing](const util::Easing::Param&) { return aEasing; });
    };
    // A CONTINUOUS gesture the picker streams (bezier handle drag, typed spin):
    // one rewriteable command over the selection, rewritten per step, closed when
    // the gesture ends - so the document and the graph follow the pointer while the
    // whole gesture is ONE undo entry. The popup's close ends it too (its hide
    // closes the session before the popup goes away).
    mEasingHost->onEasingLiveBegin = [this] {
        beginEasingEdit(CmndName::tr("Change key(s) easing"));
    };
    mEasingHost->onEasingLiveStep = [this](util::Easing::Param aStep) {
        stepEasingEdit(aStep);
    };
    mEasingHost->onEasingLiveEnd = [this] {
        endEasingEdit();
        // A gesture just ended, so a re-read the popup REFUSED while it was dragging
        // (a graph edit that landed mid-drag) is taken now: the popup shows what the
        // document holds, which is the drag's own value unless something else (an
        // undo, another view) won in the meantime.
        refreshOpenPicker();
    };
    // The popover is gone, so a right-click's temporary selection ends with it:
    // those keys were lent for the open, and the open is over. Clearing the
    // selection is itself a change, so it announces the chip's new state by the
    // same route every other change takes.
    mEasingHost->onPickerClosed = [this]() {
        endEasingEdit(); // a gesture the close interrupted owns no open session
        mEditor->endTemporarySelection();
        this->update(); // repaints, which re-reads the selection for the chip
    };
    pushEasingChipState();
}

//-------------------------------------------------------------------------------------------------
bool TimeLineEditorWidget::openEasingPicker(const core::TimeLineEvent& aTargets, const QPoint& aGlobalAnchor) {
    if (aTargets.targets().isEmpty() || !mEasingHost)
        return false; // nothing to change the easing of (or no header hosts a picker)

    // What the picker opens on, read by the SAME function that keeps it in step while
    // it is open (pickerShownState): the FIRST key the open names, with the Auto
    // context that key's own neighbours give it (prop::autoContextOf - an Auto key
    // stores no control points, so without them the tile would draw a shape no key
    // plays). A key of a type that carries no easing is skipped; a target list of only
    // those opens nothing, and nothing is ever written.
    //
    // THE COMPONENT is the one the shared selection NAMES for that key
    // (Selection::componentsOf), not always the key's first: a graph-panel press
    // selects a marker (Move X), and the picker must open on that marker's curve.
    // A whole-key entry names every component, and then componentsOf answers them
    // ascending, so the first is component 0 as before; where the selection names
    // SEVERAL, pushEasingChipState puts them in the chip's tooltip (see there), so
    // the open never silently claims the shown curve is the only one edited.
    mTargets = aTargets;

    util::Easing::Param shown;
    util::Easing::AutoContext autoContext;
    if (!pickerShownState(shown, autoContext))
        return false;

    // The chip names the same keys the popover edits, so the two must agree the
    // moment it opens - the anchor may be a right-click in the animation graph
    // panel, which changed the shared selection and whose repaint of this surface
    // may not have happened yet.
    pushEasingChipState();
    mEasingHost->openPicker(aGlobalAnchor, this, shown, autoContext);
    return true;
}

//-------------------------------------------------------------------------------------------------
bool TimeLineEditorWidget::pickerShownState(
    util::Easing::Param& aShown, util::Easing::AutoContext& aAutoContext
) const {
    if (!mEditor)
        return false;

    // THE COMPONENT the shared selection NAMES for a key (see openEasingPicker).
    const auto componentOf = [this](const core::TimeLineEvent::Target& aTarget,
                                    core::TimeKeyType aType) -> int {
        const QVector<int> named =
            mEditor->selection().componentsOf(aTarget, core::easingStorageCount(aType));
        return named.isEmpty() ? 0 : named.first();
    };

    // TWO PASSES, one rule. A key the selection STILL NAMES is preferred, so a
    // selection change among the keys the open edits moves what the popup shows; when
    // none of them is selected any more (the open's keys are not what the user picked,
    // or a deletion dropped them from the set) the open's own order still decides. An
    // entry that no longer resolves is skipped by both passes - the picker never shows
    // a curve from a key the document has dropped.
    for (int pass = 0; pass < 2; ++pass) {
        const bool preferSelected = (pass == 0);
        for (const core::TimeLineEvent::Target& target : mTargets.targets()) {
            // By value, for the same reason as in assignSelectionEasing: the target list
            // is const here and TimeKeyPos::key() is not a const member.
            core::TimeKeyPos pos = target.pos;
            core::TimeKey* key = pos.key();
            if (!key)
                continue;
            if (preferSelected && !mEditor->selection().contains(target))
                continue;
            const int component = componentOf(target, pos.type());
            const util::Easing::Param* easing =
                ctrl::graph::Curve::easingOf(*key, pos.type(), component);
            if (!easing)
                continue; // a type that carries no easing
            aShown = *easing;
            aAutoContext = prop::autoContextOf(target, *key, component);
            return true;
        }
    }
    return false;
}

//-------------------------------------------------------------------------------------------------
void TimeLineEditorWidget::refreshOpenPicker() {
    if (!mEasingHost || !mEasingHost->isPickerOpen())
        return; // no popup on screen: nothing to keep in step

    util::Easing::Param shown;
    util::Easing::AutoContext autoContext;
    if (!pickerShownState(shown, autoContext))
        return; // the open's keys are gone: leave the popup as it is

    // The control forwards it; the PICKER decides whether this is a re-read it may take
    // (a gesture in progress in the popup wins until it ends).
    mEasingHost->refreshShown(shown, autoContext);
}

//-------------------------------------------------------------------------------------------------
bool TimeLineEditorWidget::openBackgroundMenu(
    const QPoint& aGlobalPos, const QString& aExtraText, const std::function<void()>& aExtraAction
) {
    if (!mEditor)
        return false;

    // THE PASTE ANCHOR: the pointer's frame, mapped through this surface's camera the
    // way the timeline's own context menu maps it. Only the ANCHOR is taken from the
    // point - the items are the SHARED SELECTION's, and no lane hit test runs here (a
    // caller's pointer is in another view's space; see the header).
    const QPoint local = this->mapFromGlobal(aGlobalPos);
    mTargets = core::TimeLineEvent();
    mEditor->retrieveSelectionTargets(mTargets);
    mPastePos = mCamera ? mCamera->toWorldPos(QPointF(local)).toPoint() : local;

    QMenu menu(this);
    buildKeyMenu(menu);
    if (!aExtraText.isEmpty() && aExtraAction) {
        menu.addSeparator();
        QAction* extra = menu.addAction(aExtraText);
        extra->connect(extra, &QAction::triggered, aExtraAction);
    }
    menu.exec(aGlobalPos);

    // The menu blocked, so the selection its items acted on may have changed under this
    // widget's ink by now.
    this->update();
    return true;
}

} // namespace gui
