#include "util/CollDetect.h"
#include "ctrl/Driver.h"
#include "core/VirtualTexture.h"
#include "ctrl/MaskEditor.h"

namespace {

struct ScopeCounter {
    int& count;
    explicit ScopeCounter(int& aCount): count(aCount) { ++count; }
    ~ScopeCounter() { --count; }
};

} // namespace

namespace ctrl {

Driver::Driver(core::Project& aProject, DriverResources& aResources, GraphicStyle& aGraphicStyle, UILogger& aUILogger):
    mProject(aProject),
    mResources(aResources),
    mGraphicStyle(aGraphicStyle),
    mUILogger(aUILogger),
    mToolType(ToolType_TERM),
    mBlender(aProject.objectTree()),
    mEditor(),
    mGraphEditor(new GraphEditor(aProject, aUILogger)),
    mMaskEditor(nullptr),
    mBoneEditor(nullptr),
    mCurrentNode(),
    mOnUpdating(0),
    mRejectedTarget() {
    // initialize blending
    mBlender.updateCurrents(mProject.objectTree().topNode(), mProject.currentTimeInfo());
}

void Driver::setTarget(core::ObjectNode* aNode) {
    ScopeCounter counter(mOnUpdating);
    mCurrentNode = aNode;
    mRejectedTarget = false;

    if (mEditor) {
        mRejectedTarget = !mEditor->setTarget(mCurrentNode);
    }
}

void Driver::setTool(ToolType aType) {
    ScopeCounter counter(mOnUpdating);
    mToolType = aType;
    mEditor.reset();

    if (mToolType == ToolType_Cursor) {
    } else if (mToolType == ToolType_SRT) {
        mEditor.reset(new SRTEditor(mProject, mUILogger));
    } else if (mToolType == ToolType_Bone) {
        auto* editor = new BoneEditor(mProject, mGraphicStyle, mUILogger);
        mBoneEditor = editor;
        // The editor owns the selected bone; its state changes must reach the
        // panel even when the change came from a canvas click.
        editor->onWeightsStateChanged.connect([this]() { onBoneWeightsStateChanged(); });
        mEditor.reset(editor);
    } else if (mToolType == ToolType_Pose) {
        mEditor.reset(new PoseEditor(mProject, mUILogger));
    } else if (mToolType == ToolType_Mesh) {
        mEditor.reset(new MeshEditor(mProject, mUILogger));
    } else if (mToolType == ToolType_FFD) {
        mEditor.reset(new FFDEditor(mProject, mResources, mUILogger));
    } else if (mToolType == ToolType_Mask) {
        // the raster paint tool; it keeps a raw pointer for the panel's Clear, so it
        // is cleared with the editor's lifetime
        mMaskEditor = new MaskEditor(mProject);
        mEditor.reset(mMaskEditor);
    }
    if (mToolType != ToolType_Mask) {
        mMaskEditor = nullptr;
    }
    if (mToolType != ToolType_Bone) {
        mBoneEditor = nullptr;
    }

    setTarget(mCurrentNode);
}

bool Driver::updateCursor(const core::AbstractCursor& aCursor, const core::CameraInfo& aCamera) {
    ScopeCounter counter(mOnUpdating);

    // stop animation
    if (aCursor.emitsLeftPressedEvent() && mToolType != ToolType_Cursor) {
        mProject.animator().stop();
    }

    // THE VIEWPORT'S events are the TOOLS' alone. The graph panel used to arbitrate
    // here as a viewport overlay, against GraphEditor::currentPanelRect (the WHOLE
    // viewport), so a canvas press could open a graph marquee instead of running the
    // current tool. The panel lives in the dock now: gui::GraphPanelWidget owns its
    // own page's events and is the only thing that routes them to the editor (see
    // its routeCursor), so nothing of the panel's is decided here - no rect test, no
    // visibility flag, and no press the user aimed at the canvas can be swallowed by
    // it.
    if (mEditor) {
        return mEditor->updateCursor(aCamera, aCursor);
    }

    return false;
}

void Driver::updateFrame() {
    ScopeCounter counter(mOnUpdating);

    // update blending
    mBlender.updateCurrents(mProject.objectTree().topNode(), mProject.currentTimeInfo());

    if (mEditor) {
        mEditor->updateEvent(IEditor::EventType_Frame);
    }
}

void Driver::markPendingBlend(core::TimeLineEvent& aEvent) {
    if (mPendingBlend) {
        for (core::TimeLineEvent::Target& target : aEvent.targets())
            mPendingEvent.targets().append(target);
        if (mPendingEvent.type() < aEvent.type())
            mPendingEvent.setType(aEvent.type());
    } else {
        mPendingEvent = aEvent;
        mPendingBlend = true;
    }
}

void Driver::updateKey(core::TimeLineEvent& aEvent, bool aUndo) {
#if 0
    qDebug() << "type: " << aEvent.type() << (aUndo ? " undo" : " redo");
    for (const TimeLineEvent::Target& target : aEvent.targets())
    {
        qDebug() << target.pos.index() << target.subIndex;
    }
#endif
    (void)aUndo;

    // MARK DIRTY, DO NOT RE-BLEND. Re-blending here means once per mouse sample, and a drag
    // delivers dozens: the loop is then busy in the blend instead of draining the pointer, and the
    // platform collapses the motion it could not deliver (measured: 40 of 40 moves with no drag in
    // flight, 8-10 of 40 during one, handler 0.4ms). The pan had the same shape and was fixed the
    // same way - mark, and do the work once per painted frame (flushPendingBlend).
    markPendingBlend(aEvent);

    if (mOnUpdating > 0)
        return;

    if (mEditor) {
        mEditor->updateEvent(IEditor::EventType_TimeKey);
    }
}

void Driver::frameTick() {
    // THE EDITOR'S DEFERRED WORK IS DRAINED HERE, INDEPENDENT OF THE BLEND. Measured on an FFD stroke:
    // 22 drag updates reached `ffd::BrushMode` in State_Draw with a valid target and queued their
    // stamps, while `flushPendingWork` never ran (26 calls of flushPendingBlend all had pending=0 and
    // returned first) - so every stamp stayed in the queue and the brush did nothing. The frame drains
    // it unconditionally; blending is a separate duty that still only happens when something changed.
    if (mEditor)
        mEditor->flushPendingWork();
}

void Driver::updateSkinWeights(core::ObjectNode& aOwner) {
    // The owner's BoneKey caches were already rebuilt by BoneKeyUpdater (the
    // Project signal that got us here); the blend only has to re-derive the
    // per-node expans pointers and matrices from them. No editor event: this is
    // not a key edit, and a mode reset here would end the stroke that asked for
    // it. The next paint's flushPendingBlend does the work once.
    core::TimeLineEvent event;
    event.setType(core::TimeLineEvent::Type_ChangeKeyValue);
    event.pushTarget(aOwner, core::TimeKeyType_Bone, mProject.animator().currentFrame().get());
    markPendingBlend(event);
    // An undo/redo of an authored-weight command reaches the document without
    // the mode, so the panel's state has to be re-read here too.
    onBoneWeightsStateChanged();
}

bool Driver::bakeBoneWeights(bool aReplace, QString* aError) {
    return mBoneEditor && mBoneEditor->bakeWeights(aReplace, aError);
}

bool Driver::revertBoneWeights(QString* aError) {
    return mBoneEditor && mBoneEditor->revertWeights(aError);
}

bool Driver::clearBoneWeights(QString* aError) {
    return mBoneEditor && mBoneEditor->clearWeights(aError);
}

void Driver::flushPendingBlend() {
    if (!mPendingBlend)
        return;
    mPendingBlend = false;
    // ONE reset for the union of everything the frame asked for: clearCaches walks targets, so a
    // merged list is the same answer as the sum of the parts.
    mBlender.clearCaches(mPendingEvent);
    mPendingEvent = core::TimeLineEvent();
    mBlender.updateCurrents(mProject.objectTree().topNode(), mProject.currentTimeInfo());
}

void Driver::updateTree(core::ObjectTreeEvent& aEvent, bool aUndo) {
    (void)aUndo;
    ScopeCounter counter(mOnUpdating);

    // reset blending
    for (auto root : aEvent.roots()) {
        mBlender.clearCaches(root);
    }

    mBlender.updateCurrents(mProject.objectTree().topNode(), mProject.currentTimeInfo());

    if (mEditor) {
        mEditor->updateEvent(IEditor::EventType_Tree);
    }
}

void Driver::updateResource(core::ResourceEvent& aEvent, bool aUndo) {
    (void)aEvent;
    (void)aUndo;
    ScopeCounter counter(mOnUpdating);

    // reset blending
    mBlender.clearCaches(mProject.objectTree().topNode());
    mBlender.updateCurrents(mProject.objectTree().topNode(), mProject.currentTimeInfo());

    if (mEditor) {
        mEditor->updateEvent(IEditor::EventType_Resource);
    }
}

void Driver::updateProjectAttribute() {
    ScopeCounter counter(mOnUpdating);

    // reset blending
    mBlender.clearCaches(mProject.objectTree().topNode());
    mBlender.updateCurrents(mProject.objectTree().topNode(), mProject.currentTimeInfo());

    if (mEditor) {
        mEditor->updateEvent(IEditor::EventType_ProjectAttribute);
    }
}

void Driver::renderGL(const core::RenderInfo& aRenderInfo, core::ObjectNode* aGridTarget) const {
    // THE PAGED TEXTURES' FRAME: map last frame's demand, upload what this frame will sample,
    // then draw; afterwards issue this frame's demand readbacks. Both are no-ops with the
    // switch off, and both are inside this one call so the viewport, the exporter and the
    // headless render tool share one frame boundary.
    core::VirtualTexture::beginFrame();

    auto& tree = mProject.objectTree();
    auto info = aRenderInfo;
    if (mToolType == ToolType_Bone) {
        info.nonPosed = true;
    } else if (mToolType == ToolType_Mesh) {
        info.originMesh = true;
    }

    tree.render(info, false);

    if (aGridTarget && aGridTarget->renderer()) {
        info.isGrid = true;
        core::TimeCacheAccessor accessor(*aGridTarget, tree.timeCacheLock(), info.time, false);
        aGridTarget->renderer()->render(info, accessor);
    }

    core::VirtualTexture::endFrame();
}

void Driver::renderQt(const core::RenderInfo& aRenderInfo, QPainter& aPainter) const {
    auto info = aRenderInfo;
    if (mToolType == ToolType_Bone) {
        info.nonPosed = true;
    }

    // draw editor
    if (mEditor) {
        mEditor->renderQt(info, aPainter);
    }

    // draw outline
    drawOutline(info, aPainter);

    // draw ban mark
    if (mRejectedTarget) {
        drawBanMark(info, aPainter);
    }

    // The animation graph is no longer painted here: it is drawn by
    // gui::ImGuiOverlay, in the ImGui pass at the end of
    // MainDisplayWidget::paintEvent (which is what draws it after this QPainter
    // pass, so the panel still reads on top of everything the tools drew). The
    // graph's own QPainter renderer (ctrl::graph::Renderer) and
    // GraphEditor::renderQt were removed with that cutover.
}

void Driver::setGraphTargets(const QVector<core::ObjectNode*>& aTargets) {
    mGraphEditor->setTargets(aTargets);
}

void Driver::setGraphModifiers(Qt::KeyboardModifiers aModifiers) {
    mGraphEditor->setModifiers(aModifiers);
}

void Driver::drawOutline(const core::RenderInfo& aRenderInfo, QPainter& aPainter) {
    aPainter.setBrush(Qt::NoBrush);
    {
        QPen pen(QBrush(QColor(255, 255, 255, 128)), 1.5f, Qt::CustomDashLine);
        QVector<qreal> dashes;
        dashes << 4 << 4;
        pen.setDashPattern(dashes);
        aPainter.setPen(pen);
    }
    aPainter.setRenderHint(QPainter::Antialiasing);

    auto quad = aRenderInfo.camera.screenImageQuadangle();

#if 0 // qpainter bug? Some strange lines are rendered when very big values are assigned.
    aPainter.drawLine(quad[0].toPointF(), quad[1].toPointF());
    aPainter.drawLine(quad[1].toPointF(), quad[2].toPointF());
    aPainter.drawLine(quad[2].toPointF(), quad[3].toPointF());
    aPainter.drawLine(quad[3].toPointF(), quad[0].toPointF());
#elif 0
    const QPointF poly[5] = {
        quad[0].toPointF(), quad[1].toPointF(), quad[2].toPointF(), quad[3].toPointF(), quad[0].toPointF()};
    aPainter.drawConvexPolygon(poly, 5);
#else
    const QRectF scrRect(QPointF(0, 0), aRenderInfo.camera.screenSize());

    for (int i = 0; i < 4; ++i) {
        const int k = (i + 1) % 4;
        if (util::CollDetect::intersects(scrRect, util::Segment2D(quad[i], quad[k] - quad[i]))) {
            aPainter.drawLine(quad[i].toPointF(), quad[k].toPointF());
        }
    }
#endif
}

void Driver::drawBanMark(const core::RenderInfo& aRenderInfo, QPainter& aPainter) {
    aPainter.setRenderHint(QPainter::Antialiasing);
#if 0
    aPainter.setBrush(QBrush(QColor(0, 0, 0, 128)));
    aPainter.setPen(Qt::NoPen);

    const QPointF c = aRenderInfo.camera.screenCenter().toPointF();
    const QSize scrSize = aRenderInfo.camera.screenSize();
    const float r = std::min(scrSize.width(), scrSize.height());
    const float r1 = r * 0.04f;
    const float r2 = r * 0.20f;

    const QPointF a[4] = { c + QPointF(0.0f, -r1), c + QPointF(r1, 0.0f), c + QPointF(0.0f, r1), c + QPointF(-r1, 0.0f) };
    aPainter.drawConvexPolygon(a, 4);

    const QPointF x[4] = { QPointF(r2, -r2), QPointF(r2, r2), QPointF(-r2, r2), QPointF(-r2, -r2) };

    for (int i = 0; i < 4; ++i)
    {
        auto k = (i + 1) % 4;
        const QPointF b[4] = { a[k], a[i], a[i] + x[i], a[k] + x[i] };
        aPainter.drawConvexPolygon(b, 4);
    }
#elif 1
    aPainter.setBrush(Qt::NoBrush);
    aPainter.setPen(QPen(QColor(0, 0, 0, 128)));
    auto quad = aRenderInfo.camera.screenImageQuadangle();
    aPainter.drawLine(quad[1].toPointF(), quad[3].toPointF());
#endif
}

void Driver::updateParam(const SRTParam& aParam) {
    XC_ASSERT(mToolType == ToolType_SRT);

    ((SRTEditor*)mEditor.data())->updateParam(aParam);
}

void Driver::updateParam(const FFDParam& aParam) {
    XC_ASSERT(mToolType == ToolType_FFD);

    ((FFDEditor*)mEditor.data())->updateParam(aParam);
}

void Driver::updateParam(const BoneParam& aParam) {
    XC_ASSERT(mToolType == ToolType_Bone);

    ((BoneEditor*)mEditor.data())->updateParam(aParam);
}

void Driver::updateParam(const PoseParam& aParam) {
    XC_ASSERT(mToolType == ToolType_Pose);

    ((PoseEditor*)mEditor.data())->updateParam(aParam);
}

void Driver::updateParam(const MeshParam& aParam) {
    XC_ASSERT(mToolType == ToolType_Mesh);

    ((MeshEditor*)mEditor.data())->updateParam(aParam);
}

void Driver::updateParam(const MaskParam& aParam) {
    XC_ASSERT(mToolType == ToolType_Mask);
    if (!mMaskEditor)
        return;

    mMaskEditor->setParam(aParam);
}

void Driver::clearMask() {
    if (mMaskEditor)
        mMaskEditor->clearMask();
}

} // namespace ctrl
